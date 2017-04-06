// Version 1.10
// Pinbelegung:
// D1 : I²C (SCL)
// D2 : I²C (SDA)
// D3 : DS18B20 Sensor
// D5 : Heizung Relais
// D6 : Rührwerk Relais
// D7 : Pumpe Relais
// D8 : Alarm Relais

// PCF8574 P3 : Alarmschalter
// PCF8574 P4 : Ruehrwerksschalter
// PCF8574 P5 : Pumpenschalter
// PCF8574 P6 : Heizungsschalter
// PCF8574 P7 : Automatikschalter

// Arduino lauscht auf Port 5002 nach UDP Strings
// Beschreibung des seriellen Strings:
// Serieller String ist immer 19stellig
// Beispiel: C$%"dp/1----------c
// 1.    Zeichen: "C" Startzeichen
// 2.    Zeichen: Relaisstatus
// 3.    Zeichen: Programmstatus und Sensortyp
// 4.    Zeichen: Solltemperatur
// 5.+6. Zeichen: Isttemperatur
// 7.+8. Zeichen: Status der Zusatzfunktionen
// 9.-18.Zeichen: Frei für weitere Funktionen
// 19.   Zeichen: "c" Stopzeichen

// Durch Setzen der Variable "ExterneSteuerung" kann die Brauerei auf dem Rechner gestartet, gestoppt oder pausiert werden
// t= keine Funktion
// s= Start
// p= Pause
// e= Stop


#include <OneWire.h>                 // Kommt von http://www.pjrc.com/teensy/td_libs_OneWire.html
#include <ESP8266WiFi.h>             // Kommt von http://www.wemos.cc/tutorial/get_started_in_arduino.html
#include <WiFiClient.h>              // Kommt von http://www.wemos.cc/tutorial/get_started_in_arduino.html       
#include <WiFiUDP.h>                 // Kommt von http://www.wemos.cc/tutorial/get_started_in_arduino.html
#include <ESP8266WebServer.h>        // Kommt von http://www.wemos.cc/tutorial/get_started_in_arduino.html
#include <LiquidCrystal_I2C.h>       // Kommt von http://arduino-info.wikispaces.com/LCD-Blue-I2C - VERSION 1.2.1 verwenden !!!!!!!!!
#include <Wire.h>                    // Kommt von Arduino IDE

// Einstellungen für Messungen und Meldungen
#define ZEITINTERVALL_MESSUNGEN 2          // Abfragezyklus der Sensoren in Sekunden
#define ZEITINTERVALL_MELDUNGEN 5          // Sendeintervall an die Brauerei in Sekunden
#define PCF8574 60                         // Dezimal-Adresse des PCF8574

// ******* Netzwerkeinstellungen, bitte anpassen! *******
const char* ssid = "FRITZ!Box 7490";               // SSID des vorhandenen WLANs
const char* password = "meinsbekommtihrnicht";     // Passwort für das vorhandene WLAN
IPAddress gateway(192,168,178,1);                  // IP-Adresse des WLAN-Gateways
IPAddress subnet(255,255,255,0);                   // Subnetzmaske
IPAddress ip(192,168,178,223);                     // feste IP-Adresse für den WeMos
IPAddress UDPip(192,168,178,255);                  // IP-Adresse an welche UDP-Nachrichten geschickt werden xx.xx.xx.255 = Alle Netzwerkteilnehmer die am Port horchen.
unsigned int localPort = 5002;                     // Port auf dem nach Brauerei-Daten gehorcht wird
unsigned int answerPort = 5003;                    // Port auf den Temperaturen geschickt werden
ESP8266WebServer server(80);                       // Webserver initialisieren auf Port 80
WiFiUDP Udp;

OneWire ds(D3);                                    // an pin D3

LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  // Set the LCD I2C address

const boolean aus = LOW;                           // Hier kann bei Low-Aktiven Relaiskarten einfach High und Low vertauscht werden
const boolean an = HIGH;                           // Hier kann bei Low-Aktiven Relaiskarten einfach High und Low vertauscht werden

const int Heizung = D5;                            // Im folgenden sind die Pins der Sensoren und Aktoren festgelegt
const int Ruehrwerk = D6;
const int Pumpe = D7;
const int Summer = D8;


