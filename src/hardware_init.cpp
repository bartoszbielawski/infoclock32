#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <cstring>
#include <atomic>
#include <wifi_cache.h>
#include <wifi_policy.hpp>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string>
#include <data_store.hpp>
#include <logger.hpp>
#include <task_registry.hpp>
#include <resource_manager.hpp>
#include <LMDS.hpp>
#include <graphic_utils.hpp>
#include <display_strings.hpp>

static volatile bool s_ap_mode = false;   // true while not connected to any AP
static volatile bool s_unprovisioned = false;   // no credentials anywhere at boot
static std::string s_hostname = "infoclock32";

bool wifi_is_ap_mode() { return s_ap_mode; }

static void start_mdns()
{
    if (MDNS.begin(s_hostname.c_str()))
        logPrintf("WIFI", "mDNS started — http://%s.local", s_hostname.c_str());
    MDNS.addService("http", "tcp", 80);
}

void wifi_erase_cache()
{
    WiFi.mode(WIFI_STA);                          // driver must be started
    esp_wifi_set_storage(WIFI_STORAGE_FLASH);     // the erase has to hit flash
    wifi_config_t empty;
    memset(&empty, 0, sizeof(empty));
    esp_wifi_set_config(WIFI_IF_STA, &empty);
    WiFi.disconnect(false, true);
}

// Some ESP32-C3 boards (small clones with a weak antenna match) cannot complete
// an association at full TX power: the AP is visible but phones get "connection
// failed". `wifi_tx_dbm` (e.g. 8.5) caps the power; unset leaves the default.
// The radio must already be started, so call this after WiFi.mode().
static void apply_tx_power()
{
    float dbm = DataStore::getInstance().get_value<float>("wifi_tx_dbm", 0.0f);
    if (dbm <= 0.0f) return;
    int quarters = (int)(dbm * 4.0f + 0.5f);          // esp_wifi unit: 0.25 dBm
    quarters = max(8, min(84, quarters));
    WiFi.setTxPower((wifi_power_t)quarters);
    logPrintf("WIFI", "TX power capped at %.2f dBm", quarters / 4.0f);
}

// Keeps the "join <hostname>-setup, open 192.168.4.1" instructions on the
// matrix for as long as the device is in setup mode (issue #9): while the
// boot portal blocks setup() and while the fallback AP is up. Unprovisioned
// devices repeat it back to back; a device whose saved network is merely
// unreachable shows it every 30 s so the clock stays readable in between.
//
// The task only exists while the device is offline: enter_ap_mode() starts
// it and it deletes itself once connected, returning its 4 KB stack to the
// heap for the normal (online) case.
static std::atomic<bool> s_hint_running{false};

static void setup_hint_task(void* /*pv*/)
{
    registerTask("SetupHint", 4096, 120000);
    for (;;)
    {
        task_heartbeat();
        if (!s_ap_mode)
        {
            // Drop the flag before the final check: an enter_ap_mode() racing
            // with this exit either sees the flag down and starts a new task,
            // or this task sees s_ap_mode back up and keeps going.
            s_hint_running = false;
            if (!s_ap_mode)
            {
                task_mark_exited();
                vTaskDelete(nullptr);
            }
            s_hint_running = true;
        }
        if (s_ap_mode)
        {
            const DisplayStrings& tr = display_strings(
                parse_lang(DataStore::getInstance().get_value("language", "en")));
            std::string msg = std::string(tr.ap_connect) + s_hostname + "-setup  " +
                              tr.ap_browse + " 192.168.4.1";
            task_heartbeat_grace(120000);   // FIFO wait + scroll
            if (auto display = ResourceManager<LMDS>::getInstance().acquire())
                scrollMessage(msg, display, 40);
            task_heartbeat();
            vTaskDelay((s_unprovisioned ? 1000 : 30000) / portTICK_PERIOD_MS);
        }
        else
        {
            vTaskDelay(1000 / portTICK_PERIOD_MS);
        }
    }
}

// Mark the device offline and make sure the setup hint is scrolling. Cheap to
// call repeatedly (the monitor does, every pass while offline).
static void enter_ap_mode()
{
    s_ap_mode = true;
    bool expected = false;
    if (s_hint_running.compare_exchange_strong(expected, true) &&
        xTaskCreate(setup_hint_task, "SetupHint", 4096, nullptr, 1, nullptr) != pdPASS)
    {
        s_hint_running = false;
        logPrintf("WIFI", "setup hint task could not start (heap %u)",
                  (unsigned)esp_get_free_heap_size());
    }
}

