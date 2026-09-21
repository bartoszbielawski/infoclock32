#include <Arduino.h>
#include <WiFi.h>
#include <wrap_client.hpp>
#include <wrap_subscriptions.hpp>
#include <runtime_store.hpp>
#include <data_store.hpp>
#include <logger.hpp>

static void onWrapMsg(const WrapMsg& msg)
{
    if (msg.type == 'E')
    {
        logPrintf("WRAP", "error [%s]: %s", msg.key.c_str(), msg.error.c_str());
        return;
    }

    std::string val = msg.values[msg.field.c_str()] | "";
    RuntimeStore::getInstance().set(msg.key, val);
    logPrintf("WRAP", "%s = %s", msg.key.c_str(), val.c_str());
}

void wrap_task(void* parameter)
{
    // Wait for WiFi before connecting.
    while (WiFi.status() != WL_CONNECTED)
        vTaskDelay(pdMS_TO_TICKS(1000));

    DataStore& ds = DataStore::getInstance();
    std::string host = ds.get_value("wrap_host", "wrap-staging.cern.ch");

    WrapClient wrap(host, 443, "", onWrapMsg);

    for (size_t i = 0; i < kWrapSubscriptionCount; i++)
        wrap.subscribe(kWrapSubscriptions[i]);

    logPrintf("WRAP", "task started, %u subscription(s)", (unsigned)kWrapSubscriptionCount);

    for (;;)
    {
        wrap.loop();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
