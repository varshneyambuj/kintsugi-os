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
 *   Copyright 2006-2012 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       John Scipione <jscipione@gmail.com>
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */

/** @file ExpressionParser.cpp
 *  @brief Arbitrary-precision mathematical expression parser built on the
 *         MAPM library.  Supports the four arithmetic operators, exponentiation,
 *         factorials, bitwise NOT, and a comprehensive set of math functions.
 */

#include <ExpressionParser.h>

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <m_apm.h>


static const int32 kMaxDecimalPlaces = 32;


enum {
	TOKEN_NONE					= 0,
	TOKEN_IDENTIFIER,
	TOKEN_CONSTANT,

	TOKEN_END_OF_LINE			= '\n',

	TOKEN_PLUS					= '+',
	TOKEN_MINUS					= '-',

	TOKEN_STAR					= '*',
	TOKEN_SLASH					= '/',
	TOKEN_MODULO				= '%',

	TOKEN_POWER					= '^',
	TOKEN_FACTORIAL				= '!',

	TOKEN_OPENING_BRACKET		= '(',
	TOKEN_CLOSING_BRACKET		= ')',

	TOKEN_AND					= '&',
	TOKEN_OR					= '|',
	TOKEN_NOT					= '~'
};


/** @brief Represents a single lexical token produced by the Tokenizer. */
struct ExpressionParser::Token {
	/** @brief Constructs a default empty token of type TOKEN_NONE. */
	Token()
		: string(""),
		  type(TOKEN_NONE),
		  value(0),
		  position(0)
	{
	}

	/** @brief Copy-constructs a token.
	 *  @param other  The token to copy.
	 */
	Token(const Token& other)
		: string(other.string),
		  type(other.type),
		  value(other.value),
		  position(other.position)
	{
	}

	/** @brief Constructs a token from a substring of the source expression.
	 *  @param string    Pointer to the start of the token text in the source.
	 *  @param length    Number of characters in the token.
	 *  @param position  Byte offset of the token within the source string.
	 *  @param type      Token type constant (TOKEN_CONSTANT, TOKEN_IDENTIFIER, …).
	 */
	Token(const char* string, int32 length, int32 position, int32 type)
		: string(string, length),
		  type(type),
		  value(0),
		  position(position)
	{
	}

	/** @brief Assignment operator.
	 *  @param other  The token to assign from.
	 *  @return Reference to this token.
	 */
	Token& operator=(const Token& other)
	{
		string = other.string;
		type = other.type;
		value = other.value;
		position = other.position;
		return *this;
	}

	BString		string;   /**< The raw text of the token. */
	int32		type;     /**< Token type (TOKEN_CONSTANT, TOKEN_PLUS, …). */
	MAPM		value;    /**< Pre-parsed numeric value (set for TOKEN_CONSTANT). */

	int32		position; /**< Byte offset within the source string. */
};


/** @brief Lexical analyser for mathematical expression strings.
 *
 *  Breaks the input expression into a sequence of Token objects. Supports
 *  decimal numbers with optional exponents, hex literals (0x…), identifiers
 *  (function names and constants), and single-character operator tokens.
 *  Configurable decimal and group separators allow locale-specific input.
 */
class ExpressionParser::Tokenizer {
 public:
	/** @brief Constructs a Tokenizer with default ('.') decimal and (',') group separators. */
	Tokenizer()
		: fString(""),
		  fCurrentChar(NULL),
		  fCurrentToken(),
		  fReuseToken(false),
		  fHexSupport(false),
		  fDecimalSeparator("."),
		  fGroupSeparator(",")
	{
	}

	/** @brief Enables or disables hexadecimal literal input (0x prefix).
	 *  @param enabled  true to allow "0x…" hex literals; false to treat 'x' as multiply.
	 */
	void SetSupportHexInput(bool enabled)
	{
		fHexSupport = enabled;
	}

	/** @brief Resets the tokenizer to parse a new expression string.
	 *  @param string  NUL-terminated expression to tokenize.
	 */
	void SetTo(const char* string)
	{
		fString = string;
		fCurrentChar = fString.String();
		fCurrentToken = Token();
		fReuseToken = false;
	}

