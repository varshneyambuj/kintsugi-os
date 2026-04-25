/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2014 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Rene Gollent <rene@gollent.com>
 *       John Scipione <jscipione@gmail.com>
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


/**
 * @file CLanguageTokenizer.cpp
 * @brief Hand-written lexer for the debugger's C/C++ expression language.
 *
 * Tokenizer steps a cursor through an in-memory expression string and
 * produces a stream of Tokens consumed by CLanguageExpressionEvaluator and
 * the syntax highlighter. It recognises identifiers, decimal/hex/float
 * numeric constants, string literals, every C operator the evaluator
 * supports, and the punctuation needed for casts and member access. On
 * malformed input it throws ParseException with a column position.
 */


#include "CLanguageTokenizer.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>


using CLanguage::ParseException;
using CLanguage::Token;
using CLanguage::Tokenizer;


// #pragma mark - Token


/**
 * @brief Construct an empty token of type @c TOKEN_NONE.
 */
Token::Token()
	:
	string(""),
	type(TOKEN_NONE),
	value(),
	position(0)
{
}


/**
 * @brief Copy-construct a Token from @a other.
 *
 * @param other  Source token whose fields are duplicated.
 */
Token::Token(const Token& other)
	:
	string(other.string),
	type(other.type),
	value(other.value),
	position(other.position)
{
}


/**
 * @brief Construct a Token from raw input.
 *
 * @param string    Pointer to the start of the token's text in the buffer.
 * @param length    Number of characters in the token.
 * @param position  Column position where the token begins.
 * @param type      One of the @c TOKEN_* enumerators.
 */
Token::Token(const char* string, int32 length, int32 position, int32 type)
	:
	string(string, length),
	type(type),
	value(),
	position(position)
{
}


/**
 * @brief Copy-assignment.
 *
 * @param other  Source token whose fields replace this token's state.
 * @return Reference to @c *this.
 */
Token&
Token::operator=(const Token& other)
{
	string = other.string;
	type = other.type;
	value = other.value;
	position = other.position;
	return *this;
}


// #pragma mark - Tokenizer


/**
 * @brief Construct a Tokenizer with no input attached.
 *
 * The tokeniser must be primed via SetTo() before NextToken() is called.
 */
Tokenizer::Tokenizer()
	:
	fString(""),
	fCurrentChar(NULL),
	fCurrentToken(),
	fReuseToken(false)
{
}


/**
 * @brief Bind the tokeniser to a new expression string.
 *
 * Resets the cursor and the cached current token. The supplied string must
 * remain valid for the lifetime of the next sequence of NextToken() calls;
 * an internal BString copy is made.
 *
 * @param string  Null-terminated expression text to tokenise.
 */
void
Tokenizer::SetTo(const char* string)
{
	fString = string;
	fCurrentChar = fString.String();
	fCurrentToken = Token();
	fReuseToken = false;
}


/**
 * @brief Advances the cursor and returns the next token.
 *
 * Skips whitespace, classifies the next character, and dispatches to the
 * appropriate sub-parser (numeric constant / hex constant / identifier /
 * string literal / operator / punctuation). At end of input the cached
 * @c TOKEN_END_OF_LINE is returned repeatedly. RewindToken() makes the
 * subsequent call re-emit the same token.
 *
 * @return Reference to the cached current token.
 * @note Throws @c ParseException on malformed numeric constants or
 *       unexpected characters.
 */
