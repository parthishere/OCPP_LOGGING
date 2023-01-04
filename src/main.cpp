// matth-x/ArduinoOcpp
// Copyright Matthias Akstaller 2019 - 2022
// MIT License

#include <Arduino.h>
#if defined(ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266WiFiMulti.h>
ESP8266WiFiMulti WiFiMulti;
#elif defined(ESP32)
#include <WiFi.h>
#include "FS.h"
#include "SPIFFS.h"
#include "time.h"
#include <Ticker.h>

#include <MFRC522.h> //MFRC522 library
#include <SPI.h>     //Include SPI library

#include <HTTPClient.h>

#include "ArduinoOcpp/Core/FileManage.h"
#include <ArduinoOcpp.h> //Matt-x/ArduinoOcpp library

#else
#error only ESP32 or ESP8266 supported at the moment
#endif

#define STASSID "ROG-hp"
#define STAPSK ""

// #define STASSID "ALIENWARE"
// #define STAPSK "He@ven$heth05"

// 159

#define SDA_SS_PIN 5                  // 21 //ESP Interface Pin
#define RST_PIN 15                    // 22    //ESP Interface Pin
MFRC522 mfrc522(SDA_SS_PIN, RST_PIN); // create instance of class
MFRC522::MIFARE_Key key;

#define OCPP_HOST "192.168.1.77"
#define OCPP_PORT 8000
#define OCPP_URL "ws://192.168.1.77:8000/ws/socket/"

// #define OCPP_HOST "192.168.1.102"
// #define OCPP_PORT 8000
// #define OCPP_URL "ws://192.168.1.102:8000/ws/socket/"

//
////  Settings which worked for my SteVe instance
//
// #define OCPP_HOST "my.instance.com"
// #define OCPP_PORT 80
// #define OCPP_URL "ws://my.instance.com/steve/websocket/CentralSystemService/gpio-based-charger"

#define MAX 100

// Pins
#ifdef BUILTIN_LED
int led = BUILTIN_LED; // In case it's on, turn LED off, as sometimes PIN-5 on some boards is used for SPI-SS
#else
int led = 2;
#endif

#define FORMAT_SPIFFS_IF_FAILED true

Ticker periodicTicker;
Ticker onceTicker;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 3600;
unsigned long time_now = 0;
int period = 1000;

bool connected_to_wifi = false, to_count = false, disconnected;
int count = 0;

char timeSeconds[10];
char timeMin[3];
char timeHour[10];
char timeDay[10];
char timeMonth[10];
char timeYear[5];

void listDir(fs::FS &fs, const char *dirname, uint8_t levels);
void readFile(fs::FS &fs, const char *path);
void deleteFile(fs::FS &fs, const char *path);
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);
void checkStatus();

#define Amperage_Pin 4 // modulated as PWM

// #define EV_Plug_Pin 36 // Input pin | Read if an EV is connected to the EVSE
// #define EV_Plugged HIGH
// #define EV_Unplugged LOW

#define OCPP_Charge_Permission_Pin 10 // Output pin | Signal if OCPP allows / forbids energy flow
#define OCPP_Charge_Permitted HIGH
#define OCPP_Charge_Forbidden LOW

#define EV_Charge_Pin 35 // Input pin | Read if EV requests energy (corresponds to SAE J1772 State C)
#define EV_Charging LOW
#define EC_Suspended HIGH

#define OCPP_Availability_Pin 9 // Output pin | Signal if this EVSE is out of order (set by Central System)
#define OCPP_Available HIGH
#define OCPP_Unavailable LOW

#define EVSE_Ground_Fault_Pin 34 // Input pin | Read ground fault detector
#define EVSE_Grud_Faulted HIGH
#define EVSE_Ground_Clear LOW

// variables declaration
bool transaction_in_process = false; // Check if transaction is in-progress
int evPlugged = EV_Unplugged;        // Check if EV is plugged-in

bool booted = false;
ulong scheduleReboot = 0;   // 0 = no reboot scheduled; otherwise reboot scheduled in X ms
ulong reboot_timestamp = 0; // timestamp of the triggering event; if scheduleReboot=0, the timestamp has no meaning
extern String content = "";
extern String content2 = "";
extern signed tran_id = -1;