	/** @brief Returns the next token from the input.
	 *
	 *  If RewindToken() was called, the previous token is returned again.
	 *  Whitespace is skipped. Throws ParseException on unrecognised input.
	 *
	 *  @return Const reference to the current token.
	 */
	const Token& NextToken()
	{
		if (fCurrentToken.type == TOKEN_END_OF_LINE)
			return fCurrentToken;

		if (fReuseToken) {
			fReuseToken = false;
//printf("next token (recycled): '%s'\n", fCurrentToken.string.String());
			return fCurrentToken;
		}

		while (*fCurrentChar != 0 && isspace(*fCurrentChar))
			fCurrentChar++;

		int32 decimalLen = fDecimalSeparator.Length();
		int32 groupLen = fGroupSeparator.Length();

		if (*fCurrentChar == 0 || decimalLen == 0)
			return fCurrentToken = Token("", 0, _CurrentPos(), TOKEN_END_OF_LINE);

		if (*fCurrentChar == fDecimalSeparator[0] || isdigit(*fCurrentChar)) {
			if (fHexSupport && *fCurrentChar == '0' && fCurrentChar[1] == 'x')
				return _ParseHexNumber();

			BString temp;

			const char* begin = fCurrentChar;

			// optional digits before the comma
			while (isdigit(*fCurrentChar) ||
				(groupLen > 0 && *fCurrentChar == fGroupSeparator[0])) {
				if (groupLen > 0 && *fCurrentChar == fGroupSeparator[0]) {
					int i = 0;
					while (i < groupLen && *fCurrentChar == fGroupSeparator[i]) {
						fCurrentChar++;
						i++;
					}
				} else {
					temp << *fCurrentChar;
					fCurrentChar++;
				}
			}

			// optional post comma part
			// (required if there are no digits before the comma)
			if (*fCurrentChar == fDecimalSeparator[0]) {
				int i = 0;
				while (i < decimalLen && *fCurrentChar == fDecimalSeparator[i]) {
					fCurrentChar++;
					i++;
				}

				temp << '.';

				// optional post comma digits
				while (isdigit(*fCurrentChar)) {
					temp << *fCurrentChar;
					fCurrentChar++;
				}
			}

			// optional exponent part
			if (*fCurrentChar == 'E') {
				temp << *fCurrentChar;
				fCurrentChar++;

				// optional exponent sign
				if (*fCurrentChar == '+' || *fCurrentChar == '-') {
					temp << *fCurrentChar;
					fCurrentChar++;
				}

				// required exponent digits
				if (!isdigit(*fCurrentChar)) {
					throw ParseException("missing exponent in constant",
						fCurrentChar - begin);
				}

				while (isdigit(*fCurrentChar)) {
					temp << *fCurrentChar;
					fCurrentChar++;
				}
			}

			int32 length = fCurrentChar - begin;
			BString test = temp;
			test << "&_";
			double value;
			char t[2];
			int32 matches = sscanf(test.String(), "%lf&%s", &value, t);
			if (matches != 2) {
				throw ParseException("error in constant",
					_CurrentPos() - length);
			}

			fCurrentToken = Token(begin, length, _CurrentPos() - length,
				TOKEN_CONSTANT);
			fCurrentToken.value = temp.String();
		} else if (isalpha(*fCurrentChar) && *fCurrentChar != 'x') {
			const char* begin = fCurrentChar;
			while (*fCurrentChar != 0 && (isalpha(*fCurrentChar)
				|| isdigit(*fCurrentChar))) {
				fCurrentChar++;
			}
			int32 length = fCurrentChar - begin;
			fCurrentToken = Token(begin, length, _CurrentPos() - length,
				TOKEN_IDENTIFIER);
		} else if (strncmp(fCurrentChar, "π", 2) == 0) {
			fCurrentToken = Token(fCurrentChar, 2, _CurrentPos() - 1,
				TOKEN_IDENTIFIER);
			fCurrentChar += 2;
		} else {
			int32 type = TOKEN_NONE;

			switch (*fCurrentChar) {
				case TOKEN_PLUS:
				case TOKEN_MINUS:
				case TOKEN_STAR:
				case TOKEN_SLASH:
				case TOKEN_MODULO:
				case TOKEN_POWER:
				case TOKEN_FACTORIAL:
				case TOKEN_OPENING_BRACKET:
				case TOKEN_CLOSING_BRACKET:
				case TOKEN_AND:
				case TOKEN_OR:
				case TOKEN_NOT:
				case TOKEN_END_OF_LINE:
					type = *fCurrentChar;
					break;

				case '\\':
				case ':':
				type = TOKEN_SLASH;
					break;

				case 'x':
					if (!fHexSupport) {
						type = TOKEN_STAR;
						break;
					}
					// fall through

				default:
					throw ParseException("unexpected character", _CurrentPos());
			}
			fCurrentToken = Token(fCurrentChar, 1, _CurrentPos(), type);
			fCurrentChar++;
		}

//printf("next token: '%s'\n", fCurrentToken.string.String());
		return fCurrentToken;
	}

