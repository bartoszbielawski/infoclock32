#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <Arduino.h>
#include <string>

class StringViewStream: public Stream
{
	public:
		StringViewStream(const String& s): s(s) {}
		virtual ~StringViewStream() {}

		int available() override {return s.length() - read_pos;}

		int read() override {return s[read_pos++];}
		int peek() override {return s[read_pos];}

		size_t write(uint8_t c) override {return 0;}

		void flush() override {}

		const String& s;
		size_t read_pos = 0;
};

// Transliterate common UTF-8 French/Latin characters to plain ASCII.
// Handles accented vowels, ç/Ç, œ/Œ, and curly apostrophes.
inline std::string normalizeFrench(const char* in) {
    std::string out;
    for (size_t i = 0; in[i]; ) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0xC3) { out += in[i++]; continue; }
        if (c == 0xC3 && in[i+1]) {
            unsigned char d = (unsigned char)in[i+1];
            switch (d) {
                case 0xA0: case 0xA1: case 0xA2: case 0xA3: case 0xA4: case 0xA5: out += 'a'; break;
                case 0xA7: out += 'c'; break;
                case 0xA8: case 0xA9: case 0xAA: case 0xAB: out += 'e'; break;
                case 0xAC: case 0xAD: case 0xAE: case 0xAF: out += 'i'; break;
                case 0xB2: case 0xB3: case 0xB4: case 0xB5: case 0xB6: out += 'o'; break;
                case 0xB9: case 0xBA: case 0xBB: case 0xBC: out += 'u'; break;
                case 0xBF: out += 'y'; break;
                case 0x80: case 0x82: case 0x83: case 0x84: case 0x85: out += 'A'; break;
                case 0x87: out += 'C'; break;
                case 0x88: case 0x89: case 0x8A: case 0x8B: out += 'E'; break;
                case 0x8C: case 0x8D: case 0x8E: case 0x8F: out += 'I'; break;
                case 0x92: case 0x93: case 0x94: case 0x95: case 0x96: out += 'O'; break;
                case 0x99: case 0x9A: case 0x9B: case 0x9C: out += 'U'; break;
                case 0x9F: out += 'Y'; break;
                default: out += in[i]; out += in[i+1]; break;
            }
            i += 2; continue;
        }
        if (c == 0xC5 && in[i+1]) {
            unsigned char d = (unsigned char)in[i+1];
            if (d == 0x92) { out += "OE"; i += 2; continue; }
            if (d == 0x93) { out += "oe"; i += 2; continue; }
        }
        // curly apostrophe U+2019 (E2 80 99)
        if (c == 0xE2 && in[i+1] && in[i+2]) {
            if ((unsigned char)in[i+1] == 0x80 && (unsigned char)in[i+2] == 0x99) {
                out += '\''; i += 3; continue;
            }
        }
        out += in[i++];
    }
    return out;
}

#endif // STRING_UTILS_H