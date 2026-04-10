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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file Parser.cpp
 * @brief MIME sniffer rule parser and tokenizer implementation.
 *
 * Implements the full lexer (TokenStream) and recursive-descent parser (Parser)
 * for MIME sniffer rules as used by the storage kit. The lexer converts a raw
 * rule string into a stream of typed Token objects (integers, floats, strings,
 * punctuation, flags), and the parser consumes that stream to build a Rule object
 * consisting of a priority and a conjunction of disjunction lists. Helper free
 * functions handle escape sequences, hex/octal literals, and token classification.
 *
 * @see BPrivate::Storage::Sniffer::Rule
 * @see BPrivate::Storage::Sniffer::TokenStream
 */

#include <sniffer/Parser.h>
#include <sniffer/Pattern.h>
#include <sniffer/PatternList.h>
#include <sniffer/Range.h>
#include <sniffer/RPattern.h>
#include <sniffer/RPatternList.h>
#include <sniffer/Rule.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>	// For atol(), atof()
#include <string.h>
#include <String.h>	// BString

using namespace BPrivate::Storage::Sniffer;

// Miscellaneous helper functions
char escapeChar(char ch);
char hexToChar(char hi, char low);
char hexToChar(char hex);
char octalToChar(char octal);
char octalToChar(char hi, char low);
char octalToChar(char hi, char mid, char low);
bool isHexChar(char ch);
bool isWhiteSpace(char ch);
bool isOctalChar(char ch);
bool isDecimalChar(char ch);
bool isPunctuation(char ch);

/**
 * @brief Parses the given sniffer rule string into a Rule object.
 *
 * The resulting parsed Rule structure is stored in @p result, which must be
 * pre-allocated. If parsing fails, a descriptive error message (meant to be
 * viewed in a monospaced font) is placed in the pre-allocated BString pointed
 * to by @p parseError (which may be NULL if error text is not needed).
 *
 * @param rule       Pointer to a null-terminated string containing the sniffer rule.
 * @param result     Pointer to a pre-allocated Rule object that receives the result.
 * @param parseError Pointer to a pre-allocated BString that receives a descriptive
 *                   error message on failure; may be NULL.
 * @return B_OK on success, B_BAD_MIME_SNIFFER_RULE on parse failure.
 */
status_t
BPrivate::Storage::Sniffer::parse(const char *rule, Rule *result, BString *parseError) {
	Parser parser;
	return parser.Parse(rule, result, parseError);
}

//------------------------------------------------------------------------------
// Token
//------------------------------------------------------------------------------

/**
 * @brief Constructs a Token with the given type and stream position.
 *
 * @param type The type of this token.
 * @param pos  The position in the input stream where this token begins.
 */
Token::Token(TokenType type, const ssize_t pos)
	: fType(type)
	, fPos(pos)
{
//	if (type != EmptyToken)
//		cout << "New Token, fType == " << tokenTypeToString(fType) << endl;
}

/**
 * @brief Destroys the Token.
 */
Token::~Token() {
}

/**
 * @brief Returns the type of this token.
 *
 * @return The TokenType enumeration value for this token.
 */
TokenType
Token::Type() const {
	return fType;
}

/**
 * @brief Returns the string value of this token.
 *
 * Base implementation always throws; override in StringToken.
 *
 * @return The string value (never reached in base class).
 */
const std::string&
Token::String() const {
	throw new Err("Sniffer scanner error: Token::String() called on non-string token", fPos);
}

/**
 * @brief Returns the integer value of this token.
 *
 * Base implementation always throws; override in IntToken.
 *
 * @return The integer value (never reached in base class).
 */
int32
Token::Int() const {
	throw new Err("Sniffer scanner error: Token::Int() called on non-integer token", fPos);
}

/**
 * @brief Returns the floating-point value of this token.
 *
 * Base implementation always throws; override in FloatToken or IntToken.
 *
 * @return The float value (never reached in base class).
 */
double
Token::Float() const {
	throw new Err("Sniffer scanner error: Token::Float() called on non-float token", fPos);
}

/**
 * @brief Returns the stream position of this token.
 *
 * @return The zero-based byte offset in the input string where this token starts.
 */
ssize_t
Token::Pos() const {
	return fPos;
}

/**
 * @brief Compares this token to another for equality.
 *
 * Tokens are equal when their types match. For string, integer, and float tokens
 * the stored values are also compared.
 *
 * @param ref The other Token to compare against.
 * @return true if the tokens are equal, false otherwise.
 */
bool
Token::operator==(Token &ref) const {
	// Compare types, then data if necessary
	if (Type() == ref.Type()) {
		switch (Type()) {
			case CharacterString:
//				printf(" str1 == '%s'\n", String());
//				printf(" str2 == '%s'\n", ref.String());
//				printf(" strcmp() == %d\n", strcmp(String(), ref.String()));
			{
				return String() == ref.String();

/*
				// strcmp() seems to choke on certain, non-normal ASCII chars
				// (i.e. chars outside the usual alphabets, but still valid
				// as far as ASCII is concerned), so we'll just compare the
				// strings by hand to be safe.
				const char *str1 = String();
				const char *str2 = ref.String();
				int len1 = strlen(str1);
				int len2 = strlen(str2);
//				printf("len1 == %d\n", len1);
//				printf("len2 == %d\n", len2);
				if (len1 == len2) {
					for (int i = 0; i < len1; i++) {
//						printf("i == %d, str1[%d] == %x, str2[%d] == %x\n", i, i, str1[i], i, str2[i]);
						if (str1[i] != str2[i])
							return false;
					}
				}
				return true;
*/
			}
//				return strcmp(String(), ref.String()) == 0;

			case Integer:
				return Int() == ref.Int();

			case FloatingPoint:
				return Float() == ref.Float();

			default:
				return true;
		}
	} else
		return false;
}

