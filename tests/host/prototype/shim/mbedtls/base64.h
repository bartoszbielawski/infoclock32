#pragma once
// Host shim: mbedtls base64 (only the decoder web_ui.cpp uses).
#include <stddef.h>

#define MBEDTLS_ERR_BASE64_INVALID_CHARACTER -0x002C

int mbedtls_base64_decode(unsigned char* dst, size_t dlen, size_t* olen,
                          const unsigned char* src, size_t slen);
