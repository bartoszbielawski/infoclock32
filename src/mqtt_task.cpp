#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <logger.hpp>

#include <string>

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

bool mqtt_is_connected() { return mqttClient.connected(); }

static std::string loopedMessage;
static QueueHandle_t pushQueue;

// Pending hardware commands set in the callback, applied in the main loop
// where display access is safe to acquire.
static int8_t  pendingBrightness = -1;  // -1 = none, 0-15 = set level
static bool    pendingReboot     = false;

static void publishStatus(const std::string& clientId)
{
    unsigned long ms  = millis();
    unsigned long h   = ms / 3600000UL;
    unsigned long m   = (ms % 3600000UL) / 60000UL;
    unsigned long s   = (ms % 60000UL) / 1000UL;

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"ip\":\"%s\",\"heap\":%u,\"uptime\":\"%luh%lum%lus\",\"ssid\":\"%s\"}",
             WiFi.localIP().toString().c_str(),
             (unsigned)esp_get_free_heap_size(),
             h, m, s,
             WiFi.SSID().c_str());

    std::string topic = clientId + "/status";
    mqttClient.publish(topic.c_str(), payload);
}

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
                free(copy);
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
    else if (topicStr.endsWith("/brightness"))
    {
        int level = atoi(buf);
        if (level >= 0 && level <= 15)
            pendingBrightness = (int8_t)level;
    }
    else if (topicStr.endsWith("/config"))
    {
        String line(buf);
        int sep = line.indexOf('=');
        if (sep <= 0)
            return;

        String key   = line.substring(0, sep);
        String value = line.substring(sep + 1);

        // Block sensitive keys
        String keyLower = key;
        keyLower.toLowerCase();
        if (keyLower.indexOf("password") >= 0 || keyLower.indexOf("secret") >= 0)
        {
            logPrintf("MQT", "/config blocked sensitive key '%s'", key.c_str());
            return;
        }

        DataStore::getInstance().set_value(key.c_str(), value.c_str());
        logPrintf("MQT", "/config set '%s' = '%s'", key.c_str(), value.c_str());
    }
    else if (topicStr.endsWith("/reboot"))
    {
        logPrintf("MQT", "reboot requested");
        pendingReboot = true;
    }
    else if (topicStr.endsWith("/request"))
    {
        DataStore &ds = DataStore::getInstance();
        std::string clientId = ds.get_value("mqtt_client_id", "infoclock32");
        String varName(buf);

        if (varName.endsWith("assword"))
            return;

        String response;
        if (varName == "IP")
        {
            response = WiFi.localIP().toString();
        }
        else if (varName == "HEAP")
        {
            response = String((unsigned)esp_get_free_heap_size());
        }
        else if (varName == "UPTIME")
        {
            unsigned long ms = millis();
            unsigned long h  = ms / 3600000UL;
            unsigned long m  = (ms % 3600000UL) / 60000UL;
            unsigned long s  = (ms % 60000UL) / 1000UL;
            char upbuf[32];
            snprintf(upbuf, sizeof(upbuf), "%luh%lum%lus", h, m, s);
            response = upbuf;
        }
        else if (varName == "SSID")
        {
            response = WiFi.SSID();
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
        logPrintf("MQT", "no mqtt_server configured");
        return false;
    }

    std::string clientId = ds.get_value("mqtt_client_id", "infoclock32");
    std::string user     = ds.get_value("mqtt_user", "");
    std::string pass     = ds.get_value("mqtt_password", "");

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
        logPrintf("MQT", "connect failed, rc=%d", mqttClient.state());
        return false;
    }

    std::string topic = clientId + "/+";
    mqttClient.subscribe(topic.c_str());
    logPrintf("MQT", "connected to %s, subscribed to %s", server.c_str(), topic.c_str());
    return true;
}

// Pump the MQTT loop for the given number of milliseconds.
static void pumpLoop(int ms)
{
    int iterations = ms / 10;
    for (int i = 0; i < iterations; i++)
    {
        mqttClient.loop();
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// Apply any pending hardware commands that require display access.
static void applyPendingHardware(ResourceManager<LMDS> &rmd)
{
    if (pendingBrightness < 0)
        return;

    if (!rmd.make_access_request())
        return;

    rmd.getResourceRef().setIntensity((uint8_t)pendingBrightness);
    DataStore::getInstance().set_value("brightness", std::to_string(pendingBrightness));
    DataStore::getInstance().save_to_file("/config.txt");
    logPrintf("MQT", "brightness set to %d", pendingBrightness);
    pendingBrightness = -1;

    rmd.release_access();
}

void mqtt_task(void *parameter)
{
    pushQueue = xQueueCreate(4, sizeof(char *));

    auto &rmd    = ResourceManager<LMDS>::getInstance();
    auto &matrix = rmd.getResourceRef();

    time_t lastHeartbeat = 0;

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

        // Pump MQTT to receive incoming messages
        pumpLoop(100);

        // Reboot takes priority over everything else
        if (pendingReboot)
        {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            ESP.restart();
        }

        // Apply brightness / power changes
        applyPendingHardware(rmd);

        // Push messages next — drain the whole queue
        char *pushMsg = nullptr;
        while (xQueueReceive(pushQueue, &pushMsg, 0) == pdTRUE && pushMsg)
        {
            if (rmd.make_access_request())
            {
                scrollMessage(std::string(pushMsg), matrix, 40);
                rmd.release_access();
            }
            free(pushMsg);
            pumpLoop(50);
        }

        // Display looped message
        if (!loopedMessage.empty())
        {
            if (rmd.make_access_request())
            {
                scrollMessage(loopedMessage, matrix, 50);
                rmd.release_access();
            }
        }

        // Periodic heartbeat every 60 seconds
        time_t now = time(nullptr);
        if (difftime(now, lastHeartbeat) >= 60)
        {
            std::string clientId = DataStore::getInstance().get_value("mqtt_client_id", "infoclock32");
            publishStatus(clientId);
            lastHeartbeat = now;
        }

        pumpLoop(loopedMessage.empty() ? 5000 : 500);
    }
}