byte heat[8] = {  // Heizungssymbol erstellen
  B01001,
  B10010,
  B01001,
  B10010,
  B00000,
  B11111,
  B01110,
  B00000
};

byte ruehr[8] = {  // Rührwerkssymbol erstellen
  B11111,
  B00100,
  B00100,
  B00100,
  B10101,
  B11111,
  B10101,
  B00000
};

byte alarm[8] = {  // Alarmsymbol erstellen
  B00000,
  B00100,
  B01110,
  B01110,
  B01110,
  B11111,
  B00100,
  B00000
};

byte pumpe[8] = {  // Pumpensymbol erstellen
  B11111,
  B01110,
  B10001,
  B10001,
  B10001,
  B01110,
  B11111,
  B00000
};

char ConnectString[255] = "";
char ExterneSteuerung = 't';
String SensorString = "";
String Antwort = "";
unsigned long deltaMessungSekunden = ZEITINTERVALL_MESSUNGEN; //Zeitintervall (Sekunden) nach dem eine Messung erfolgt
unsigned long deltaMeldungSekunden = ZEITINTERVALL_MELDUNGEN; // Zeitintervall (Sekunden) nach dem eine CCU-Meldung erfolgt (0 bedeutet nie)
unsigned long jetztMillis = 0;
unsigned long letzteUDPMillis = 0;
unsigned long deltaMessungMillis = deltaMessungSekunden * 1000, letzteMessungMillis = 0;
unsigned long deltaMeldungMillis = deltaMeldungSekunden * 1000, letzteMeldungMillis = 0;
String antwort = "", meldung = "";
float temp = 0, tempCCU = 0;
float Temp = 0.0;
float externeisttemp = 0.0;
float NewTemp[10] =  {-255,-255,-255,-255,-255,-255,-255,-255,-255,-255};
float OldTemp[10] = {-255,-255,-255,-255,-255,-255,-255,-255,-255,-255};
char charVal[8];
char packetBuffer[24];   // buffer to hold incoming packet,
char temprec[24] = "";
char relais[5] = "";
char sensor= 'D';
char state[3] = "";
int solltemp = 0;
boolean Funktionslog[10];
  
String zeitstempel()
{ // Betriebszeit als Stunde:Minute:Sekunde
  char stempel[10];
  int lfdStunden = millis()/3600000;
  int lfdMinuten = millis()/60000-lfdStunden*60;
  int lfdSekunden = millis()/1000-lfdStunden*3600-lfdMinuten*60;
  sprintf (stempel,"%03d:%02d:%02d", lfdStunden, lfdMinuten, lfdSekunden);
  return stempel;
}

void Funktion1()           // Individuelle Funktion
{
//  ExterneSteuerung = 's'; // Beispiel für ExterneSteuerung
//  if ( Temp > 31 ) { ExterneSteuerung = 'p'; } // Beispiel für ExterneSteuerung
//  if ( Temp > 33 ) { ExterneSteuerung = 'e'; } // Beispiel für ExterneSteuerung
}

void Funktion2()           // Individuelle Funktion
{

}

void Funktion3()           // Individuelle Funktion
{

}

void Funktion4()           // Individuelle Funktion
{

}

void Funktion5()           // Individuelle Funktion
{

}

void Funktion6()           // Individuelle Funktion
{

}

void Funktion7()           // Individuelle Funktion
{

}

void Funktion8()           // Individuelle Funktion
{

}

void Funktion9()           // Individuelle Funktion
{

}

void Funktion10()           // Individuelle Funktion
{

}

void noFunktion()
{
//  ExterneSteuerung == 't'; // Beispiel für ExterneSteuerung
}
 
void WLANVerbindung()
{
  // WLAN-Verbindung herstellen
  WiFi.config(ip, gateway, subnet); // auskommentieren, falls eine dynamische IP bezogen werden soll
  WiFi.begin(ssid, password);
  Serial.print("Verbindungsaufbau");

  Serial.println(" erfolgreich!");
  Serial.println("");     
  Serial.print("Verbunden mit: ");
  Serial.println(ssid);
  Serial.print("Signalstaerke: ");
  int rssi = WiFi.RSSI();
  Serial.print(rssi);
  Serial.println(" dBm");
  Serial.print("IP-Adresse: ");
  Serial.println(WiFi.localIP());
  Serial.println("");

  sprintf(ConnectString, "</br></br>Verbunden mit: %s </br>Signalstaerke: %d dBm  </br></br>", ssid, rssi);

  server.on("/", Hauptseite);
  server.begin();            // HTTP-Server starten

  Udp.begin(localPort);
}

