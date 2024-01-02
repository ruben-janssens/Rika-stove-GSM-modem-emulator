#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESPAsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>
#include "arduino_secrets.h"

#define D8 (15)
#define D5 (14)
#define D7 (13)
#define D6 (12)
#define RX (3)
#define TX (1)

AsyncWebServer server(80);


const char *ssid = SECRET_SSID;
const char *pass = SECRET_PASS;

// AT Commands
// Just accept all these
// AT -> Test command
const String AT = "AT\r";
// AT&F -> Restore factory settings
const String ATF = "AT&F\r";
// ATE{0-1} -> Turn ECHO off or on
const String ATE = "ATE";
// AT+CMGF={0-1} -> Set message format. 0 = "PDU" / 1 = "TEXT"
const String ATCMGF = "AT+CMGF=";
// AT+CNMI={params} -> https://infocenter.nordicsemi.com/index.jsp?topic=%2Fref_at_commands%2FREF%2Fat_commands%2Ftext_mode%2Fcnmi_read.html
const String ATCNMI = "AT+CNMI=";
// Do something with these
// AT+CMGR={number} -> Reads the messsage at index
const String ATCMGR = "AT+CMGR=";
// AT+CMGS -> Request to send a message
const String ATCMGS = "AT+CMGS=";
// AT+CMGD={number} -> Deletes the messsage at index
const String ATCMGD = "AT+CMGD=";


// Bogus phone number and pincode
const String phoneNumber = "+32479123456";
const String pinCode = "1234";

// Incomming command  for stove
String myCommand = "";
// Possible commands
// ON
// OFF
// M{30-100} -> Manual mode
// A{30-100} -> Timeprogram mode
// C{number} -> Room temperature mode without timemode
// CT{number} -> Room temperature mode wit timemode
// FON -> Frost protection on
// FOFF -> Frost protection off
// ? -> Get status

// Failures are send automatically
// FAILURE: code error
// FAILURE: text error
// FAILURE: sending SMS - resetting GSM module
// FAILURE: no room sensor available
// FAILURE: no pellets
// FAILURE: not ignited
// FAILURE: flame sensor defect
// FAILURE: dischargemotor defect
// FAILURE: dischargemotor blocked
// FAILURE: ID fan defect
// FAILURE: low pressure control
// FAILURE: safety temperature limiter

// Warnings are send when the user sends a request
// WARNING: room sensor signal lost - please switch to MANUAL MODE
// WARNING: pellet cover open
// WARNING: door open
// WARNING: not enough low pressure


// Serial inputs
String rikaSerialCmdIn = "";
boolean rikaSerialCmdInReady = false;
String rikaSerialSmsIn = "";
boolean rikaSerialSmsInReady = false;


// Message callback of WebSerial
void recvMsg(uint8_t* data, size_t len) {
  String d = "";
  for (int i = 0; i < len; i++) {
    d += char(data[i]);
  }
  myCommand = d;
}

void sendReturnChars() {
  Serial.print("\r");
  Serial.print("\n");
}

void sendOK() {
  Serial.print("OK");
  sendReturnChars();
}

bool isNumeric(String input) {
  bool valid = true;
  for (int i = 0; i < input.length(); i++) {
     valid = isDigit(input[i]);
  }
  return valid;
}

void setup() {
  Serial.begin(115200, SERIAL_8N1, SERIAL_FULL, TX, true);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("WiFi Failed!\n");
    return;
  }
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&server);
  WebSerial.msgCallback(recvMsg);
  server.begin();
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}

void loop() {
  // Handle incomming request
  if (Serial.available() > 0) {
    while (Serial.available()) {
      char inChar = (char)Serial.read();
      rikaSerialCmdIn += inChar;
      if ((inChar == '\n') || (inChar == char(26)) || (inChar == char(13))) {
        rikaSerialCmdInReady = true;
      }
    }
  }

  // Handle an incomming request
  if (rikaSerialCmdInReady) {
    // Handle just accepts
    WebSerial.println("-> Stove Requests `" + rikaSerialCmdIn + "`");
    if (rikaSerialCmdIn == AT || rikaSerialCmdIn == ATF || rikaSerialCmdIn.startsWith(ATE) || rikaSerialCmdIn.startsWith(ATCMGF) || rikaSerialCmdIn.startsWith(ATCNMI)) {
      sendOK();
      WebSerial.println("<- Send OK");
    } else if (rikaSerialCmdIn.startsWith(ATCMGR)) {
      // Stove wants to read message
      if (myCommand != "") {
        // We have something for the stove

        Serial.print("+CMGR: \"REC UNREAD\",\"");
        Serial.print(phoneNumber);
        Serial.print("\",,\"");
        Serial.print("70/01/01");
        Serial.print(",");
        Serial.print("01:00:00+0");
        Serial.print("\"");
        sendReturnChars();
        Serial.print(pinCode);
        Serial.print(" ");
        Serial.print(myCommand);
        sendReturnChars();
        sendReturnChars();
        sendOK();
        WebSerial.println("<- Send command, return and OK");
      } else {
        sendReturnChars();
        sendOK();
        WebSerial.println("<- Send return and OK");
      }
    } else if (rikaSerialCmdIn.startsWith(ATCMGS)) {
      // Stove wants to send a message
      sendReturnChars();
      Serial.print(">");
      delay(2000);
      while (!rikaSerialSmsInReady) {
        if (Serial.available()) {
          char inChar = (char)Serial.read();
          rikaSerialSmsIn += inChar;
          // CTRL+z (ASCII 26) to end the SMS
          if (inChar == char(26)) {
            rikaSerialSmsInReady = true;
          }
        }
      }
      sendReturnChars();
      WebSerial.println("<- Stove wants to send `" + rikaSerialSmsIn + "`");

      Serial.print("+CMGS : 1");
      sendReturnChars();
      sendOK();
      WebSerial.println("<- Send return and OK");
      rikaSerialSmsIn = "";
      rikaSerialSmsInReady = false;
    } else if (rikaSerialCmdIn.startsWith(ATCMGD)) {
      // Stove wants to delete message
      myCommand = "";
      sendOK();
      WebSerial.println("<- Send OK");
    }

    rikaSerialCmdIn = "";
    rikaSerialCmdInReady = false;
  }
}