//------------------------------------------------------------------------------
// StringToken
//------------------------------------------------------------------------------

/**
 * @brief Constructs a StringToken with the given string value and stream position.
 *
 * @param str The string content of this token.
 * @param pos The position in the input stream where this token begins.
 */
StringToken::StringToken(const std::string &str, const ssize_t pos)
	: Token(CharacterString, pos)
	, fString(str)
{
}

/**
 * @brief Destroys the StringToken.
 */
StringToken::~StringToken() {
}

/**
 * @brief Returns the string value stored in this token.
 *
 * @return A const reference to the internal string.
 */
const std::string&
StringToken::String() const {
	return fString;
}

//------------------------------------------------------------------------------
// IntToken
//------------------------------------------------------------------------------

/**
 * @brief Constructs an IntToken with the given integer value and stream position.
 *
 * @param value The integer value of this token.
 * @param pos   The position in the input stream where this token begins.
 */
IntToken::IntToken(const int32 value, const ssize_t pos)
	: Token(Integer, pos)
	, fValue(value)
{
}

/**
 * @brief Destroys the IntToken.
 */
IntToken::~IntToken() {
}

/**
 * @brief Returns the integer value stored in this token.
 *
 * @return The 32-bit integer value.
 */
int32
IntToken::Int() const {
	return fValue;
}

/**
 * @brief Returns the integer value as a double-precision float.
 *
 * @return The stored integer cast to double.
 */
double
IntToken::Float() const {
	return (double)fValue;
}

//------------------------------------------------------------------------------
// FloatToken
//------------------------------------------------------------------------------

/**
 * @brief Constructs a FloatToken with the given double value and stream position.
 *
 * @param value The floating-point value of this token.
 * @param pos   The position in the input stream where this token begins.
 */
FloatToken::FloatToken(const double value, const ssize_t pos)
	: Token(FloatingPoint, pos)
	, fValue(value)
{
}

/**
 * @brief Destroys the FloatToken.
 */
FloatToken::~FloatToken() {
}

/**
 * @brief Returns the floating-point value stored in this token.
 *
 * @return The double-precision float value.
 */
double
FloatToken::Float() const {
	return fValue;
}

//------------------------------------------------------------------------------
// TokenStream
//------------------------------------------------------------------------------

/**
 * @brief Constructs and initializes a TokenStream by scanning the given string.
 *
 * @param string The raw sniffer rule string to tokenize.
 */
TokenStream::TokenStream(const std::string &string)
	: fCStatus(B_NO_INIT)
	, fPos(-1)
	, fStrLen(-1)
{
	SetTo(string);
}

/**
 * @brief Constructs an uninitialized TokenStream.
 *
 * Call SetTo() before using the stream.
 */
TokenStream::TokenStream()
	: fCStatus(B_NO_INIT)
	, fPos(-1)
	, fStrLen(-1)
{
}

/**
 * @brief Destroys the TokenStream and all Token objects it owns.
 */
TokenStream::~TokenStream() {
	Unset();
}

/**
 * @brief Tokenizes the given string, replacing any previous token list.
 *
 * Runs a state-machine scanner over the input character stream to produce
 * a vector of Token objects. On success the stream's read position is reset
 * to the beginning.
 *
 * @param string The raw sniffer rule string to tokenize.
 * @return B_OK on success; throws a Sniffer::Err on any lexical error.
 */
