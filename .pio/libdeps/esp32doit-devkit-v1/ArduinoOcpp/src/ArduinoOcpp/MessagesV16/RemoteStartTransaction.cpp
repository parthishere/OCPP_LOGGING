// matth-x/ArduinoOcpp
// Copyright Matthias Akstaller 2019 - 2022
// MIT License

#include <ArduinoOcpp/MessagesV16/RemoteStartTransaction.h>
#include <ArduinoOcpp/Core/OcppModel.h>
#include <ArduinoOcpp/Tasks/ChargePointStatus/ChargePointStatusService.h>
#include <ArduinoOcpp/Debug.h>
#include <ArduinoOcpp.h> //Matt-x/ArduinoOcpp library
#include <Arduino.h>     //Arduino firmware library
#include <ArduinoOcpp/MessagesV16/Authorize.h>
#include <iostream>
#include <ArduinoOcpp/MessagesV16/StatusNotification.h>
using ArduinoOcpp::Ocpp16::RemoteStartTransaction;

void st();

// variables declaration
// bool transaction_in_process = false; //Check if transaction is in-progress
int evPlugged1 = EV_Unplugged; // Check if EV is plugged-in
bool transaction = false;
const char *idTag;
bool canStartTransaction = false;
RemoteStartTransaction::RemoteStartTransaction()
{
}

const char *RemoteStartTransaction::getOcppOperationType()
{
    return "RemoteStartTransaction";
}

void RemoteStartTransaction::processReq(JsonObject payload)
{
    connectorId = payload["connectorId"] | -1;

    const char *idTagIn = payload["idTag"] | "A0000000";
    size_t len = strnlen(idTagIn, IDTAG_LEN_MAX + 2);
    if (len <= IDTAG_LEN_MAX)
    {
        snprintf(idTag, IDTAG_LEN_MAX + 1, "%s", idTagIn);
    }

    if (payload.containsKey("chargingProfile"))
    {
        AO_DBG_WARN("chargingProfile via RmtStartTransaction not supported yet");
    }
}

std::unique_ptr<DynamicJsonDocument> RemoteStartTransaction::createConf()
{
    auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(JSON_OBJECT_SIZE(1)));
    JsonObject payload = doc->to<JsonObject>();

    if (*idTag == '\0')
    {
        AO_DBG_WARN("idTag format violation");
        payload["status"] = "Rejected";
        return doc;
    }

    if (connectorId >= 1)
    {
        // connectorId specified for given connector, try to start Transaction here
        if (ocppModel && ocppModel->getConnectorStatus(connectorId))
        {
            auto connector = ocppModel->getConnectorStatus(connectorId);
            if (connector->getTransactionId() < 0 &&
                connector->getAvailability() == AVAILABILITY_OPERATIVE &&
                connector->getSessionIdTag() == nullptr)
            {
                canStartTransaction = true;
            }
        }
    }
    else
    {
        // connectorId not specified. Find free connector
        if (ocppModel && ocppModel->getChargePointStatusService())
        {
            auto cpStatusService = ocppModel->getChargePointStatusService();
            for (int i = 1; i < cpStatusService->getNumConnectors(); i++)
            {
                auto connIter = cpStatusService->getConnector(i);
                if (connIter->getTransactionId() < 0 &&
                    connIter->getAvailability() == AVAILABILITY_OPERATIVE &&
                    connIter->getSessionIdTag() == nullptr)
                {
                    canStartTransaction = true;
                    connectorId = i;
                    break;
                }
            }
        }
    }

    if (canStartTransaction)
    {

        if (ocppModel && ocppModel->getConnectorStatus(connectorId))
        {
            auto connector = ocppModel->getConnectorStatus(connectorId);

            connector->beginSession(idTag);
            authorize(idTag, [](JsonObject response)
                      {
                          char idTag[IDTAG_LEN_MAX + 1] = {'\0'};
                          if (!strcmp("Accepted", response["idTagInfo"]["status"] | "Invalid"))
                          {
                              Serial.println(F("User is authorized to start charging"));
                              delay(9000);
                              if (digitalRead(EV_Plug_Pin) == EV_Plugged && evPlugged1 == EV_Unplugged && isAvailable())
                              {
                                  // evPlugged1 = EV_Plugged;
                                  startTransaction(idTag, [](JsonObject response)
                                                   {
                            Serial.print("start transaction from remote");
                            Serial.printf("Started OCPP transaction from remote. Status: %s, transactionId: %u\n",
                            response["idTagInfo"]["status"] | "Invalid",
                            response["transactionId"] | -1);
                            delay(5000);
                            evPlugged1==EV_Plugged;
                            if(evPlugged1==EV_Plugged && response["status"]=="Accepted")
                            {
                                st();
                            } });
                                  /*if(evPlugged1==EV_Plugged && response["status"]=="Accepted")
                                  {
                                      st();
                                  }*/
                              }
                          } //
                      });
        }
        payload["status"] = "Accepted";
    }
    else
    {
        AO_DBG_INFO("No connector to start transaction");
        // payload["status"] = "Accepted";
        payload["status"] = "Rejected";
    }
    /*    if(payload["status"] == "Accepted" && evPlugged1 == EV_Plugged && digitalRead(EV_Plug_Pin)==EV_Plugged)
        {
            Serial.print("inside if");
                st();
        }
        else
        {
            Serial.print("inside else");
            st();
        }*/
    return doc;
}

// if authorised
// transaction_in_process = true;

// if charging started
//  isInSession() == true ----> if true
//  evPlugged == EV_Plugged
std::unique_ptr<DynamicJsonDocument> RemoteStartTransaction::createReq()
{
    auto doc = std::unique_ptr<DynamicJsonDocument>(new DynamicJsonDocument(JSON_OBJECT_SIZE(1)));
    JsonObject payload = doc->to<JsonObject>();

    payload["idTag"] = "A0-00-00-00";

    return doc;
}

void RemoteStartTransaction::processConf(JsonObject payload)
{
}

void st()
{
    Serial.print(evPlugged1);
    // if (evPlugged1 == EV_Plugged) {
    do
    {
        if (digitalRead(EV_Plug_Pin) == EV_Unplugged)
        {
            stopTransaction([](JsonObject response)
                            {
                                Serial.print("Stopped OCPP transaction");
                                evPlugged1 = EV_Unplugged; });
        }
    } while (digitalRead(EV_Plug_Pin) == EV_Plugged);
    //}
}