	/** @brief Causes the next NextToken() call to return the current token again. */
	void RewindToken()
	{
		fReuseToken = true;
	}

	/** @brief Returns the current decimal separator string.
	 *  @return The decimal separator (e.g. "." or ",").
	 */
	BString DecimalSeparator()
	{
		return fDecimalSeparator;
	}

	/** @brief Returns the current group (thousands) separator string.
	 *  @return The group separator (e.g. "," or ".").
	 */
	BString GroupSeparator()
	{
		return fGroupSeparator;
	}

	/** @brief Sets the decimal and group separator strings.
	 *  @param decimal  The new decimal separator.
	 *  @param group    The new group (thousands) separator.
	 */
	void SetSeparators(BString decimal, BString group)
	{
		fDecimalSeparator = decimal;
		fGroupSeparator = group;
	}

 private:
	/** @brief Returns true if @p c is a valid hexadecimal digit.
	 *  @param c  Character to test.
	 *  @return true for 0–9, a–f, A–F.
	 */
	static bool _IsHexDigit(char c)
	{
		return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
	}

	/** @brief Parses a "0x…" hexadecimal constant and stores it as TOKEN_CONSTANT.
	 *
	 *  Converts the hex string to a uint64 and then to MAPM via arithmetic
	 *  decomposition (MAPM has no direct 64-bit integer constructor).
	 *
	 *  @return Reference to the populated fCurrentToken.
	 */
	Token& _ParseHexNumber()
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

		// MAPM has no conversion from long long, so we need to improvise.
		uint64 value = strtoll(fCurrentToken.string.String(), NULL, 0);
		if (value <= 0x7fffffff) {
			fCurrentToken.value = (long)value;
		} else {
			fCurrentToken.value = (int)(value >> 60);
			fCurrentToken.value *= 1 << 30;
			fCurrentToken.value += (int)((value >> 30) & 0x3fffffff);
			fCurrentToken.value *= 1 << 30;
			fCurrentToken.value += (int)(value& 0x3fffffff);
		}

		return fCurrentToken;
	}

	/** @brief Returns the current byte offset within fString.
	 *  @return Byte offset of fCurrentChar from the start of fString.
	 */
	int32 _CurrentPos() const
	{
		return fCurrentChar - fString.String();
	}

	BString		fString;
	const char*	fCurrentChar;
	Token		fCurrentToken;
	bool		fReuseToken;
	bool		fHexSupport;
	BString		fDecimalSeparator;
	BString		fGroupSeparator;
};


/** @brief Constructs an ExpressionParser in radian mode. */
ExpressionParser::ExpressionParser()
	:	fTokenizer(new Tokenizer()),
		fDegreeMode(false)
{
}


/** @brief Destroys the parser and its internal tokenizer. */
ExpressionParser::~ExpressionParser()
{
	delete fTokenizer;
}


/** @brief Returns whether the parser is currently in degree mode.
 *  @return true if trig functions interpret angles as degrees.
 */
bool
ExpressionParser::DegreeMode()
{
	return fDegreeMode;
}


/** @brief Sets whether trig functions interpret angles as degrees or radians.
 *  @param degrees  true for degree mode, false for radian mode.
 */
void
ExpressionParser::SetDegreeMode(bool degrees)
{
	fDegreeMode = degrees;
}


/** @brief Enables or disables hexadecimal literal input (0x… notation).
 *  @param enabled  true to allow hex literals.
 */
void
ExpressionParser::SetSupportHexInput(bool enabled)
{
	fTokenizer->SetSupportHexInput(enabled);
}


/** @brief Evaluates @p expressionString and returns the result as a BString.
 *
 *  Trailing zeros after the decimal point are stripped. The decimal separator
 *  used in the output matches the one configured via SetSeparators().
 *
 *  @param expressionString  NUL-terminated infix expression.
 *  @return The formatted result string.
 *  @throws ParseException on syntax errors or domain violations.
 */
