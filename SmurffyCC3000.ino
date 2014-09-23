#include <Adafruit_CC3000.h>
#include <ccspi.h>
#include <SPI.h>

// CC3000 interrupt and control pins
#define ADAFRUIT_CC3000_IRQ   3 // MUST be an interrupt pin!
#define ADAFRUIT_CC3000_VBAT  5 // These can be
#define ADAFRUIT_CC3000_CS   10 // any two pins
// Hardware SPI required for remaining pins.
// On an UNO, SCK = 13, MISO = 12, and MOSI = 11
Adafruit_CC3000 cc3000 = Adafruit_CC3000(
  ADAFRUIT_CC3000_CS, ADAFRUIT_CC3000_IRQ, ADAFRUIT_CC3000_VBAT,
  SPI_CLOCK_DIVIDER);

// WiFi access point credentials
#define WLAN_SSID     "Dolphin"  // 32 characters max
#define WLAN_PASS     "BaDo2206@"
#define WLAN_SECURITY WLAN_SEC_WPA2 // WLAN_SEC_UNSEC/WLAN_SEC_WEP/WLAN_SEC_WPA/WLAN_SEC_WPA2

const unsigned long
  dhcpTimeout     = 60L * 1000L, // Max time to wait for address from DHCP
  connectTimeout  = 15L * 1000L, // Max time to wait for server connection
  responseTimeout = 15L * 1000L; // Max time to wait for data from server
  
unsigned long
  currentTime = 0L;
Adafruit_CC3000_Client
  client;        // For WiFi connections

void setup(void) {

  uint32_t ip = 0L, t;

  Serial.begin(115200);

  Serial.print(F("Hello! Initializing CC3000..."));
  if(!cc3000.begin()) hang(F("failed. Check your wiring?"));

/*
  uint16_t firmware = checkFirmwareVersion();
  if ((firmware != 0x113) && (firmware != 0x118)) {
    hang(F("Wrong firmware version!"));
  }
*/

  Serial.print(F("OK\r\nDeleting old connection profiles..."));
  if(!cc3000.deleteProfiles()) hang(F("failed."));

  Serial.print(F("OK\r\nConnecting to network..."));
  /* NOTE: Secure connections are not available in 'Tiny' mode! */
  if(!cc3000.connectToAP(WLAN_SSID, WLAN_PASS, WLAN_SECURITY)) hang(F("Failed!"));

  Serial.print(F("OK\r\nRequesting address from DHCP server..."));
  for(t=millis(); !cc3000.checkDHCP() && ((millis() - t) < dhcpTimeout); delay(100));
  if(!cc3000.checkDHCP()) hang(F("failed"));
  Serial.println(F("OK"));

  while(!displayConnectionDetails());

  // Get initial time from time server (make a few tries if needed)
  for(uint8_t i=0; (i<5) && !(currentTime = getTime()); delay(5000L), i++);
}

// On error, print PROGMEM string to serial monitor and stop
void hang(const __FlashStringHelper *str) {
  Serial.println(str);
  for(;;);
}

uint8_t countdown = 23; // Countdown to next time server query (once per ~24hr)

void loop() {
  unsigned long t = millis();
  
  String data;
  data = "{\"deviceID\":\"S001\", \"temperature\": \"29.28\", \"humidity\": \"80.45\"}";
  
  sendData(data);
  
  if(!countdown) {        // 24 hours elapsed?
    if((t = getTime())) { // Synchronize time if server contacted
      currentTime = t;
      countdown   = 23;   // and reset counter
    }
  } else countdown--;
}

