#pragma once
#include <string>
class RuntimeStore {
public:
    static RuntimeStore& getInstance() { static RuntimeStore i; return i; }
    std::string get(const std::string&) { return ""; }
};