const Token&
Tokenizer::NextToken()
{
	if (fCurrentToken.type == TOKEN_END_OF_LINE)
		return fCurrentToken;

	if (fReuseToken) {
		fReuseToken = false;
		return fCurrentToken;
	}

	while (*fCurrentChar != 0 && isspace(*fCurrentChar))
		fCurrentChar++;

	if (*fCurrentChar == 0) {
		return fCurrentToken = Token("", 0, _CurrentPos(),
			TOKEN_END_OF_LINE);
	}

	bool decimal = *fCurrentChar == '.';

	if (decimal || isdigit(*fCurrentChar)) {
		if (*fCurrentChar == '0' && fCurrentChar[1] == 'x')
			return _ParseHexOperand();

		BString temp;

		const char* begin = fCurrentChar;

		// optional digits before the comma
		while (isdigit(*fCurrentChar)) {
			temp << *fCurrentChar;
			fCurrentChar++;
		}

		// optional post decimal part
		// (required if there are no digits before the decimal)
		if (*fCurrentChar == '.') {
			decimal = true;
			temp << '.';
			fCurrentChar++;

			// optional post decimal digits
			while (isdigit(*fCurrentChar)) {
				temp << *fCurrentChar;
				fCurrentChar++;
			}
		}

		int32 length = fCurrentChar - begin;
		if (length == 1 && decimal) {
			// check for . operator
			fCurrentChar = begin;
			if (!_ParseOperator())
				throw ParseException("unexpected character", _CurrentPos());

			return fCurrentToken;
		}

		BString test = temp;
		test << "&_";
		double value;
		char t[2];
		int32 matches = sscanf(test.String(), "%lf&%s", &value, t);
		if (matches != 2)
			throw ParseException("error in constant", _CurrentPos() - length);

		fCurrentToken = Token(begin, length, _CurrentPos() - length,
			TOKEN_CONSTANT);
		if (decimal)
			fCurrentToken.value.SetTo(value);
		else
			fCurrentToken.value.SetTo((int64)strtoll(temp.String(), NULL, 10));
	} else if (isalpha(*fCurrentChar) || *fCurrentChar == '_') {
		const char* begin = fCurrentChar;
		while (*fCurrentChar != 0 && (isalpha(*fCurrentChar)
			|| isdigit(*fCurrentChar) || *fCurrentChar == '_')) {
			fCurrentChar++;
		}
		int32 length = fCurrentChar - begin;
		fCurrentToken = Token(begin, length, _CurrentPos() - length,
			TOKEN_IDENTIFIER);
	} else if (*fCurrentChar == '"' || *fCurrentChar == '\'') {
		bool terminatorFound = false;
		const char* begin = fCurrentChar++;
		while (*fCurrentChar != 0) {
			if (*fCurrentChar == '\\') {
				if (*(fCurrentChar++) != 0)
					fCurrentChar++;
			} else if (*(fCurrentChar++) == *begin) {
				terminatorFound = true;
				break;
			}
		}
		int32 tokenType = TOKEN_STRING_LITERAL;
		if (!terminatorFound) {
			tokenType = *begin == '"' ? TOKEN_DOUBLE_QUOTE
					: TOKEN_SINGLE_QUOTE;
			fCurrentChar = begin + 1;
		}

		int32 length = fCurrentChar - begin;
		fCurrentToken = Token(begin, length, _CurrentPos() - length,
			tokenType);
	} else {
		if (!_ParseOperator()) {
			int32 type = TOKEN_NONE;
			switch (*fCurrentChar) {
				case '\n':
					type = TOKEN_END_OF_LINE;
					break;

				case '(':
					type = TOKEN_OPENING_PAREN;
					break;
				case ')':
					type = TOKEN_CLOSING_PAREN;
					break;

				case '[':
					type = TOKEN_OPENING_SQUARE_BRACKET;
					break;
				case ']':
					type = TOKEN_CLOSING_SQUARE_BRACKET;
					break;

				case '{':
					type = TOKEN_OPENING_CURLY_BRACE;
					break;
				case '}':
					type = TOKEN_CLOSING_CURLY_BRACE;
					break;

				case '\\':
					type = TOKEN_BACKSLASH;
					break;

				case ':':
					type = TOKEN_COLON;
					break;

				case ';':
					type = TOKEN_SEMICOLON;
					break;

				case ',':
					type = TOKEN_COMMA;
					break;

				case '.':
					type = TOKEN_PERIOD;
					break;

				case '#':
					type = TOKEN_POUND;
					break;

				default:
					throw ParseException("unexpected character",
						_CurrentPos());
			}
			fCurrentToken = Token(fCurrentChar, 1, _CurrentPos(),
				type);
			fCurrentChar++;
		}
	}

	return fCurrentToken;
}


/**
 * @brief Attempts to consume a one- or two-character operator.
 *
 * Recognises arithmetic, bitwise, logical, comparison, member-access, and
 * comment-marker operators (e.g. @c +, @c ->, @c &&, @c <=, @c \/\* and
 * @c \*\/). On a match the current token is updated and the cursor
 * advanced; otherwise the cursor is left untouched.
 *
 * @return @c true when an operator was consumed, @c false otherwise.
 */
