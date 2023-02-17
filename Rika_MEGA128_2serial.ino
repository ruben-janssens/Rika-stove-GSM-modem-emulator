#include "Arduino.h"
// Ethernet shield uses pins
// 10 : SS    for Ethernet
//  4 : SS    for SD Card
// 50 : MISO
// 51 : MOSI
// 52 : SCK
// 53 : SS    must be configured in output, although not used by the W5100 shield otherwise the SPI interface does not work
//            according to the official Arduino doc
//

// be careful, you must use the modified Ethernet library (otherwise no hostname)
// https://github.com/technofreakz/Ethernet/archive/master.zip

#include <Ethernet.h>
#include <SPI.h>

#define IPFIXE    // to be commented out to use DHCP

// Serial : USB port : displays operating information
// Serial1 : RS232 port : communicates with the RIKA stove

#define baudUSB 115200
#define baudRIKA 38400
#define DELAY_SAC 25000

#define server_port 10005

#define button 9
#define led_comm 7
#define led_error 5
#define led_sac 3      //13 pour les essais

// ETHERNET NETWORK PARAMETERS
// to save space in memory, we will not use the DNS
// you must therefore provide the IP address of the machines to be contacted
//
// To increase the number of bags, we make an http request
// http://#IP_JEEDOM#/core/api/jeeApi.php?apikey=#APIKEY#&type=scenario&id=#ID#&action=#ACTION#
// ACTION can be start, stop, desaction or activate

byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xEF};   // arduino mac address
IPAddress ip(192,168,1,30);                     // fixed IP for the Arduino
IPAddress jeedom(192,168,1,20);                 // jeedom ip address
IPAddress mydns(192,168,1,254);                 // DNS IP address
IPAddress mygateway(192,168,1,254);             // gateway IP address
IPAddress mymask(255,255,255,0);                // network mask
EthernetClient RIKAclient;                      // we create a client
EthernetClient client;
EthernetServer RIKAserveur = EthernetServer(port_serveur);

// HTTP GET requests to send to the home automation unit
const char requete[]="GET /core/api/jeeApi.php?apikey=xxxxxxxxxxxxxxxxxx&type=scenario&id=50&action=start HTTP/1.0";
const char requeteDATE[]="GET / HTTP/1.0";  // HTTP request just to get the date


// GLOBAL VARIABLES
String requetePoele="";
volatile boolean requetePoeleComplete = false;
String requeteUSB="";
volatile boolean requeteUSBComplete = false;
String dataHTTP="";
bool old_b_status=1;   // pellet trap button status 1= closed, 0= open
long chrono_start=0;
long chrono_stop=0;
long duree_ouverture=0;
unsigned char sacs_verses=0;

unsigned char erreur=0;     // error number in case of command received via HTTP
String sms="NONE";
String last_sms="NONE";
String STATUS="NO STATUS";
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

void send_retour(void) {      // we send CR + LF to the stove
    Serial1.write(char(13));
    Serial1.write(char(10));
}
void send_OK(void) {          // we send OK to the stove
    Serial1.print("OK");
    Serial.println("-> Response : OK");
    Serial.println();
    send_retour();
}
void send_ERROR(void) {       // we send error to the stove
    Serial1.print("ERROR");
    Serial.println("-> Response : ERROR");
    Serial.println();
    send_retour();
}

