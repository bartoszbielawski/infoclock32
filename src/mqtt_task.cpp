#include <Arduino.h>
#include <reboot_utils.hpp>
#include <WiFi.h>
#include <PubSubClient.h>
#include <task_registry.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <data_store.hpp>
#include <runtime_store.hpp>
#include <device_store.hpp>
#include <uptime_utils.hpp>
#include <logger.hpp>
#include <night_mode_task.h>
#include <mqtt_discovery.hpp>
#include <version.hpp>
#include <esp_timer.h>

#include <string>

static WiFiClient wifiClient;
static PubSubClient mqttClient(wifiClient);

bool mqtt_is_connected() { return mqttClient.connected(); }

static std::string loopedMessage;
static QueueHandle_t pushQueue;

// Set in the callback, acted on in the main loop.
static bool    pendingReboot     = false;

static std::string mqtt_client_id()
{
    return DataStore::getInstance().get_value("mqtt_client_id", "infoclock32");
}

// Publish to <clientId>/publish/<key> with retain — the same topics the
// `…/request` path answers on. Retain means HA (and any subscriber) sees the
// last value immediately after a restart.
static void publish_state(const std::string& clientId, const char* key, const std::string& value)
{
    if (value.empty()) return;
    std::string topic = clientId + "/publish/" + key;
    mqttClient.publish(topic.c_str(), value.c_str(), true);
}

// Home Assistant availability: a dedicated topic — the /status heartbeat
// publishes JSON there, which HA's exact-payload matcher would never see as
// "online". A retained last-will covers unclean disconnects.
static void publish_availability(const std::string& clientId)
{
    std::string topic = ha_discovery::availability_topic(clientId);
    mqttClient.publish(topic.c_str(), "online", true);
}

static void publish_discovery(const std::string& clientId)
{
    if (DataStore::getInstance().get_value<int>("enable_mqtt_discovery", 1) == 0)
        return;

    ha_discovery::DeviceMeta meta;
    meta.name        = WiFi.getHostname();
    meta.identifiers = WiFi.macAddress().c_str();
    meta.model       = ESP.getChipModel();
    meta.sw_version  = APP_VERSION;

    for (const auto& m : ha_discovery::build_discovery_messages(clientId, meta))
        mqttClient.publish(m.topic.c_str(), m.payload.c_str(), true);

    logPrintf("MQT", "Home Assistant discovery published");
}

// Refresh all HA-facing state values. Called on connect and from the 60 s
// heartbeat. Missing sensor values are skipped silently — the discovery
// config stays retained and the entity just stays unknown until data arrives.
static void publish_ha_states(const std::string& clientId)
{
    RuntimeStore& rs   = RuntimeStore::getInstance();
    DeviceStore&  dst  = DeviceStore::getInstance();

    publish_state(clientId, "temp_c",   rs.get("temp_c"));
    publish_state(clientId, "temp_rh",  rs.get("temp_rh"));
    publish_state(clientId, "temp_hpa", rs.get("temp_hpa"));
    publish_state(clientId, "weather_desc", rs.get("weather_desc"));
    publish_state(clientId, "heap",     dst.get("heap"));
    publish_state(clientId, "rssi",     dst.get("rssi"));
    publish_state(clientId, "display_power", rs.get("display_power", "on"));
    publish_state(clientId, "display_brightness", std::to_string(current_display_brightness()));

    char uptimeS[24];
    snprintf(uptimeS, sizeof(uptimeS), "%lld",
             (long long)(esp_timer_get_time() / 1000000LL));
    publish_state(clientId, "uptime_s", uptimeS);
}