void setup(void)
{
  pinMode(Heizung, OUTPUT);       // Im folgenden werden die Pins als I/O definiert
  pinMode(Summer, OUTPUT);
  pinMode(Ruehrwerk, OUTPUT);
  pinMode(Pumpe, OUTPUT);

  Wire.begin();
  
  Serial.begin(9600);     // seriellen port starten

  WLANVerbindung();

  for (int schleife = 0; schleife < 23; schleife++) { temprec[schleife] = ' '; }

  lcd.begin(20,4);         // initialize the lcd for 20 chars 4 lines and turn on backlight
  lcd.backlight();

  lcd.createChar(1, heat);         // Aktivierung der eigenen Zeichen
  lcd.createChar(2, ruehr);
  lcd.createChar(3, alarm);
  lcd.createChar(4, pumpe);
  
  lcd.setCursor(0, 0);                // Startbildschirm
  lcd.print("      Brauerei      ");
  lcd.setCursor(0, 1);          
  lcd.print("Arduino  WLAN Server");
  lcd.setCursor(0, 2);
  lcd.print("       V1.10        ");
  lcd.setCursor(0, 3);
  lcd.print("     by emilio      ");
  delay(3000);
  lcd.clear();
  lcd.setCursor(0, 0);              
  lcd.write("I:      ");
  lcd.write(223);
  lcd.print("C  S:    ");
  lcd.write(223);
  lcd.print("C");
}

bool Contains(String s, String search)
{
  int max = s.length() - search.length();

  for (int i = 0; i <= max; i++) {
  if (s.substring(i) == search) return true; // or i
  }

  return false; //or -1
}

void ListSensors(void)
{         
  int count=0,i=0;
  byte addr[8];
  char Adress[10][255] = {"","","","","","","","","",""};
  SensorString = "";
  while(ds.search(addr))
  {
    sprintf(Adress[count], "");
    for( i = 0; i < 8; i++)
    {
      sprintf(Adress[count], "%s %X", Adress[count],addr[i]);
    }
    count++;
  }

  for(count = 0; count<10;count++)
  {
    if(strcmp(Adress[count], ""))
    {
      SensorString += Adress[count];
      SensorString += "   Wert: ";
      Temp = GetTemperature(Adress[count], true);
      SensorString += Temp;
      UDPOut();
    }
  }

  letzteMessungMillis = jetztMillis;
  ds.reset_search();
}
   
void Hauptseite()
{
  char dummy[8];
  
  Antwort = "";
  Antwort += "<meta http-equiv='refresh' content='60'/>";
  Antwort += "<font face=";
  Antwort += char(34);
  Antwort += "Courier New";
  Antwort += char(34);
  Antwort += ">";
   
  Antwort += "<b>Gefundene Sensoren: </b>\n</br>";
  Antwort += SensorString;
  Antwort += "\n</br>\n</br>";
  
  Antwort += "<b>Aktuelle Temperatur: </b>\n</br>";
  
  if (sensor=='d') {dtostrf(externeisttemp, 5, 1, dummy);} else {dtostrf(Temp, 5, 1, dummy);}  
  Antwort += dummy;
  Antwort += " ";
  Antwort += char(176);
  Antwort += "C\n</br>";

  Antwort += "\n</br><b>Schaltstatus: </b>\n</br>Heizung:&nbsp;&nbsp;";
  if (relais[1] == 'H') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort +="R"; Antwort +=char(252); Antwort +="hrwerk:&nbsp;";
  if (relais[2] == 'R') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort += "Pumpe:&nbsp;&nbsp;&nbsp;&nbsp;";
  if (relais[3] == 'P') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort += "Alarm:&nbsp;&nbsp;&nbsp;&nbsp;";
  if (relais[4] == 'A') { Antwort +="Ein\n</br>"; } else { Antwort +="Aus\n</br>"; }
  Antwort +="\n</br><b>Brauereistatus: </b>\n</br>";
  if (state[1]=='o') { Antwort +="OFFLINE "; }        
  else if (state[1]=='x') { Antwort +="INAKTIV"; }
  else if (state[1]=='y') { Antwort +="AKTIV"; }
  else if (state[1]=='z') { Antwort +="PAUSIERT"; }
  Antwort +="\n</br>\n</br><b>Zusatzfunktionen: </b>\n</br>";
  for (int i=1; i<=10; i++) { if (Funktionslog[i]==false) {Antwort +="0";} else {Antwort +="1";} }
  Antwort +="\n</br>";      
  Antwort += ConnectString;
  Antwort += "</font>";
  
  server.send ( 300, "text/html", Antwort );
}

