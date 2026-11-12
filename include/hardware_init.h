#ifndef HARDWARE_INIT_H
#define HARDWARE_INIT_H

void hardware_init();

// Wi-Fi reconnect / fallback-AP watchdog; called every 5 s by the
// housekeeping task (no-op until hardware_init() has finished).
void wifi_monitor_tick();
bool wifi_is_ap_mode();

#endif // HARDWARE_INIT_H