BString
ExpressionParser::Evaluate(const char* expressionString)
{
	fTokenizer->SetTo(expressionString);

	MAPM value = _ParseBinary();
	Token token = fTokenizer->NextToken();
	if (token.type != TOKEN_END_OF_LINE)
		throw ParseException("parse error", token.position);

	if (value == 0)
		return BString("0");

	char* buffer = value.toFixPtStringExp(kMaxDecimalPlaces,
						'.', 0, 0);

	if (buffer == NULL)
		throw ParseException("out of memory", 0);

	// remove surplus zeros
	int32 lastChar = strlen(buffer) - 1;
	if (strchr(buffer, '.')) {
		while (buffer[lastChar] == '0')
			lastChar--;

		if (buffer[lastChar] == '.')
			lastChar--;
	}

	BString result(buffer, lastChar + 1);
	result.Replace(".", fTokenizer->DecimalSeparator(), 1);
	free(buffer);
	return result;
}


/** @brief Evaluates @p expressionString and returns the integer result.
 *
 *  The result is truncated to int64.
 *
 *  @param expressionString  NUL-terminated infix expression.
 *  @return The integer part of the evaluated expression.
 *  @throws ParseException on syntax errors or domain violations.
 */
int64
ExpressionParser::EvaluateToInt64(const char* expressionString)
{
	fTokenizer->SetTo(expressionString);

	MAPM value = _ParseBinary();
	Token token = fTokenizer->NextToken();
	if (token.type != TOKEN_END_OF_LINE)
		throw ParseException("parse error", token.position);

	char buffer[128];
	value.toIntegerString(buffer);

	return strtoll(buffer, NULL, 0);
}


/** @brief Evaluates @p expressionString and returns the result as a double.
 *
 *  @param expressionString  NUL-terminated infix expression.
 *  @return The double-precision result.
 *  @throws ParseException on syntax errors or domain violations.
 */
double
ExpressionParser::EvaluateToDouble(const char* expressionString)
{
	fTokenizer->SetTo(expressionString);

	MAPM value = _ParseBinary();
	Token token = fTokenizer->NextToken();
	if (token.type != TOKEN_END_OF_LINE)
		throw ParseException("parse error", token.position);

	char buffer[1024];
	value.toString(buffer, sizeof(buffer) - 4);

	return strtod(buffer, NULL);
}


/** @brief Entry point for the recursive-descent parser; currently delegates to _ParseSum.
 *
 *  Binary bitwise operations (& and |) are stubbed out and left as future work
 *  because MAPM has no direct large-integer bitwise support.
 *
 *  @return The parsed MAPM value.
 */
MAPM
ExpressionParser::_ParseBinary()
{
	return _ParseSum();
	// binary operation appearantly not supported by m_apm library,
	// should not be too hard to implement though....

//	double value = _ParseSum();
//
//	while (true) {
//		Token token = fTokenizer->NextToken();
//		switch (token.type) {
//			case TOKEN_AND:
//				value = (uint64)value & (uint64)_ParseSum();
//				break;
//			case TOKEN_OR:
//				value = (uint64)value | (uint64)_ParseSum();
//				break;
//
//			default:
//				fTokenizer->RewindToken();
//				return value;
//		}
//	}
}


/** @brief Parses additive expressions (+ and -).
 *
 *  Handles left-to-right chaining of TOKEN_PLUS and TOKEN_MINUS operators.
 *
 *  @return The computed sum/difference.
 */
MAPM
ExpressionParser::_ParseSum()
{
	// TODO: check isnan()...
	MAPM value = _ParseProduct();

	while (true) {
		Token token = fTokenizer->NextToken();
		switch (token.type) {
			case TOKEN_PLUS:
				value = value + _ParseProduct();
				break;
			case TOKEN_MINUS:
				value = value - _ParseProduct();
				break;

			default:
				fTokenizer->RewindToken();
				return _ParseFactorial(value);
		}
	}
}


/** @brief Parses multiplicative expressions (*, /, %).
 *
 *  Throws ParseException on division or modulo by zero.
 *
 *  @return The computed product/quotient/remainder.
 */