void UDPRead()
{
  int packetSize = Udp.parsePacket();
  if (packetSize)
  {
    for (int schleife = 0; schleife < 23; schleife++) { temprec[schleife] = ' '; }
    // read the packet into packetBufffer
    Udp.read(packetBuffer, packetSize);
    for (int schleife = 0; schleife < 23; schleife++) { temprec[schleife] = packetBuffer[schleife]; }
    letzteUDPMillis = millis();
    packetAuswertung();
  }
}    

void UDPOut()
{
  dtostrf(Temp, 3, 1, charVal);
  Udp.beginPacket(UDPip, answerPort);
  Udp.write('T');
  Udp.write(charVal);
  if (ExterneSteuerung != 'p' && ExterneSteuerung != 's' && ExterneSteuerung != 'e') { ExterneSteuerung = 't'; }
  Udp.write(ExterneSteuerung);
  Udp.println();
  Udp.endPacket();
}

void OfflineCheck()
{
  if (jetztMillis > letzteUDPMillis+10000) 
  {
    relais[1]='h';      
    relais[2]='r';               
    relais[3]='p'; 
    relais[4]='a';       
    state[1]='o';
    solltemp=0;
    for (int i=1; i<=10; i++) {Funktionslog[i]=false;}
  }
}

void ManuellerStatus()
{
  byte x=255;
  Wire.requestFrom(PCF8574,1); 
  if(Wire.available()){x=Wire.read();}          //Receive the data 
  if (!bitRead(x,7))                            // Wenn das 8te Bit "0", Schalter also auf "manuell" ist, dann...
  {
    solltemp=0;
    state[1]='m';
    if (bitRead(x,6)) {relais[1] = 'h';} else {relais[1] = 'H';}    // Wenn das 7te Bit HIGH ist Relais "Ein" sonst "Aus"
    if (bitRead(x,5)) {relais[2] = 'r';} else {relais[2] = 'R';}    // Wenn das 6te Bit HIGH ist Relais "Ein" sonst "Aus"
    if (bitRead(x,4)) {relais[3] = 'p';} else {relais[3] = 'P';}    // usw.
    if (bitRead(x,3)) {relais[4] = 'a';} else {relais[4] = 'A';}
  }  
}

void LCDOut()
{
  lcd.setCursor(2, 0);
  float TempOut = 0;
  if (sensor=='d') {TempOut=externeisttemp;} else {TempOut=Temp;}          
  if (TempOut > -10 & TempOut < 0) { lcd.print(" "); } 
  else if (TempOut >= 0 & TempOut < 10) { lcd.print("  "); } 
  else if (TempOut >= 10 & TempOut < 100) { lcd.print(" "); }
  lcd.print(TempOut, 1);

  lcd.setCursor(15, 0);         
  if (solltemp >= 0 & solltemp < 10) { lcd.print(" "); } 
  if (solltemp > 0 & solltemp < 100) { lcd.print(solltemp); }  
  else { lcd.print("-"); }
  
  lcd.setCursor(13, 1);              
  if (relais[1]=='H') { lcd.write(1); lcd.print(" "); } else { lcd.print("  "); } 
  lcd.setCursor(15, 1);     
  if (relais[2]=='R') { lcd.write(2); lcd.print(" "); } else { lcd.print("  "); } 
  lcd.setCursor(17, 1);              
  if (relais[3]=='P') { lcd.write(4); lcd.print(" "); } else { lcd.print("  "); } 
  lcd.setCursor(19, 1);     
  if (relais[4]=='A') { lcd.write(3); } else { lcd.print(" "); } 

  lcd.setCursor(0, 1);
  if (state[1]=='o') { lcd.print("OFFLINE "); }        
  else if (state[1]=='x') { lcd.print("INAKTIV "); }
  else if (state[1]=='y') { lcd.print("AKTIV   "); }
  else if (state[1]=='z') { lcd.print("PAUSIERT"); }
  else if (state[1]=='m') { lcd.print("MANUELL "); }

  lcd.setCursor(0, 3);
  lcd.print("Funktion: ");
  for (int i=1; i<=10; i++) { if (Funktionslog[i]==false) {lcd.print("0");} else {lcd.print("1");} }
    
}