boolean sendData(String msg) { // 140 chars max! No checks made here.
  unsigned long t;
  
  // Hostname lookup
  uint32_t ip = cc3000.IP2U32(128,199,246,241);

  // Connect to numeric IP
  Serial.print(F("OK\r\nConnecting to server..."));
  t = millis();
  do {
    client = cc3000.connectTCP(ip, 49160);
  } while((!client.connected()) &&
          ((millis() - t) < connectTimeout));

  if(client.connected()) { // Success!

    Serial.println(F("Issuing HTTP request..."));
    // Unlike the hash prep, parameters in the HTTP request don't require sorting.
    client.fastrprintln(F("POST /api/sensors HTTP/1.1"));
    client.fastrprintln(F("Host: 128.199.246.241:49160"));
    client.fastrprintln(F("Content-Type: application/json"));
    client.fastrprint(F("Content-Length: "));
    Serial.println(msg.length());
    client.fastrprintln(F(""));
    Serial.println(F("Sending data..."));
    client.println(msg);

    Serial.print(F("OK\r\nAwaiting response..."));
    int c = 0;
    // Dirty trick: instead of parsing results, just look for opening
    // curly brace indicating the start of a successful JSON response.
    while(((c = timedRead()) > 0) && (c != '{'));
    if(c == '{')   Serial.println(F("success!"));
    else if(c < 0) Serial.println(F("timeout"));
    else           Serial.println(F("error (invalid Twitter credentials?)"));
    client.close();
    return (c == '{');
  } else { // Couldn't contact server
    Serial.println(F("failed"));
    return false;
  }
}

bool displayConnectionDetails(void) {
  uint32_t addr, netmask, gateway, dhcpserv, dnsserv;

  if(!cc3000.getIPAddress(&addr, &netmask, &gateway, &dhcpserv, &dnsserv))
    return false;

  Serial.print(F("IP Addr: ")); cc3000.printIPdotsRev(addr);
  Serial.print(F("\r\nNetmask: ")); cc3000.printIPdotsRev(netmask);
  Serial.print(F("\r\nGateway: ")); cc3000.printIPdotsRev(gateway);
  Serial.print(F("\r\nDHCPsrv: ")); cc3000.printIPdotsRev(dhcpserv);
  Serial.print(F("\r\nDNSserv: ")); cc3000.printIPdotsRev(dnsserv);
  Serial.println();
  return true;
}

// Read from client stream with a 5 second timeout.  Although an
// essentially identical method already exists in the Stream() class,
// it's declared private there...so this is a local copy.
int timedRead(void) {
  unsigned long start = millis();
  while((!client.available()) && ((millis() - start) < responseTimeout));
  return client.read();  // -1 on timeout
}

// Minimalist time server query; adapted from Adafruit Gutenbird sketch,
// which in turn has roots in Arduino UdpNTPClient tutorial.
unsigned long getTime(void) {

  uint8_t       buf[48];
  unsigned long ip, startTime, t = 0L;

  Serial.print(F("Locating time server..."));

  // Hostname to IP lookup; use NTP pool (rotates through servers)
  if(cc3000.getHostByName("pool.ntp.org", &ip)) {
    static const char PROGMEM
      timeReqA[] = { 227,  0,  6, 236 },
      timeReqB[] = {  49, 78, 49,  52 };

    Serial.println(F("found\r\nConnecting to time server..."));
    startTime = millis();
    do {
      client = cc3000.connectUDP(ip, 123);
    } while((!client.connected()) &&
            ((millis() - startTime) < connectTimeout));

    if(client.connected()) {
      Serial.print(F("connected!\r\nIssuing request..."));

      // Assemble and issue request packet
      memset(buf, 0, sizeof(buf));
      memcpy_P( buf    , timeReqA, sizeof(timeReqA));
      memcpy_P(&buf[12], timeReqB, sizeof(timeReqB));
      client.write(buf, sizeof(buf));

      Serial.print(F("OK\r\nAwaiting response..."));
      memset(buf, 0, sizeof(buf));
      startTime = millis();
      while((!client.available()) &&
            ((millis() - startTime) < responseTimeout));
      if(client.available()) {
        client.read(buf, sizeof(buf));
        t = (((unsigned long)buf[40] << 24) |
             ((unsigned long)buf[41] << 16) |
             ((unsigned long)buf[42] <<  8) |
              (unsigned long)buf[43]) - 2208988800UL;
        Serial.println(F("success!"));
      }
      client.close();
    }
  }
  if(!t) Serial.println(F("error"));
  return t;
}
