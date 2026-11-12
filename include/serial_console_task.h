#pragma once

// Line-based command console on the USB serial port (physical access only).
// Commands: set key=value | get key | wifi_forget | reboot | help
// Handle pending serial input (non-blocking); called by the housekeeping task.
void serial_console_poll();
