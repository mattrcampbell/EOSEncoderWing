
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
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>
#include <Keypad.h>


#include <Encoder.h>
#define OLED_RESET 4
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>

#include "header.h"

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

const byte ROWS = 5; //four rows
const byte COLS = 2; //three columns
char keys[ROWS][COLS] = {
{'a','1'},
{'b','2'},
{'c','3'},
{'d','4'},
{'e','5'}
};
byte rowPins[ROWS] = {33, 34, 35, 36, 37}; //connect to the row pinouts of the kpd
byte colPins[COLS] = {39, 38}; //connect to the column pinouts of the kpd

Keypad kpd = Keypad( makeKeymap(keys), rowPins, colPins, ROWS, COLS );


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
LiquidCrystal_I2C lcd(0x27,40,2);
Encoder *wheels[NUMWHEELS];;

void displayInit( int i ) {
  lcd.init();                      // initialize the lcd 
  lcd.backlight();
  lcd.clear();
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

bool readLine(File &f, char* line, size_t maxLen) {
  for (size_t n = 0; n < maxLen; n++) {
    int c = f.read();
    if ( c < 0 && n == 0) return false;  // EOF
    if (c < 0 || c == '\n') {
      line[n] = 0;
      return true;
    }
    line[n] = c;
  }
  return false; // line too long
}

int findFileName() {
  if (!SD.exists("index.txt")) {
    File dataFile = SD.open("index.txt", FILE_WRITE);
    dataFile.close();
  }
  File dataFile = SD.open("index.txt");
  int i=0;
  while (dataFile.available()) {
    char line[200], *ptr, *str;
    if (!readLine(dataFile, line, sizeof(line))) {
      return false;  // EOF or too long
    }
    char *comma = strchr(line,',');
    *comma = '\0';
    int num = atoi(comma+1);
    if(strcmp(currentDevice,line)==0){
      return num;
    }
    i++;
  }

  dataFile.close();
  dataFile = SD.open("index.txt", FILE_WRITE);
  dataFile.printf("%s,%d\n", currentDevice, i);
  dataFile.close();

  return -1;

  // int lastind = 0;
  // for (int i = 0; i < EEPROM.length(); i += sizeof(int) + NSIZE) {
  //   int ind;
  //   char n[NSIZE];
  //   EEPROM.get(i, ind);
  //   EEPROM.get(i + sizeof(int), n);
  //   if (strncmp(n, currentDevice, NSIZE) == 0) {
  //     return ind;
  //   }
  //   if (n[0] == 0) {
  //     lastind++;
  //     EEPROM.put(i, lastind);
  //     EEPROM.put(i + sizeof(int), currentDevice);
  //     return lastind;
  //   }
  //   lastind = ind;
  // }
  // return -1;
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
      // TODO disp[0]->print("error ");
      // TODO disp[0]->println(OSCin.getError());
      // TODO disp[0]->display();
    }
    return 1;
  }
  return 0;
}

void printMessage(int i, char *msg) {
  //  // TODO disp[i]->fillRect(0, 0, // TODO disp[i]->width(), // TODO disp[i]->height() - 16, 0);
  //  // TODO disp[i]->setCursor(0, 0);
  // TODO disp[i]->setTextSize(1);
  // TODO disp[i]->print(msg);
  // TODO disp[i]->display();
  lcd.setCursor(i*10,0);
  lcd.print(msg);
}

void printWheelName(int num) {
  lcd.setCursor(num*10,0);
  int padlen = (10 - strlen(pages[currentPage].activeWheels[num].name)) / 2;
  lcd.printf( "%*s%s%*s", padlen,"",pages[currentPage].activeWheels[num].name,padlen,"" );

}

void printWheelValue(int num, int value) {

  char s[10];
  itoa(value,s,10);
  lcd.setCursor(num*10,1);
  int padlen = (10 - strlen(s)) / 2;
  lcd.printf( "%*s%s%*s", padlen,"",s,padlen,"" );
}
void printWheelClickSpeed(int i) {
  // TODO disp[i]->fillRect(// TODO disp[i]->width() - 10, 0, // TODO disp[i]->width() - 10, // TODO disp[i]->height() - 16, 0);
  // TODO disp[i]->setCursor(// TODO disp[i]->width() - 10, 0);
  // TODO disp[i]->setTextSize(1);
  // TODO disp[i]->print((int)wheelClickSpeed[i]);
  // TODO disp[i]->display();
}