void RelaisOut()
{
  if (relais[1] == 'H') { digitalWrite(Heizung,an); } else { digitalWrite(Heizung,aus); }
  if (relais[2] == 'R') { digitalWrite(Ruehrwerk,an); } else { digitalWrite(Ruehrwerk,aus); }
  if (relais[3] == 'P') { digitalWrite(Pumpe,an); } else { digitalWrite(Pumpe,aus); }
  if (relais[4] == 'A') { digitalWrite(Summer,an); } else { digitalWrite(Summer,aus); }
}

void SerialOut()
{
  Serial.print(zeitstempel() + "  Zeitintervall erreicht: \n");
  Serial.print("\nGefundene Sensoren:\n");
  Serial.print(SensorString);
  Serial.print("\n\n");

  Serial.print("Aktuelle Temperatur: \n");
  if (sensor=='d') {Serial.print(externeisttemp,1);} else {Serial.print(Temp,1);}  
  Serial.print(" ");
  Serial.print(char(176));
  Serial.print("C\n\n");
  
  Serial.print("Schaltstatus: \n");
  Serial.print("Heizung:  ");
  if (relais[1] == 'H') { Serial.print("Ein\n"); } else { Serial.print("Aus\n"); }
  Serial.print("R"); Serial.print(char(252)); Serial.print("hrwerk: ");
  if (relais[2] == 'R') { Serial.print("Ein\n"); } else { Serial.print("Aus\n"); }
  Serial.print("Pumpe:    ");
  if (relais[3] == 'P') { Serial.print("Ein\n"); } else { Serial.print("Aus\n"); }
  Serial.print("Alarm:    ");
  if (relais[4] == 'A') { Serial.print("Ein\n"); } else { Serial.print("Aus\n"); }
  Serial.print("\n");
  Serial.print("Brauereistatus: \n");
  if (state[1]=='o') { Serial.print("OFFLINE "); }        
  else if (state[1]=='x') { Serial.print("INAKTIV "); }
  else if (state[1]=='y') { Serial.print("AKTIV   "); }
  else if (state[1]=='z') { Serial.print("PAUSIERT"); }
  Serial.print("\n\n");
  Serial.print("Zusatzfunktionen: ");
  Serial.print("\n");
  for (int i=1; i<=10; i++) { if (Funktionslog[i]==false) {Serial.print("0");} else {Serial.print("1");} }
  Serial.print("\n\n");      
}

