#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <Arduino.h>
#include <string>

#include <display_strings.hpp>

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

// Transliterate UTF-8 Latin text (French, German, Polish, ...) to plain ASCII.
inline std::string normalizeFrench(const char* in) {
    return ascii_fold(in);
}

#endif // STRING_UTILS_H