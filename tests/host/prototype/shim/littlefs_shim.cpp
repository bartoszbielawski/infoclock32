// Host shim implementation: directory-backed filesystem.
#include "LittleFS.h"

#include <sys/stat.h>
#include <sys/types.h>

#include <cstring>

FS LittleFS;

static std::string resolvePath(const char* path)
{
    std::string p = LittleFS.root();
    if (!path) return p;
    if (path[0] == '/') path++;   // firmware paths are absolute; map under the root dir
    if (!p.empty() && p.back() != '/') p += '/';
    return p + path;
}

bool FS::begin(bool)
{
    // create root + one nesting level, ignore "exists" errors
    mkdir(root_.c_str(), 0755);
    return true;
}

bool FS::exists(const char* path) const
{
    struct stat st;
    return stat(resolvePath(path).c_str(), &st) == 0;
}

File FS::open(const char* path, const char* mode)
{
    return File(resolvePath(path), mode);
}

File::File(const std::string& fullPath, const char* mode)
    : path_(fullPath)
{
    if (mode && mode[0] == 'w')
        f_ = fopen(fullPath.c_str(), "wb");
    else
        f_ = fopen(fullPath.c_str(), "rb");
}

size_t File::size() const
{
    if (!f_) return 0;
    long cur = ftell(f_);
    fseek(f_, 0, SEEK_END);
    long end = ftell(f_);
    fseek(f_, cur, SEEK_SET);
    return (size_t)end;
}

size_t File::position() const { return f_ ? (size_t)ftell(f_) : 0; }

size_t File::seek(size_t pos) { return f_ && fseek(f_, (long)pos, SEEK_SET) == 0 ? pos : 0; }

int File::available()
{
    return f_ ? (position() < size() ? 1 : 0) : 0;
}

int File::read()
{
    if (!f_) return -1;
    int c = fgetc(f_);
    return c == EOF ? -1 : c;
}

int File::peek()
{
    if (!f_) return -1;
    int c = fgetc(f_);
    if (c == EOF) return -1;
    ungetc(c, f_);
    return c;
}

size_t File::readBytesUntil(char terminator, char* buffer, size_t length)
{
    size_t n = 0;
    while (n < length)
    {
        int c = read();
        if (c < 0 || c == terminator) break;
        buffer[n++] = (char)c;
    }
    return n;
}

void File::close()
{
    if (f_)
    {
        fclose(f_);
        f_ = nullptr;
    }
}
