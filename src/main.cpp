
/*
   EOSEncodeWing - This program implements a simple encoder wing for ETC/EOS
                   using OSC for the Teensy 3.6 platform

   Author: Matt Campbell
   Date:   9/26/2017

   Revision 1

*/
#include <ArduinoJson.h>
#include <Bounce2.h>
#include <EncoderTool.h>
#include <Keypad.h>
#include <LCD_I2C.h>
#include <OSCBoards.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <OSCMatch.h>
#include <OSCMessage.h>
#include <OSCTiming.h>
#include <SD.h>
#include <SLIPEncodedUSBSerial.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

// #define CIRCULAR_BUFFER_INT_SAFE
// #include <CircularBuffer.h>

#include "header.h"

const byte ROWS = 5; // four rows
const byte COLS = 2; // three columns
char keys[ROWS][COLS] = {{'a', '1'}, {'b', '2'}, {'c', '3'}, {'d', '4'}, {'e', '5'}};
byte rowPins[ROWS] = {33, 34, 35, 36, 37}; // connect to the row pinouts of the kpd
byte colPins[COLS] = {39, 38};             // connect to the column pinouts of the kpd

Keypad kpd = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

Config config;

// page pages[5];
int currentPage = 0;
char allWheelNames[SUPPORTEDWHEELS][NSIZE + 1];
long wheelPos[NUMWHEELS];
char currentDevice[NSIZE + 1];
char fileName[12];
int line = 0;
boolean forceUpdate = false;
boolean selectMode = false;
boolean handshakeComplete = false;

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
LCD_I2C lcd(0x27, 40, 2);

EncoderTool::Encoder *encoders[NUMWHEELS];
EncoderTool::Encoder enc1, enc2, enc3, enc4;

Bounce knobButtons[NUMWHEELS];

const String HANDSHAKE_QUERY = "ETCOSC?";
const String HANDSHAKE_REPLY = "OK";

struct wheelEntry {
  uint32_t readTime = 0;
  int ticks = 0;
};
wheelEntry wheelEntries[ENCREADS];
int curEntry = 0;

void setup() {
  Serial1.begin(115200);
  for (int i = 0; i < NUMWHEELS; i++) {
    wheelPos[i] = -999;
  }
  knobButtons[0].attach(W1PIN, INPUT_PULLUP);
  knobButtons[1].attach(W2PIN, INPUT_PULLUP);
  knobButtons[2].attach(W3PIN, INPUT_PULLUP);
  knobButtons[3].attach(W4PIN, INPUT_PULLUP);

  lcd.begin(); // initialize the lcd
  lcd.backlight();
  lcd.clear();

  SLIPSerial.begin(115200);
  // This is a hack around an arduino bug. It was taken from the OSC library
  // examples while (!Serial);

  // this is necessary for reconnecting a device because it need some timme for
  // the serial port to get open, but meanwhile the handshake message was send
  // from eos
  SLIPSerial.beginPacket();
  SLIPSerial.write((const uint8_t *)"OK", 2);
  SLIPSerial.endPacket();
  delay(200);

  if (!SD.begin(BUILTIN_SDCARD)) {
    printMessage(0, "SD FAIL", true);
  } else {
    printMessage(0, "SD INIT", true);
  }

  encoders[3] = &enc1;
  encoders[2] = &enc2;
  encoders[1] = &enc3;
  encoders[0] = &enc4;

  enc1.begin(32, 31);
  enc2.begin(30, 29);
  enc3.begin(28, 27);
  enc4.begin(26, 25);

  printMessage(0, "Setup Complete", true);
}

bool readLine(File &f, char *line, size_t maxLen) {
  for (size_t n = 0; n < maxLen; n++) {
    int c = f.read();
    if (c < 0 && n == 0)
      return false; // EOF
    if (c < 0 || c == '\n') {
      line[n] = '\0';
      return true;
    }
    line[n] = c;
  }
  return false; // line too long
}

int findFileName() {
  if (!SD.exists("index.txt")) {
    File indexFile = SD.open("index.txt", FILE_WRITE);
    indexFile.close();
  }

  File indexFile = SD.open("index.txt");
  int i = 0;
  while (indexFile.available()) {
    char line[200];
    if (!readLine(indexFile, line, sizeof(line))) {
      return false;
    }
    char *comma = strchr(line, ',');
    *comma = '\0';
    int num = atoi(comma + 1);
    if (strcmp(currentDevice, line) == 0) {
      return num;
    }
    i++;
  }

  indexFile.close();
  indexFile = SD.open("index.txt", FILE_WRITE);
  indexFile.printf("%s,%d\n", currentDevice, i);
  indexFile.close();

  return -1;
}

