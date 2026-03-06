#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>

#include <string>

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

static std::string loopedMessage;
static QueueHandle_t pushQueue;

static void onMessage(char *topic, byte *payload, unsigned int length)
{
    length = min(length, (unsigned int)128);
    char buf[129];
    memcpy(buf, payload, length);
    buf[length] = '\0';

    String topicStr(topic);

    if (topicStr.endsWith("/push"))
    {
        char *copy = (char *)malloc(length + 1);
        if (copy)
        {
            memcpy(copy, buf, length + 1);
            if (xQueueSend(pushQueue, &copy, 0) != pdTRUE)
                free(copy); // queue full, drop message
        }
    }
    else if (topicStr.endsWith("/looped"))
    {
        loopedMessage = buf;
    }
    else if (topicStr.endsWith("/clear"))
    {
        loopedMessage.clear();
    }
    else if (topicStr.endsWith("/request"))
    {
        DataStore &ds = DataStore::getInstance();
        std::string clientId = ds.get_value("mqtt_client_id", "infoclock32");
        String varName(buf);

        // Block password requests
        if (varName.endsWith("assword"))
            return;

        String response;
        if (varName == "IP")
        {
            response = WiFi.localIP().toString();
        }
        else if (varName == "HEAP")
        {
            response = String(esp_get_free_heap_size());
        }
        else if (varName == "UPTIME")
        {
            unsigned long ms = millis();
            unsigned long h = ms / 3600000UL;
            unsigned long m = (ms % 3600000UL) / 60000UL;
            unsigned long s = (ms % 60000UL) / 1000UL;
            char upbuf[32];
            snprintf(upbuf, sizeof(upbuf), "%luh%lum%lus", h, m, s);
            response = upbuf;
        }
        else
        {
            response = ds.get_value(varName.c_str(), "").c_str();
        }

        if (!response.isEmpty())
        {
            String pubTopic = String(clientId.c_str()) + "/publish/" + varName;
            mqttClient.publish(pubTopic.c_str(), response.c_str());
        }
    }
}

static bool reconnect()
{
    DataStore &ds = DataStore::getInstance();
    std::string server = ds.get_value("mqtt_server", "");
    if (server.empty())
    {
        Serial.println("MQTT: no mqtt_server configured");
        return false;
    }

    std::string clientId = ds.get_value("mqtt_client_id", "infoclock32");
    std::string user = ds.get_value("mqtt_user", "");
    std::string pass = ds.get_value("mqtt_password", "");

    mqttClient.setServer(server.c_str(), 1883);
    mqttClient.setCallback(onMessage);
    mqttClient.setKeepAlive(60);

    bool connected;
    if (user.empty())
        connected = mqttClient.connect(clientId.c_str());
    else
        connected = mqttClient.connect(clientId.c_str(), user.c_str(), pass.c_str());

    if (!connected)
    {
        Serial.printf("MQTT: connect failed, rc=%d\n", mqttClient.state());
        return false;
    }

    std::string topic = clientId + "/+";
    mqttClient.subscribe(topic.c_str());
    Serial.printf("MQTT: connected to %s, subscribed to %s\n", server.c_str(), topic.c_str());
    return true;
}

// Call mqttClient.loop() for a given number of milliseconds total.
static void pumpLoop(int ms)
{
    int iterations = ms / 10;
    for (int i = 0; i < iterations; i++)
    {
        mqttClient.loop();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void mqtt_task(void *parameter)
{
    pushQueue = xQueueCreate(4, sizeof(char *));

    auto &rmd = ResourceManager<LMDS>::getInstance();
    auto &matrix = rmd.getResourceRef();

    // Give WiFi and DataStore time to initialise
    vTaskDelay(6000 / portTICK_PERIOD_MS);

    while (true)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (!mqttClient.connected())
        {
            if (!reconnect())
            {
                vTaskDelay(15000 / portTICK_PERIOD_MS);
                continue;
            }
        }

        // Keep connection alive while checking for messages
        pumpLoop(100);

        // Push messages take priority — drain the whole queue
        char *pushMsg = nullptr;
        while (xQueueReceive(pushQueue, &pushMsg, 0) == pdTRUE && pushMsg)
        {
            if (rmd.make_access_request())
            {
                scrollMessage(std::string(pushMsg), matrix, 40);
                rmd.release_access();
            }
            free(pushMsg);
            pumpLoop(50); // keep MQTT alive between scrolls
        }

        // Display looped message
        if (!loopedMessage.empty())
        {
            if (rmd.make_access_request())
            {
                scrollMessage(loopedMessage, matrix, 50);
                rmd.release_access();
            }
            pumpLoop(500);
        }
        else
        {
            // Nothing to display — just keep the connection alive
            pumpLoop(5000);
        }
    }
}