MAPM
ExpressionParser::_ParseProduct()
{
	// TODO: check isnan()...
	MAPM value = _ParsePower();

	while (true) {
		Token token = fTokenizer->NextToken();
		switch (token.type) {
			case TOKEN_STAR:
				value = value * _ParsePower();
				break;
			case TOKEN_SLASH: {
				MAPM rhs = _ParsePower();
				if (rhs == MAPM(0))
					throw ParseException("division by zero", token.position);
				value = value / rhs;
				break;
			}
			case TOKEN_MODULO: {
				MAPM rhs = _ParsePower();
				if (rhs == MAPM(0))
					throw ParseException("modulo by zero", token.position);
				value = value % rhs;
				break;
			}

			default:
				fTokenizer->RewindToken();
				return _ParseFactorial(value);
		}
	}
}


/** @brief Parses exponentiation expressions (^).
 *
 *  Right-associative: a^b^c is evaluated as a^(b^c).
 *
 *  @return The computed power.
 */
MAPM
ExpressionParser::_ParsePower()
{
	MAPM value = _ParseUnary();

	while (true) {
		Token token = fTokenizer->NextToken();
		if (token.type != TOKEN_POWER) {
			fTokenizer->RewindToken();
			return _ParseFactorial(value);
		}
		value = value.pow(_ParseUnary());
	}
}


/** @brief Parses unary +, -, and dispatches identifiers to _ParseFunction().
 *
 *  @return The unary-modified value.
 *  @throws ParseException on unexpected end of expression.
 */
MAPM
ExpressionParser::_ParseUnary()
{
	Token token = fTokenizer->NextToken();
	if (token.type == TOKEN_END_OF_LINE)
		throw ParseException("unexpected end of expression", token.position);

	switch (token.type) {
		case TOKEN_PLUS:
			return _ParseUnary();
		case TOKEN_MINUS:
			return -_ParseUnary();
// TODO: Implement !
//		case TOKEN_NOT:
//			return ~(uint64)_ParseUnary();

		case TOKEN_IDENTIFIER:
			return _ParseFunction(token);

		default:
			fTokenizer->RewindToken();
			return _ParseAtom();
	}

	return MAPM(0);
}


/** @brief Represents a built-in mathematical function (unused table; retained for reference). */
struct Function {
	const char*	name;
	int			argumentCount;
	void*		function;
	MAPM		value;
};


/** @brief Reads exactly @p argumentCount arguments between '(' and ')'.
 *
 *  Arguments are separated by the binary parse level; each is stored in
 *  values[0..argumentCount-1].
 *
 *  @param values         Output array of at least @p argumentCount MAPM values.
 *  @param argumentCount  Number of comma-delimited arguments expected.
 *  @throws ParseException if brackets are missing.
 */
void
ExpressionParser::_InitArguments(MAPM values[], int32 argumentCount)
{
	_EatToken(TOKEN_OPENING_BRACKET);

	for (int32 i = 0; i < argumentCount; i++)
		values[i] = _ParseBinary();

	_EatToken(TOKEN_CLOSING_BRACKET);
}


/** @brief Evaluates a named constant or function call given its identifier token.
 *
 *  Handles the constants "e" and "pi" (and the Unicode π character), and
 *  all built-in single- or two-argument math functions (abs, sin, cos, …).
 *  Applies degree-mode conversion where applicable.
 *
 *  @param token  The TOKEN_IDENTIFIER token naming the function or constant.
 *  @return The evaluated MAPM result.
 *  @throws ParseException for unknown identifiers or out-of-domain arguments.
 */