void writeDevice() {
  char fn[12];
  sprintf(fn, "%d.txt", findFileName());
  saveConfiguration(fn, config);
}

boolean readDevice() {
  char fn[12];
  sprintf(fn, "%d.txt", findFileName());
  if (!SD.exists(fn)) {
    return false;
  }
  loadConfiguration(fn, config);
  return true;
}

// Loads the configuration from a file
void loadConfiguration(const char *filename, Config &config) {
  File file = SD.open(filename);
  StaticJsonDocument<2048> doc;

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(doc, file);
  if (error)
    slotMessage(1, 0, "Failed to read file, using default configuration", false);

  for (int p = 0; p < NPAGES; p++) {
    for (int w = 0; w < NUMWHEELS; w++) {
      if (doc[p][w]["num"] != NULL) {
        strncpy(config.pages[p].activeWheels[w].name, doc[p][w]["name"], NSIZE);
        config.pages[p].activeWheels[w].num = doc[p][w]["num"];
      } else {
        strncpy(config.pages[p].activeWheels[w].name, "Intens", NSIZE);
        config.pages[p].activeWheels[w].num = 1;
      }
    }
  }
  file.close();
}

// Saves the configuration to a file
void saveConfiguration(const char *filename, const Config &config) {
  SD.remove(filename);
  File file = SD.open(filename, FILE_WRITE);
  if (!file) {
    Serial.println(F("Failed to create file"));
    return;
  }
  StaticJsonDocument<2048> doc;

  for (int p = 0; p < NPAGES; p++) {
    for (int w = 0; w < NUMWHEELS; w++) {
      doc[p][w]["name"] = config.pages[p].activeWheels[w].name;
      doc[p][w]["num"] = config.pages[p].activeWheels[w].num;
    }
  }
  if (serializeJson(doc, file) == 0) {
    Serial.println(F("Failed to write to file"));
  }
  file.close();
}

int getPadding(int size, const char *msg) {
  int padlen = (size - strlen(msg)) / 2;
  if (padlen > size || padlen < 0)
    padlen = 0;
  return padlen;
}

void setWheelName(int num, const char *name, int wNum) {
  strncpy(config.pages[currentPage].activeWheels[num].name, name, sizeof(config.pages[currentPage].activeWheels[num].name));
  config.pages[currentPage].activeWheels[num].num = wNum;
}

void wheelMessage(OSCMessage &msg, int addressOffset) {
  int length = msg.getDataLength(0);
  if (length < 7) {
    return;
  }
  char tmp[length];
  char dev[length];
  int value;
  int res;
  res = msg.getString(0, tmp, length);
  if (res == -1 || res == (int)NULL) {
    return;
  };
  sscanf(tmp, "%[^[][%d", dev, &value);

  dev[strlen(dev) - 2] = 0; // Remove the spaces

  // Get the wheel address
  msg.getAddress(tmp, addressOffset + 1);
  int wheelNum = atoi(tmp);

  for (int i = 0; i < NUMWHEELS; i++) {
    if (strncmp(config.pages[currentPage].activeWheels[i].name, dev, NSIZE) == 0) {
      printWheelValue(i, value);
    }
  }
  // Serial1.printf("msg %s\nl %d  d %s\n", msg, length, dev);
  strncpy(allWheelNames[wheelNum], dev, NSIZE);
}

void displayNoChannel() {
  lcd.clear();
  printMessage(0, "No channel currently selected.", true);
}

void chanMessage(OSCMessage &msg, int addressOffset) {
  int length = msg.getDataLength(0);
  if (length == 0) {
    return;
  }

  char tmp[length];
  char dev[length];
  int dlen = msg.getString(0, tmp, length);
  if (dlen == 1) {
    strcpy(currentDevice, "");
    currentPage = 0;
    forceUpdate = true;
    return;
  }

  // sscanf(tmp, "%*s [%*d] %*s %[^@ ]", dev);

  char *token = strtok(tmp, " ");

  while (token != NULL) {
    // lcd.setCursor(0,0);
    // lcd.clear();
    // lcd.println(token);
    token = strtok(NULL, " ");
    if (strstr(token, "@"))
      break;
    strncpy(dev, token, length);
  }

  if (strncmp(currentDevice, dev, NSIZE) != 0) {
    currentPage = 0;
    strncpy(currentDevice, dev, NSIZE);
    if (dev[0] != 0 && !readDevice()) {
      for (int i = 0; i < NUMWHEELS; i++) {
        setWheelName(i, "Intens", 1);
        printWheelName(i);
      }
    }
    forceUpdate = true;
  }
}