status_t
TokenStream::SetTo(const std::string &string) {
	Unset();
	fStrLen = string.length();
	CharStream stream(string);
	if (stream.InitCheck() != B_OK)
		throw new Err("Sniffer scanner error: Unable to intialize character stream", -1);

	typedef enum TokenStreamScannerState {
		tsssStart,
		tsssOneSingle,
		tsssOneDouble,
		tsssOneZero,
		tsssZeroX,
		tsssOneHex,
		tsssTwoHex,
		tsssIntOrFloat,
		tsssFloat,
		tsssLonelyDecimalPoint,
		tsssLonelyMinusOrPlus,
		tsssLonelyFloatExtension,
		tsssLonelyFloatExtensionWithSign,
		tsssExtendedFloat,
		tsssUnquoted,
		tsssEscape,
		tsssEscapeX,
		tsssEscapeOneOctal,
		tsssEscapeTwoOctal,
		tsssEscapeOneHex,
	} TokenStreamScannerState;

	TokenStreamScannerState state = tsssStart;
	TokenStreamScannerState escapedState = tsssStart;
		// Used to remember which state to return to from an escape sequence

	std::string charStr = "";	// Used to build up character strings
	char lastChar = 0;			// For two char lookahead
	char lastLastChar = 0;		// For three char lookahead (have I mentioned I hate octal?)
	bool keepLooping = true;
	ssize_t startPos = 0;
	while (keepLooping) {
		ssize_t pos = stream.Pos();
		char ch = stream.Get();
		switch (state) {
			case tsssStart:
				startPos = pos;
				switch (ch) {
					case 0x3:	// End-Of-Text
						if (stream.IsEmpty())
							keepLooping = false;
						else
							throw new Err(std::string("Sniffer pattern error: invalid character '") + ch + "'", pos);
						break;

					case '\t':
					case '\n':
					case ' ':
						// Whitespace, so ignore it.
						break;

					case '"':
						charStr = "";
						state = tsssOneDouble;
						break;

					case '\'':
						charStr = "";
						state = tsssOneSingle;
						break;

					case '+':
					case '-':
						charStr = ch;
						lastChar = ch;
						state = tsssLonelyMinusOrPlus;
						break;

					case '.':
						charStr = ch;
						state = tsssLonelyDecimalPoint;
						break;

					case '0':
						charStr = ch;
						state = tsssOneZero;
						break;

					case '1':
					case '2':
					case '3':
					case '4':
					case '5':
					case '6':
					case '7':
					case '8':
					case '9':
						charStr = ch;
						state = tsssIntOrFloat;
						break;

					case '&':	AddToken(Ampersand, pos);		break;
					case '(':	AddToken(LeftParen, pos);		break;
					case ')':	AddToken(RightParen, pos);		break;
					case ':':	AddToken(Colon, pos);			break;
					case '[':	AddToken(LeftBracket, pos);		break;

					case '\\':
						charStr = "";					// Clear our string
						state = tsssEscape;
						escapedState = tsssUnquoted;	// Unquoted strings begin with an escaped character
						break;

					case ']':	AddToken(RightBracket, pos);		break;
					case '|':	AddToken(Divider, pos);			break;

					default:
						throw new Err(std::string("Sniffer pattern error: invalid character '") + ch + "'", pos);
				}
				break;

			case tsssOneSingle:
				switch (ch) {
					case '\\':
						escapedState = state;		// Save our state
						state = tsssEscape;			// Handle the escape sequence
						break;
					case '\'':
						AddString(charStr, startPos);
						state = tsssStart;
						break;
					case 0x3:
						if (stream.IsEmpty())
							throw new Err(std::string("Sniffer pattern error: unterminated single-quoted string"), pos);
						else
							charStr += ch;
						break;
					default:
						charStr += ch;
						break;
				}
				break;

			case tsssOneDouble:
				switch (ch) {
					case '\\':
						escapedState = state;		// Save our state
						state = tsssEscape;			// Handle the escape sequence
						break;
					case '"':
						AddString(charStr, startPos);
						state = tsssStart;
						break;
					case 0x3:
						if (stream.IsEmpty())
							throw new Err(std::string("Sniffer pattern error: unterminated double-quoted string"), pos);
						else
							charStr += ch;
						break;
					default:
						charStr += ch;
						break;
				}
				break;

			case tsssOneZero:
				if (ch == 'x') {
					charStr = "";	// Reinit, since we actually have a hex string
					state = tsssZeroX;
				} else if ('0' <= ch && ch <= '9') {
					charStr += ch;
					state = tsssIntOrFloat;
				} else if (ch == '.') {
					charStr += ch;
					state = tsssFloat;
				} else if (ch == 'e' || ch == 'E') {
					charStr += ch;
					state = tsssLonelyFloatExtension;
				} else {
					// Terminate the number
					AddInt(charStr.c_str(), startPos);

					// Push the last char back on and try again
					stream.Unget();
					state = tsssStart;
				}
				break;

			case tsssZeroX:
				if (isHexChar(ch)) {
					lastChar = ch;
					state = tsssOneHex;
				} else
					throw new Err(std::string("Sniffer pattern error: incomplete hex code"), pos);
				break;

			case tsssOneHex:
				if (isHexChar(ch)) {
					try {
						charStr += hexToChar(lastChar, ch);
					} catch (Err *err) {
						if (err)
							err->SetPos(pos);
						throw err;
					}
					state = tsssTwoHex;
				} else
					throw new Err(std::string("Sniffer pattern error: bad hex literal"), pos);	// Same as R5
				break;

			case tsssTwoHex:
				if (isHexChar(ch)) {
					lastChar = ch;
					state = tsssOneHex;
				} else {
					AddString(charStr, startPos);
					stream.Unget();		// So punctuation gets handled properly
					state = tsssStart;
				}
				break;

			case tsssIntOrFloat:
				if (isDecimalChar(ch))
					charStr += ch;
				else if (ch == '.') {
					charStr += ch;
					state = tsssFloat;
				} else if (ch == 'e' || ch == 'E') {
					charStr += ch;
					state = tsssLonelyFloatExtension;
				} else {
					// Terminate the number
					AddInt(charStr.c_str(), startPos);

					// Push the last char back on and try again
					stream.Unget();
					state = tsssStart;
				}
				break;

			case tsssFloat:
				if (isDecimalChar(ch))
					charStr += ch;
				else if (ch == 'e' || ch == 'E') {
					charStr += ch;
					state = tsssLonelyFloatExtension;
				} else {
					// Terminate the number
					AddFloat(charStr.c_str(), startPos);

					// Push the last char back on and try again
					stream.Unget();
					state = tsssStart;
				}
				break;

			case tsssLonelyDecimalPoint:
				if (isDecimalChar(ch)) {
					charStr += ch;
					state = tsssFloat;
				} else
					throw new Err(std::string("Sniffer pattern error: incomplete floating point number"), pos);
				break;

			case tsssLonelyMinusOrPlus:
				if (isDecimalChar(ch)) {
					charStr += ch;
					state = tsssIntOrFloat;
				} else if (ch == '.') {
					charStr += ch;
					state = tsssLonelyDecimalPoint;
				} else if (ch == 'i' && lastChar == '-') {
					AddToken(CaseInsensitiveFlag, startPos);
					state = tsssStart;
				} else
					throw new Err(std::string("Sniffer pattern error: incomplete signed number or invalid flag"), pos);
				break;

			case tsssLonelyFloatExtension:
				if (ch == '+' || ch == '-') {
					charStr += ch;
					state = tsssLonelyFloatExtensionWithSign;
				} else if (isDecimalChar(ch)) {
					charStr += ch;
					state = tsssExtendedFloat;
				} else
					throw new Err(std::string("Sniffer pattern error: incomplete extended-notation floating point number"), pos);
				break;

			case tsssLonelyFloatExtensionWithSign:
				if (isDecimalChar(ch)) {
					charStr += ch;
					state = tsssExtendedFloat;
				} else
					throw new Err(std::string("Sniffer pattern error: incomplete extended-notation floating point number"), pos);
				break;

			case tsssExtendedFloat:
				if (isDecimalChar(ch)) {
					charStr += ch;
					state = tsssExtendedFloat;
				} else {
					// Terminate the number
					AddFloat(charStr.c_str(), startPos);

					// Push the last char back on and try again
					stream.Unget();
					state = tsssStart;
				}
				break;

			case tsssUnquoted:
				if (ch == '\\') {
					escapedState = state;		// Save our state
					state = tsssEscape;			// Handle the escape sequence
				} else if (isWhiteSpace(ch) || isPunctuation(ch)) {
					AddString(charStr, startPos);
					stream.Unget();				// In case it's punctuation, let tsssStart handle it
					state = tsssStart;
				} else if (ch == 0x3 && stream.IsEmpty()) {
					AddString(charStr, startPos);
					keepLooping = false;
				} else {
					charStr += ch;
				}
				break;

			case tsssEscape:
				if (isOctalChar(ch)) {
					lastChar = ch;
					state = tsssEscapeOneOctal;
				} else if (ch == 'x') {
					state = tsssEscapeX;
				} else {
					// Check for a true end-of-text marker
					if (ch == 0x3 && stream.IsEmpty())
						throw new Err(std::string("Sniffer pattern error: incomplete escape sequence"), pos);
					else {
						charStr += escapeChar(ch);
						state = escapedState;	// Return to the state we were in before the escape
					}
				}
				break;

			case tsssEscapeX:
				if (isHexChar(ch)) {
					lastChar = ch;
					state = tsssEscapeOneHex;
				} else
					throw new Err(std::string("Sniffer pattern error: incomplete escaped hex code"), pos);
				break;

			case tsssEscapeOneOctal:
				if (isOctalChar(ch)) {
					lastLastChar = lastChar;
					lastChar = ch;
					state = tsssEscapeTwoOctal;
				} else {
					// First handle the octal
					try {
						charStr += octalToChar(lastChar);
					} catch (Err *err) {
						if (err)
							err->SetPos(startPos);
						throw err;
					}

					// Push the new char back on and let the state we
					// were in when the escape sequence was hit handle it.
					stream.Unget();
					state = escapedState;
				}
				break;

			case tsssEscapeTwoOctal:
				if (isOctalChar(ch)) {
					try {
						charStr += octalToChar(lastLastChar, lastChar, ch);
					} catch (Err *err) {
						if (err)
							err->SetPos(startPos);
						throw err;
					}
					state = escapedState;
				} else {
					// First handle the octal
					try {
						charStr += octalToChar(lastLastChar, lastChar);
					} catch (Err *err) {
						if (err)
							err->SetPos(startPos);
						throw err;
					}

					// Push the new char back on and let the state we
					// were in when the escape sequence was hit handle it.
					stream.Unget();
					state = escapedState;
				}
				break;

			case tsssEscapeOneHex:
				if (isHexChar(ch)) {
					try {
						charStr += hexToChar(lastChar, ch);
					} catch (Err *err) {
						if (err)
							err->SetPos(pos);
						throw err;
					}
					state = escapedState;
				} else
					throw new Err(std::string("Sniffer pattern error: incomplete escaped hex code"), pos);
				break;

		}
	}
	if (state == tsssStart)	{
		fCStatus = B_OK;
		fPos = 0;
	} else {
		throw new Err("Sniffer pattern error: unterminated rule", stream.Pos());
	}

	return fCStatus;
}