#include <ArduinoOcpp.h>

void setup()
{
  Serial.begin(115200);

  pinMode(led, OUTPUT);

  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(STASSID);

  pinMode(EV_Plug_Pin, INPUT);
  pinMode(EV_Charge_Pin, INPUT);
  pinMode(EVSE_Ground_Fault_Pin, INPUT);
  pinMode(OCPP_Charge_Permission_Pin, OUTPUT);
  digitalWrite(OCPP_Charge_Permission_Pin, OCPP_Charge_Forbidden);
  pinMode(OCPP_Availability_Pin, OUTPUT);
  digitalWrite(OCPP_Availability_Pin, OCPP_Unavailable);

  pinMode(Amperage_Pin, OUTPUT);
  pinMode(Amperage_Pin, OUTPUT);
  ledcSetup(0, 1000, 8); // channel=0, freq=1000Hz, range=(2^8)-1
  ledcAttachPin(Amperage_Pin, 0);
  ledcWrite(Amperage_Pin, 256); // 256 is constant +3.3V DC

  WiFi.begin(STASSID, STAPSK);

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  connected_to_wifi = true;
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  listDir(SPIFFS, "/", 0);

  periodicTicker.attach_ms(5000, checkStatus);

  writeFile(SPIFFS, "/hello.csv", "TYPE,DAY,MONTH,YEAR,HOURS,MINUTES,SECONDS,CODE\n");

  readFile(SPIFFS, "/hello.csv");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

  /*
   * Initialize the OCPP library
   */
  OCPP_initialize(OCPP_HOST, OCPP_PORT, OCPP_URL);

  /*
   * Integrate OCPP functionality. You can leave out the following part if your EVSE doesn't need it.
   */
  setEnergyActiveImportSampler([]()
                               {
    //read the energy input register of the EVSE here and return the value in Wh
    /** Approximated value. TODO: Replace with real reading**/
    static ulong lastSampled = millis();
    static float energyMeter = 0.f;
    if (getTransactionId() > 0 && digitalRead(EV_Charge_Pin) == EV_Charging)
        energyMeter += ((float) millis() - lastSampled) * 0.003f; //increase by 0.003Wh per ms (~ 10.8kWh per h)
    lastSampled = millis();
    return energyMeter; });

  setEvRequestsEnergySampler([]()
                             {
    //return true if the EV is in state "Ready for charging" (see https://en.wikipedia.org/wiki/SAE_J1772#Control_Pilot)
    //return false;
    return digitalRead(EV_Charge_Pin) == EV_Charging; });

  addConnectorErrorCodeSampler([]()
                               {
                                 // if (digitalRead(EVSE_GROUND_FAULT_PIN) != EVSE_GROUND_CLEAR) {
                                 // return "GroundFault";
                                 // } else {
                                 return (const char *)NULL;
                                 // }
                               });

  setOnResetSendConf([](JsonObject payload)
                     {
        if (getTransactionId() >= 0)
            stopTransaction();
        
        reboot_timestamp = millis();
        scheduleReboot = 5000;
        booted = false; });

  // Check connector
  setOnUnlockConnector([]()
                       { return true; });

  /*------------Notify the Central System that this station is ready--------------*/
  /*-----------------------------BOOT NOTIFICATION---------------------------------*/

  bootNotification("EV CHARGER", "GRIDEN POWER", [](JsonObject payload)
                   {
    const char *status = payload["status"] | "INVALID";
    if (!strcmp(status, "Accepted")) {
        booted = true;
        Serial.println("Sever Connected!");
        //digitalWrite(SERVER_CONNECT_LED, SERVER_CONNECT_ON);
    } else {
        //retry sending the BootNotification
        delay(60000);
        ESP.restart();
    } });

  /*---------------------Initializing MFRC522 RFID Library------------------------*/
  SPI.begin();        // Initiate  SPI bus
  mfrc522.PCD_Init(); // Initiate MFRC522
                      // Serial.println("Please verify your RFID tag...");
}