static void publishStatus(const std::string& clientId)
{
    char uptime[32];
    format_uptime(uptime, sizeof(uptime), millis());

    char payload[160];
    snprintf(payload, sizeof(payload),
             "{\"ip\":\"%s\",\"heap\":%u,\"uptime\":\"%s\",\"ssid\":\"%s\"}",
             WiFi.localIP().toString().c_str(),
             (unsigned)esp_get_free_heap_size(),
             uptime,
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
        {
            bool saved = set_user_brightness(level);
            logPrintf("MQT", "brightness set to %d%s", level,
                      saved ? "" : " (night mode: until it ends)");

            char lvl[8];
            snprintf(lvl, sizeof(lvl), "%d", current_display_brightness());
            publish_state(mqtt_client_id(), "display_brightness", lvl);
        }
    }
    else if (topicStr.endsWith("/power"))
    {
        String state(buf);
        state.trim();
        state.toLowerCase();
        if (state == "on" || state == "off")
        {
            // Turning on while brightness is 0 would leave the display
            // unblanked yet dark — bump to the configured day brightness.
            if (state == "on" && current_display_brightness() == 0)
                set_user_brightness(DataStore::getInstance().get_value<int>("brightness", 7));
            ResourceManager<LMDS>::getInstance().getResourceRef().requestEnabled(state == "on");
            RuntimeStore::getInstance().set("display_power", std::string(state.c_str()));
            logPrintf("MQT", "display power %s", state.c_str());

            publish_state(mqtt_client_id(), "display_power", state.c_str());
        }
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
        std::string varName(buf);

        if (varName.find("assword") != std::string::npos)
            return;

        // DeviceStore (WiFi/system) → RuntimeStore (sensors) → DataStore (config)
        std::string response = DeviceStore::getInstance().get(varName);
        if (response.empty()) response = RuntimeStore::getInstance().get(varName);
        if (response.empty()) response = ds.get_value(varName.c_str(), "");

        if (!response.empty())
        {
            String pubTopic = String(clientId.c_str()) + "/publish/" + varName.c_str();
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
    // Retained "offline" last-will: HA marks the device unavailable if it
    // dies without a clean disconnect (passed via the connect overloads —
    // this PubSubClient version has no setWill()).
    std::string willTopic = ha_discovery::availability_topic(clientId);

    bool connected;
    if (user.empty())
        connected = mqttClient.connect(clientId.c_str(), willTopic.c_str(), 1, true, "offline");
    else
        connected = mqttClient.connect(clientId.c_str(), user.c_str(), pass.c_str(),
                                       willTopic.c_str(), 1, true, "offline");

    if (!connected)
    {
        logPrintf("MQT", "connect failed, rc=%d", mqttClient.state());
        return false;
    }

    std::string topic = clientId + "/+";
    mqttClient.subscribe(topic.c_str());
    logPrintf("MQT", "connected to %s, subscribed to %s", server.c_str(), topic.c_str());

    // Retained discovery configs and availability survive broker restarts,
    // and republishing on every connect also heals a wiped broker.
    publish_availability(clientId);
    publish_discovery(clientId);
    publish_ha_states(clientId);
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

void mqtt_task(void *parameter)
{
    registerTask("MQTT", 8192, 90000);
    pushQueue = xQueueCreate(4, sizeof(char *));

    // The display boots enabled — seed the power state before anything reads it.
    RuntimeStore::getInstance().set("display_power", "on");

    auto &rmd = ResourceManager<LMDS>::getInstance();

    time_t lastHeartbeat = 0;

    // Give WiFi and DataStore time to initialise
    vTaskDelay(6000 / portTICK_PERIOD_MS);

    while (true)
    {
        task_heartbeat();
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
            reboot_with_message();
        }

        // Push messages next — drain the whole queue
        char *pushMsg = nullptr;
        while (xQueueReceive(pushQueue, &pushMsg, 0) == pdTRUE && pushMsg)
        {
            if (auto display = rmd.acquire(pdMS_TO_TICKS(10000), true))
            {
                scrollMessage(std::string(pushMsg), display, 40);
                free(pushMsg);
                pumpLoop(50);
            }
            else if (xQueueSend(pushQueue, &pushMsg, 0) == pdTRUE)
            {
                // Display busy — put the message back and retry after the
                // next pump cycle instead of blocking or losing it.
                logPrintf("MQT", "display busy, push re-queued");
                break;
            }
            else
            {
                logPrintf("MQT", "push queue full, message dropped");
                free(pushMsg);
                pumpLoop(50);
            }
        }

        // Display looped message
        if (!loopedMessage.empty())
        {
            // Wait long enough to outlast a clock hold (7-9 s, cut short once
            // anyone is queued); a shorter timeout almost never got a turn.
            // On timeout the message is simply tried again next iteration.
            if (auto display = rmd.acquire(pdMS_TO_TICKS(15000)))
                scrollMessage(loopedMessage, display, 50);
        }

        // Periodic heartbeat every 60 seconds
        time_t now = time(nullptr);
        if (difftime(now, lastHeartbeat) >= 60)
        {
            std::string clientId = mqtt_client_id();
            publishStatus(clientId);
            publish_ha_states(clientId);
            lastHeartbeat = now;
        }

        pumpLoop(loopedMessage.empty() ? 5000 : 500);
    }
}