/**
 * @brief Clears the token list and resets the stream to an uninitialized state.
 */
void
TokenStream::Unset() {
	std::vector<Token*>::iterator i;
	for (i = fTokenList.begin(); i != fTokenList.end(); i++)
		delete *i;
	fTokenList.clear();
	fCStatus = B_NO_INIT;
	fStrLen = -1;
}

/**
 * @brief Returns the initialization status of the token stream.
 *
 * @return B_OK if the stream has been successfully tokenized, B_NO_INIT otherwise.
 */
status_t
TokenStream::InitCheck() const {
	return fCStatus;
}

/**
 * @brief Returns a pointer to the next token in the stream and advances the position.
 *
 * The TokenStream retains ownership of the returned Token. Throws a Sniffer::Err
 * if called on an uninitialized stream or when the stream is exhausted.
 *
 * @return A const pointer to the next Token in the stream.
 */
const Token*
TokenStream::Get() {
	if (fCStatus != B_OK)
		throw new Err("Sniffer parser error: TokenStream::Get() called on uninitialized TokenStream object", -1);
	if (fPos < (ssize_t)fTokenList.size())
		return fTokenList[fPos++];
	else {
		throw new Err("Sniffer pattern error: unterminated rule", EndPos());
//		fPos++;			// Increment fPos to keep Unget()s consistent
//		return NULL;	// Return NULL to signal end of list
	}
}

/**
 * @brief Places the most recently returned token back at the head of the stream.
 *
 * Throws a Sniffer::Err if called on an uninitialized stream or at the beginning.
 */
void
TokenStream::Unget() {
	if (fCStatus != B_OK)
		throw new Err("Sniffer parser error: TokenStream::Unget() called on uninitialized TokenStream object", -1);
	if (fPos > 0)
		fPos--;
	else
		throw new Err("Sniffer parser error: TokenStream::Unget() called at beginning of token stream", -1);
}

/**
 * @brief Reads and discards the next token, throwing if it is not of the expected type.
 *
 * @param type The expected TokenType of the next token in the stream.
 */
void
TokenStream::Read(TokenType type) {
	const Token *t = Get();
	if (t->Type() != type) {
		throw new Err((std::string("Sniffer pattern error: expected ") + tokenTypeToString(type)
	                + ", found " + tokenTypeToString(t->Type())).c_str(), t->Pos());
	}
}

