/*
 * AJSP.cpp
 *
 *  Created on: Jan 2, 2017
 *      Author: Bartosz Bielawski
 */

#include "AJSP.hpp"
#include <ctype.h>
#include <Arduino.h>

using namespace AJSP;
using namespace std;

#ifdef USE_PC
const map<AJSP::Parser::Entity, std::string> AJSP::Parser::entityNames =
{
		{Parser::Entity::OBJECT, "Object"},
		{Parser::Entity::ARRAY,  "Array"},
		{Parser::Entity::VALUE,  "Value"},
		{Parser::Entity::KEY,	 "Key"},
		{Parser::Entity::STRING, "String"},
		{Parser::Entity::BOOLEAN,	 "Boolean"},
		{Parser::Entity::NULL_VALUE, "Null"},
		{Parser::Entity::NUMBER, "Number"}
};
#endif


static std::string localToString(uint32_t v)
{
	char buffer[12];
	snprintf(buffer, 12,  "%d", v);
	return std::string(buffer);
}

static int hexValue(char c)
{
	if ((c >= '0') and (c <= '9'))
		return c - '0';
	if ((c >= 'a') and (c <= 'f'))
		return c - 'a' + 10;
	if ((c >= 'A') and (c <= 'F'))
		return c - 'A' + 10;
	return -1;
}

