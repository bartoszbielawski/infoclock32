// Host-side check of certs/x509_crt_bundle.bin: it must parse exactly the way
// the Arduino-ESP32 2.0.x core parses it (esp_crt_bundle.c), and be sorted by
// subject because the device binary-searches it with memcmp.
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
#include <string>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

int main()
{
    FILE* f = fopen("../../certs/x509_crt_bundle.bin", "rb");
    CHECK(f != nullptr, "bundle file missing");
    if (!f) return 1;
    std::vector<uint8_t> b;
    int ch;
    while ((ch = fgetc(f)) != EOF) b.push_back((uint8_t)ch);
    fclose(f);

    CHECK(b.size() > 2, "bundle too small (%zu bytes)", b.size());
    const size_t n = (size_t)(b[0] << 8 | b[1]);
    CHECK(n >= 50 && n <= 200, "certificate count %zu out of range", n);

    size_t pos = 2;
    std::vector<std::string> names;
    for (size_t i = 0; i < n && pos + 4 <= b.size(); i++)
    {
        size_t nameLen = (size_t)(b[pos] << 8 | b[pos + 1]);
        size_t keyLen  = (size_t)(b[pos + 2] << 8 | b[pos + 3]);
        CHECK(nameLen > 0 && keyLen > 0, "entry %zu has an empty field", i);
        CHECK(b[pos + 4] == 0x30, "entry %zu subject is not a DER SEQUENCE", i);
        CHECK(b[pos + 4 + nameLen] == 0x30, "entry %zu key is not a DER SEQUENCE", i);
        names.emplace_back((const char*)&b[pos + 4], nameLen);
        pos += 4 + nameLen + keyLen;
    }
    CHECK(names.size() == n, "only %zu of %zu entries parsed", names.size(), n);
    CHECK(pos == b.size(), "parse ended at %zu, file is %zu bytes", pos, b.size());

    // memcmp order over the stored subject bytes, as the device searches it
    for (size_t i = 1; i < names.size(); i++)
    {
        size_t m = names[i - 1].size() < names[i].size() ? names[i - 1].size() : names[i].size();
        int c = memcmp(names[i - 1].data(), names[i].data(), m);
        CHECK(c < 0 || (c == 0 && names[i - 1].size() < names[i].size()),
              "entries %zu/%zu not sorted", i - 1, i);
    }

    if (failures) { printf("%d CA bundle check(s) FAILED\n", failures); return 1; }
    printf("all CA bundle checks passed (%zu certificates, %zu bytes)\n", n, b.size());
    return 0;
}