/**
 * @brief Conditionally reads the next token if it matches the given type.
 *
 * Peeks at the next token; if it matches, consumes it and returns true.
 * Otherwise the token remains at the head of the stream and false is returned.
 *
 * @param type The TokenType to test for.
 * @return true if the token was consumed, false if the type did not match.
 */
bool
TokenStream::CondRead(TokenType type) {
	const Token *t = Get();
	if (t->Type() == type) {
		return true;
	} else {
		Unget();
		return false;
	}
}

/**
 * @brief Returns the stream position of the next unconsumed token.
 *
 * @return The byte offset of the next token, or the end-of-string position if empty.
 */
ssize_t
TokenStream::Pos() const {
	return fPos < (ssize_t)fTokenList.size() ? fTokenList[fPos]->Pos() : fStrLen;
}

/**
 * @brief Returns the position corresponding to the end of the input string.
 *
 * @return The length of the tokenized input string.
 */
ssize_t
TokenStream::EndPos() const {
	return fStrLen;
}

/**
 * @brief Returns true if the stream is uninitialized or has no more tokens.
 *
 * @return true if there are no more tokens to read, false otherwise.
 */
bool
TokenStream::IsEmpty() const {
	return fCStatus != B_OK || fPos >= (ssize_t)fTokenList.size();
}

/**
 * @brief Appends a punctuation or flag token of the given type to the token list.
 *
 * @param type The TokenType of the new token.
 * @param pos  The byte offset in the input string where this token was found.
 */
void
TokenStream::AddToken(TokenType type, ssize_t pos) {
	Token *token = new Token(type, pos);
	fTokenList.push_back(token);
}

/**
 * @brief Appends a string token to the token list.
 *
 * @param str The string content of the token.
 * @param pos The byte offset in the input string where this token was found.
 */
void
TokenStream::AddString(const std::string &str, ssize_t pos) {
	Token *token = new StringToken(str, pos);
	fTokenList.push_back(token);
}

/**
 * @brief Converts a numeric string to an integer and appends the token.
 *
 * @param str A null-terminated decimal string representation of the integer.
 * @param pos The byte offset in the input string where this token was found.
 */
void
TokenStream::AddInt(const char *str, ssize_t pos) {
	// Convert the string to an int
	int32 value = atol(str);
	Token *token = new IntToken(value, pos);
	fTokenList.push_back(token);
}

/**
 * @brief Converts a numeric string to a double and appends the token.
 *
 * @param str A null-terminated decimal or scientific-notation float string.
 * @param pos The byte offset in the input string where this token was found.
 */
void
TokenStream::AddFloat(const char *str, ssize_t pos) {
	// Convert the string to a float
	double value = atof(str);
	Token *token = new FloatToken(value, pos);
	fTokenList.push_back(token);
}

//------------------------------------------------------------------------------
// Helper functions
//------------------------------------------------------------------------------

/**
 * @brief Translates a single character following a backslash into its escaped value.
 *
 * Handles standard C escape sequences (\\a, \\b, \\f, \\n, \\r, \\t, \\v).
 * Hex and octal escapes are handled elsewhere; other characters are returned as-is.
 *
 * @param ch The character following the backslash.
 * @return The corresponding escape character.
 */
char
escapeChar(char ch) {
	// I've manually handled all the escape sequences I could come
	// up with, and for anything else I just return the character
	// passed in. Hex escapes are handled elsewhere, so \x just
	// returns 'x'. Similarly, octals are handled elsewhere, so \0
	// through \9 just return '0' through '9'.
	switch (ch) {
		case 'a':
			return '\a';
		case 'b':
			return '\b';
		case 'f':
			return '\f';
		case 'n':
			return '\n';
		case 'r':
			return '\r';
		case 't':
			return '\t';
		case 'v':
			return '\v';
		default:
			return ch;
	}
}

/**
 * @brief Converts a two-digit hex pair (0x|hi|low|) to a single byte.
 *
 * @param hi  The high nibble hex digit character.
 * @param low The low nibble hex digit character.
 * @return The byte value represented by the two hex digits.
 */
char
hexToChar(char hi, char low) {
	return (hexToChar(hi) << 4)	| hexToChar(low);
}

/**
 * @brief Converts a single hex digit character to its nibble value.
 *
 * Throws a Sniffer::Err if @p hex is not a valid hexadecimal digit.
 *
 * @param hex A hex digit character ('0'-'9', 'a'-'f', 'A'-'F').
 * @return The 4-bit nibble value (0-15).
 */
char
hexToChar(char hex) {
	if ('0' <= hex && hex <= '9')
		return hex-'0';
	else if ('a' <= hex && hex <= 'f')
		return hex-'a'+10;
	else if ('A' <= hex && hex <= 'F')
		return hex-'A'+10;
	else
		throw new Err(std::string("Sniffer parser error: invalid hex digit '") + hex + "' passed to hexToChar()", -1);
}

/**
 * @brief Converts a one-digit octal character to a byte value.
 *
 * @param octal A single octal digit ('0'-'7').
 * @return The corresponding byte value.
 */
char
octalToChar(char octal) {
	return octalToChar('0', '0', octal);
}

/**
 * @brief Converts a two-digit octal sequence to a byte value.
 *
 * @param hi  The high octal digit.
 * @param low The low octal digit.
 * @return The corresponding byte value.
 */
char
octalToChar(char hi, char low) {
	return octalToChar('0', hi, low);
}

/**
 * @brief Converts a three-digit octal sequence to a byte value.
 *
 * Throws a Sniffer::Err if any digit is not a valid octal character or if the
 * resulting value exceeds 255 (i.e., greater than octal 377).
 *
 * @param hi  The most significant octal digit.
 * @param mid The middle octal digit.
 * @param low The least significant octal digit.
 * @return The corresponding byte value (0-255).
 */
