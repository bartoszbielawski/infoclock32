#pragma once
// Host shim: mDNS — the prototype is reachable on localhost only, so the
// hostname action is a no-op that keeps the real handler code compiling.
class MDNSResponder
{
public:
    bool begin(const char*) { return true; }
    void end() {}
    void addService(const char*, const char*, uint16_t) {}
};
extern MDNSResponder MDNS;
