#pragma once
// Host shim: RTC memory attributes are plain host variables (see README
// limitation: they do not survive a host "reboot").
#define RTC_NOINIT_ATTR
#define RTC_DATA_ATTR