char
octalToChar(char hi, char mid, char low) {
	if (isOctalChar(hi) && isOctalChar(mid) && isOctalChar(low)) {
		// Check for octals >= decimal 256
		if ((hi-'0') <= 3)
			return ((hi-'0') << 6) | ((mid-'0') << 3) | (low-'0');
		else
			throw new Err("Sniffer pattern error: invalid octal literal (octals must be between octal 0 and octal 377 inclusive)", -1);
	} else
		throw new Err(std::string("Sniffer parser error: invalid octal digit passed to hexToChar()"), -1);
}

/**
 * @brief Returns true if the character is a valid hexadecimal digit.
 *
 * @param ch The character to test.
 * @return true if @p ch is '0'-'9', 'a'-'f', or 'A'-'F'.
 */
bool
isHexChar(char ch) {
	return ('0' <= ch && ch <= '9')
	         || ('a' <= ch && ch <= 'f')
	           || ('A' <= ch && ch <= 'F');
}

/**
 * @brief Returns true if the character is an ASCII whitespace character.
 *
 * @param ch The character to test.
 * @return true if @p ch is a space, newline, or horizontal tab.
 */
bool
isWhiteSpace(char ch) {
	return ch == ' ' || ch == '\n' || ch == '\t';
}

/**
 * @brief Returns true if the character is a valid octal digit.
 *
 * @param ch The character to test.
 * @return true if @p ch is '0'-'7'.
 */
bool
isOctalChar(char ch) {
	return ('0' <= ch && ch <= '7');
}

/**
 * @brief Returns true if the character is a decimal digit.
 *
 * @param ch The character to test.
 * @return true if @p ch is '0'-'9'.
 */
bool
isDecimalChar(char ch) {
	return ('0' <= ch && ch <= '9');
}

/**
 * @brief Returns true if the character is a sniffer rule punctuation token.
 *
 * @param ch The character to test.
 * @return true if @p ch is one of '&', '(', ')', ':', '[', ']', or '|'.
 */
bool
isPunctuation(char ch) {
	switch (ch) {
		case '&':
		case '(':
		case ')':
		case ':':
		case '[':
		case ']':
		case '|':
			return true;
		default:
			return false;
	}
}

/**
 * @brief Returns a human-readable string name for a TokenType value.
 *
 * @param type The TokenType to convert.
 * @return A static string naming the token type, or "UNKNOWN TOKEN TYPE".
 */
const char*
BPrivate::Storage::Sniffer::tokenTypeToString(TokenType type) {
	switch (type) {
		case LeftParen:
			return "LeftParen";
			break;
		case RightParen:
			return "RightParen";
			break;
		case LeftBracket:
			return "LeftBracket";
			break;
		case RightBracket:
			return "RightBracket";
			break;
		case Colon:
			return "Colon";
			break;
		case Divider:
			return "Divider";
			break;
		case Ampersand:
			return "Ampersand";
			break;
		case CaseInsensitiveFlag:
			return "CaseInsensitiveFlag";
			break;
		case CharacterString:
			return "CharacterString";
			break;
		case Integer:
			return "Integer";
			break;
		case FloatingPoint:
			return "FloatingPoint";
			break;
		default:
			return "UNKNOWN TOKEN TYPE";
			break;
	}
}

//------------------------------------------------------------------------------
// Parser
//------------------------------------------------------------------------------

/**
 * @brief Constructs a Parser, pre-allocating a sentinel out-of-memory error object.
 */
Parser::Parser()
	: fOutOfMemErr(new(std::nothrow) Err("Sniffer parser error: out of memory", -1))
{
}

/**
 * @brief Destroys the Parser and releases the out-of-memory sentinel.
 */
Parser::~Parser() {
	delete fOutOfMemErr;
}

/**
 * @brief Parses a sniffer rule string and populates the given Rule object.
 *
 * Tokenizes the input, then runs the recursive-descent grammar. On failure, a
 * descriptive error message is written to @p parseError (if non-NULL) and
 * B_BAD_MIME_SNIFFER_RULE is returned.
 *
 * @param rule       Null-terminated sniffer rule string to parse.
 * @param result     Pre-allocated Rule object to receive the parsed result.
 * @param parseError Optional BString to receive a human-readable error on failure.
 * @return B_OK on success, B_BAD_VALUE for NULL arguments, B_BAD_MIME_SNIFFER_RULE on error.
 */
status_t
Parser::Parse(const char *rule, Rule *result, BString *parseError) {
	try {
		if (!rule)
			throw new Err("Sniffer pattern error: NULL pattern", -1);
		if (!result)
			return B_BAD_VALUE;
		if (stream.SetTo(rule) != B_OK)
			throw new Err("Sniffer parser error: Unable to intialize token stream", -1);

		ParseRule(result);

		return B_OK;

	} catch (Err *err) {
//		cout << "Caught error" << endl;
		if (parseError)
			parseError->SetTo(ErrorMessage(err, rule).c_str());
		delete err;
		return rule ? (status_t)B_BAD_MIME_SNIFFER_RULE : (status_t)B_BAD_VALUE;
	}
}

/**
 * @brief Builds a formatted, pointer-annotated error message string.
 *
 * Reproduces the original rule string and places a caret (^) under the error
 * position, followed by the error description, suitable for monospaced display.
 *
 * @param err  The Err object containing message text and position.
 * @param rule The original rule string being parsed.
 * @return A formatted multi-line error string.
 */
std::string
Parser::ErrorMessage(Err *err, const char *rule) {
	const char* msg = (err && err->Msg())
    	                ? err->Msg()
    	                  : "Sniffer parser error: Unexpected error with no supplied error message";
    ssize_t pos = err && (err->Pos() >= 0) ? err->Pos() : 0;
    std::string str = std::string(rule ? rule : "") + "\n";
    for (int i = 0; i < pos; i++)
    	str += " ";
    str += "^    ";
    str += msg;
    return str;
}

