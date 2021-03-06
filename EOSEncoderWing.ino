
/*
   EOSEncodeWing - This program implements a simple encoder wing for ETC/EOS
                   using OSC for the Teensy 3.6 platform

   Author: Matt Campbell
   Date:   9/26/2017

   Revision 1

*/
#include <OSCBoards.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <OSCMatch.h>
#include <OSCMessage.h>
#include <OSCTiming.h>
#include <SLIPEncodedUSBSerial.h>
#include <string.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Encoder.h>
#define OLED_RESET 4
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>

#define SELECTPIN 0
#define UPPIN     1
#define DOWNPIN   2
#define M1PIN     3
#define M2PIN     4
#define M3PIN     5

#define W1PIN     6
#define W2PIN     7
#define W3PIN     8
#define W4PIN     9
#define SUPPORTEDWHEELS 50
#define NUMWHEELS 4
#define NSIZE     80

// Active wheel structure stores the currently selected wheel name/number
struct activeWheel {
  char name[NSIZE];
  int num = 1;
};

// Page structure holds one page of active weels
struct page {
  activeWheel activeWheels[NUMWHEELS];
};
page pages[5];
int currentPage = 0;
char allWheelNames[SUPPORTEDWHEELS][NSIZE];
long wheelPos[NUMWHEELS];
char currentDevice[NSIZE];
char fileName[12];
int line = 0;
boolean forceUpdate = false;
boolean updatePageConfig = false;
float wheelClickSpeed[NUMWHEELS];

SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
Adafruit_SSD1306 *disp[NUMWHEELS];
Encoder *wheels[NUMWHEELS];;

void displayInit( int i ) {
  disp[i] = new Adafruit_SSD1306(OLED_RESET);
  byte addr = 0;
  switch (i) {
    case 0: addr = 0x3D; disp[i]->begin(SSD1306_SWITCHCAPVCC, &Wire, addr); break;
    case 1: addr = 0x3C; disp[i]->begin(SSD1306_SWITCHCAPVCC, &Wire, addr); break;
    case 2: addr = 0x3D; disp[i]->begin(SSD1306_SWITCHCAPVCC, &Wire1, addr); break;
    case 3: addr = 0x3C; disp[i]->begin(SSD1306_SWITCHCAPVCC, &Wire1, addr); break;
  }
  disp[i]->setRotation(2);
  disp[i]->clearDisplay();
  disp[i]->stopscroll();
  disp[i]->display();
  disp[i]->setCursor(0, 0);
  disp[i]->setTextSize(1);
  disp[i]->setTextColor(WHITE);
  disp[i]->print("Initializing ");
  disp[i]->println(i);
  disp[i]->display();
}

void setup()
{
  pinMode(SELECTPIN, INPUT_PULLUP);
  pinMode(UPPIN,     INPUT_PULLUP);
  pinMode(DOWNPIN,   INPUT_PULLUP);
  pinMode(W1PIN,     INPUT_PULLUP);
  pinMode(W2PIN,     INPUT_PULLUP);
  pinMode(W3PIN,     INPUT_PULLUP);
  pinMode(W4PIN,     INPUT_PULLUP);
  pinMode(M1PIN,     INPUT_PULLUP);
  pinMode(M2PIN,     INPUT_PULLUP);
  pinMode(M3PIN,     INPUT_PULLUP);

  for (int i = 0; i < NUMWHEELS; i++) {
    pages[currentPage].activeWheels[i].name[0] = '\0';
    allWheelNames[i][0] = '\0';
    wheelPos[i] = -999;
    displayInit(i);
    wheelClickSpeed[i] = 1.0;
  }
  wheels[0] = new Encoder(32, 31);
  wheels[1] = new Encoder(30, 29);
  wheels[2] = new Encoder(28, 27);
  wheels[3] = new Encoder(26, 25);

  SLIPSerial.begin(115200);
  // This is a hack around an arduino bug. It was taken from the OSC library examples
  while (!Serial);

  // this is necessary for reconnecting a device because it need some timme for the serial port to get open, but meanwhile the handshake message was send from eos
  SLIPSerial.beginPacket();
  SLIPSerial.write((const uint8_t*)"OK", 2);
  SLIPSerial.endPacket();
  delay(200);

  if (!SD.begin(BUILTIN_SDCARD)) {
    printMessage(3, "SD FAIL");
  } else {
    printMessage(3, "SD INIT");
  }
}

