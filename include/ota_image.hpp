#pragma once

// First-chunk check for web OTA uploads (pure, host-tested in
// tests/host/ota_image_check.cpp).
//
// An ESP32 application image starts with esp_image_header_t:
//   offset 0      magic 0xE9
//   offset 12-13  chip_id (little-endian; 0 = ESP32, 2 = S2, 5 = C3, 9 = S3)
// followed at offset 32 by esp_app_desc_t, whose first word is 0xABCD5432.
// A bootloader / merged image has something else at 32, and an image built for
// another board has a different chip_id: flashing it would at best be refused
// by the bootloader after the reboot.

#include <cstddef>
#include <cstdint>

enum class OtaImageCheck { Ok, NotAppImage, WrongChip };

inline const char* espChipName(uint16_t id)
{
    switch (id)
    {
        case 0:  return "ESP32";
        case 2:  return "ESP32-S2";
        case 5:  return "ESP32-C3";
        case 9:  return "ESP32-S3";
        case 12: return "ESP32-C2";
        case 13: return "ESP32-C6";
        case 16: return "ESP32-H2";
        default: return "unknown chip";
    }
}

// `foundChip` (optional) receives the image's chip id when it could be read.
inline OtaImageCheck checkAppImageHeader(const uint8_t* buf, size_t len,
                                         uint16_t expectedChip,
                                         uint16_t* foundChip = nullptr)
{
    static const uint8_t kAppDescMagic[4] = {0x32, 0x54, 0xCD, 0xAB};
    if (len < 36 || buf[0] != 0xE9) return OtaImageCheck::NotAppImage;
    for (int i = 0; i < 4; i++)
        if (buf[32 + i] != kAppDescMagic[i]) return OtaImageCheck::NotAppImage;
    const uint16_t chip = (uint16_t)(buf[12] | (buf[13] << 8));
    if (foundChip) *foundChip = chip;
    return chip == expectedChip ? OtaImageCheck::Ok : OtaImageCheck::WrongChip;
}