void loop()
{

  /*
   * Do all OCPP stuff (process WebSocket input, send recorded meter values to Central System, etc.)
   */
  OCPP_loop();

  /*
   * Check internal OCPP state and bind EVSE hardware to it
   */
  if (isInSession() == false /*true*/ && transaction_in_process == true /*true*/ && evPlugged == EV_Plugged && digitalRead(EV_Plug_Pin) == EV_Plugged)
  {
    Serial.println("Reset");
    while (digitalRead(EV_Plug_Pin) == EV_Plugged)
    {
      Serial.print(".");
      delay(5000);
    }
    if (isAvailable())
    {

      ESP.restart();
    }

    transaction_in_process = false;
    evPlugged = EV_Unplugged;
    tran_id = 0;
  }

  // If RIFD chip is detected process for verification
  if (!mfrc522.PICC_IsNewCardPresent() && transaction_in_process == false)
  {
    return;
  }
  // Verify if the NUID has been readed
  else if (!mfrc522.PICC_ReadCardSerial() && transaction_in_process == false)
  {
    Serial.println("Card not readed");
    return;
  }
  else if (transaction_in_process == false) // && isInSession() == false)
  {
    // Show UID on serial monitor
    String content = "";
    MFRC522::PICC_Type piccType = mfrc522.PICC_GetType(mfrc522.uid.sak);
    Serial.println(mfrc522.PICC_GetTypeName(piccType));
    // Check is the PICC of Classic MIFARE type
    if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&
        piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
        piccType != MFRC522::PICC_TYPE_MIFARE_4K)
    {
      Serial.println(F("Your tag is not of type MIFARE Classic."));
      return;
    }

    Serial.println("");
    Serial.print(F("The ID tag no is  : "));

    for (byte i = 0; i < mfrc522.uid.size; i++)
    {
      Serial.print(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : "");
      Serial.print(mfrc522.uid.uidByte[i], HEX);
      content.concat(String(mfrc522.uid.uidByte[i] < 0x10 ? " 0" : ""));
      content.concat(String(mfrc522.uid.uidByte[i], HEX));
    }

    Serial.println("");
    content2 = content;
    content.toUpperCase();
    char *idTag = new char[content.length() + 1];
    strcpy(idTag, content.c_str());
    delay(2000);
    authorize(idTag, [](JsonObject response)
              {
            //check if user with idTag is authorized
            if (!strcmp("Accepted", response["idTagInfo"]["status"] | "Invalid")){
            Serial.println(F("[main] User is authorized to start charging"));
            transaction_in_process = true;

            } else {
            Serial.printf("[main] Authorize denied. Reason: %s\n", response["idTagInfo"]["status"] | "");
            } });
    Serial.printf("[main] Authorizing user with idTag %s\n", idTag);

    delay(2000);

    if (ocppPermitsCharge())
    {
      Serial.println("OCPP Charge Permission Forbidden");
    }
    else
    {
      Serial.println("OCPP Charge Permission Permitted");
    }

    if (isInSession() == true)
    {
      transaction_in_process = true;
    }
  }
  else
  {
    delay(2000);
    if (!booted)
      return;
    if (scheduleReboot > 0 && millis() - reboot_timestamp >= scheduleReboot)
    {
      ESP.restart();
    }

    if (digitalRead(EV_Plug_Pin) == EV_Plugged && evPlugged == EV_Unplugged && getTransactionId() >= 0)
    {
      // transition unplugged -> plugged; Case A: transaction has already been initiated
      evPlugged = EV_Plugged;
      Serial.println("In Case A");
    }
    else if (digitalRead(EV_Plug_Pin) == EV_Plugged && evPlugged == EV_Unplugged && isAvailable())
    {
      // transition unplugged -> plugged; Case B: no transaction running; start transaction
      // Serial.println("Case B: no transaction running; start transaction");

      evPlugged = EV_Plugged;
      content2.toUpperCase();
      char *idTag = new char[content2.length() + 1];
      strcpy(idTag, content2.c_str());
      startTransaction(idTag, [](JsonObject response)
                       {
            //Callback: Central System has answered. Could flash a confirmation light here.

            Serial.printf("[main] Started OCPP transaction. Status: %s, transactionId: %u\n",
                response["idTagInfo"]["status"] | "Invalid",
                response["transactionId"] | -1);
        
            tran_id = (response["transactionId"] | -1); });

      delay(2000);
    }
    else if (digitalRead(EV_Plug_Pin) == EV_Unplugged && evPlugged == EV_Plugged)
    { //  && isInSession() == true) {
      // transition plugged -> unplugged
      evPlugged = EV_Unplugged;

      if (tran_id >= 0)
      {
        stopTransaction([](JsonObject response)
                        {
                    //Callback: Central System has answered.
                    Serial.print(F("[main] Stopped OCPP transaction\n")); });

        transaction_in_process = false;
        tran_id = -1;
      }
    }
  }
  //... see ArduinoOcpp.h for more possibilities
}

