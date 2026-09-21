#pragma once
#include <string>
class DataStore {
public:
    static DataStore& getInstance() { static DataStore i; return i; }
    std::string get_value(const std::string&, const std::string& d = "") { return d; }
};