// Persistent watchdog. Survives the WiFiManager portal closing: whenever the
// STA link drops (or boot ended offline) it retries every 30 s - with the
// config.txt credentials when they exist (the driver's own copy may have been
// erased), otherwise with whatever the driver holds (see wifi_policy.hpp).
static void wifi_monitor_task(void* /*pv*/)
{
    registerTask("WiFiMonitor", 4096, 90000);
    // Connected at boot: main.cpp already scrolls the network info after the banner.
    bool wasConnected = WiFi.status() == WL_CONNECTED;
    // Fallback AP: the WiFiManager portal only lives for its boot-time timeout,
    // so while offline we raise a plain soft-AP (web UI at 192.168.4.1, /wifi
    // page edits the credentials) after wifi_ap_after_s and drop it on reconnect.
    const int apAfterS = max(0, DataStore::getInstance().get_value<int>("wifi_ap_after_s", 60));
    const std::string apName = s_hostname + "-setup";
    bool apUp = false;
    uint32_t offlineSinceMs = millis();
    int ticksToRetry = 0;
    for (;;)
    {
        task_heartbeat();
        if (WiFi.status() == WL_CONNECTED)
        {
            if (apUp)
            {
                WiFi.softAPdisconnect(true);
                WiFi.mode(WIFI_STA);
                apUp = false;
                logPrintf("WIFI", "fallback AP closed");
            }
            if (!wasConnected)
            {
                wasConnected = true;
                s_ap_mode = false;
                s_unprovisioned = false;
                logPrintf("WIFI", "connected, IP=%s",
                          WiFi.localIP().toString().c_str());
                start_mdns();
                if (DataStore::getInstance().get_value<int>("show_ip_on_boot", 1))
                {
                    std::string msg = std::string(WiFi.SSID().c_str()) + "  " +
                                      WiFi.localIP().toString().c_str() + "  " + s_hostname + ".local";
                    task_heartbeat_grace(90000);   // FIFO wait + scroll
                    if (auto display = ResourceManager<LMDS>::getInstance().acquire())
                        scrollMessage(msg, display, 30);
                    task_heartbeat();
                }
            }
            vTaskDelay(5000 / portTICK_PERIOD_MS);
            continue;
        }

        if (wasConnected)
        {
            logPrintf("WIFI", "link lost — retrying every 30 s");
            offlineSinceMs = millis();
        }
        wasConnected = false;
        enter_ap_mode();

        if (!apUp && apAfterS > 0 && millis() - offlineSinceMs >= (uint32_t)apAfterS * 1000)
        {
            WiFi.mode(WIFI_AP_STA);
            apUp = WiFi.softAP(apName.c_str());
            apply_tx_power();
            logPrintf("WIFI", "offline for %d s — fallback AP %s at %s",
                      apAfterS, apName.c_str(), WiFi.softAPIP().toString().c_str());
        }

        if (ticksToRetry-- <= 0)
        {
            // Re-read each time: /wifi or the portal may have changed them.
            auto& ds = DataStore::getInstance();
            const std::string ssid = ds.get_value("wifi_ssid", "");
            if (wifi_policy::retryMethod(!ssid.empty()) == wifi_policy::Retry::BeginWithConfig)
            {
                WiFi.persistent(false);   // never let a retry rewrite the NVS cache
                WiFi.begin(ssid.c_str(), ds.get_value("wifi_password", "").c_str());
            }
            else
            {
                WiFi.reconnect();   // no-op if the device never had credentials
            }
            ticksToRetry = 5;   // 6 ticks x 5 s = every 30 s
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void hardware_init()
{
    Serial.println("hardware_init: start");

    auto& ds = DataStore::getInstance();
    s_hostname = ds.get_value("hostname", "infoclock32");
    std::string ssid     = ds.get_value("wifi_ssid", "");
    std::string password = ds.get_value("wifi_password", "");

    int portal_timeout_s = max(30, min(600,
        ds.get_value<int>("wifi_portal_timeout_s", 180)));

    WiFi.setHostname(s_hostname.c_str());

    std::string apName = s_hostname + "-setup";

    // config.txt is the single source of truth for station credentials.
    // The setup portal is just another editor of the file: whatever is
    // provisioned there is mirrored into wifi_ssid/wifi_password, so the
    // /wifi page, /edit and the portal all write the same keys and a
    // provisioned device boots straight from the file.
    static WiFiManager wm;
    wm.setHostname(s_hostname.c_str());
    wm.setConfigPortalTimeout(portal_timeout_s);
    wm.setBreakAfterConfig(true);
    wm.setSaveConfigCallback([&wm]() {
        auto& ds = DataStore::getInstance();
        ds.set_value("wifi_ssid",     std::string(wm.getWiFiSSID().c_str()));
        ds.set_value("wifi_password", std::string(wm.getWiFiPass().c_str()));
        ds.save_to_file("/config.txt");
        logPrintf("WIFI", "portal credentials saved to config, SSID=%s",
                  wm.getWiFiSSID().c_str());
    });

    // The portal blocks this function; the hint task keeps the instructions
    // scrolling meanwhile (the display is initialised before hardware_init).
    wm.setAPCallback([](WiFiManager*) { enter_ap_mode(); });

    bool connected = false;
    WiFi.mode(WIFI_STA);
    apply_tx_power();

    // Nothing to connect to (fresh flash or /forget_wifi): the portal is the
    // only way forward, so keep it open until someone provisions the device
    // instead of timing out into a gap with no setup network (issue #9).
    if (ssid.empty() && !wm.getWiFiIsSaved())
    {
        s_unprovisioned = true;
        wm.setConfigPortalTimeout(0);
        logPrintf("WIFI", "no credentials — setup portal stays open until provisioned");
    }
    if (!ssid.empty())
    {
        // Try the config.txt credentials first — without persisting them, so
        // the attempt cannot rewrite the NVS cache (issue #3).
        WiFi.persistent(false);
        logPrintf("WIFI", "trying config.txt credentials, SSID=%s", ssid.c_str());
        WiFi.begin(ssid.c_str(), password.c_str());
        for (int i = 0; i < 100; i++)
        {
            wl_status_t st = WiFi.status();
            if (st == WL_CONNECTED ||
                st == WL_NO_SSID_AVAIL ||     // SSID not in the air
                st == WL_CONNECT_FAILED)      // rejected by the AP
                break;
            vTaskDelay(100 / portTICK_PERIOD_MS);
        }
        connected = WiFi.status() == WL_CONNECTED;
        if (connected)
            logPrintf("WIFI", "connected with config.txt credentials");
        else
            logPrintf("WIFI", "config.txt credentials failed (status %d)", (int)WiFi.status());
    }

    if (!connected)
    {
        const wifi_policy::BootFailurePlan plan =
            wifi_policy::onBootFailure(!ssid.empty(), (int)WiFi.status());
        if (plan.eraseDriverCache)
        {
            // The AP rejected the file's credentials: erase the stored pair
            // so a stale cache cannot outvote it, and autoConnect() opens the
            // portal directly (no doomed stored-credentials attempt first).
            wifi_erase_cache();
            logPrintf("WIFI", "credentials rejected - opening setup portal at %s / 192.168.4.1",
                      apName.c_str());
        }
        if (plan.openPortal)
        {
            // With wifi_ssid blank this still tries the NVS pair first — the
            // recovery hatch for a freshly reflashed filesystem.
            connected = wm.autoConnect(apName.c_str());
        }
        else
        {
            // Network not visible / slow (router still booting after a power
            // cut, out of range): keep the credentials and go on offline; the
            // monitor retries with them and opens the fallback AP if needed.
            logPrintf("WIFI", "network not reachable yet - continuing offline, retrying every 30 s");
        }
    }

    if (connected)
    {
        s_ap_mode = false;
        s_unprovisioned = false;
        // mains-powered: modem sleep only adds latency and packet loss
        WiFi.setSleep(false);
        logPrintf("WIFI", "connected, IP=%s",
                  WiFi.localIP().toString().c_str());
        start_mdns();
    }
    else
    {
        enter_ap_mode();
        logPrintf("WIFI", "offline - retrying every 30 s; fallback AP %s after wifi_ap_after_s",
                  apName.c_str());
    }

    xTaskCreate(wifi_monitor_task, "WiFiMonitor", 4096, nullptr, 1, nullptr);
}
