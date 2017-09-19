
#include <OSCBoards.h>
#include <OSCBundle.h>
#include <OSCData.h>
#include <OSCMatch.h>
#include <OSCMessage.h>
#include <OSCTiming.h>
#ifdef BOARD_HAS_USB_SERIAL
#include <SLIPEncodedUSBSerial.h>
SLIPEncodedUSBSerial SLIPSerial(thisBoardsSerialUSB);
#else
#include <SLIPEncodedSerial.h>
SLIPEncodedSerial SLIPSerial(Serial);
#endif
#include <string.h>

#include <SPI.h>
//#include "Wire.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <Encoder.h>
#define OLED_RESET 4

/*******************************************************************************
   Global Variables
 ******************************************************************************/
#define SUPPORTEDWHEELS 25
#define NUMWHEELS 4
int wheelShift = 2;
char activeWheelNames[NUMWHEELS][80];
long wheelPos[NUMWHEELS];

Adafruit_SSD1306 *disp[NUMWHEELS];
Encoder *wheels[NUMWHEELS];;

int line = 0;

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
  for (int i = 0; i < NUMWHEELS; i++) {
    activeWheelNames[i][0] = '\0';
    wheelPos[i] = -999;
    displayInit(i);
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
  setWheelName(0, "Pan");
  setWheelName(1, "Tilt");
  setWheelName(2, "Edge");
  setWheelName(3, "Zoom");
}

void setWheelName(int num, char *name) {
  disp[num]->clearDisplay();
  int length = strlen(name) * 10;
  disp[num]->setCursor(disp[num]->width() / 2 - length / 2, disp[num]->height() - 16);
  disp[num]->setTextSize(2);
  disp[num]->print(name);
  disp[num]->display();
  strncpy(activeWheelNames[num], name, sizeof(activeWheelNames[num]));
}
void eosMessage(OSCMessage &msg, int addressOffset) {
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
    if (strncmp(activeWheelNames[i], dev, 80) == 0) {
      disp[i]->fillRect(0, 0, disp[i]->width(), disp[i]->height() - 16, 0);
      if (value < -99) {
        length = 80;
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
  }
}

void printMessage(int i, char *msg) {
  disp[i]->fillRect(0, 0, disp[i]->width(), disp[i]->height() - 16, 0);
  disp[i]->setCursor(0, 0);
  disp[i]->setTextSize(1);
  disp[i]->print(msg);
  disp[i]->display();
}
int parseOSCMessage(char *msg, int len)
{

  // check to see if this is the handshake string
  if (strstr(msg, "ETCOSC?"))
  {
    printMessage(0,"Handshake");
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
      OSCin.route("/eos/out/active/wheel", eosMessage);
    } else {
      disp[0]->print("error ");
      disp[0]->println(OSCin.getError());
      disp[0]->display();
    }
    return 1;
  }
  return 0;
}

void loop()
{
  static char *curMsg;
  static int i;
  int size;

  // Check to see if any OSC commands have come from Eos that we need to respond to.
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

  if (SLIPSerial.endofPacket()) {
    if (parseOSCMessage(curMsg, i)) {
      free(curMsg);
      curMsg = NULL;
      i = 0;
    }
  }
  for (int w = 0; w < NUMWHEELS; w++) {
    long wheelin = wheels[w]->read();
    if (wheelin != wheelPos[w]) {
      if (activeWheelNames[w][0] != 0) {
        char addr[80] = "/eos/wheel/";
        strncat(addr, activeWheelNames[w], sizeof(activeWheelNames[w]) - 11);
        OSCMessage wheelMsg(addr);
        wheelMsg.add(wheelin > wheelPos[w] ? 1.0 : -1.0);
        SLIPSerial.beginPacket();
        wheelMsg.send(SLIPSerial);
        SLIPSerial.endPacket();
      }
      wheelPos[w] = wheelin;
    }
  }
}

