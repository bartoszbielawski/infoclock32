#pragma once
#include <map>
#include <string>
// Host stub: a plain map. Empty unless a test sets values, so get_value()
// returns the default as before.
class DataStore {
public:
    static DataStore& getInstance() { static DataStore i; return i; }
    std::string get_value(const std::string& k, const std::string& d = "") {
        auto it = data_.find(k);
        return it == data_.end() ? d : it->second;
    }
    void set_value(const std::string& k, const std::string& v) { data_[k] = v; }
    void clear() { data_.clear(); }
private:
    std::map<std::string, std::string> data_;
};
