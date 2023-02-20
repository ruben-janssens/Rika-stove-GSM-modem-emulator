#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include "arduino_secrets.h"

#define IPFIXE    // Comment this out to enable DHCP

// Serial : USB port : displays operating information
// Serial1 : RS232 port : communicates with the RIKA stove

#define baudUSB 115200
#define baudRIKA 38400
#define DELAY_SAC 25000

#define server_port 10005

#define led_comm 4
#define led_error 5
#define led_sac 16

const char *ssid =  SECRET_SSID;
const char *pass =  SECRET_PASS;
IPAddress ip(192,168,69,30);                    // fixed IP for the Arduino
IPAddress HA(192,168,69,243);                   // HA ip address
IPAddress gateway(192,168,69,1);             // gateway IP address
IPAddress mask(255,255,255,0);                // network mask
WiFiClient RIKAclient;                      // we create a client
WiFiClient client;
WiFiServer RIKAserveur = WiFiServer(server_port);

// HTTP GET requests to send to the home automation unit
const char requete[]="POST /api/webhook/" SECRET_HA_API_KEY " HTTP/1.1";
const char requeteDATE[]="GET / HTTP/1.1";  // HTTP request just to get the date


// GLOBAL VARIABLES
String requetePoele="";
volatile boolean requetePoeleComplete = false;
String requeteUSB="";
volatile boolean requeteUSBComplete = false;
String dataHTTP="";

unsigned char erreur=0;     // error number in case of command received via HTTP
String sms="NONE";
String last_sms="NONE";
String SMS_STATUS="NO SMS_STATUS";
const String numtel="+32479123456";
const String codepin="2107";
String jour="70/01/01";
String heure="01:00:00";
char recu;


void clignote(unsigned char led, unsigned char repete, unsigned int delay_on, unsigned int delay_off) {
    unsigned char i;
    for (i=0;i<repete; i++) {
        digitalWrite(led,HIGH);
        delay(delay_on);
        digitalWrite(led,LOW);
        delay(delay_off);
    }
}


void getHTTPdate (void) {
		// get the date by parsing the HTTP response from a server
    // in order to be able to date the SMS that the stove will read more or less correctly
    // The date and time are indeed transmitted during an AT+CMGR request
    // (It should be checked if a correct date is really necessary for the stove when it receives an SMS)
	   Serial.print("-> Date AA/MM/JJ: ");
	   dataHTTP.remove(0,11);							        // we remove the name of the day
	   String day=dataHTTP.substring(0,2);   			// we recover the day
	   dataHTTP.remove(0,3);  							      // we delete the day and the space
	   String month = dataHTTP.substring(0,3);  	// we get the month
	   dataHTTP.remove(0,4);  							      // we delete the month and the space
	   String year = dataHTTP.substring(0,4);			// we get the year
	   year.remove(0,2);
       dataHTTP.remove(0,5);								    // delete the year and the space
       String hour = dataHTTP.substring(0,8);
	   if (month == "Jan") {month="01";}
	   else if (month == "Jan") {month="01";}
	   else if (month == "Feb") {month="02";}
	   else if (month == "Mar") {month="03";}
	   else if (month == "Apr") {month="04";}
	   else if (month == "May") {month="05";}
	   else if (month == "Jun") {month="06";}
	   else if (month == "Jul") {month="07";}
	   else if (month == "Aug") {month="08";}
	   else if (month == "Sep") {month="09";}
	   else if (month == "Oct") {month="10";}
	   else if (month == "Nov") {month="11";}
	   else if (month == "Dec") {month="12";}
	   // we replace the hour and the day with the new data
	   jour=year;   // year in format YY and not YYYY
	   jour +="/";
	   jour +=month;
	   jour +="/";
	   jour += day;
	   heure=hour;
       Serial.print(jour);
       Serial.print(" ");
       Serial.println(heure);
}

void Serial1SendReturnChar(void) {      // we send CR + LF to the stove
    Serial1.write(char(13));
    Serial1.write(char(10));
}

void Serial1SendOK(void) {          // we send OK to the stove
    Serial1.print("OK");
    Serial.println("-> Response : OK");
    Serial.println();
    Serial1SendReturnChar();
}

void Serial1SendERROR(void) {       // we send error to the stove
    Serial1.print("ERROR");
    Serial.println("-> Response : ERROR");
    Serial.println();
    Serial1SendReturnChar();
}