int findFileName() {
  int lastind = 0;
  for (int i = 0; i < EEPROM.length(); i += sizeof(int) + NSIZE) {
    int ind;
    char n[NSIZE];
    EEPROM.get(i, ind);
    EEPROM.get(i + sizeof(int), n);
    if (strncmp(n, currentDevice, NSIZE) == 0) {
      return ind;
    }
    if (n[0] == 0) {
      lastind++;
      EEPROM.put(i, lastind);
      EEPROM.put(i + sizeof(int), currentDevice);
      return lastind;
    }
    lastind = ind;
  }
  return -1;
}

void writeDevice() {
  char fn[12];
  sprintf(fn, "%d.txt", findFileName());
  SD.remove(fn);
  File dataFile = SD.open(fn, FILE_WRITE);
  dataFile.write((const uint8_t *)&pages, sizeof(pages));
  dataFile.close();
}

boolean readDevice() {

  char fn[12];
  sprintf(fn, "%d.txt", findFileName());
  if (!SD.exists(fn)) {
    return false;
  }
  File dataFile = SD.open(fn);
  dataFile.read((uint8_t *)&pages, sizeof(pages));
  dataFile.close();
  return true;
}

void setWheelName(int num, char *name, int wNum) {
  strncpy(pages[currentPage].activeWheels[num].name, name, sizeof(pages[currentPage].activeWheels[num].name));
  pages[currentPage].activeWheels[num].num = wNum;
}
void printWheelName(int num) {
  disp[num]->clearDisplay();
  int length;
  if (strlen(pages[currentPage].activeWheels[num].name) > 10) {
    length = strlen(pages[currentPage].activeWheels[num].name) * 5;
    disp[num]->setTextSize(1);
  } else {
    length = strlen(pages[currentPage].activeWheels[num].name) * 10;
    disp[num]->setTextSize(2);
  }
  disp[num]->setCursor(disp[num]->width() / 2 - length / 2, disp[num]->height() - 16);

  disp[num]->print(pages[currentPage].activeWheels[num].name);
  disp[num]->display();

}

void wheelMessage(OSCMessage &msg, int addressOffset) {
  int length = msg.getDataLength(0);
  if (length == 0 ) {
    return;
  }
  char tmp[length];
  char dev[length];
  int value;
  msg.getString(0, tmp, length);

  sscanf(tmp, "%[^[][%d", dev, &value);
  dev[strlen(dev) - 2] = 0; // Remove the spaces

  // Get the wheel address
  msg.getAddress(tmp, addressOffset + 1);

  int wheelNum = atoi(tmp) - 1;

  for (int i = 0; i < NUMWHEELS; i++) {
    if (strncmp(pages[currentPage].activeWheels[i].name, dev, NSIZE) == 0) {
      printWheelValue(i, value);
    }
  }
  strncpy(allWheelNames[wheelNum], dev, NSIZE);
}

void chanMessage(OSCMessage &msg, int addressOffset) {
  int length = msg.getDataLength(0);
  if (length == 0 ) {
    return;
  }
  char tmp[length];
  char dev[length];
  msg.getString(0, tmp, length);
  sscanf(tmp, "%*s [%*d] %*s %[^@ ]", dev);
  if (strncmp(currentDevice, dev, NSIZE) != 0) {
    currentPage = 0;
    strncpy(currentDevice, dev, NSIZE);
    if (dev[0] != 0 && !readDevice()) {
      for (int i = 0; i < NUMWHEELS; i++) {
        setWheelName(i, "Intens", 1);
        printWheelName(i);
      }

      writeDevice();
    }
    forceUpdate = true;
  }
}

