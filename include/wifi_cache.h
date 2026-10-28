#pragma once

// Erase the station credentials the Wi-Fi driver keeps in flash (NVS), so a stale
// pair cannot outvote config.txt. WiFi.disconnect(false, true) is not enough:
// after WiFi.persistent(false) it only clears the RAM copy.
void wifi_erase_cache();
