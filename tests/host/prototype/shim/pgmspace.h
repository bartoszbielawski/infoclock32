#pragma once
// Host shim: AVR/ESP program-memory access macros — flash is flat on the host.
#define PROGMEM
#define PSTR(s) (s)
#define snprintf_P snprintf
#define sprintf_P sprintf
#define printf_P printf
#define memcpy_P memcpy
#define strcpy_P strcpy
#define strlen_P strlen
#define strcmp_P strcmp
#define pgm_read_byte(addr) (*(const uint8_t*)(addr))
#define pgm_read_word(addr) (*(const uint16_t*)(addr))
#define pgm_read_dword(addr) (*(const uint32_t*)(addr))
#define pgm_read_byte_near(addr) pgm_read_byte(addr)
#define pgm_read_word_near(addr) pgm_read_word(addr)
