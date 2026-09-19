#pragma once
// Host shim: LittleFS mapped onto a plain host directory. The REAL DataStore
// compiles against this unchanged — config editing behaves like on device.
#include "Arduino.h"
#include "Stream.h"

#include <cstdio>
#include <string>

class File : public Stream
{
public:
    File() = default;
    File(const std::string& fullPath, const char* mode);

    operator bool() const { return f_ != nullptr; }
    bool isDirectory() const { return false; }
    const char* name() const { return path_.c_str(); }

    size_t size() const;
    size_t position() const;
    size_t seek(size_t pos);

    int available() override;
    int read() override;
    int peek() override;
    size_t write(uint8_t c) override { return fwrite(&c, 1, 1, f_) == 1 ? 1 : 0; }
    size_t write(const uint8_t* buffer, size_t size) override
    { return f_ ? fwrite(buffer, 1, size, f_) : 0; }

    size_t readBytesUntil(char terminator, char* buffer, size_t length) override;
    void close();
    ~File() override { close(); }

private:
    FILE* f_ = nullptr;
    std::string path_;
};

class FS
{
public:
    bool begin(bool = false);
    File open(const char* path, const char* mode = "r");
    bool exists(const char* path) const;
    bool remove(const char* path);
    bool rename(const char* from, const char* to);
    void setRoot(const std::string& root) { root_ = root; }
    const std::string& root() const { return root_; }

private:
    std::string root_ = "fs";
};

extern FS LittleFS;
