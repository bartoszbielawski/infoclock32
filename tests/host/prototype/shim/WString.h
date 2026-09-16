#pragma once
// Host shim: minimal Arduino String backed by std::string.
// Implements the API surface actually used by the firmware sources.
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

class __FlashStringHelper;

class String
{
public:
    // Arduino contract: concat() returns success as bool.
    // Arduino flash-string fiction (see F() below).
    friend class __FlashStringHelper;

    String() {}
    String(const char* s) : buf_(s ? s : "") {}
    String(const std::string& s) : buf_(s) {}
    String(char c) : buf_(1, c) {}
    String(int v) : buf_(std::to_string(v)) {}
    String(unsigned v) : buf_(std::to_string(v)) {}
    String(long v) : buf_(std::to_string(v)) {}
    String(unsigned long v) : buf_(std::to_string(v)) {}
    String(double v, int decimals = 2)
    {
        char tmp[40];
        snprintf(tmp, sizeof(tmp), "%.*f", decimals, v);
        buf_ = tmp;
    }

    const char* c_str() const { return buf_.c_str(); }
    unsigned length() const { return (unsigned)buf_.size(); }
    bool isEmpty() const { return buf_.empty(); }
    void clear() { buf_.clear(); }
    void reserve(unsigned n) { buf_.reserve(n); }

    char operator[](unsigned i) const { return i < buf_.size() ? buf_[i] : '\0'; }
    char& operator[](unsigned i) { return buf_[i]; }

    const char* begin() const { return buf_.data(); }
    const char* end() const { return buf_.data() + buf_.size(); }

    bool equals(const String& o) const { return buf_ == o.buf_; }
    bool equalsIgnoreCase(const String& o) const;

    bool startsWith(const String& p) const
    { return buf_.size() >= p.buf_.size() && buf_.compare(0, p.buf_.size(), p.buf_) == 0; }
    bool endsWith(const String& p) const
    { return buf_.size() >= p.buf_.size() && buf_.compare(buf_.size() - p.buf_.size(), p.buf_.size(), p.buf_) == 0; }

    int indexOf(const String& p, unsigned from = 0) const
    {
        if (from > buf_.size()) return -1;
        auto pos = buf_.find(p.buf_, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }
    int indexOf(char c, unsigned from = 0) const
    {
        if (from > buf_.size()) return -1;
        auto pos = buf_.find(c, from);
        return pos == std::string::npos ? -1 : (int)pos;
    }

    String substring(unsigned from) const
    { return from >= buf_.size() ? String() : String(buf_.substr(from)); }
    String substring(unsigned from, unsigned to) const
    { return from >= buf_.size() ? String() : String(buf_.substr(from, to - from)); }

    void replace(const String& from, const String& to)
    {
        if (from.buf_.empty()) return;
        for (size_t pos = 0; (pos = buf_.find(from.buf_, pos)) != std::string::npos;)
        {
            buf_.replace(pos, from.buf_.size(), to.buf_);
            pos += to.buf_.size();
        }
    }

    void trim()
    {
        while (!buf_.empty() && isspace((unsigned char)buf_.front())) buf_.erase(buf_.begin());
        while (!buf_.empty() && isspace((unsigned char)buf_.back())) buf_.pop_back();
    }
    void toUpperCase() { for (auto& c : buf_) c = toupper((unsigned char)c); }
    void toLowerCase() { for (auto& c : buf_) c = tolower((unsigned char)c); }

    // Arduino remove(pos) trims to end; remove(pos, count) removes count chars.
    void remove(unsigned pos) { if (pos < buf_.size()) buf_.resize(pos); }
    void remove(unsigned pos, unsigned count)
    { if (pos < buf_.size()) buf_.erase(pos, count); }

    long toInt() const { return atol(buf_.c_str()); }
    float toFloat() const { return (float)atof(buf_.c_str()); }

    String& operator+=(const String& o) { buf_ += o.buf_; return *this; }
    String& operator+=(const char* s) { if (s) buf_ += s; return *this; }
    String& operator+=(char c) { buf_ += c; return *this; }
    String& operator+=(const __FlashStringHelper* s)
    { return *this += reinterpret_cast<const char*>(s); }

    String& concat(const String& o) { return *this += o; }
    bool concat(const char* s) { *this += s; return true; }
    bool concat(const char* s, size_t n) { if (s) buf_.append(s, n); return true; }
    bool concat(char c) { *this += c; return true; }

    friend String operator+(const String& a, const String& b) { return String(a.buf_ + b.buf_); }
    friend String operator+(const char* a, const String& b) { return String(std::string(a) + b.buf_); }

    friend bool operator==(const String& a, const String& b) { return a.buf_ == b.buf_; }
    friend bool operator==(const String& a, const char* b) { return a.buf_ == (b ? b : ""); }
    friend bool operator!=(const String& a, const String& b) { return !(a == b); }
    friend bool operator!=(const String& a, const char* b) { return !(a == b); }

private:
    std::string buf_;
};

// Flash-string fiction: PROGMEM is flat memory on the host, so F() just
// reinterprets the literal (matching the real core's contract).
class __FlashStringHelper;
#define F(string_literal) (reinterpret_cast<const __FlashStringHelper*>(string_literal))