static void appendUtf8(std::string& out, uint32_t cp)
{
	if (cp < 0x80)
	{
		out += char(cp);
	}
	else if (cp < 0x800)
	{
		out += char(0xC0 | (cp >> 6));
		out += char(0x80 | (cp & 0x3F));
	}
	else if (cp < 0x10000)
	{
		out += char(0xE0 | (cp >> 12));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
	else
	{
		out += char(0xF0 | (cp >> 18));
		out += char(0x80 | ((cp >> 12) & 0x3F));
		out += char(0x80 | ((cp >> 6) & 0x3F));
		out += char(0x80 | (cp & 0x3F));
	}
}

AJSP::Parser::Parser()
{
	stack.emplace_back(Entity::VALUE, State::NONE);
	stack.reserve(6);
	localBuffer.reserve(32);
}

void AJSP::Parser::setListener(Listener* l)
{
	listener = l;
}

void AJSP::Parser::reset()
{
	localBuffer.clear();
	pathConstructor.clear();

	lastKey = rootElementName;
	offset = 0;

	stack.clear();
	stack.emplace_back(Entity::VALUE, State::NONE);
}

bool AJSP::Parser::skipWhitespace(char c) const
{
	if (not isspace(c))
		return false;

	auto& currentElement = stack.back();

	if (currentElement.entity != Entity::STRING)
		return true;

	//inside a string body and in escape sequences whitespace is significant
	switch (currentElement.state)
	{
		case State::STRING_START:	//quote expected
			return true;
			default:;
	}

	return false;
}


AJSP::Parser::Result AJSP::Parser::parse(char c)
{
	if (!c)
	{
		//end of the stream - a pending number would never be terminated otherwise
		//(strings and keywords complete on their own)
		if ((not stack.empty()) and (result == Result::OK) and (stack.back().entity == Entity::NUMBER))
			finishNumber();

		if (stack.empty() and result == Result::OK)
		{
			if (listener) listener->done();
			reset();
			return Result::DONE;
		}

		return result;
	}

	if (skipWhitespace(c))
	{
		offset++;
		return Result::OK;
	}

	bool consumed = false;

	while ((not consumed) and (result == Result::OK) and (not stack.empty()))
	{
		switch (stack.back().entity)
		{
			case Entity::OBJECT:
				consumed = parseObject(c);
				break;

			case Entity::ARRAY:
				consumed = parseArray(c);
				break;

			case Entity::VALUE:
				consumed = parseValue(c);
				break;

			case Entity::STRING:
			case Entity::KEY:
				consumed = parseString(c);
				break;

			case Entity::BOOLEAN:
			case Entity::NULL_VALUE:
				consumed = parseKeyword(c);
				break;

			case Entity::NUMBER:
				consumed = parseNumber(c);
				break;
		}
	};

	if (consumed)
		offset++;

	if (stack.empty() && result == Result::OK)
	{
		if (listener) listener->done();
		reset();
		return Result::DONE;
	}

	return result;
}


//changes the VALUE entity from the top of the stack to the proper entity
bool AJSP::Parser::parseValue(char c)
{
	//here we don't push anything on the stack to it's safe to take the reference here
	auto& currentElement = stack.back();

	if (currentElement.entity != Entity::VALUE)
	{
		result = Result::INVALID_INTERNAL_STATE;
		return false;
	}

	if (c == '{')	//object - consumes the element
	{
		//NOTE: exit point
		pathConstructor.push(lastKey);
		if (listener) listener->objectStart();
		
		currentElement = StackElement(Entity::OBJECT, State::OBJECT_KEY_OR_END);
		return true;
	}

	if (c == '[')	//array - consumes the char
	{
		//NOTE: exit point
		pathConstructor.push(lastKey);
		if (listener) listener->arrayStart();

		currentElement = StackElement(Entity::ARRAY, State::ARRAY_VALUE_OR_END);
		return true;
	}

	if ((c == 'u') or (c == '\"') or (c == '\''))	//string
	{
		currentElement = StackElement(Entity::STRING, State::STRING_START);
		return parseString(c);
	}

	static const struct Keyword
	{
		char 		first;
		const char* text;
		Entity 		entity;
	} keywords[] =
	{
		{'t', "true", 	Entity::BOOLEAN},
		{'f', "false", 	Entity::BOOLEAN},
		{'n', "null", 	Entity::NULL_VALUE},
	};

	for (const auto& k: keywords)
	{
		if (c != k.first)
			continue;

		currentElement = StackElement(k.entity, State::KEYWORD_START);
		currentElement.keyword = k.text;
		currentElement.counter = 1;		//first character already matched
		return true;
	}

	if (isdigit(c) or c == '-' or c == '+')
	{
		//number - validated character by character, see parseNumber
		currentElement = StackElement(Entity::NUMBER, State::NUMBER_START);
		localBuffer.clear();
		return parseNumber(c);
	}

	//failed to recognize character
	stack.pop_back();

	//if that was the root value the document is invalid
	if (stack.empty())
		result = Result::INVALID_CHARACTER;

	return false;
}

bool AJSP::Parser::parseString(char c)
{
	auto& currentElement = stack.back();	//no stack allocation

	switch (currentElement.state)
	{
		case State::STRING_START:
			//we should skip 'u' that is at the beginning - u for unicode
			if (c == 'u')
				return true;
			if ((c == '\"') or (c == '\''))
			{
				currentElement.state = State::STRING_BODY;	//we're in the string
				localBuffer.clear();
				return true;
			}

			result = Result::IC_STRING_START_EXPECTED;
			return false;

		case State::STRING_BODY:
			if ((c == '\"') or (c == '\''))		//end of string
			{
				//NOTE: exit point
				bool isKey = currentElement.entity == Entity::KEY;

				if (isKey)
				{
					lastKey = localBuffer;
					if (listener) listener->key(localBuffer);
				}
				else
				{	
					if (listener) 
					{
						pathConstructor.push(lastKey);
						listener->value(localBuffer, Entity::STRING);
						pathConstructor.pop();
					}
				}

				stack.pop_back();
				return true;
			}

			if (c == '\\')
			{
				currentElement.state = State::STRING_ESCAPE;
				return true;
			}

			localBuffer += c;
			return true;

		case State::STRING_ESCAPE:
			currentElement.state = State::STRING_BODY;

			switch (c)
			{
				case 'n': localBuffer += '\n'; break;
				case 'r': localBuffer += '\r'; break;
				case 't': localBuffer += '\t'; break;
				case 'b': localBuffer += '\b'; break;
				case 'f': localBuffer += '\f'; break;
				case '\\': localBuffer += '\\'; break;
				case '/': localBuffer += '/'; break;

				case 'u':
					currentElement.state = State::STRING_UNICODE;
					currentElement.counter = 0;
					currentElement.unicode = 0;
					break;

				default:
					localBuffer += c;		//just put the raw value
			}

			return true;

		case State::STRING_UNICODE:
		case State::STRING_SURROGATE_DIGITS:
		{
			int digit = hexValue(c);
			if (digit < 0)
			{
				result = Result::INVALID_UNICODE;
				return false;
			}

			currentElement.unicode = (currentElement.unicode << 4) | digit;
			currentElement.counter++;

			if (currentElement.counter != 4)
				return true;

			uint16_t value = currentElement.unicode;

			if (currentElement.state == State::STRING_UNICODE)
			{
				if ((value >= 0xD800) and (value <= 0xDBFF))	//high surrogate - expect the matching one
				{
					currentElement.surrogate = value;
					currentElement.counter = 0;
					currentElement.unicode = 0;
					currentElement.state = State::STRING_SURROGATE_BACKSLASH;
					return true;
				}

				if ((value >= 0xDC00) and (value <= 0xDFFF))	//lone low surrogate
				{
					result = Result::INVALID_UNICODE;
					return false;
				}

				appendUtf8(localBuffer, value);
				currentElement.state = State::STRING_BODY;
				return true;
			}

			//low half of a surrogate pair expected
			if ((value < 0xDC00) or (value > 0xDFFF))
			{
				result = Result::INVALID_UNICODE;
				return false;
			}

			uint32_t cp = 0x10000 + ((uint32_t(currentElement.surrogate) - 0xD800) << 10) + (value - 0xDC00);
			appendUtf8(localBuffer, cp);
			currentElement.state = State::STRING_BODY;
			return true;
		}

		case State::STRING_SURROGATE_BACKSLASH:
			if (c != '\\')
			{
				result = Result::INVALID_UNICODE;
				return false;
			}

			currentElement.state = State::STRING_SURROGATE_U;
			return true;

		case State::STRING_SURROGATE_U:
			if (c != 'u')
			{
				result = Result::INVALID_UNICODE;
				return false;
			}

			currentElement.state = State::STRING_SURROGATE_DIGITS;
			currentElement.counter = 0;
			currentElement.unicode = 0;
			return true;

				default:;
	}

	result = Result::INVALID_INTERNAL_STATE;
	return false;
}

bool AJSP::Parser::parseArray(char c)
{
	switch(stack.back().state)
	{
		case State::ARRAY_VALUE_OR_END:
			lastKey = "0";
			if (c == ']')
			{
				//NOTE: exit point
				if (listener)
					listener->arrayEnd();
				
				pathConstructor.pop();
				stack.pop_back();
				return true;
			}

			stack.back().state = State::ARRAY_SEPARATOR_OR_END;
			stack.emplace_back(Entity::VALUE, State::NONE);
			if (parseValue(c))
				return true;

			result = Result::IC_ARRAY_VALUE_OR_END_EXPECTED;
			return false;

		case State::ARRAY_VALUE:
			stack.back().state = State::ARRAY_SEPARATOR_OR_END;
			stack.emplace_back(Entity::VALUE, State::NONE);
			if (parseValue(c))
				return true;

			result = Result::IC_ARRAY_VALUE_EXPECTED;
			return false;

		case State::ARRAY_SEPARATOR_OR_END:
			if (c == ']')
			{
				//NOTE: exit point
				if (listener)
					listener->arrayEnd();

				pathConstructor.pop();
				stack.pop_back();
				return true;
			}

			if (c == ',')
			{
				stack.back().state = State::ARRAY_VALUE;
				lastKey = localToString(++stack.back().counter);
				return true;
			}

			result = Result::IC_ARRAY_COMMA_OR_END_EXPECTED;
			return false;
		default:;
	}

	result = Result::INVALID_INTERNAL_STATE;
	return false;
}


bool		AJSP::Parser::parseObject(char c)
{
	switch (stack.back().state)
	{
		case State::OBJECT_KEY_OR_END:
			if (c == '}')
			{
				//NOTE: exit point
				if (listener)
					listener->objectEnd();

				pathConstructor.pop();
				stack.pop_back();
				return true;
			}

			//the next thing we're expecting on this stack level
			//is a colon (after the string is done)
			stack.back().state = State::OBJECT_COLON;

			//try parsing it as a string
			{
				stack.emplace_back(Entity::KEY, State::STRING_START);
				bool consumed = parseString(c);

				if (!consumed and result == Result::IC_STRING_START_EXPECTED)
				{
					result = Result::IC_OBJECT_KEY_OR_END_EXPECTED;
				}
				return consumed;
			}

		case State::OBJECT_COLON:
			//here we only expect K and V separator
			if (c == ':')
			{
				stack.back().state = State::OBJECT_VALUE;
				return true;
			}

			result = Result::IC_OBJECT_COLON_EXPECTED;
			return false;

		case State::OBJECT_VALUE:
			stack.back().state = State::OBJECT_SEPARATOR_OR_END;
			stack.emplace_back(Entity::VALUE, State::NONE);
			if (parseValue(c))
			{
				return true;
			}

			result = Result::IC_OBJECT_VALUE_EXPECTED;
			return false;


		case State::OBJECT_SEPARATOR_OR_END:
			if (c == '}')
			{
				//NOTE: exit point
				if (listener)
					listener->objectEnd();

				pathConstructor.pop();
				stack.pop_back();
				return true;
			}

			if (c == ',')
			{
				stack.back().state = State::OBJECT_COLON;
				stack.emplace_back(Entity::KEY, State::STRING_START);
				return true;
			}

			result = Result::IC_OBJECT_SEPARATOR_OR_END_EXPECTED;
			return false;

		default:;
	}

	result = Result::INVALID_CHARACTER;
	return false;
}

bool		AJSP::Parser::parseKeyword(char c)
{
	auto& currentElement = stack.back();

	if (c != currentElement.keyword[currentElement.counter])
	{
		result = Result::INVALID_KEYWORD;
		return false;
	}

	currentElement.counter++;

	//last character of the keyword - it's done
	if (currentElement.keyword[currentElement.counter] == '\0')
	{
		//NOTE: exit point
		if (listener)
		{
			pathConstructor.push(lastKey);
			listener->value(currentElement.keyword, currentElement.entity);
			pathConstructor.pop();
		}

		stack.pop_back();
	}

	return true;
}

static bool isNumberChar(char c)
{
	return isdigit(c) or c == '.' or c == 'e' or c == 'E' or c == '+' or c == '-';
}

bool		AJSP::Parser::parseNumber(char c)
{
	auto& currentElement = stack.back();

	//anything else terminates the number (',' '}' ']' whitespace...)
	if (not isNumberChar(c))
		return finishNumber();

	switch (currentElement.state)
	{
		//integer part, or a sign
		case State::NUMBER_START:
			if (c == '-' or c == '+')
			{
				currentElement.state = State::NUMBER_INT_FIRST;
				break;
			}

			if (c == '0')
			{
				currentElement.state = State::NUMBER_LEAD_ZERO;
				break;
			}

			currentElement.state = State::NUMBER_INT;
			break;

		//a digit is required after the sign
		case State::NUMBER_INT_FIRST:
			if (not isdigit(c))
			{
				result = Result::INVALID_NUMBER;
				return false;
			}

			currentElement.state = (c == '0') ? State::NUMBER_LEAD_ZERO : State::NUMBER_INT;
			break;

		//'0' cannot be followed by another digit
		case State::NUMBER_LEAD_ZERO:
			if (isdigit(c))
			{
				result = Result::INVALID_NUMBER;
				return false;
			}

			if (c == '.')
				currentElement.state = State::NUMBER_FRAC_FIRST;
			else if (c == 'e' or c == 'E')
				currentElement.state = State::NUMBER_EXP_SIGN;
			else
			{
				result = Result::INVALID_NUMBER;
				return false;
			}
			break;

		case State::NUMBER_INT:
			if (isdigit(c))
				break;

			if (c == '.')
			{
				currentElement.state = State::NUMBER_FRAC_FIRST;
				break;
			}

			if (c == 'e' or c == 'E')
			{
				currentElement.state = State::NUMBER_EXP_SIGN;
				break;
			}

			result = Result::INVALID_NUMBER;		//stray sign in the middle
			return false;

		//a digit is required right after '.'
		case State::NUMBER_FRAC_FIRST:
			if (not isdigit(c))
			{
				result = Result::INVALID_NUMBER;
				return false;
			}

			currentElement.state = State::NUMBER_FRAC;
			break;

		case State::NUMBER_FRAC:
			if (isdigit(c))
				break;

			if (c == 'e' or c == 'E')
			{
				currentElement.state = State::NUMBER_EXP_SIGN;
				break;
			}

			result = Result::INVALID_NUMBER;		//second dot or stray sign
			return false;

		//optional sign after e/E
		case State::NUMBER_EXP_SIGN:
			if (c == '+' or c == '-')
			{
				currentElement.state = State::NUMBER_EXP_FIRST;
				break;
			}

			if (not isdigit(c))
			{
				result = Result::INVALID_NUMBER;	//second exponent or a dot
				return false;
			}

			currentElement.state = State::NUMBER_EXP;
			break;

		//a digit is required after the (optional) exponent sign
		case State::NUMBER_EXP_FIRST:
			if (not isdigit(c))
			{
				result = Result::INVALID_NUMBER;
				return false;
			}

			currentElement.state = State::NUMBER_EXP;
			break;

		case State::NUMBER_EXP:
			if (not isdigit(c))
			{
				result = Result::INVALID_NUMBER;	//second exponent, dot or sign
				return false;
			}
			break;

				default:;
	}

	localBuffer += c;
	return true;
}

bool		AJSP::Parser::finishNumber()
{
	switch (stack.back().state)
	{
		//valid terminations
		case State::NUMBER_START:
		case State::NUMBER_LEAD_ZERO:
		case State::NUMBER_INT:
		case State::NUMBER_FRAC:
		case State::NUMBER_EXP:
			//NOTE: exit point
			if (listener)
			{
				pathConstructor.push(lastKey);
				listener->value(localBuffer, Entity::NUMBER);
				pathConstructor.pop();
			}

			localBuffer.clear();
			stack.pop_back();
			return false;

				default:;
	}

	//terminated in a state that requires more input ("1.", "1e", "1e+", "-", "+2.")
	result = Result::INVALID_NUMBER;
	return false;
}


#ifdef USE_PC
void 	  AJSP::Parser::printState(const std::string& msg) const
{
	cout << "=================  " << msg << "  ==============" << endl;
	cout << "StackSize:   " << stack.size() << endl;
	cout << "Top element: " << entityNames.at(stack.back().entity) << endl;
	cout << "Offset:      " << offset << endl;
	cout << "Result:      " << getResultDescription(result) << endl;
	cout << "State:       " << int(result) << endl;
	printStack();
}

void AJSP::Parser::printStack() const
{
	for (const auto& se: stack)
	{
		cout << entityNames.at(se.entity) << "\t\t, " << getStateDescription(se.state) << endl;
	}
}

const char* AJSP::Parser::getStateDescription(State s)
{
	switch(s)
	{
		case State::NONE: return "NONE";
		case State::OBJECT_KEY_OR_END: return "OBJECT_KEY_OR_END";
		case State::OBJECT_COLON: return "OBJECT_COLON";
		case State::OBJECT_VALUE: return "OBJECT_VALUE";
		case State::OBJECT_SEPARATOR_OR_END: return "OBJECT_SEPARATOR_OR_END";
		case State::ARRAY_VALUE_OR_END: return "ARRAY_VALUE_OR_END";
		case State::ARRAY_SEPARATOR_OR_END: return "ARRAY_SEPARATOR_OR_END";
		case State::ARRAY_VALUE: return "ARRAY_VALUE";
		case State::STRING_START: return "STRING_START";
		case State::STRING_BODY: return "STRING_BODY";
		case State::STRING_ESCAPE: return "STRING_ESCAPE";
		case State::STRING_UNICODE: return "STRING_UNICODE";
		case State::STRING_SURROGATE_BACKSLASH: return "STRING_SURROGATE_BACKSLASH";
		case State::STRING_SURROGATE_U: return "STRING_SURROGATE_U";
		case State::STRING_SURROGATE_DIGITS: return "STRING_SURROGATE_DIGITS";
		case State::KEYWORD_START: return "KEYWORD_START";
		case State::NUMBER_START: return "NUMBER_START";
		case State::NUMBER_INT_FIRST: return "NUMBER_INT_FIRST";
		case State::NUMBER_LEAD_ZERO: return "NUMBER_LEAD_ZERO";
		case State::NUMBER_INT: return "NUMBER_INT";
		case State::NUMBER_FRAC_FIRST: return "NUMBER_FRAC_FIRST";
		case State::NUMBER_FRAC: return "NUMBER_FRAC";
		case State::NUMBER_EXP_SIGN: return "NUMBER_EXP_SIGN";
		case State::NUMBER_EXP_FIRST: return "NUMBER_EXP_FIRST";
		case State::NUMBER_EXP: return "NUMBER_EXP";
		case State::INVALID: return "INVALID";
		default:;
	}
	return "Unknown";
}

const char* AJSP::Parser::getResultDescription(Result r)
{
	switch (r)
	{
		case Result::OK: 	return "OK";
		case Result::DONE: 	return "Done";
		case Result::INVALID_CHARACTER: 			return "Invalid character";
		case Result::IC_STRING_START_EXPECTED:		return "String start expected";
		case Result::IC_ARRAY_COMMA_OR_END_EXPECTED:return "Array separator or end brace expected";
		case Result::IC_ARRAY_VALUE_OR_END_EXPECTED:return "Value or end brace expected";
		case Result::IC_ARRAY_VALUE_EXPECTED:		return "Value expected";
		case Result::IC_OBJECT_COLON_EXPECTED:		return "Colon expected";
		case Result::IC_OBJECT_VALUE_EXPECTED:		return "Value expected";
		case Result::IC_OBJECT_KEY_OR_END_EXPECTED:	return "Key or end brace expected";
		case Result::IC_OBJECT_SEPARATOR_OR_END_EXPECTED: 			return "Comma or end brace expected";
		case Result::INVALID_KEYWORD:								return "Invalid keyword";
		case Result::INVALID_NUMBER:								return "Invalid number";
		case Result::INVALID_UNICODE:								return "Invalid unicode escape";
		case Result::INVALID_INTERNAL_STATE:		return "Invalid internal state";
	}

	return "Unknown";
}
#endif		//USE_PC