void packetAuswertung()
{
  int temp = 0;
  int temp2 = 0;
  if ((temprec[0]=='C') && (temprec[18]=='c'))             // Begin der Decodierung des seriellen Strings  
  { 
    temp=(int)temprec[1];
    if ( temp < 0 ) { temp = 256 + temp; }
    if ( temp > 7) {relais[4]='A';temp=temp-8;} else {relais[4]='a';} 
    if ( temp > 3) {relais[3]='P';temp=temp-4;} else {relais[3]='p';} 
    if ( temp > 1) {relais[2]='R';temp=temp-2;} else {relais[2]='r';}
    if ( temp > 0) {relais[1]='H';temp=temp-1;} else {relais[1]='h';}   

    temp=(int)temprec[2];
    if ( temp < 0 ) { temp = 256 + temp; }
    if ( temp > 127) {sensor='N';temp=temp-128;}  
    if ( temp > 63) {sensor='D';temp=temp-64;}
    if ( temp > 31) {sensor='d';temp=temp-32;}    
    if ( temp > 15) {state[2]='t';temp=temp-16;}  
    if ( temp > 7) {state[2]='T';temp=temp-8;}  
    if ( temp > 3) {state[1]='x';temp=temp-4;} 
    else if ( temp > 1) {state[1]='z';temp=temp-2;}  
    else if ( temp > 0) {state[1]='y';temp=temp-1;}    
  
    temp=(int)temprec[3];
    if ( temp < 0 ) { temp = 256 + temp; }
    solltemp=temp;

    temp=(int)temprec[5];
    if ( temp < 0 ) { temp = 256 + temp; }
    temp2=temp;
    temp=(int)temprec[4];
    if ( temp < 0 ) { temp = 256 + temp; }
    temp=temp*256;
    temp2=temp2+temp;
    externeisttemp=temp2;
    externeisttemp=externeisttemp/10;

    temp=(int)temprec[6];
    if ( temp < 0 ) { temp = 256 + temp; }  
    temp2=(int)temprec[7];
    if ( temp2 < 0 ) { temp2 = 256 + temp2; }
    for (int i=1; i<=10; i++) {Funktionslog[i]=false;}
    if ( (temp == 0) & (temp2 == 0) ) {noFunktion();} 
    if ( temp > 127) {Funktion1();Funktionslog[1]=true;temp=temp-128;} 
    if ( temp > 63) {Funktion2();Funktionslog[2]=true;temp=temp-64;} 
    if ( temp > 31) {Funktion3();Funktionslog[3]=true;temp=temp-32;} 
    if ( temp > 15) {Funktion4();Funktionslog[4]=true;temp=temp-16;}    
    if ( temp > 7) {Funktion5();Funktionslog[5]=true;temp=temp-8;}  
    if ( temp > 3) {Funktion6();Funktionslog[6]=true;temp=temp-4;} 
    if ( temp > 1) {Funktion7();Funktionslog[7]=true;temp=temp-2;}  
    if ( temp > 0) {Funktion8();Funktionslog[8]=true;temp=temp-1;}  
    if ( temp2 > 1) {Funktion9();Funktionslog[9]=true;temp2=temp2-2;}    
    if ( temp2 > 0) {Funktion10();Funktionslog[10]=true;temp2=temp2-1;}  

  }
}

float GetTemperature(char adress[255], bool doreset)
{
  int HighByte, LowByte, TReading, SignBit, Tc_100, Whole, Fract;
  byte i,present = 0, data[12], addr[8];
  char stradd[255];
  while(ds.search(addr))
  {     
    sprintf(stradd, "");
    //Serial.print("\nR=");
    for( i = 0; i < 8; i++)
    {
      sprintf(stradd, "%s %X", stradd,addr[i]);     
    }
    ds.reset();
    ds.select(addr);
    ds.write(0x44,1);         // start Konvertierung, mit power-on am Ende

    delay(50);     // 750ms sollten ausreichen
    // man sollte ein ds.depower() hier machen, aber ein reset tut das auch

    present = ds.reset();
    ds.select(addr);   
    ds.write(0xBE);         // Wert lesen

    for ( i = 0; i < 9; i++)
    {         
      // 9 bytes
      data[i] = ds.read();
    }

    if(Contains(stradd, adress))
    {
      LowByte = data[0];
      HighByte = data[1];
      TReading = (HighByte << 8) + LowByte;
      SignBit = TReading & 0x8000;  // test most sig bit

      if (SignBit) // negative
      {
      TReading = (TReading ^ 0xffff) + 1; // 2's comp
      }     
      Tc_100 = (6 * TReading) + TReading / 4;    // mal (100 * 0.0625) oder 6.25
      /* Für DS18S20 folgendes verwenden Tc_100 = (TReading*100/2);    */       
      Whole = Tc_100 / 100;  // Ganzzahlen und Brüche trennen
      Fract = Tc_100 % 100;     

      if (SignBit) // negative Werte ermitteln
      {
      //Serial.print("-");
      }

      if(doreset)ds.reset_search();
      return (float)Whole+((float)Fract/100);
    }
  }
}

char Sensor[16];

void loop(void)
{
  server.handleClient(); // auf HTTP-Anfragen warten
 
  jetztMillis = millis();

  UDPRead();
  OfflineCheck();
  ManuellerStatus();
  LCDOut();
  RelaisOut();
   
  if(!deltaMeldungMillis == 0 && jetztMillis - letzteMeldungMillis > deltaMeldungMillis)
  {
    ListSensors();
    SerialOut();
    letzteMeldungMillis = jetztMillis;
  }
}