int parseOSCMessage(char *msg, int len)
{
  // check to see if this is the handshake string
  if (strstr(msg, "ETCOSC?"))
  {
    printMessage(0, "Handshake");
    SLIPSerial.beginPacket();
    SLIPSerial.write((const uint8_t*)"OK", 2);
    SLIPSerial.endPacket();
    SLIPSerial.flush();
    return 1;
  } else {
    OSCMessage OSCin;
    for (int i = 0; i < len; i++) {
      char c = msg[i];
      OSCin.fill((uint8_t)c);
    }
    if (!OSCin.hasError()) {
      OSCin.route("/eos/out/active/wheel", wheelMessage);
      OSCin.route("/eos/out/active/chan", chanMessage);
    } else {
      disp[0]->print("error ");
      disp[0]->println(OSCin.getError());
      disp[0]->display();
    }
    return 1;
  }
  return 0;
}

void printMessage(int i, char *msg) {
  //  disp[i]->fillRect(0, 0, disp[i]->width(), disp[i]->height() - 16, 0);
  //  disp[i]->setCursor(0, 0);
  disp[i]->setTextSize(1);
  disp[i]->print(msg);
  disp[i]->display();
}

void printWheelValue(int i, int value) {
  int length = 0;
  disp[i]->fillRect(0, 0, disp[i]->width() - 10, disp[i]->height() - 16, 0);
  if (value < -99) {
    length = NSIZE;
  } else if (value >= 0 && value < 10) {
    length = 20;
  } else if (value > -10 && value < 100) {
    length = 40;
  } else {
    length = 60;
  }
  disp[i]->setCursor(disp[i]->width() / 2 - length / 2, 0);
  disp[i]->setTextSize(4);
  disp[i]->print(value);
  disp[i]->display();
}
void printWheelClickSpeed(int i) {
  disp[i]->fillRect(disp[i]->width() - 10, 0, disp[i]->width() - 10, disp[i]->height() - 16, 0);
  disp[i]->setCursor(disp[i]->width() - 10, 0);
  disp[i]->setTextSize(1);
  disp[i]->print((int)wheelClickSpeed[i]);
  disp[i]->display();
}