void parseOSCMessage(String &msg) {
  // check to see if this is the handshake string
  if (msg.indexOf(HANDSHAKE_QUERY) != -1) {
    // handshake string found!
    SLIPSerial.beginPacket();
    SLIPSerial.write((const uint8_t *)HANDSHAKE_REPLY.c_str(), (size_t)HANDSHAKE_REPLY.length());
    SLIPSerial.endPacket();
    handshakeComplete = true;
  } else {
    // prepare the message for routing by filling an OSCMessage object with our
    // message string
    OSCMessage oscmsg;
    oscmsg.fill((uint8_t *)msg.c_str(), (int)msg.length());

    // Try the various OSC routes
    if (oscmsg.route("/eos/out/active/wheel", wheelMessage))
      return;
    if (oscmsg.route("/eos/out/active/chan", chanMessage))
      return;
  }
}

void printMessage(int row, const char *msg, boolean center = true) {
  lcd.setCursor(0, row);
  lcd.print("                                        ");
  lcd.setCursor(0, row);
  if (center) {
    int padlen = getPadding(40, msg);
    // Serial1.printf("%d %d %d : %s\n",row, slot, padlen, msg);
    lcd.printf("%*s%s%*s", padlen, "", msg, padlen, "");
  } else {
    lcd.print(msg);
  }
}

void slotMessage(int row, int slot, const char *msg, boolean center = true) {
  lcd.setCursor(slot * 10, row);
  lcd.print("          ");
  lcd.setCursor(slot * 10, row);
  if (center) {
    int padlen = getPadding(10, msg);
    // Serial1.printf("%d %d %d : %s\n", row, slot, padlen, msg);
    lcd.printf("%*s%s%*s", padlen, "", msg, padlen, "");
  } else {
    lcd.print(msg);
  }
}

void printWheelName(int num) {
  lcd.setCursor(num * 10, 0);
  lcd.print("          ");
  lcd.setCursor(num * 10, 0);
  int padlen = getPadding(10, config.pages[currentPage].activeWheels[num].name);
  lcd.printf("%*s%s%*s", padlen, "", config.pages[currentPage].activeWheels[num].name, padlen, "");
}

void printWheelValue(int num, int value) {
  char s[10];
  itoa(value, s, 10);
  int padlen = getPadding(10, s);
  lcd.setCursor(num * 10, 1);
  lcd.print("          ");
  lcd.setCursor(num * 10, 1);
  lcd.printf("%*s%s%*s", padlen, "", s, padlen, "");
}

void printWheelSelect(int num) {
  if (config.pages[currentPage].activeWheels[num].num <= SUPPORTEDWHEELS) {
    char m[NSIZE + 2];
    sprintf(m, "[%s]", allWheelNames[config.pages[currentPage].activeWheels[num].num]);
    slotMessage(0, num, m, true);
    sprintf(m, "[%d]", config.pages[currentPage].activeWheels[num].num);
    slotMessage(1, num, m, true);
  } else {
    slotMessage(0, num, "ERROR", true);
  }
}

void knobCheck(int num) {
  knobButtons[num].update();
  if (knobButtons[num].fell()) {
    char addr[80];
    sprintf(addr, "/eos/param/%s/home", config.pages[currentPage].activeWheels[num].name);
    OSCMessage wheelMsg(addr);
    SLIPSerial.beginPacket();
    wheelMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
  }
}