void printWheelSelect(int i) {
  // TODO disp[i]->fillRect(0, 0, // TODO disp[i]->width(), // TODO disp[i]->height() - 16, 0);
  // TODO disp[i]->setCursor(0, 0);
  // TODO disp[i]->setTextSize(1);
  //if (pages[currentPage].activeWheels[i].num - 2 >= 0)// TODO disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num - 2]);
  //if (pages[currentPage].activeWheels[i].num - 1 >= 0)// TODO disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num - 1]);
  // TODO disp[i]->setTextSize(2);
  // TODO disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num]);
  // TODO disp[i]->setTextSize(1);
  // TODO disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num + 1]);
  // TODO disp[i]->println(allWheelNames[pages[currentPage].activeWheels[i].num + 2]);
  // TODO disp[i]->display();
  
  //lcd.clear();
  lcd.setCursor(i*10,0);
  if( pages[currentPage].activeWheels[i].num <= SUPPORTEDWHEELS){
    lcd.printf( "%10s", allWheelNames[pages[currentPage].activeWheels[i].num] );
  } else {
    lcd.printf( "ERROR" );
  }

}
void loop()
{
  static char *curMsg;
  static int i;
  int size;
  if (kpd.getKeys()) {
    for (int i = 0; i < LIST_MAX; i++)  // Scan the whole key list.
    {
      if (kpd.key[i].stateChanged && kpd.key[i].kstate == PRESSED)  // Only find keys that have changed state.
      {
        switch(kpd.key[i].kchar){
          case '1':
          case '2':
          case '3':
          case '4':
          case '5':
            currentPage = kpd.key[i].kchar - '1';
            forceUpdate = true;
            break;
          case 'a':
          case 'b':
          case 'c':
          case 'd':
          case 'e':
            OSCMessage macroMsg("/eos/macro/fire");
            macroMsg.add(700+kpd.key[i].kchar-'a');
            SLIPSerial.beginPacket();
            macroMsg.send(SLIPSerial);
            SLIPSerial.endPacket();
            delay(200);
            break;
        }
      }
    }
  }

  // if (!digitalRead(M1PIN)) {
  //   OSCMessage wheelMsg("/eos/macro/fire");
  //   wheelMsg.add(701);
  //   SLIPSerial.beginPacket();
  //   wheelMsg.send(SLIPSerial);
  //   SLIPSerial.endPacket();
  //   delay(200);
  // }
  // if (!digitalRead(M2PIN)) {
  //   OSCMessage wheelMsg("/eos/macro/fire");
  //   wheelMsg.add(702);
  //   SLIPSerial.beginPacket();
  //   wheelMsg.send(SLIPSerial);
  //   SLIPSerial.endPacket();
  //   delay(200);
  // }
  // if (!digitalRead(M3PIN)) {
  //   OSCMessage wheelMsg("/eos/macro/fire");
  //   wheelMsg.add(703);
  //   SLIPSerial.beginPacket();
  //   wheelMsg.send(SLIPSerial);
  //   SLIPSerial.endPacket();
  //   delay(200);
  // }
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
  // if (!digitalRead(UPPIN)) {
  //   forceUpdate = true;

  //   if (currentPage < 4) {
  //     currentPage++;
  //   }
  // } else if (!digitalRead(DOWNPIN)) {
  //   forceUpdate = true;

  //   if (currentPage > 0) {
  //     currentPage--;
  //   }
  // }

  //
  // Select wheels for current page
  if (!digitalRead(SELECTPIN)) {
    // forceUpdate = true;
    // updatePageConfig = true;
    // for (int w = 0; w < NUMWHEELS; w++) {
    //   long wheelin = wheels[w]->read();
    //   if (wheelin != wheelPos[w]) {
    //     pages[currentPage].activeWheels[w].num += wheelin > wheelPos[w] ? 1 : -1;
    //     strncpy(pages[currentPage].activeWheels[w].name, allWheelNames[pages[currentPage].activeWheels[w].num], NSIZE);
    //     wheelPos[w] = wheelin;
    //   }
    //   printWheelSelect(w);
    // }
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
          // TODO disp[1]->println("ERROR!");
          // TODO disp[1]->display();
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

