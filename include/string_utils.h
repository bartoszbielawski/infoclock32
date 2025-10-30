#ifndef STRING_UTILS_H
#define STRING_UTILS_H

#include <Arduino.h>

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

#endif // STRING_UTILS_H