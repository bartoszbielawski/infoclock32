/*
 * AJSP.hpp
 *
 *  Created on: Jan 2, 2017
 *      Author: Bartosz Bielawski
 *
 */

#ifndef AJSP_HPP_
#define AJSP_HPP_

#include <string>
#include <stack>
#include <utility>
#include <vector>

#include "PathConstructor.hpp"

//this define will make the library use more memory and use some standard
//features available on PCs (like stdio...)
//#define USE_PC

#ifdef USE_PC
#include <map>
#endif

namespace AJSP
{
	class Listener;

	class Parser
	{
		public:
			Parser();
			~Parser() {}

			void reset();

			enum class Result: uint8_t;

			Result parse(char c);		//returns true when it's done
			void setListener(Listener* l);
			uint32_t getCurrentOffset() const {return offset;}
			const std::string& getLastKey() const {return lastKey;}
			const std::string& getCurrentPath() const {return pathConstructor.getPath();}

			bool done() const {return stack.empty();}

			enum class Result: uint8_t
			{
				OK,
				DONE,
				INVALID_CHARACTER = 0x10,		//generic
				IC_STRING_START_EXPECTED,
				IC_ARRAY_COMMA_OR_END_EXPECTED,
				IC_ARRAY_VALUE_OR_END_EXPECTED,
				IC_ARRAY_VALUE_EXPECTED,
				IC_OBJECT_COLON_EXPECTED,
				IC_OBJECT_VALUE_EXPECTED,
				IC_OBJECT_KEY_OR_END_EXPECTED,
				IC_OBJECT_SEPARATOR_OR_END_EXPECTED,
				INVALID_KEYWORD,
				INVALID_NUMBER,
				INVALID_UNICODE,
				INVALID_INTERNAL_STATE = 0x80
			};
#ifdef USE_PC
			static const char* getResultDescription(Result r);
#endif
			enum class Entity: uint8_t
			{
				VALUE,
				OBJECT,
				ARRAY,
				STRING,
				KEY,
				BOOLEAN,
				NULL_VALUE,
				NUMBER,
			};
			Result getLastResult() const {return result;}

		private:
			enum class State: uint8_t
			{
				NONE = 0,		//for anything that doesn't need state

				OBJECT_KEY_OR_END = 0x10,
				OBJECT_COLON,
				OBJECT_VALUE,
				OBJECT_SEPARATOR_OR_END,

				ARRAY_VALUE_OR_END = 0x20,
				ARRAY_SEPARATOR_OR_END,
				ARRAY_VALUE,

				STRING_START = 0x30,	//for strings and keys
				STRING_BODY,
				STRING_ESCAPE,
				STRING_UNICODE,				//collecting 4 hex digits of \uXXXX
				STRING_SURROGATE_BACKSLASH,	//high surrogate seen, '\' expected
				STRING_SURROGATE_U,			//'u' expected
				STRING_SURROGATE_DIGITS,	//low surrogate \uDC00-\uDFFF expected

				KEYWORD_START = 0x40,	//for bools and nulls, matching char by char

				NUMBER_START = 0x50,	//for numbers
				NUMBER_INT_FIRST,		//digit expected (after a sign)
				NUMBER_LEAD_ZERO,		//integer part is '0' - no more digits allowed
				NUMBER_INT,
				NUMBER_FRAC_FIRST,		//digit expected right after '.'
				NUMBER_FRAC,
				NUMBER_EXP_SIGN,		//optional sign expected after e/E
				NUMBER_EXP_FIRST,		//digit expected after optional exponent sign
				NUMBER_EXP,

				INVALID = 0xFF
			};

			struct StackElement
			{
					StackElement(Entity e, State s): entity(e), state(s) {}
					Entity entity;
					State state;
					uint16_t counter = 0;
					const char* keyword = nullptr;
					uint16_t unicode = 0;	//\uXXXX accumulator
					uint16_t surrogate = 0;	//high half of a surrogate pair
			};

			bool 		skipWhitespace(char c) const;

			bool 		parseValue(char c);
			bool		parseString(char c);
			bool		parseObject(char c);
			bool		parseArray(char c);
			bool		parseKeyword(char c);
			bool		parseNumber(char c);

			bool		finishNumber();

			Listener* 	listener = nullptr;

			std::string localBuffer;

			constexpr static const char* rootElementName = "root";

			std::string lastKey = rootElementName;
			PathConstructor pathConstructor;

			uint32_t	offset = 0;
			Result   	result = Result::OK;

			std::vector<StackElement> stack;

#ifdef USE_PC
			static const char* getStateDescription(State s);

			void 	  printState(const std::string& msg) const;
			void 	  printStack() const;
			static const std::map<Entity, std::string> entityNames;
#endif
	};

	class Listener
	{
		public:
			Listener() {}
			virtual ~Listener() {}

			virtual void arrayStart() {};
			virtual void arrayEnd() {};

			virtual void objectStart() {};
			virtual void objectEnd() {};

			virtual void key(const std::string& key) {};
			virtual void value(const std::string& value, Parser::Entity entity) = 0;

			virtual void done() {};
	};


}


#endif /* AJSP_HPP_ */