bool
Tokenizer::_ParseOperator()
{
	int32 type = TOKEN_NONE;
	int32 length = 0;
	switch (*fCurrentChar) {
		case '+':
			type = TOKEN_PLUS;
			length = 1;
			break;

		case '-':
			 if (_Peek() == '>') {
			 	type = TOKEN_MEMBER_PTR;
			 	length = 2;
			 } else {
				type = TOKEN_MINUS;
				length = 1;
			 }
			break;

		case '*':
			switch (_Peek()) {
				case '/':
					type = TOKEN_END_COMMENT_BLOCK;
					length = 2;
					break;
				default:
					type = TOKEN_STAR;
					length = 1;
					break;
			}
			break;

		case '/':
			switch (_Peek()) {
				case '*':
					type = TOKEN_BEGIN_COMMENT_BLOCK;
					length = 2;
					break;
				case '/':
					type = TOKEN_INLINE_COMMENT;
					length = 2;
					break;
				default:
					type = TOKEN_SLASH;
					length = 1;
					break;
			}
			break;

		case '%':
			type = TOKEN_MODULO;
			length = 1;
			break;

		case '^':
			type = TOKEN_BITWISE_XOR;
			length = 1;
			break;

		case '&':
			if (_Peek() == '&') {
			 	type = TOKEN_LOGICAL_AND;
			 	length = 2;
			} else {
				type = TOKEN_BITWISE_AND;
				length = 1;
			}
			break;

		case '|':
			if (_Peek() == '|') {
				type = TOKEN_LOGICAL_OR;
				length = 2;
			} else {
				type = TOKEN_BITWISE_OR;
				length = 1;
			}
			break;

		case '!':
			if (_Peek() == '=') {
				type = TOKEN_NE;
				length = 2;
			} else {
				type = TOKEN_LOGICAL_NOT;
				length = 1;
			}
			break;

		case '=':
			if (_Peek() == '=') {
				type = TOKEN_EQ;
				length = 2;
			} else {
				type = TOKEN_ASSIGN;
				length = 1;
			}
			break;

		case '>':
			if (_Peek() == '=') {
				type = TOKEN_GE;
				length = 2;
			} else {
				type = TOKEN_GT;
				length = 1;
			}
			break;

		case '<':
			if (_Peek() == '=') {
				type = TOKEN_LE;
				length = 2;
			} else {
				type = TOKEN_LT;
				length = 1;
			}
			break;

		case '~':
			type = TOKEN_BITWISE_NOT;
			length = 1;
			break;


		case '?':
			type = TOKEN_CONDITION;
			length = 1;
			break;

		case '.':
			type = TOKEN_MEMBER_PTR;
			length = 1;
			break;

		default:
			break;
	}

	if (length == 0)
		return false;

	fCurrentToken = Token(fCurrentChar, length, _CurrentPos(), type);
	fCurrentChar += length;

	return true;
}


/**
 * @brief Causes the next NextToken() call to re-emit the cached token.
 *
 * Used by recursive-descent parsers that need a one-token lookahead.
 */
void
Tokenizer::RewindToken()
{
	fReuseToken = true;
}


/**
 * @brief Returns the character one position past the current cursor.
 *
 * @return Lookahead character, or @c '\0' at end of input.
 */
char
Tokenizer::_Peek() const
{
	if (_CurrentPos() < fString.Length())
		return *(fCurrentChar + 1);

	return '\0';
}


/**
 * @brief Returns @c true when @a c is a hexadecimal digit (0-9, a-f, A-F).
 *
 * @param c  Character to test.
 */
/*static*/ bool
Tokenizer::_IsHexDigit(char c)
{
	return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}


/**
 * @brief Consumes a @c 0x-prefixed hexadecimal constant.
 *
 * The numeric value is stored on the token as either @c uint32 or @c uint64
 * depending on the token width.
 *
 * @return Reference to the updated current token.
 * @note Throws @c ParseException when no hex digits follow @c 0x.
 */
Token&
Tokenizer::_ParseHexOperand()
{
	const char* begin = fCurrentChar;
	fCurrentChar += 2;
		// skip "0x"

	if (!_IsHexDigit(*fCurrentChar))
		throw ParseException("expected hex digit", _CurrentPos());

	fCurrentChar++;
	while (_IsHexDigit(*fCurrentChar))
		fCurrentChar++;

	int32 length = fCurrentChar - begin;
	fCurrentToken = Token(begin, length, _CurrentPos() - length,
		TOKEN_CONSTANT);

	if (length <= 10) {
		// including the leading 0x, a 32-bit constant will be at most
		// 10 characters. Anything larger, and 64 is necessary.
		fCurrentToken.value.SetTo((uint32)strtoul(
			fCurrentToken.string.String(), NULL, 16));
	} else {
		fCurrentToken.value.SetTo((uint64)strtoull(
			fCurrentToken.string.String(), NULL, 16));
	}
	return fCurrentToken;
}


/**
 * @brief Returns the current cursor position as a column offset.
 *
 * @return Number of characters consumed so far.
 */
int32
Tokenizer::_CurrentPos() const
{
	return fCurrentChar - fString.String();
}
