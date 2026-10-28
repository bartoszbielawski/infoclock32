#pragma once

// Line-based command console on the USB serial port (physical access only).
// Commands: set key=value | get key | wifi_forget | reboot | help
void serial_console_task(void* parameter);
