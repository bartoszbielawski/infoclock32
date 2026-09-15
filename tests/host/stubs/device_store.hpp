#pragma once
#include <string>
class DeviceStore {
public:
    static DeviceStore& getInstance() { static DeviceStore i; return i; }
    std::string get(const std::string&) { return ""; }
};