void listDir(fs::FS &fs, const char *dirname, uint8_t levels)
{
  Serial.printf("Listing directory: %s\r\n", dirname);

  File root = fs.open(dirname);
  if (!root)
  {
    Serial.println("- failed to open directory");
    return;
  }
  if (!root.isDirectory())
  {
    Serial.println(" - not a directory");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    if (file.isDirectory())
    {
      Serial.print("  DIR : ");
      Serial.println(file.name());
      if (levels)
      {
        listDir(fs, file.path(), levels - 1);
      }
    }
    else
    {
      Serial.print("  FILE: ");
      Serial.print(file.name());
      Serial.print("\tSIZE: ");
      Serial.println(file.size());
    }
    file = root.openNextFile();
  }
}

void readFile(fs::FS &fs, const char *path)
{
  Serial.printf("Reading file: %s\r\n", path);

  File file = fs.open(path);
  if (!file || file.isDirectory())
  {
    Serial.println("- failed to open file for reading");
    return;
  }

  Serial.println("- read from file:");
  while (file.available())
  {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Writing file: %s\r\n", path);

  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("- failed to open file for writing");
    return;
  }
  if (file.print(message))
  {
    Serial.println("- file written");
  }
  else
  {
    Serial.println("- write failed");
  }
  file.close();
}

void appendFile(fs::FS &fs, const char *path, const char *message)
{
  Serial.printf("Appending to file: %s\r\n", path);

  File file = fs.open(path, FILE_APPEND);
  if (!file)
  {
    Serial.println("- failed to open file for appending");
    return;
  }
  if (file.print(message))
  {
    Serial.println("- message appended");
  }
  else
  {
    Serial.println("- append failed");
  }
  file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
  Serial.printf("Renaming file %s to %s\r\n", path1, path2);
  if (fs.rename(path1, path2))
  {
    Serial.println("- file renamed");
  }
  else
  {
    Serial.println("- rename failed");
  }
}

void deleteFile(fs::FS &fs, const char *path)
{
  Serial.printf("Deleting file: %s\r\n", path);
  if (fs.remove(path))
  {
    Serial.println("- file deleted");
  }
  else
  {
    Serial.println("- delete failed");
  }
}

void checkServer()
{
  struct tm timeinfo;

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
  }

  HTTPClient http;
  http.begin("http://example.com/index.html");
  Serial.print("[HTTP] GET...\n");
  // start connection and send HTTP header
  int httpCode = http.GET();

  // httpCode will be negative on error
  if (httpCode > 0)
  {
    // HTTP header has been send and Server response header has been handled
    Serial.printf("[HTTP] GET... code: %d\n", httpCode);

    // file found at server
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      Serial.println(payload);
    }
  }
  else
  {
    strftime(timeSeconds, 10, "%S", &timeinfo);
    strftime(timeMin, 3, "%M", &timeinfo);
    strftime(timeHour, 10, "%H", &timeinfo);
    strftime(timeDay, 10, "%A", &timeinfo);
    strftime(timeMonth, 10, "%B", &timeinfo);
    strftime(timeYear, 5, "%Y", &timeinfo);

    Serial.printf("[HTTP] GET... failed, error: %s\n", http.errorToString(httpCode).c_str());
    appendFile(SPIFFS, "/hello.csv", "SERVER_ERROR,");
    appendFile(SPIFFS, "/hello.csv", timeDay);
    appendFile(SPIFFS, "/hello.csv", ",");

    appendFile(SPIFFS, "/hello.csv", timeMonth);
    appendFile(SPIFFS, "/hello.csv", ",");

    appendFile(SPIFFS, "/hello.csv", timeYear);
    appendFile(SPIFFS, "/hello.csv", ",");

    appendFile(SPIFFS, "/hello.csv", timeHour);
    appendFile(SPIFFS, "/hello.csv", ",");

    appendFile(SPIFFS, "/hello.csv", timeMin);
    appendFile(SPIFFS, "/hello.csv", ",");

    appendFile(SPIFFS, "/hello.csv", timeSeconds);
    appendFile(SPIFFS, "/hello.csv", ",");

    appendFile(SPIFFS, "/hello.csv", (char *)httpCode);
    appendFile(SPIFFS, "/hello.csv", "\n");
  }

  http.end();
}