MAPM
ExpressionParser::_ParseFunction(const Token& token)
{
	if (token.string == "e")
		return _ParseFactorial(MAPM(MM_E));
	else if (token.string.ICompare("pi") == 0 || token.string == "π")
		return _ParseFactorial(MAPM(MM_PI));

	// hard coded cases for different count of arguments
	// supports functions with 3 arguments at most

	MAPM values[3];

	if (strcasecmp("abs", token.string.String()) == 0) {
		_InitArguments(values, 1);
		return _ParseFactorial(values[0].abs());
	} else if (strcasecmp("acos", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (fDegreeMode)
			values[0] = values[0] * MM_PI / 180;

		if (values[0] < -1 || values[0] > 1)
			throw ParseException("out of domain", token.position);

		return _ParseFactorial(values[0].acos());
	} else if (strcasecmp("asin", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (fDegreeMode)
			values[0] = values[0] * MM_PI / 180;

		if (values[0] < -1 || values[0] > 1)
			throw ParseException("out of domain", token.position);

		return _ParseFactorial(values[0].asin());
	} else if (strcasecmp("atan", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (fDegreeMode)
			values[0] = values[0] * MM_PI / 180;

		return _ParseFactorial(values[0].atan());
	} else if (strcasecmp("atan2", token.string.String()) == 0) {
		_InitArguments(values, 2);

		if (fDegreeMode) {
			values[0] = values[0] * MM_PI / 180;
			values[1] = values[1] * MM_PI / 180;
		}

		return _ParseFactorial(values[0].atan2(values[1]));
	} else if (strcasecmp("cbrt", token.string.String()) == 0) {
		_InitArguments(values, 1);
		return _ParseFactorial(values[0].cbrt());
	} else if (strcasecmp("ceil", token.string.String()) == 0) {
		_InitArguments(values, 1);
		return _ParseFactorial(values[0].ceil());
	} else if (strcasecmp("cos", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (fDegreeMode)
			values[0] = values[0] * MM_PI / 180;

		return _ParseFactorial(values[0].cos());
	} else if (strcasecmp("cosh", token.string.String()) == 0) {
		_InitArguments(values, 1);
		// This function always uses radians
		return _ParseFactorial(values[0].cosh());
	} else if (strcasecmp("exp", token.string.String()) == 0) {
		_InitArguments(values, 1);
		return _ParseFactorial(values[0].exp());
	} else if (strcasecmp("floor", token.string.String()) == 0) {
		_InitArguments(values, 1);
		return _ParseFactorial(values[0].floor());
	} else if (strcasecmp("ln", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (values[0] <= 0)
			throw ParseException("out of domain", token.position);

		return _ParseFactorial(values[0].log());
	} else if (strcasecmp("log", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (values[0] <= 0)
			throw ParseException("out of domain", token.position);

		return _ParseFactorial(values[0].log10());
	} else if (strcasecmp("pow", token.string.String()) == 0) {
		_InitArguments(values, 2);
		return _ParseFactorial(values[0].pow(values[1]));
	} else if (strcasecmp("sin", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (fDegreeMode)
			values[0] = values[0] * MM_PI / 180;

		return _ParseFactorial(values[0].sin());
	} else if (strcasecmp("sinh", token.string.String()) == 0) {
		_InitArguments(values, 1);
		// This function always uses radians
		return _ParseFactorial(values[0].sinh());
	} else if (strcasecmp("sqrt", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (values[0] < 0)
			throw ParseException("out of domain", token.position);

		return _ParseFactorial(values[0].sqrt());
	} else if (strcasecmp("tan", token.string.String()) == 0) {
		_InitArguments(values, 1);
		if (fDegreeMode)
			values[0] = values[0] * MM_PI / 180;

		MAPM divided_by_half_pi = values[0] / MM_HALF_PI;
		if (divided_by_half_pi.is_integer() && divided_by_half_pi.is_odd())
			throw ParseException("out of domain", token.position);

		return _ParseFactorial(values[0].tan());
	} else if (strcasecmp("tanh", token.string.String()) == 0) {
		_InitArguments(values, 1);
		// This function always uses radians
		return _ParseFactorial(values[0].tanh());
	}

	throw ParseException("unknown identifier", token.position);
}


/** @brief Parses an atom: a constant or a parenthesised sub-expression.
 *
 *  If the next token is TOKEN_CONSTANT its value is returned directly;
 *  otherwise the token is rewound and a '(' … ')' group is expected.
 *
 *  @return The parsed atom value.
 *  @throws ParseException on unexpected end of expression or missing bracket.
 */
MAPM
ExpressionParser::_ParseAtom()
{
	Token token = fTokenizer->NextToken();
	if (token.type == TOKEN_END_OF_LINE)
		throw ParseException("unexpected end of expression", token.position);

	if (token.type == TOKEN_CONSTANT)
		return _ParseFactorial(token.value);

	fTokenizer->RewindToken();

	_EatToken(TOKEN_OPENING_BRACKET);

	MAPM value = _ParseBinary();

	_EatToken(TOKEN_CLOSING_BRACKET);

	return _ParseFactorial(value);
}


/** @brief Checks for an optional trailing '!' and applies the factorial if present.
 *
 *  For values >= 1000 Stirling's approximation (9-term expansion) is used
 *  because MAPM's exact factorial is too slow.
 *
 *  @param value  The value to optionally factorialise.
 *  @return @p value! if '!' follows, otherwise @p value unchanged.
 */
MAPM
ExpressionParser::_ParseFactorial(MAPM value)
{
	if (fTokenizer->NextToken().type == TOKEN_FACTORIAL) {
		fTokenizer->RewindToken();
		_EatToken(TOKEN_FACTORIAL);
		if (value < 1000)
			return value.factorial();
		else {
			// Use Stirling's approximation (9 term expansion)
			// http://en.wikipedia.org/wiki/Stirling%27s_approximation
			// http://www.wolframalpha.com/input/?i=stirling%27s+series
			// all constants must fit in a signed long for MAPM
			// (LONG_MAX = 2147483647)
			return value.pow(value) / value.exp()
				* (MAPM(2) * MAPM(MM_PI) * value).sqrt()
				* (MAPM(1) + (MAPM(1) / (MAPM(12) * value))
					+ (MAPM(1) / (MAPM(288) * value.pow(2)))
					- (MAPM(139) / (MAPM(51840) * value.pow(3)))
					- (MAPM(571) / (MAPM(2488320) * value.pow(4)))
					+ (MAPM(163879) / (MAPM(209018880) * value.pow(5)))
						// 2147483647 * 35 + 84869155 = 75246796800
					+ (MAPM(5246819) / ((MAPM(2147483647) * MAPM(35)
						+ MAPM(84869155)) * value.pow(6)))
						// 2147483647 * 420 + 1018429860 = 902961561600
					- (MAPM(534703531) / ((MAPM(2147483647) * MAPM(420)
						+ MAPM(1018429860)) * value.pow(7)))
						// 2147483647 * 2 + 188163965 = 4483131259
						// 2147483647 * 40366 + 985018798 = 86686309913600
					- ((MAPM(2147483647) * MAPM(2) + MAPM(188163965))
						/ ((MAPM(2147483647) * MAPM(40366) + MAPM(985018798))
							* value.pow(8)))
						// 2147483647 * 201287 + 1380758682 = 432261921612371
						// 2147483647 * 239771232 + 1145740896
						// = 514904800886784000
					+ ((MAPM(2147483647) * MAPM(201287) + MAPM(1380758682))
						/ ((MAPM(2147483647) * MAPM(239771232)
								+ MAPM(1145740896))
							* value.pow(9))));
		}
	}

	fTokenizer->RewindToken();
	return value;
}


/** @brief Consumes the next token and verifies it has the expected type.
 *
 *  Throws a ParseException with a human-readable "Expected X got Y" message
 *  if the token type does not match.
 *
 *  @param type  The expected token type constant.
 *  @throws ParseException if the next token is not of the expected type.
 */
void
ExpressionParser::_EatToken(int32 type)
{
	Token token = fTokenizer->NextToken();
	if (token.type != type) {
		BString expected;
		switch (type) {
			case TOKEN_IDENTIFIER:
				expected = "an identifier";
				break;

			case TOKEN_CONSTANT:
				expected = "a constant";
				break;

			case TOKEN_PLUS:
			case TOKEN_MINUS:
			case TOKEN_STAR:
			case TOKEN_MODULO:
			case TOKEN_POWER:
			case TOKEN_FACTORIAL:
			case TOKEN_OPENING_BRACKET:
			case TOKEN_CLOSING_BRACKET:
			case TOKEN_AND:
			case TOKEN_OR:
			case TOKEN_NOT:
				expected << "'" << (char)type << "'";
				break;

			case TOKEN_SLASH:
				expected = "'/', '\\', or ':'";
				break;

			case TOKEN_END_OF_LINE:
				expected = "'\\n'";
				break;
		}
		BString temp;
		temp << "Expected " << expected.String() << " got '" << token.string << "'";
		throw ParseException(temp.String(), token.position);
	}
}


/** @brief Sets locale-specific decimal and group (thousands) separators.
 *
 *  The two separators must be different strings; otherwise B_ERROR is returned.
 *
 *  @param decimal  The decimal point string (e.g. "." or ",").
 *  @param group    The thousands-grouping separator (e.g. "," or ".").
 *  @return B_OK on success, B_ERROR if @p decimal == @p group.
 */
status_t
ExpressionParser::SetSeparators(BString decimal, BString group)
{
	if (decimal == group)
		return B_ERROR;

	fTokenizer->SetSeparators(decimal, group);

	return B_OK;
}
