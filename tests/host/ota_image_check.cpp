// Host-side tests for include/ota_image.hpp.
#include <ota_image.hpp>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("  [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static std::vector<uint8_t> appImage(uint16_t chip)
{
    std::vector<uint8_t> b(64, 0);
    b[0] = 0xE9;
    b[12] = (uint8_t)(chip & 0xFF);
    b[13] = (uint8_t)(chip >> 8);
    const uint8_t magic[4] = {0x32, 0x54, 0xCD, 0xAB};
    memcpy(&b[32], magic, 4);
    return b;
}

int main()
{
    uint16_t found = 0xFFFF;
    auto c3 = appImage(5);
    CHECK(checkAppImageHeader(c3.data(), c3.size(), 5, &found) == OtaImageCheck::Ok && found == 5, "C3 image on C3");

    auto esp32 = appImage(0);
    CHECK(checkAppImageHeader(esp32.data(), esp32.size(), 5, &found) == OtaImageCheck::WrongChip && found == 0,
          "ESP32 image on C3 refused");
    auto s3 = appImage(9);
    CHECK(checkAppImageHeader(s3.data(), s3.size(), 2) == OtaImageCheck::WrongChip, "S3 image on S2 refused");

    auto merged = appImage(5);
    merged[32] = 0;                                     // bootloader / merged: no app desc
    CHECK(checkAppImageHeader(merged.data(), merged.size(), 5) == OtaImageCheck::NotAppImage, "merged image");
    auto noMagic = appImage(5);
    noMagic[0] = 0x00;
    CHECK(checkAppImageHeader(noMagic.data(), noMagic.size(), 5) == OtaImageCheck::NotAppImage, "no 0xE9");
    CHECK(checkAppImageHeader(c3.data(), 35, 5) == OtaImageCheck::NotAppImage, "too short");
    CHECK(checkAppImageHeader(c3.data(), 0, 5) == OtaImageCheck::NotAppImage, "empty");

    CHECK(!strcmp(espChipName(5), "ESP32-C3") && !strcmp(espChipName(0), "ESP32") &&
          !strcmp(espChipName(2), "ESP32-S2") && !strcmp(espChipName(9), "ESP32-S3"), "chip names");
    CHECK(!strcmp(espChipName(77), "unknown chip"), "unknown chip name");

    if (failures) { printf("%d OTA image check(s) FAILED\n", failures); return 1; }
    printf("all OTA image checks passed\n");
    return 0;
}