////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void inc_nb_sacs(bool demandeDate){
  // we warn jeedom
  // if demandeDate = 0, only the date is requested
  // if demandeDate = 1 we make a real request to increase the number of bags
  if (RIKAclient.connect(jeedom, 80)) {
      Serial.println("-> Connected to jeedom");
      if (!demandeDate) {
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
      }
      else {
    	  RIKAclient.println(requete);  // request to the Jeedom bag counter

			  RIKAclient.println();
			  Serial.println("-> Request transmitted");
			  Serial.print("-> HTTP response : ");
			  dataHTTP="";
			  while(1)   {          // we retrieve the HTTP response and we check that it is a code 200, and that the end contains OK
				  if (RIKAclient.available()) {
					  char c = RIKAclient.read();
					  //Serial.print(c);
					  //Serial.print(":");
					  if ((c==13) or (c==10)) {
						  //Serial.println(dataHTTP);
						  if (dataHTTP.startsWith("Date")) { getHTTPdate();}
						  if (dataHTTP.endsWith("200 OK")) {
							  Serial.println(dataHTTP);
						  }
						  dataHTTP="";
					  } else {
						  dataHTTP += c;
					  }
				  }
				  if (!RIKAclient.connected()) {
					  // we retrieve the last data (after the HTTP header)
					  while (RIKAclient.available()) {
						  char c = RIKAclient.read();
						  dataHTTP += c;
					  }
					  // the last data received is displayed
					  if (dataHTTP.startsWith("ok")) {
						Serial.print("-> Bag taken into account : ");
					  }
					  else {
						Serial.print("-> ERROR: bag not taken into account : ");
					  }
					  Serial.println(dataHTTP);
					  // we close the connection
					  Serial.println("-> Disconnect.");
					  RIKAclient.flush();
					  RIKAclient.stop();
					  break;
				  }



			  }
      }
  } else {
      Serial.println("Unable to connect to jeedom!");
      digitalWrite(led_erreur,HIGH);
      delay(500);
      digitalWrite(led_erreur, LOW);
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
			json += STATUS;
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
  requetePoele.reserve(254);
  dataHTTP.reserve(512);
  requeteUSB.reserve(10);
  sms.reserve(100);
  // We prepare the Inputs / Outputs
  Serial.print("-> I/O preparation : ");
  pinMode(53, OUTPUT);            // necessary to operate the W5100 Ethernet shield on a Mega1280 (or 2560) card
  pinMode(4, OUTPUT);             // needed to disable Ethernet shield SD card and enable Ethernet
  digitalWrite(4,HIGH);           // needed to disable Ethernet shield SD card and enable Ethernet
  //pinMode(10, OUTPUT);          // needed to disable ethernet port and enable SD card
  //digitalWrite(10,HIGH);        // needed to disable ethernet port and enable SD card
  pinMode(led_comm, OUTPUT);
  pinMode(led_erreur, OUTPUT);
  pinMode(led_sac, OUTPUT);
  pinMode(bouton, INPUT_PULLUP);
  // we put the right values on the outputs
  digitalWrite(led_sac, LOW);
  digitalWrite(led_comm, LOW);
  digitalWrite(led_erreur, LOW);
  Serial.println("OK");
  clignote(led_comm,2,100,200);

  // network info display
  Serial.print("-> IP adress : ");
#ifdef IPFIXE 
  Ethernet.begin(mac,ip,mydns,mygateway,mymask);
  Serial.print(" FIXED IP !");  
  delay(300);     
#else
  if (!Ethernet.begin(mac)) {                                   // IP address obtained by DHCP
    Serial.print(" DHCP ERROR: Unable to continue !");
    while(1) {
      clignote(led_erreur,3,100,250);
      delay(500); 
    }
  }
#endif
  Ethernet.hostName("RIKA");
  Serial.println(Ethernet.localIP());
  Serial.print("-> Mask : ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("-> Gateway : ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("-> DNS : ");
  Serial.println(Ethernet.dnsServerIP());
  Serial.print("-> Hostname : ");
  Serial.println(Ethernet.getHostName());
  clignote(led_erreur, 2,100,200);
  delay(1000);
  Serial.print("-> server on port ");
  Serial.print(port_serveur);
  Serial.print(" : ");
  RIKAserveur.begin();
  Serial.println("OK");
  inc_nb_sacs(0);  // we make a request just to get the date
  Serial.println("-> System ready !");
  Serial.println();
  clignote(led_sac,2,100,200);
//end of the setup() loop
}

void loop() {
    // did you press the button?
    bool b_status = digitalRead(button);
    if (b_status == 0) {  // hatch open, should the LEDs flash to indicate that a bag is going to be taken into account?
        duree_ouverture = (millis() - chrono_start) / 1000;
        if (duree_ouverture >=20 and duree_ouverture < 35) {
          clignote(led_sac, 1, 100,500);
          delay(500);
        } else if (duree_ouverture >=35 and duree_ouverture <= 60) {
          clignote(led_sac, 2, 100,250);
          delay(500);
        }
    }
    if (b_status != old_b_status) {  // change of state
        delay(50);    //small anti-bounce delay
        if (b_status == 0) {
            duree_ouverture = 0;
            old_b_status= b_status;
            chrono_start=millis();
            //Serial.println(chrono_start,DEC);
            Serial.println();
            Serial.println("Opening of the reserve ...");
        }
        else {
          old_b_status=b_status;
          Serial.println("-> Closure of the reserve ...");
          Serial.print("-> Duration : ");
          chrono_stop = millis();
          //Serial.println(chrono_stop,DEC);
          duree_ouverture = (chrono_stop - chrono_start) / 1000;
          Serial.print(duree_ouverture, DEC);
          Serial.println(" s");
          if (duree_ouverture >=20 and duree_ouverture <35) {
             sacs_verses = 1;
          } else if (duree_ouverture >= 35 and duree_ouverture <=60) {
             sacs_verses = 2;
          }
          else {
            sacs_verses=0;
          }
          if (sacs_verses) {
            Serial.print("-> ");
            Serial.print(sacs_verses);
            if (sacs_verses >1) {Serial.println(" shed bags");} else {Serial.println(" single bag shed");}
            for (int i=0; i<sacs_verses; i++) {
                inc_nb_sacs(1);      // the information is transmitted to the home automation unit (HTTP request)
            }
          } else {
            Serial.println("-> No bag poured (opening too short or too long).");
            Serial.println();
          }
      }
       
             
       
    }

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
            send_retour();
            Serial1.write(">");
            // display
            Serial.write("-> SMS sending ");
            Serial.write("-> Message : ");
            // we retrieve the content of the SMS
            delay(2000); // time limit to allow the stove some time to respond
            STATUS="";
            recu=0;
            while (recu !=char(26)) {
                if (Serial1.available()) {
                    recu = (char)Serial1.read();
                    if (recu != char(26)) {   // ctrl+z (ASCII 26) to end the SMS
                        STATUS+=recu;
                    }
                }
            }
            // display
            Serial.println(STATUS);
            Serial.println("-> +CMGS : 01");
            // reponse
            send_retour();
            Serial1.print("+CMGS : 01");
            send_retour();
            send_OK();
        }
        else if (requetePoele.startsWith("AT+CMGR")) {      // the stove wants to read an SMS
            // display
            Serial.print("-> SMS reading ");
            Serial.print("-> Message : ");
            Serial.println(sms);
            if (sms != "NONE") {
                //Serial.println();
            	inc_nb_sacs(0); // false request to get the real date
            	Serial.print("-> +CMGR: \"REC READ\",\"");
                Serial.print(numtel);
                Serial.print("\",,\"");
                Serial.print(jour);
                Serial.print(",");
                Serial.print(heure);
                Serial.println("+08\"");
                Serial.print("-> SMS réel : ");
                Serial.print(codepin);
                Serial.print(" ");
                Serial.println(sms);
                // message for the stove
                send_retour();
                Serial1.print("+CMGR: \"REC READ\",\"");
                Serial1.print(numtel);
                Serial1.print("\",,\"");
                Serial1.print(jour);
                Serial1.print(",");
                Serial1.print(heure);
                Serial1.print("+08\"");
                send_retour();
                Serial1.print(codepin);
                Serial1.print(" ");
                Serial1.print(sms);
                send_retour();
                send_retour();
                send_OK();
                
            } else {                  // the SMS is none: we have no command to send
                send_retour();
                send_OK();
            }
        }
        else if (requetePoele.startsWith("AT+CMGD")) {      // the stove wants to delete the SMS
            last_sms=sms;
            sms="NONE";
            Serial.print("-> SMS deletion  ");
            Serial.print("-> Message : ");
            Serial.println(sms);
            send_OK();
            
        }
        else if (requetePoele.startsWith("ATE0") or requetePoele.startsWith("AT+CNMI")  or requetePoele.startsWith("AT+CMGF") ) {  // configuration request: we answer OK without asking questions
            send_OK();
        }
        else if (requetePoele!="" && requetePoele!="\n" && requetePoele!="\x1A" && requetePoele!= "\x0D" ) {
           send_ERROR();
           digitalWrite(led_erreur,HIGH);
           delay(500);
           digitalWrite(led_erreur,LOW);
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
                        digitalWrite(led_erreur,HIGH);
                        delay(500);
                        digitalWrite(led_erreur,LOW);
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
            Serial.println(Ethernet.localIP());
            Serial.println();
        }
        else if (requeteUSB.startsWith("SMS")) {
            Serial.println("The last SMS sent to the stove is :");
            Serial.println(last_sms);
            Serial.println();
        }
        else if (requeteUSB.startsWith("STATUS")) {
            Serial.println("The last STATUS received from the stove is :");
            Serial.println(STATUS);
            Serial.println();
        }
        else if (requeteUSB.startsWith("BAG")) {
            Serial.println("Adding a bag ...");
            inc_nb_sacs(1);
            Serial.println();
        }
        else
        {
            Serial.println("Menu :");
            Serial.println("IP  -> show IP address");
            Serial.println("SMS -> displays the last SMS sent or received");
            Serial.println("BAG -> added bag for Jeedom");
            Serial.println();
            digitalWrite(led_erreur,HIGH);
            delay(500);
            digitalWrite(led_erreur,LOW);
        }
        // we reset the buffer to zero
        requeteUSBComplete=false;
        requeteUSB="";
    }
// end of the loop() loop
}
