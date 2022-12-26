// matth-x/ArduinoOcpp
// Copyright Matthias Akstaller 2019 - 2022
// MIT License

#include <ArduinoOcpp/Core/OcppSocket.h>
#include <ArduinoOcpp/Core/OcppServer.h>
#include <ArduinoOcpp/Debug.h>
#include "FS.h"
#include "SPIFFS.h"
#include "time.h"
#include <Ticker.h>
#include "ArduinoOcpp/Core/FileManage.h"

#ifndef AO_CUSTOM_WS

bool server_disconnected_file_management = false;
char parth::timeSeconds[10];
char parth::timeMin[3];
char parth::timeHour[10];
char parth::timeDay[10];
char parth::timeMonth[10];
char parth::timeYear[5];
struct tm timeinfo;
int parth::count;

const long parth::gmtOffset_sec = 0;
const int parth::daylightOffset_sec = 3600;
unsigned long parth::time_now = 0;
int parth::period = 1000;

using namespace ArduinoOcpp;

using namespace ArduinoOcpp::EspWiFi;

OcppClientSocket::OcppClientSocket(WebSocketsClient *wsock) : wsock(wsock)
{
    configTime(parth::gmtOffset_sec, parth::daylightOffset_sec, "pool.ntp.org");
}

void OcppClientSocket::loop()
{
    wsock->loop();
}

bool OcppClientSocket::sendTXT(std::string &out)
{
    return wsock->sendTXT(out.c_str(), out.length());
}

void OcppClientSocket::setReceiveTXTcallback(ReceiveTXTcallback &callback)
{

    wsock->onEvent([callback](WStype_t type, uint8_t *payload, size_t length)
                   {
        switch (type)
        {
        case WStype_DISCONNECTED:
            parth::count += 3;   
            AO_DBG_INFO("Disconnected To Server");
            configTime(parth::gmtOffset_sec, parth::daylightOffset_sec, "pool.ntp.org");
            if(parth::count < 20){
                break;
            }
            // ocpp_deinitialize();
            // ocpp_initialize();
            if(WiFi.status() == WL_CONNECTED){
                if (!getLocalTime(&timeinfo))
                {
                    Serial.println("Failed to obtain time in ocpp socket file");
                }
                Serial.println("This is server disconnection info");

                strftime(parth::timeSeconds, 10, "%S", &timeinfo);
                strftime(parth::timeMin, 3, "%M", &timeinfo);
                strftime(parth::timeHour, 10, "%H", &timeinfo);
                strftime(parth::timeDay, 10, "%A", &timeinfo);
                strftime(parth::timeMonth, 10, "%B", &timeinfo);
                strftime(parth::timeYear, 5, "%Y", &timeinfo);
                Serial.println();
                if (!server_disconnected_file_management)
                {
                    // First time connection after disconnection

                    appendFile(SPIFFS, "/hello.csv", "DISCONNECTION_SERVER,");
                    appendFile(SPIFFS, "/hello.csv", parth::timeDay);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeMonth);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeYear);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeHour);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeMin);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeSeconds);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", "500");
                    appendFile(SPIFFS, "/hello.csv", "\n");
                }
                
                readFile(SPIFFS, "/hello.csv");

            }
            parth::count = 0;
            server_disconnected_file_management = true;
            break;


            case WStype_CONNECTED:
                AO_DBG_INFO("Connected to url: %s", payload);
            
                if (!getLocalTime(&timeinfo))
                {
                    Serial.println("Failed to obtain time in ocpp socket file");
                }
                Serial.println("This is server disconnection info");

                strftime(parth::timeSeconds, 10, "%S", &timeinfo);
                strftime(parth::timeMin, 3, "%M", &timeinfo);
                strftime(parth::timeHour, 10, "%H", &timeinfo);
                strftime(parth::timeDay, 10, "%A", &timeinfo);
                strftime(parth::timeMonth, 10, "%B", &timeinfo);
                strftime(parth::timeYear, 5, "%Y", &timeinfo);
                Serial.println();
                if (server_disconnected_file_management)
                {
                    // First time connection after disconnection

                    appendFile(SPIFFS, "/hello.csv", "CONNECTION_SERVER,");
                    appendFile(SPIFFS, "/hello.csv", parth::timeDay);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeMonth);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeYear);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeHour);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeMin);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", parth::timeSeconds);
                    appendFile(SPIFFS, "/hello.csv", ",");

                    appendFile(SPIFFS, "/hello.csv", "100");
                    appendFile(SPIFFS, "/hello.csv", "\n");
                }
                server_disconnected_file_management = false;
                readFile(SPIFFS, "/hello.csv");

                
                break;
            case WStype_TEXT:
                AO_DBG_TRAFFIC_IN(payload);

                if (!callback((const char *)payload, length))
                { // forward message to OcppEngine
                    AO_DBG_WARN("Processing WebSocket input event failed");
                }
                break;
            case WStype_BIN:
                AO_DBG_WARN("Binary data stream not supported");
                break;
            case WStype_PING:
                // pong will be send automatically
                AO_DBG_TRAFFIC_IN("WS ping");
                break;
            case WStype_PONG:
                // answer to a ping we send
                AO_DBG_TRAFFIC_IN("WS pong");
                break;
            case WStype_FRAGMENT_TEXT_START: // fragments are not supported
            default:
                AO_DBG_WARN("Unsupported WebSocket event type");
                break;
            } });
}

OcppServerSocket::OcppServerSocket(IPAddress &ip_addr) : ip_addr(ip_addr)
{
}

OcppServerSocket::~OcppServerSocket()
{
    OcppServer::getInstance()->removeReceiveTXTcallback(this->ip_addr);
}

void OcppServerSocket::loop()
{
    // nothing here. The client must call the EspWiFi server loop function
}

bool OcppServerSocket::sendTXT(std::string &out)
{
    AO_DBG_TRAFFIC_OUT(out.c_str());
    return OcppServer::getInstance()->sendTXT(ip_addr, out);
}

void OcppServerSocket::setReceiveTXTcallback(ReceiveTXTcallback &callback)
{
    OcppServer::getInstance()->setReceiveTXTcallback(ip_addr, callback);
}

// void EspWiFi::writeFile(fs::FS &fs, const char *path, const char *message)
// {
//     Serial.printf("Writing file: %s\r\n", path);

//     File file = fs.open(path, FILE_WRITE);
//     if (!file)
//     {
//         Serial.println("- failed to open file for writing");
//         return;
//     }
//     if (file.print(message))
//     {
//         Serial.println("- file written");
//     }
//     else
//     {
//         Serial.println("- write failed");
//     }
//     file.close();
// }

// void appendFile(fs::FS &fs, const char *path, const char *message)
// {
//     Serial.printf("Appending to file: %s\r\n", path);

//     File file = fs.open(path, FILE_APPEND);
//     if (!file)
//     {
//         Serial.println("- failed to open file for appending");
//         return;
//     }
//     if (file.print(message))
//     {
//         Serial.println("- message appended");
//     }
//     else
//     {
//         Serial.println("- append failed");
//     }
//     file.close();
// }

#endif