////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void requestDate(bool demandeDate){
  if (RIKAclient.connect(HA, 8123)) {
      Serial.println("-> Connected to HA");
  	  RIKAclient.println(requeteDATE);  // query only for date
  	  RIKAclient.println();
  	  Serial.println("-> Date retrieval via HTTP request.");
  	  dataHTTP="";
  	  while(1) {          // we retrieve the HTTP response and we check that it is a code 200, and that the end contains OK
  	  	  if (RIKAclient.available()) {
  	  		  char c = RIKAclient.read();
  	  		  //Serial.print(c);
  	  		  //Serial.print(":");
  	  		  if ((c==13) or (c==10)) {
  	  			  //Serial.println(dataHTTP);
  	  			  if (dataHTTP.startsWith("Date")) { getHTTPdate();break;}
  	  			  dataHTTP="";

  			  } else {
    				  dataHTTP += c;
  			  }
  		  }
  	  }
  	  RIKAclient.flush();
  	  RIKAclient.stop();
  } else {
      Serial.println("Unable to connect to HA!");
      digitalWrite(led_error,HIGH);
      delay(500);
      digitalWrite(led_error, LOW);
  }

}
/*
  SerialEvent occurs whenever a new data comes in the
  hardware serial RX.  This routine is run between each
  time loop() runs, so using delay inside loop can delay
  response.  Multiple bytes of data may be available.
 */
void serialEvent1() {
  while (Serial1.available()) {
    // get the new byte:
    char inChar = (char)Serial1.read();
    // add it to the inputString:
    requetePoele += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if ((inChar == '\n') || (inChar == char(26)) || (inChar == char(13))) {
      requetePoeleComplete = true;
    }
  }
}

void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // add it to the inputString:
    requeteUSB += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      requeteUSBComplete = true;
    }
  }
}

bool isDIGIT(String chaine) {  // checks that a string contains only numbers
  bool reponse = true;
  for (unsigned int i=0;i<chaine.length();i++) {
     reponse = reponse & isDigit(chaine[i]);
  }
  return reponse;
}

void sendEnteteHTTP(unsigned char type) {
	client.println("HTTP/1.1 200 OK");
	if (type==0) {client.println("Content-Type: text/html");}
	if (type==1) {client.println("Content-Type: application/json");}
	client.println("Connection: close");  // the connection will be closed after completion of the response
	client.println();
}

String sendDonneeHTTP(int num_erreur) {
	String json="";
	if (num_erreur) {						// error: we transmit the error number in json format
		json="{\"command\":";
		json += String(num_erreur);
		json += "}";
		client.println(json);
	} else {
		if (dataHTTP=="status") {			// status: the status of the stove is transmitted, as received by SMS
			json= json="{\"status\":\"";
			json += SMS_STATUS;
			json += "\"}";
			client.println(json);
		}
		else {
			json="{\"command\":\"OK\"}";  // no error: we transmit OK
			client.println(json);
		}
	}
	client.println();
	return json;
}

void setup() {
  // Prepare the serial port
  Serial.begin(baudUSB);
  Serial1.begin(baudRIKA);
  Serial.println();
  Serial.println("Starting Rika V1.0 modem simulator ...");
  // We prepare the Inputs / Outputs
  Serial.print("-> I/O preparation : ");
  pinMode(led_comm, OUTPUT);
  pinMode(led_error, OUTPUT);
  pinMode(led_sac, OUTPUT);
  // we put the right values on the outputs
  digitalWrite(led_sac, LOW);
  digitalWrite(led_comm, LOW);
  digitalWrite(led_error, LOW);
  Serial.println("OK");
  clignote(led_comm,2,100,200);

  // network info display
  Serial.print("-> IP adress : ");
  WiFi.mode(WIFI_STA);
  WiFi.hostname("RIKA");
#ifdef IPFIXE 
  WiFi.config(ip, gateway, mask);
  WiFi.begin(ssid, pass); 
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.print(" FIXED IP !");  
  delay(300);     
#else
  if (!WiFi.begin(ssid, pass)) {                                   // IP address obtained by DHCP
    Serial.print(" DHCP ERROR: Unable to continue !");
    while(1) {
      clignote(led_error,3,100,250);
      delay(500); 
    }
  }
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
#endif
  Serial.println(WiFi.localIP());
  Serial.print("-> Mask : ");
  Serial.println(WiFi.subnetMask());
  Serial.print("-> Gateway : ");
  Serial.println(WiFi.gatewayIP());
  Serial.print("-> Hostname : ");
  Serial.println(WiFi.getHostname());
  clignote(led_error, 2,100,200);
  delay(1000);
  Serial.print("-> server on port ");
  Serial.print(server_port);
  Serial.print(" : ");
  RIKAserveur.begin();
  Serial.println("OK");
  requestDate(0);  // we make a request just to get the date
  Serial.println("-> System ready !");
  Serial.println();
  clignote(led_sac,2,100,200);
//end of the setup() loop
}