/**
 * @brief Parses a complete sniffer rule (priority followed by conjunction list).
 *
 * Throws a Sniffer::Err if @p result is NULL or parsing fails.
 *
 * @param result Pre-allocated Rule object to populate.
 */
void
Parser::ParseRule(Rule *result) {
	if (!result)
		throw new Err("Sniffer parser error: NULL Rule object passed to Parser::ParseRule()", -1);

	// Priority
	double priority = ParsePriority();
	// Conjunction List
	std::vector<DisjList*>* list = ParseConjList();

	result->SetTo(priority, list);
}

/**
 * @brief Parses and returns the priority value from the token stream.
 *
 * Expects a floating-point or integer token in [0.0, 1.0].
 * Throws a Sniffer::Err if the token is absent or the value is out of range.
 *
 * @return The priority as a double in [0.0, 1.0].
 */
double
Parser::ParsePriority() {
	const Token *t = stream.Get();
	if (t->Type() == FloatingPoint || t->Type() == Integer) {
		double result = t->Float();
		if (0.0 <= result && result <= 1.0)
			return result;
		else {
//			cout << "(priority == " << result << ")" << endl;
			throw new Err("Sniffer pattern error: invalid priority", t->Pos());
		}
	} else
		throw new Err("Sniffer pattern error: match level expected", t->Pos());	// Same as R5
}

/**
 * @brief Parses a conjunction list (one or more DisjLists joined by implicit AND).
 *
 * Allocates a vector of DisjList pointers and calls ParseDisjList() repeatedly
 * until the stream is exhausted. Throws if no DisjList is found.
 *
 * @return A heap-allocated vector of DisjList pointers (caller takes ownership).
 */
std::vector<DisjList*>*
Parser::ParseConjList() {
	std::vector<DisjList*> *list = new(std::nothrow) std::vector<DisjList*>;
	if (!list)
		ThrowOutOfMemError(stream.Pos());
	try {
		// DisjList+
		int count = 0;
		while (true) {
			DisjList* expr = ParseDisjList();
			if (!expr)
				break;
			else {
				list->push_back(expr);
				count++;
			}
		}
		if (count == 0)
			throw new Err("Sniffer pattern error: missing expression", -1);
	} catch (...) {
		delete list;
		throw;
	}
	return list;
}

/**
 * @brief Parses a single disjunction list (PatternList or RPatternList).
 *
 * Peeks ahead to determine whether to call ParseRPatternList() or
 * ParsePatternList(). Returns NULL when the token stream is empty.
 *
 * @return A heap-allocated DisjList (PatternList or RPatternList), or NULL at end.
 */
DisjList*
Parser::ParseDisjList() {
	// If we've run out of tokens right now, it's okay, but
	// we need to let ParseConjList() know what's up
	if (stream.IsEmpty())
		return NULL;

	// Peek ahead, then let the appropriate Parse*List()
	// functions handle things
	const Token *t1 = stream.Get();

	// PatternList | RangeList
	if (t1->Type() == LeftParen) {
		const Token *t2 = stream.Get();
		// Skip the case-insensitive flag, if there is one
		const Token *tokenOfInterest = (t2->Type() == CaseInsensitiveFlag) ? stream.Get() : t2;
		if (t2 != tokenOfInterest)
			stream.Unget();	// We called Get() three times
		stream.Unget();
		stream.Unget();
		// RangeList
		if (tokenOfInterest->Type() == LeftBracket) {
			return ParseRPatternList();
		// PatternList
		} else {
			return ParsePatternList(Range(0,0));
		}
	// Range, PatternList
	} else if (t1->Type() == LeftBracket) {
		stream.Unget();
		return ParsePatternList(ParseRange());
	} else {
		throw new Err("Sniffer pattern error: missing pattern", t1->Pos());	// Same as R5
	}

	// PatternList
	// RangeList
	// Range + PatternList
}

/**
 * @brief Parses a byte-range expression of the form [start] or [start:end].
 *
 * @return A valid Range object; throws a Sniffer::Err on parse error.
 */
Range
Parser::ParseRange() {
	int32 start, end;
	// LeftBracket
	stream.Read(LeftBracket);
	// Integer
	{
		const Token *t = stream.Get();
		if (t->Type() == Integer) {
			start = t->Int();
			end = start;	// In case we aren't given an explicit end
		} else
			throw new Err("Sniffer pattern error: pattern offset expected", t->Pos());
	}
	// [Colon, Integer] RightBracket
	{
		const Token *t = stream.Get();
		// Colon, Integer, RightBracket
		if (t->Type() == Colon) {
			// Integer
			{
				const Token *t = stream.Get();
				if (t->Type() == Integer) {
					end = t->Int();
				} else
					ThrowUnexpectedTokenError(Integer, t);
			}
			// RightBracket
			stream.Read(RightBracket);
		// !(Colon, Integer) RightBracket
		} else if (t->Type() == RightBracket) {
			// Nothing to do here...

		// Something else...
		} else
			ThrowUnexpectedTokenError(Colon, Integer, t);
	}
	Range range(start, end);
	if (range.InitCheck() == B_OK)
		return range;
	else
		throw range.GetErr();
}

/**
 * @brief Parses a parenthesized list of patterns sharing a common range.
 *
 * Grammar: '(' [Flag] Pattern ('|' [Flag] Pattern)* ')'
 *
 * @param range The byte range over which all patterns in this list are searched.
 * @return A heap-allocated PatternList (caller takes ownership via DisjList*).
 */