void printWheelSelect(int i) {
  disp[i]->fillRect(0, 0, disp[i]->width(), disp[i]->height() - 16, 0);
  disp[i]->setCursor(0, 0);
  disp[i]->setTextSize(1);
  if (pages[currentPage].activeWheels[i].num - 2 >= 0)disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num - 2]);
  if (pages[currentPage].activeWheels[i].num - 1 >= 0)disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num - 1]);
  disp[i]->setTextSize(2);
  disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num]);
  disp[i]->setTextSize(1);
  disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num + 1]);
  disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num + 2]);
  disp[i]->display();
}
void loop()
{
  static char *curMsg;
  static int i;
  int size;

  if (!digitalRead(M1PIN)) {
    OSCMessage wheelMsg("/eos/macro/fire");
    wheelMsg.add(701);
    SLIPSerial.beginPacket();
    wheelMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
    delay(200);
  }
  if (!digitalRead(M2PIN)) {
    OSCMessage wheelMsg("/eos/macro/fire");
    wheelMsg.add(702);
    SLIPSerial.beginPacket();
    wheelMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
    delay(200);
  }
  if (!digitalRead(M3PIN)) {
    OSCMessage wheelMsg("/eos/macro/fire");
    wheelMsg.add(703);
    SLIPSerial.beginPacket();
    wheelMsg.send(SLIPSerial);
    SLIPSerial.endPacket();
    delay(200);
  }
  if (!digitalRead(W1PIN)) {
    wheelClickSpeed[0]++;
    if (wheelClickSpeed[0] > 5) wheelClickSpeed[0] = 1;
    printWheelClickSpeed(0);
    delay(200);
  }
  if (!digitalRead(W2PIN)) {
    wheelClickSpeed[1]++;
    if (wheelClickSpeed[1] > 5) wheelClickSpeed[1] = 1;
    printWheelClickSpeed(1);
    delay(200);
  }
  if (!digitalRead(W3PIN)) {
    wheelClickSpeed[2]++;
    if (wheelClickSpeed[2] > 5) wheelClickSpeed[2] = 1;
    printWheelClickSpeed(2);
    delay(200);
  }
  if (!digitalRead(W4PIN)) {
    wheelClickSpeed[3]++;
    if (wheelClickSpeed[3] > 5) wheelClickSpeed[3] = 1;
    printWheelClickSpeed(3);
    delay(200);
  }
  if (!digitalRead(UPPIN)) {
    forceUpdate = true;

    if (currentPage < 4) {
      currentPage++;
    }
  } else if (!digitalRead(DOWNPIN)) {
    forceUpdate = true;

    if (currentPage > 0) {
      currentPage--;
    }
  }

  //
  // Select wheels for current page
  if (!digitalRead(SELECTPIN)) {
    forceUpdate = true;
    updatePageConfig = true;
    for (int w = 0; w < NUMWHEELS; w++) {
      long wheelin = wheels[w]->read();
      if (wheelin != wheelPos[w]) {
        pages[currentPage].activeWheels[w].num += wheelin > wheelPos[w] ? 1 : -1;
        strncpy(pages[currentPage].activeWheels[w].name, allWheelNames[pages[currentPage].activeWheels[w].num], NSIZE);
        wheelPos[w] = wheelin;
      }
      printWheelSelect(w);
    }
    delay(100);
  } else {
    // Check for incoming OSC messages
    size = SLIPSerial.available();
    if (size > 0)
    {
      // Fill the msg with all of the available bytes
      while (size--) {
        curMsg = (char*)realloc(curMsg, i + size + 1);
        curMsg[i] = (char)(SLIPSerial.read());
        if (curMsg[i] == -1) {
          curMsg[i] = 0;
          disp[1]->println("ERROR!");
          disp[1]->display();
          break;
        }
        i++;
      }
    }

    // Check if message is complete and parse.
    if (SLIPSerial.endofPacket()) {
      if (parseOSCMessage(curMsg, i)) {
        free(curMsg);
        curMsg = NULL;
        i = 0;
      }
    }
    if (updatePageConfig) {
      writeDevice();
      updatePageConfig = false;
    }
    if (forceUpdate) {
      for (int w = 0; w < NUMWHEELS; w++) {
        printWheelName(w);
        printWheelClickSpeed(w);
      }
      OSCMessage wheelMsg("/eos/reset");
      SLIPSerial.beginPacket();
      wheelMsg.send(SLIPSerial);
      SLIPSerial.endPacket();
      forceUpdate = false;

    }
    for (int w = 0; w < NUMWHEELS; w++) {
      long wheelin = wheels[w]->read();
      if (wheelin != wheelPos[w]) {
        if (pages[currentPage].activeWheels[w].name[0] != 0) {
          char addr[80] = "/eos/wheel/";
          strncat(addr, pages[currentPage].activeWheels[w].name, sizeof(pages[currentPage].activeWheels[w].name) - 11);
          OSCMessage wheelMsg(addr);
          //wheelMsg.add(wheelin > wheelPos[w] ? 1.0 : -1.0);
          wheelMsg.add(wheelin > wheelPos[w] ? (wheelClickSpeed[w]) : -1.0 * (wheelClickSpeed[w]));
          SLIPSerial.beginPacket();
          wheelMsg.send(SLIPSerial);
          SLIPSerial.endPacket();
        }
        wheelPos[w] = wheelin;
      }
    }
  }
}