void loop() {
    // have you received a request from the stove?
    if (requetePoeleComplete) {
        Serial.print("Received : ");
        Serial.print(requetePoele);
        digitalWrite(led_comm,HIGH);
        delay(100);
        digitalWrite(led_comm,LOW);
        // it now remains to process this request
        if (requetePoele.startsWith("AT+CMGS")) {      // the stove wants to send an SMS
            // we give the prompt >
            Serial1SendReturnChar();
            Serial1.write(">");
            // display
            Serial.write("-> SMS sending ");
            Serial.write("-> Message : ");
            // we retrieve the content of the SMS
            delay(2000); // time limit to allow the stove some time to respond
            SMS_STATUS="";
            recu=0;
            while (recu != char(26)) {
                if (Serial1.available()) {
                    recu = (char)Serial1.read();
                    if (recu != char(26)) {   // ctrl+z (ASCII 26) to end the SMS
                        SMS_STATUS+=recu;
                    }
                }
            }
            // display
            Serial.println(SMS_STATUS);
            Serial.println("-> +CMGS : 01");
            // reponse
            Serial1SendReturnChar();
            Serial1.print("+CMGS : 01");
            Serial1SendReturnChar();
            Serial1SendOK();
        }
        else if (requetePoele.startsWith("AT+CMGR")) {      // the stove wants to read an SMS
            // display
            Serial.print("-> SMS reading ");
            Serial.print("-> Message : ");
            Serial.println(sms);
            if (sms != "NONE") {
                //Serial.println();
            	requestDate(0); // false request to get the real date
            	Serial.print("-> +CMGR: \"REC READ\",\"");
                Serial.print(numtel);
                Serial.print("\",,\"");
                Serial.print(jour);
                Serial.print(",");
                Serial.print(heure);
                Serial.println("+08\"");
                Serial.print("-> real SMS : ");
                Serial.print(codepin);
                Serial.print(" ");
                Serial.println(sms);
                // message for the stove
                Serial1SendReturnChar();
                Serial1.print("+CMGR: \"REC READ\",\"");
                Serial1.print(numtel);
                Serial1.print("\",,\"");
                Serial1.print(jour);
                Serial1.print(",");
                Serial1.print(heure);
                Serial1.print("+08\"");
                Serial1SendReturnChar();
                Serial1.print(codepin);
                Serial1.print(" ");
                Serial1.print(sms);
                Serial1SendReturnChar();
                Serial1SendReturnChar();
                Serial1SendOK();
                
            } else {                  // the SMS is none: we have no command to send
                Serial1SendReturnChar();
                Serial1SendOK();
            }
        }
        else if (requetePoele.startsWith("AT+CMGD")) {      // the stove wants to delete the SMS
            last_sms=sms;
            sms="NONE";
            Serial.print("-> SMS deletion  ");
            Serial.print("-> Message : ");
            Serial.println(sms);
            Serial1SendOK();
            
        }
        else if (requetePoele.startsWith("ATE0") or requetePoele.startsWith("AT+CNMI")  or requetePoele.startsWith("AT+CMGF") ) {  // configuration request: we answer OK without asking questions
            Serial1SendOK();
        }
        else if (requetePoele!="" && requetePoele!="\n" && requetePoele!="\x1A" && requetePoele!= "\x0D" ) {
           Serial1SendERROR();
           digitalWrite(led_error,HIGH);
           delay(500);
           digitalWrite(led_error,LOW);
        }
        // we reset everything for the next request
        requetePoele= "";
        requetePoeleComplete = false;
    }

    // did we receive an http request on the server?
    client = RIKAserveur.available();
    if (client) {
        Serial.println("HTTP request received ...");
        Serial.print("-> Date : ");
        Serial.print(jour);
        Serial.print(" ");
        Serial.println(heure);
        dataHTTP="";
        digitalWrite(led_comm,HIGH);
        delay(200);
        digitalWrite(led_comm,LOW);
        while (client.connected()) {
          if (client.available()) {
              char c = client.read();
              if ((c==13) or (c==10)) {
                  //Serial.println(dataHTTP);
                  if (dataHTTP.startsWith("GET /")) {
                      Serial.println("-> GET ");
                      Serial.print("-> command : ");
                      dataHTTP.remove(0,5);
                      char i=dataHTTP.indexOf(" ");
                      if (i) {dataHTTP.remove(i);}
                      Serial.println(dataHTTP);
                      // we will react according to the command received
                      if (dataHTTP.startsWith("ON"))  {
                        dataHTTP="ON";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("OFF"))  {
                        dataHTTP="OFF";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("TEL"))  {
                        dataHTTP="TEL";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("room"))  {
                        dataHTTP="room";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("heat"))  {
                        dataHTTP="heat";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("auto"))  {
                        dataHTTP="auto";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("status"))  {
                    	dataHTTP="status";
                    	erreur=0;			
                      }
                      else if (dataHTTP.startsWith("r"))  {
                        // we retrieve the number behind, and we check that it is valid
                        dataHTTP.remove(0,1);
                        String nombre=dataHTTP;
                        //Serial.print(nombre);
                        if (isDIGIT(nombre)) {
                            int valeur=nombre.toInt();
                            //Serial.println(valeur);
                            if (valeur < 5) {valeur=5;}
                            if (valeur > 28) {valeur=28;}
                            //Serial.println(valeur);
                            // we rebuild the command correctly
                            dataHTTP="r";
                            dataHTTP += String(valeur, DEC);
                            erreur=0;
                        }
                        else {erreur=1;}  // if problem in the number

                      }
                      else if (dataHTTP.startsWith("h")) {
                        // we retrieve the number behind, and we check that it is valid
                        dataHTTP.remove(0,1);
                        String nombre=dataHTTP;
                        //Serial.print(nombre);
                        if (isDIGIT(nombre)) {
                            int valeur=nombre.toInt();
                            //Serial.println(valeur);
                            if (valeur < 30) {valeur=30;}
                            if (valeur > 100) {valeur=100;}
                            valeur=(valeur/5)*5;
                            //Serial.println(valeur);
                            // we rebuild the command correctly
                            dataHTTP="h";
                            dataHTTP += String(valeur, DEC);
                            erreur=0;
                        }
                        else {erreur=1;}  // default value if problem in the number



                     }
                     else {
                        erreur=2;
                     }
                     if (!erreur) {				// correct message received
                        if (dataHTTP != "status") {  // if the command was not "status", an SMS is sent to the stove
                        	sms=dataHTTP;
                        	Serial.print("-> SMS to send to the stove : ");
                        	Serial.println(sms);
                        }

                        sendEnteteHTTP(1);								// we send the header
                        Serial.print("-> HTTP response sent : ");
                        Serial.println(sendDonneeHTTP(erreur));			// send the response
                        Serial.println("-> Disconnected");
                        client.flush();
                        client.stop();									// we close the connection
                        Serial.println();

                     } else {
                        
                        if (erreur ==1) {
                          Serial.print("-> error no.");
                          Serial.print(erreur);
                          Serial.print(" - invalid number ");
                          Serial.println(dataHTTP);
                        }
                        else if (erreur ==2) {
                          Serial.print("-> error no.");
                          Serial.print(erreur);
                          Serial.print(" - invalid command ");
                          Serial.println(dataHTTP);
                        }
                        digitalWrite(led_error,HIGH);
                        delay(500);
                        digitalWrite(led_error,LOW);
                        Serial.println("-> No SMS transmitted");
                        sendEnteteHTTP(1);								// we send the header
                        Serial.print("-> HTTP response sent : ");
                        Serial.println(sendDonneeHTTP(erreur));			// send the response
                        Serial.println("-> Disconnected");
                        client.flush();
                        client.stop();									// we close the connection
                        Serial.println();
                     }
                  }
                  dataHTTP="";
              } else {
                  dataHTTP += c;
              }
          }
        }
    }
    client.stop();


    // did we receive a request via the USB port?
    if (requeteUSBComplete) {
        digitalWrite(led_comm,HIGH);
        delay(100);
        digitalWrite(led_comm,LOW);
        if (requeteUSB.startsWith("IP")) {
            Serial.print("The IP address is : ");
            Serial.println(WiFi.localIP());
            Serial.println();
        }
        else if (requeteUSB.startsWith("SMS")) {
            Serial.println("The last SMS sent to the stove is :");
            Serial.println(last_sms);
            Serial.println();
        }
        else if (requeteUSB.startsWith("SMS_STATUS")) {
            Serial.println("The last SMS_STATUS received from the stove is :");
            Serial.println(SMS_STATUS);
            Serial.println();
        }
        else
        {
            Serial.println("Menu :");
            Serial.println("IP  -> show IP address");
            Serial.println("SMS -> displays the last SMS sent or received");
            Serial.println();
            digitalWrite(led_error,HIGH);
            delay(500);
            digitalWrite(led_error,LOW);
        }
        // we reset the buffer to zero
        requeteUSBComplete=false;
        requeteUSB="";
    }
// end of the loop() loop
}