DisjList*
Parser::ParsePatternList(Range range) {
	PatternList *list = new(std::nothrow) PatternList(range);
	if (!list)
		ThrowOutOfMemError(stream.Pos());
	try {
		// LeftParen
		stream.Read(LeftParen);
		// [Flag] Pattern, (Divider, [Flag] Pattern)*
		while (true) {
			// [Flag]
			if (stream.CondRead(CaseInsensitiveFlag))
				list->SetCaseInsensitive(true);
			// Pattern
			list->Add(ParsePattern());
			// [Divider]
			if (!stream.CondRead(Divider))
				break;
		}
		// RightParen
		const Token *t = stream.Get();
		if (t->Type() != RightParen)
			throw new Err("Sniffer pattern error: expecting '|', ')', or possibly '&'", t->Pos());
	} catch (...) {
		delete list;
		throw;
	}
	return list;
}

/**
 * @brief Parses a parenthesized list of ranged patterns (RPatterns).
 *
 * Grammar: '(' [Flag] RPattern ('|' [Flag] RPattern)* ')'
 *
 * @return A heap-allocated RPatternList (caller takes ownership via DisjList*).
 */
DisjList*
Parser::ParseRPatternList() {
	RPatternList *list = new(std::nothrow) RPatternList();
	if (!list)
		ThrowOutOfMemError(stream.Pos());
	try {
		// LeftParen
		stream.Read(LeftParen);
		// [Flag] RPattern, (Divider, [Flag] RPattern)*
		while (true) {
			// [Flag]
			if (stream.CondRead(CaseInsensitiveFlag))
				list->SetCaseInsensitive(true);
			// RPattern
			list->Add(ParseRPattern());
			// [Divider]
			if (!stream.CondRead(Divider))
				break;
		}
		// RightParen
		const Token *t = stream.Get();
		if (t->Type() != RightParen)
			throw new Err("Sniffer pattern error: expecting '|', ')', or possibly '&'", t->Pos());
	} catch (...) {
		delete list;
		throw;
	}
	return list;
}

/**
 * @brief Parses a single ranged pattern (a Range followed by a Pattern).
 *
 * @return A heap-allocated RPattern with the parsed range and pattern.
 */
RPattern*
Parser::ParseRPattern() {
	// Range
	Range range = ParseRange();
	// Pattern
	Pattern *pattern = ParsePattern();

	RPattern *result = new(std::nothrow) RPattern(range, pattern);
	if (result) {
		if (result->InitCheck() == B_OK)
			return result;
		else {
			Err *err = result->GetErr();
			delete result;
			throw err;
		}
	} else
		ThrowOutOfMemError(stream.Pos());
	return NULL;
}

/**
 * @brief Parses a single pattern (a string optionally followed by '&' and a mask string).
 *
 * @return A heap-allocated Pattern with the parsed byte string and optional mask.
 */
Pattern*
Parser::ParsePattern() {
	std::string str;
	// String
	{
		const Token *t = stream.Get();
		if (t->Type() == CharacterString)
			str = t->String();
		else
			throw new Err("Sniffer pattern error: missing pattern", t->Pos());
	}
	// [Ampersand, String]
	if (stream.CondRead(Ampersand)) {
		// String (i.e. Mask)
		const Token *t = stream.Get();
		if (t->Type() == CharacterString) {
			Pattern *result = new(std::nothrow) Pattern(str, t->String());
			if (!result)
				ThrowOutOfMemError(t->Pos());
			if (result->InitCheck() == B_OK) {
				return result;
			} else {
				Err *err = result->GetErr();
				delete result;
				if (err) {
					err->SetPos(t->Pos());
				}
				throw err;
			}
		} else
			ThrowUnexpectedTokenError(CharacterString, t);
	} else {
		// No mask specified.
		Pattern *result = new(std::nothrow) Pattern(str);
		if (result) {
			if (result->InitCheck() == B_OK)
				return result;
			else {
				Err *err = result->GetErr();
				delete result;
				throw err;
			}
		} else
			ThrowOutOfMemError(stream.Pos());
	}
	return NULL;
}

/**
 * @brief Throws a Sniffer::Err reporting an unexpected end of the token stream.
 */
void
Parser::ThrowEndOfStreamError() {
	throw new Err("Sniffer pattern error: unterminated rule", stream.EndPos());
}

/**
 * @brief Throws the pre-allocated out-of-memory Err at the given stream position.
 *
 * @param pos The stream position to attach to the out-of-memory error.
 */
inline
void
Parser::ThrowOutOfMemError(ssize_t pos) {
	if (fOutOfMemErr)
		fOutOfMemErr->SetPos(pos);
	Err *err = fOutOfMemErr;
	fOutOfMemErr = NULL;
	throw err;
}

/**
 * @brief Throws a Sniffer::Err reporting a single unexpected token type.
 *
 * @param expected The TokenType that was expected.
 * @param found    The Token that was actually found.
 */
void
Parser::ThrowUnexpectedTokenError(TokenType expected, const Token *found) {
	throw new Err((std::string("Sniffer pattern error: expected ") + tokenTypeToString(expected)
	                + ", found " + (found ? tokenTypeToString(found->Type()) : "NULL token")).c_str()
	                , (found ? found->Pos() : stream.EndPos()));
}

/**
 * @brief Throws a Sniffer::Err reporting that one of two expected token types was not found.
 *
 * @param expected1 The first acceptable TokenType.
 * @param expected2 The second acceptable TokenType.
 * @param found     The Token that was actually found.
 */
void
Parser::ThrowUnexpectedTokenError(TokenType expected1, TokenType expected2, const Token *found) {
	throw new Err((std::string("Sniffer pattern error: expected ") + tokenTypeToString(expected1)
	                + " or " + tokenTypeToString(expected2) + ", found "
	                + (found ? tokenTypeToString(found->Type()) : "NULL token")).c_str()
	                , (found ? found->Pos() : stream.EndPos()));
}
