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
#include <HTTPClient.h>
#include <FileManage.h>
#else
#error only ESP32 or ESP8266 supported at the moment
#endif

#define STASSID "parthishere-hotspot"
#define STAPSK ""

// #define STASSID "ALIENWARE"
// #define STAPSK "He@ven$heth05"

// 159
#define OCPP_HOST "192.168.1.77"
#define OCPP_PORT 6630
#define OCPP_URL "ws://192.168.1.77:6630/ocpp/GP001"

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
void writeFile(fs::FS &fs, const char *path, const char *message);
void appendFile(fs::FS &fs, const char *path, const char *message);
void checkStatus();

#include <ArduinoOcpp.h>

void setup()
{

  /*
   * Initialize Serial and WiFi
   */

  Serial.begin(9600);
  Serial.setDebugOutput(true);

  Serial.print(F("[main] Wait for WiFi: "));

#if defined(ESP8266)
  WiFiMulti.addAP(STASSID, STAPSK);
  while (WiFiMulti.run() != WL_CONNECTED)
  {
    Serial.print('.');
    delay(1000);
  }
#elif defined(ESP32)
  WiFi.begin(STASSID, STAPSK);
  while (!WiFi.isConnected())
  {
    Serial.print('.');
    delay(1000);
  }
#else
#error only ESP32 or ESP8266 supported at the moment
#endif

  Serial.print(F(" connected!\n"));

  if (!SPIFFS.begin(FORMAT_SPIFFS_IF_FAILED))
  {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  listDir(SPIFFS, "/", 0);

  /*
   * Initialize the OCPP library
   */
  OCPP_initialize(OCPP_HOST, OCPP_PORT, OCPP_URL);

  /*
   * Integrate OCPP functionality. You can leave out the following part if your EVSE doesn't need it.
   */
  setPowerActiveImportSampler([]()
                              {
        //measure the input power of the EVSE here and return the value in Watts
        return 0.f; });

  setOnChargingRateLimitChange([](float limit)
                               {
        //set the SAE J1772 Control Pilot value here
        Serial.print(F("[main] Smart Charging allows maximum charge rate: "));
        Serial.println(limit); });

  setEvRequestsEnergySampler([]()
                             {
        //return true if the EV is in state "Ready for charging" (see https://en.wikipedia.org/wiki/SAE_J1772#Control_Pilot)
        return false; });

  //... see ArduinoOcpp.h for more settings

  /*
   * Notify the Central System that this station is ready
   */
  bootNotification("My Charging Station", "My company name");

  periodicTicker.attach_ms(5000, checkStatus);

  writeFile(SPIFFS, "/hello.csv", "TYPE,DAY,MONTH,YEAR,HOURS,MINUTES,SECONDS");

  readFile(SPIFFS, "/hello.csv");

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
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
  if (ocppPermitsCharge())
  {
    // OCPP set up and transaction running. Energize the EV plug here
  }
  else
  {
    // No transaction running at the moment. De-energize EV plug
  }

  /*
   * Detect if something physical happened at your EVSE and trigger the corresponding OCPP messages
   */
  if (/* RFID chip detected? */ false)
  {
    const char *idTag = "my-id-tag"; // e.g. idTag = RFID.readIdTag();
    authorize(idTag);
  }

  if (/* EV plugged in? */ false)
  {
    startTransaction("my-id-tag", [](JsonObject payload)
                     {
            //Callback: Central System has answered. Could flash a confirmation light here.
            Serial.print(F("[main] Started OCPP transaction\n")); });
  }

  if (/* EV unplugged? */ false)
  {
    stopTransaction([](JsonObject payload)
                    {
            //Callback: Central System has answered.
            Serial.print(F("[main] Stopped OCPP transaction\n")); });
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
    Serial.println(timeSeconds);

    strftime(timeMin, 3, "%M", &timeinfo);
    Serial.println(timeMin);

    strftime(timeHour, 10, "%H", &timeinfo);
    Serial.println(timeHour);

    strftime(timeDay, 10, "%A", &timeinfo);
    Serial.println(timeDay);

    strftime(timeMonth, 10, "%B", &timeinfo);
    Serial.println(timeMonth);

    strftime(timeYear, 5, "%Y", &timeinfo);
    Serial.println(timeYear);

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

  if (!getLocalTime(&timeinfo))
  {
    Serial.println("Failed to obtain time");
  }

  if (WiFi.status() == WL_DISCONNECTED)
  {
    Serial.println("not connected");
    WiFi.begin(STASSID, STAPSK);
    if (count == true)
    {
      count++;
    }
    if (connected_to_wifi == true && disconnected == false)
    {
      // First time disconnection after connection
      count = true;

      Serial.println("Failed to obtain time, Saving Previous Time");

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
      appendFile(SPIFFS, "/hello.csv", "\n");
    }
    connected_to_wifi = false;
    disconnected = true;
  }

  else if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("connected");

    strftime(timeSeconds, 10, "%S", &timeinfo);
    Serial.println(timeSeconds);

    strftime(timeMin, 3, "%M", &timeinfo);
    Serial.println(timeMin);

    strftime(timeHour, 10, "%H", &timeinfo);
    Serial.println(timeHour);

    strftime(timeDay, 10, "%A", &timeinfo);
    Serial.println(timeDay);

    strftime(timeMonth, 10, "%B", &timeinfo);
    Serial.println(timeMonth);

    strftime(timeYear, 5, "%Y", &timeinfo);
    Serial.println(timeYear);

    Serial.println();
    if (connected_to_wifi == false && disconnected == true)
    {
      // First time connection after disconnection

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
      appendFile(SPIFFS, "/hello.csv", "\n");
    }
    connected_to_wifi = true;
    disconnected = false;
    count = false;
  }

  readFile(SPIFFS, "/hello.csv");
}

void testFileIO(fs::FS &fs, const char *path)
{
  Serial.printf("Testing file I/O with %s\r\n", path);

  static uint8_t buf[512];
  size_t len = 0;
  File file = fs.open(path, FILE_WRITE);
  if (!file)
  {
    Serial.println("- failed to open file for writing");
    return;
  }

  size_t i;
  Serial.print("- writing");
  uint32_t start = millis();
  for (i = 0; i < 2048; i++)
  {
    if ((i & 0x001F) == 0x001F)
    {
      Serial.print(".");
    }
    file.write(buf, 512);
  }
  Serial.println("");
  uint32_t end = millis() - start;
  Serial.printf(" - %u bytes written in %u ms\r\n", 2048 * 512, end);
  file.close();

  file = fs.open(path);
  start = millis();
  end = start;
  i = 0;
  if (file && !file.isDirectory())
  {
    len = file.size();
    size_t flen = len;
    start = millis();
    Serial.print("- reading");
    while (len)
    {
      size_t toRead = len;
      if (toRead > 512)
      {
        toRead = 512;
      }
      file.read(buf, toRead);
      if ((i++ & 0x001F) == 0x001F)
      {
        Serial.print(".");
      }
      len -= toRead;
    }
    Serial.println("");
    end = millis() - start;
    Serial.printf("- %u bytes read in %u ms\r\n", flen, end);
    file.close();
  }
  else
  {
    Serial.println("- failed to open file for reading");
  }
}