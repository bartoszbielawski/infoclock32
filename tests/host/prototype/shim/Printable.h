#pragma once
// Host shim: Arduino Printable interface.
#include <stddef.h>
#include <stdint.h>

class Print;

class Printable
{
public:
    virtual ~Printable() {}
    virtual size_t printTo(Print& p) const = 0;
};