void checkStatus()
{
  struct tm timeinfo;
  Serial.println("Fucntion is getting called !");
  if (WiFi.status() == WL_DISCONNECTED)
  {
    Serial.println("not connected in the function");
    WiFi.begin(STASSID, STAPSK);

    {
      count++;
    }
    if (connected_to_wifi == true && disconnected == false)
    {
      // First time disconnection after connection
      Serial.println("Failed to obtain time, Saving Previous  (means disconnection)");

      appendFile(SPIFFS, "/hello.csv", "DISCONNECT,");
      appendFile(SPIFFS, "/hello.csv", timeDay);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeMonth);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeYear);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeHour);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeMin);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeSeconds);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", "600");
      appendFile(SPIFFS, "/hello.csv", "\n");
    }
    connected_to_wifi = false;
    disconnected = true;
  }

  else if (WiFi.status() == WL_CONNECTED)
  {
    if (!getLocalTime(&timeinfo))
    {
      Serial.println("Failed to obtain time in check status");
    }
    strftime(timeSeconds, 10, "%S", &timeinfo);

    strftime(timeMin, 3, "%M", &timeinfo);

    strftime(timeHour, 10, "%H", &timeinfo);

    strftime(timeDay, 10, "%A", &timeinfo);

    strftime(timeMonth, 10, "%B", &timeinfo);

    strftime(timeYear, 5, "%Y", &timeinfo);

    Serial.println();
    if (connected_to_wifi == false && disconnected == true)
    {
      // First time connection after disconnection
      Serial.println("wifi connected after a disconnection");
      appendFile(SPIFFS, "/hello.csv", "CONNECT,");
      appendFile(SPIFFS, "/hello.csv", timeDay);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeMonth);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeYear);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeHour);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeMin);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", timeSeconds);
      appendFile(SPIFFS, "/hello.csv", ",");

      appendFile(SPIFFS, "/hello.csv", "200");
      appendFile(SPIFFS, "/hello.csv", "\n");

      readFile(SPIFFS, "/hello.csv");
    }
    connected_to_wifi = true;
    disconnected = false;
  }
}

// void testFileIO(fs::FS &fs, const char *path)
// {
//   Serial.printf("Testing file I/O with %s\r\n", path);

//   static uint8_t buf[512];
//   size_t len = 0;
//   File file = fs.open(path, FILE_WRITE);
//   if (!file)
//   {
//     Serial.println("- failed to open file for writing");
//     return;
//   }

//   size_t i;
//   Serial.print("- writing");
//   uint32_t start = millis();
//   for (i = 0; i < 2048; i++)
//   {
//     if ((i & 0x001F) == 0x001F)
//     {
//       Serial.print(".");
//     }
//     file.write(buf, 512);
//   }
//   Serial.println("");
//   uint32_t end = millis() - start;
//   Serial.printf(" - %u bytes written in %u ms\r\n", 2048 * 512, end);
//   file.close();

//   file = fs.open(path);
//   start = millis();
//   end = start;
//   i = 0;
//   if (file && !file.isDirectory())
//   {
//     len = file.size();
//     size_t flen = len;
//     start = millis();
//     Serial.print("- reading");
//     while (len)
//     {
//       size_t toRead = len;
//       if (toRead > 512)
//       {
//         toRead = 512;
//       }
//       file.read(buf, toRead);
//       if ((i++ & 0x001F) == 0x001F)
//       {
//         Serial.print(".");
//       }
//       len -= toRead;
//     }
//     Serial.println("");
//     end = millis() - start;
//     Serial.printf("- %u bytes read in %u ms\r\n", flen, end);
//     file.close();
//   }
//   else
//   {
//     Serial.println("- failed to open file for reading");
//   }
// }