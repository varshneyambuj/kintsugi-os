/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2006-2014 Haiku, Inc.
 * Original authors: Stephan Aßmus, Rene Gollent, John Scipione,
 *                   Ingo Weinhold.
 */

/** @file CLanguageTokenizer.h
    @brief Token enums, ParseException, Token struct and Tokenizer class
           used by the C/C++ expression evaluator and syntax highlighter. */

#ifndef C_LANGUAGE_TOKENIZER
#define C_LANGUAGE_TOKENIZER


#include <String.h>

#include <Variant.h>


namespace CLanguage {


/** @brief Token-type identifiers produced by the C/C++ expression
 *         tokeniser. */
enum {
	TOKEN_NONE					= 0,
	TOKEN_IDENTIFIER,
	TOKEN_CONSTANT,
	TOKEN_END_OF_LINE,

	TOKEN_PLUS,
	TOKEN_MINUS,

	TOKEN_STAR,
	TOKEN_SLASH,
	TOKEN_MODULO,

	TOKEN_OPENING_PAREN,
	TOKEN_CLOSING_PAREN,

	TOKEN_OPENING_SQUARE_BRACKET,
	TOKEN_CLOSING_SQUARE_BRACKET,

	TOKEN_OPENING_CURLY_BRACE,
	TOKEN_CLOSING_CURLY_BRACE,

	TOKEN_ASSIGN,
	TOKEN_LOGICAL_AND,
	TOKEN_LOGICAL_OR,
	TOKEN_LOGICAL_NOT,
	TOKEN_BITWISE_AND,
	TOKEN_BITWISE_OR,
	TOKEN_BITWISE_NOT,
	TOKEN_BITWISE_XOR,
	TOKEN_EQ,
	TOKEN_NE,
	TOKEN_GT,
	TOKEN_GE,
	TOKEN_LT,
	TOKEN_LE,

	TOKEN_BACKSLASH,
	TOKEN_CONDITION,
	TOKEN_COLON,
	TOKEN_SEMICOLON,
	TOKEN_COMMA,
	TOKEN_PERIOD,
	TOKEN_POUND,

	TOKEN_SINGLE_QUOTE,
	TOKEN_DOUBLE_QUOTE,

	TOKEN_STRING_LITERAL,
	TOKEN_BEGIN_COMMENT_BLOCK,
	TOKEN_END_COMMENT_BLOCK,
	TOKEN_INLINE_COMMENT,

	TOKEN_MEMBER_PTR
};


/**
 * @brief Diagnostic thrown by the tokeniser and the expression evaluator.
 *
 * Carries a human-readable message and the column position where the
 * problem was detected.
 */
class ParseException {
 public:
	ParseException(const char* message, int32 position)
		: message(message),
		  position(position)
	{
	}

	ParseException(const ParseException& other)
		: message(other.message),
		  position(other.position)
	{
	}

	BString	message;
	int32	position;
};


/**
 * @brief One unit produced by the tokeniser.
 *
 * Carries the source text slice, the @c TOKEN_* type code, an optional
 * BVariant value (for numeric constants) and the column position.
 */
struct Token {
								Token();
								Token(const Token& other);
								Token(const char* string, int32 length,
								int32 position, int32 type);
			Token& 	operator=(const Token& other);

	BString						string;
	int32						type;
	BVariant					value;
	int32						position;
};


/**
 * @brief Hand-rolled lexer used by the debugger's C/C++ expression engine.
 *
 * Stream-style: SetTo() binds an input string, NextToken() advances and
 * returns the next token, RewindToken() makes the next call re-emit the
 * cached token.
 */
class Tokenizer {
public:
								Tokenizer();

			void 				SetTo(const char* string);

			const Token& 		NextToken();
			void 				RewindToken();
private:
			bool 				_ParseOperator();
 			char 				_Peek() const;

	static 	bool 				_IsHexDigit(char c);

			Token& 				_ParseHexOperand();
			int32 				_CurrentPos() const;

private:
	BString						fString;
	const char*					fCurrentChar;
	Token						fCurrentToken;
	bool						fReuseToken;
};


}	// namespace CLanguage


#endif	// C_LANGUAGE_TOKENIZER
