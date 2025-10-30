#include "wifi_mananger.h"

WiFiManager& getWiFiManagerInstance() {
    static WiFiManager wifiManagerInstance;
    return wifiManagerInstance;
}