void loop() {
  static String curMsg;
  // static int i;
  int size;
  if (kpd.getKeys()) {
    for (int i = 0; i < LIST_MAX; i++) {
      if (kpd.key[i].stateChanged && kpd.key[i].kstate == RELEASED) {
        if (selectMode) {
          switch (kpd.key[i].kchar) {
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
            currentPage = kpd.key[i].kchar - '1';
            forceUpdate = true;
            break;
          case 'a':
            readDevice();
            selectMode = false;
            forceUpdate = true;
            lcd.clear();
            break;
          case 'c':
            lcd.clear();
            slotMessage(0, 0, "Writing file...", false);
            writeDevice();
            lcd.clear();
            selectMode = false;
            forceUpdate = true;
            break;
          }
        } else {
          switch (kpd.key[i].kchar) {
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
            if (kpd.key[kpd.findInList('e')].kstate == HOLD) {
              selectMode = true;
              forceUpdate = true;
            } else {
              currentPage = kpd.key[i].kchar - '1';
              forceUpdate = true;
            }
            break;
          case 'a':
          case 'b':
          case 'c':
          case 'd':
          case 'e':
            OSCMessage macroMsg("/eos/macro/fire");
            macroMsg.add(700 + kpd.key[i].kchar - 'a');
            SLIPSerial.beginPacket();
            macroMsg.send(SLIPSerial);
            SLIPSerial.endPacket();
            delay(200);
            break;
          }
        }
      }
    }
  }
  //
  // Select encoders for current page
  if (selectMode) {
    for (int w = 0; w < NUMWHEELS; w++) {
      long wheelin = encoders[w]->getValue();
      if (wheelin != wheelPos[w]) {
        forceUpdate = true;
        config.pages[currentPage].activeWheels[w].num += wheelin > wheelPos[w] ? 1 : -1;
        if (config.pages[currentPage].activeWheels[w].num < 1) {
          config.pages[currentPage].activeWheels[w].num = 1;
        }
        if (config.pages[currentPage].activeWheels[w].num > SUPPORTEDWHEELS) {
          config.pages[currentPage].activeWheels[w].num = SUPPORTEDWHEELS;
        }
        strncpy(config.pages[currentPage].activeWheels[w].name, allWheelNames[config.pages[currentPage].activeWheels[w].num], NSIZE);
        wheelPos[w] = wheelin;
      }
    }
    if (forceUpdate) {
      for (int w = 0; w < NUMWHEELS; w++) {
        printWheelSelect(w);
        forceUpdate = false;
      }
      lcd.setCursor(0, 1);
      lcd.print(currentPage);
    }
  } else {
    // Check for incoming OSC messages
    size = SLIPSerial.available();
    if (size > 0) {
      // Fill the msg with all of the available bytes
      while (size--) {
        curMsg += (char)(SLIPSerial.read());
      }
    }
    // Check if message is complete and parse.
    if (SLIPSerial.endofPacket()) {
      parseOSCMessage(curMsg);
      curMsg = String();
    }
    if (forceUpdate) {
      if (strcmp(currentDevice, "") == 0) {
        displayNoChannel();
      } else {
        for (int w = 0; w < NUMWHEELS; w++) {
          printWheelName(w);
        }
        OSCMessage wheelMsg("/eos/reset");
        SLIPSerial.beginPacket();
        wheelMsg.send(SLIPSerial);
        SLIPSerial.endPacket();
      }

      forceUpdate = false;
    }
    if (handshakeComplete) {
      uint32_t curTime = micros();
      for (int w = 0; w < NUMWHEELS; w++) {
        knobCheck(w);
        long wheelin = encoders[w]->getValue();
        if (wheelin != wheelPos[w]) {
          wheelEntries[curEntry] = wheelEntry{curTime, wheelPos[w] - wheelin};
          curEntry++;
          if (curEntry >= ENCREADS)
            curEntry = 0;
          float total = 0;
          for (int i = 0; i < ENCREADS; i++) {
            if (wheelEntries[i].readTime > curTime - 50000) {
              total += wheelEntries[i].ticks;
            } else {
              wheelEntries[i].ticks = 0;
            }
          }
          if (config.pages[currentPage].activeWheels[w].name[0] != 0) {
            char addr[80] = "/eos/wheel/coarse/";
            String wheelName = config.pages[currentPage].activeWheels[w].name;
            wheelName.replace("/", "\\");
            strncat(addr, wheelName.c_str(), wheelName.length() - 18);
            OSCMessage wheelMsg(addr);
            wheelMsg.add(total * -1);
            SLIPSerial.beginPacket();
            wheelMsg.send(SLIPSerial);
            SLIPSerial.endPacket();
          }
          wheelPos[w] = wheelin;
        }
      }
      for (int i = 0; i < ENCREADS; i++) {
        if (wheelEntries[i].readTime < curTime - 50000) {
          wheelEntries[i].ticks = 0;
        }
      }
    }
  }
}
