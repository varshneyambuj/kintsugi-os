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
 *   Copyright 2008-2010, Ingo Weinhold, ingo_weinhold@gmx.de
 *   Copyright 2006, Stephan Aßmus, superstippi@gmx.de
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file debug_parser.cpp
 * @brief Expression and command-line parser for the in-kernel debugger (KDL).
 *
 * Implements the tokenizer and recursive-descent parser that KDL uses to
 * evaluate arithmetic expressions, dereferences (`*addr`, `*{size}addr`),
 * variable lookups (see debug_variables.cpp) and assignments, plus to parse
 * command pipes (`cmd | cmd`) and command sequences (`cmd ; cmd`). All public
 * entry points (evaluate_debug_expression(), evaluate_debug_command(),
 * parse_next_debug_command_argument()) run under setjmp/longjmp exception
 * handling so that malformed input in the debugger context aborts cleanly
 * with a diagnostic instead of corrupting parse state.
 */


#include <debug.h>

#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <KernelExport.h>

#include <debug_heap.h>

#include "debug_commands.h"
#include "debug_variables.h"


/*
	Grammar:

	commandLine	:= ( commandPipe [ ";" commandLine  ] ) | assignment
	expression	:= term | assignment
	assignment	:= lhs ( "=" | "+=" | "-=" | "*=" | "/=" | "%=" )
				   expression
	lhs			:= variable | dereference
	term		:= sum
	sum			:= product ( ( "+" | "-" ) product )*
	product		:= unary ( ( "*" | "/" | "%" ) unary )*
	unary		:= atom | ( "-"  unary ) | dereference
	dereference	:= "*" [ "{" expression "}" ] unary
	atom		:= variable | ( "(" expression ")" ) | ( "[" command "]" )
	variable	:= identifier
	identifier	:= ( "$" | "@" | "_" | "a" - "z" | "A" - "Z" )
				   ( "_" | "a" - "z" | "A" - "Z" | "0" - "9" )*
	commandPipe	:= command ( "|" command )*
	command		:= identifier argument*
	argument	:= ( "(" expression ")" ) | ( "[" commandLine "]" )
				   | unquotedString | quotedString
*/


static const int kMaxTokenLength = 128;
static const int kJumpBufferCount = 10;

static const int kMaxArgumentCount = 64;

static jmp_buf sJumpBuffers[kJumpBufferCount];
static int sNextJumpBufferIndex = 0;

static char sExceptionMessage[128];
static int	sExceptionPosition;

static char sTempBuffer[128];
	// for composing debug output etc.

enum {
	TOKEN_ASSIGN_FLAG			= 0x100,
	TOKEN_FLAGS					= TOKEN_ASSIGN_FLAG,

	TOKEN_IDENTIFIER			= 'a',

	TOKEN_CONSTANT				= '0',

	TOKEN_PLUS					= '+',
	TOKEN_MINUS					= '-',

	TOKEN_STAR					= '*',
	TOKEN_SLASH					= '/',
	TOKEN_MODULO				= '%',

	TOKEN_ASSIGN				= '='			| TOKEN_ASSIGN_FLAG,
	TOKEN_PLUS_ASSIGN			= TOKEN_PLUS	| TOKEN_ASSIGN_FLAG,
	TOKEN_MINUS_ASSIGN			= TOKEN_MINUS	| TOKEN_ASSIGN_FLAG,
	TOKEN_STAR_ASSIGN			= TOKEN_STAR	| TOKEN_ASSIGN_FLAG,
	TOKEN_SLASH_ASSIGN			= TOKEN_SLASH	| TOKEN_ASSIGN_FLAG,
	TOKEN_MODULO_ASSIGN			= TOKEN_MODULO	| TOKEN_ASSIGN_FLAG,

	TOKEN_OPENING_PARENTHESIS	= '(',
	TOKEN_CLOSING_PARENTHESIS	= ')',
	TOKEN_OPENING_BRACKET		= '[',
	TOKEN_CLOSING_BRACKET		= ']',
	TOKEN_OPENING_BRACE			= '{',
	TOKEN_CLOSING_BRACE			= '}',

	TOKEN_PIPE					= '|',
	TOKEN_SEMICOLON				= ';',

	TOKEN_STRING				= '"',
	TOKEN_UNKNOWN				= '?',
	TOKEN_NONE					= ' ',
	TOKEN_END_OF_LINE			= '\n',
};

struct Token {
	char	string[kMaxTokenLength];
	uint64	value;
	int32	type;
	int32	position;

	/**
	 * @brief Populate this Token with text, type and source position.
	 * @param string Pointer to the token text (not required to be NUL-terminated).
	 * @param length Number of characters to copy; clamped to the internal buffer.
	 * @param position Byte offset of the token in the source expression.
	 * @param type Token type tag (see the TOKEN_* enum).
	 */
	void SetTo(const char* string, int32 length, int32 position, int32 type)
	{
		length = min_c((size_t)length, (sizeof(this->string) - 1));
		strlcpy(this->string, string, length + 1);
		this->type = type;
		this->value = 0;
		this->position = position;
	}

	/**
	 * @brief Reset the token to an empty TOKEN_NONE state.
	 */
	void Unset()
	{
		string[0] = '\0';
		value = 0;
		type = TOKEN_NONE;
		position = 0;
	}
};


// #pragma mark - exceptions


/**
 * @brief Raise a parse error by longjmp'ing out of the current parser frame.
 * @param message Human-readable error text (copied into sExceptionMessage).
 * @param position Byte offset in the input where the error was detected,
 *        or -1 if no position is available.
 */
static void
parse_exception(const char* message, int32 position)
{
	if (sNextJumpBufferIndex == 0) {
		kprintf_unfiltered("parse_exception(): No jump buffer!\n");
		kprintf_unfiltered("exception: \"%s\", position: %" B_PRId32 "\n",
			message, position);
		return;
	}

	strlcpy(sExceptionMessage, message, sizeof(sExceptionMessage));
	sExceptionPosition = position;

	longjmp(sJumpBuffers[sNextJumpBufferIndex - 1], 1);
}


/**
 * @brief Allocate from the debug heap, raising a parse exception on failure.
 * @param size Number of bytes to allocate.
 * @return Pointer to the allocation; never NULL on normal return.
 */
static void*
checked_malloc(size_t size)
{
	void* address = debug_malloc(size);
	if (address == NULL) {
		parse_exception("out of memory for command execution", -1);
		return NULL;
	}

	return address;
}


// #pragma mark - Tokenizer


class Tokenizer {
public:
	/**
	 * @brief Construct a tokenizer bound to an input string.
	 * @param string The source text to tokenize; must outlive the tokenizer.
	 */
	Tokenizer(const char* string)
		: fCommandMode(false)
	{
		SetTo(string);
	}

	/**
	 * @brief Rebind the tokenizer to a new input string and reset parse state.
	 * @param string New source text; must outlive subsequent token accesses.
	 */
	void SetTo(const char* string)
	{
		fString = fCurrentChar = string;
		fCurrentToken.Unset();
		fReuseToken = false;
	}

	/**
	 * @brief Seek the tokenizer to an absolute byte position in the input.
	 * @param position Offset from the start of the input string.
	 */
	void SetPosition(int32 position)
	{
		fCurrentChar = fString + position;
		fCurrentToken.Unset();
		fReuseToken = false;
	}

	/**
	 * @brief Switch between expression-token and command-argument token modes.
	 *
	 * The two modes classify punctuation differently (e.g. `|` is a pipe token
	 * in command mode but part of an unquoted string in expression mode).
	 *
	 * @param commandMode true to enter command mode, false for expression mode.
	 */
	void SetCommandMode(bool commandMode)
	{
		if (fCommandMode == commandMode)
			return;

		fCommandMode = commandMode;

		if (fReuseToken) {
			// We can't reuse the token, since the parsing mode changed.
			SetPosition(fCurrentToken.position);
		}
	}

	/**
	 * @brief Return the full input string the tokenizer is operating on.
	 * @return Pointer to the source string.
	 */
	const char* String() const
	{
		return fString;
	}

	/**
	 * @brief Advance to and return the next token, honouring rewind requests.
	 * @return Reference to the current token after advancing.
	 */
	const Token& NextToken()
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
			fCurrentToken.SetTo("", 0, _CurrentPos(), TOKEN_END_OF_LINE);
			return fCurrentToken;
		}

		return (fCommandMode ? _NextTokenCommand() : _NextTokenExpression());
	}

	/**
	 * @brief Return the most recently scanned token without advancing.
	 * @return Reference to the current token.
	 */
	const Token& CurrentToken() const
	{
		return fCurrentToken;
	}

	/**
	 * @brief Mark the current token so the next NextToken() returns it again.
	 */
	void RewindToken()
	{
		fReuseToken = true;
	}

 private:
	/**
	 * @brief Scan the next token using expression-mode lexical rules.
	 * @return Reference to the newly scanned current token.
	 */
	const Token& _NextTokenExpression()
	{
		if (isdigit(*fCurrentChar)) {
			// number
			const char* begin = fCurrentChar++;

			if (*fCurrentChar == 'x') {
				// hex number
				fCurrentChar++;
				while (*fCurrentChar != 0
					&& (isdigit(*fCurrentChar)
						|| strchr("abcdefABCDEF", *fCurrentChar))) {
					fCurrentChar++;
				}

				if (fCurrentChar - begin == 2)
					parse_exception("invalid hex number", begin - fString);

			} else {
				// decimal number
				while (*fCurrentChar != 0 && isdigit(*fCurrentChar))
					fCurrentChar++;
			}

			int32 length = fCurrentChar - begin;
			fCurrentToken.SetTo(begin, length, _CurrentPos() - length,
				TOKEN_CONSTANT);
			fCurrentToken.value = strtoull(fCurrentToken.string, NULL, 0);

		} else if (isalpha(*fCurrentChar) || *fCurrentChar == '_'
				|| *fCurrentChar == '$' || *fCurrentChar == '@') {
			// identifier
			const char* begin = fCurrentChar;
			fCurrentChar++;
			while (*fCurrentChar != 0
				&& (isalpha(*fCurrentChar) || *fCurrentChar == '_'
					|| isdigit(*fCurrentChar))) {
				fCurrentChar++;
			}

			int32 length = fCurrentChar - begin;
			fCurrentToken.SetTo(begin, length, _CurrentPos() - length,
				TOKEN_IDENTIFIER);

		} else {
			const char* begin = fCurrentChar;
			char c = *fCurrentChar;
			fCurrentChar++;
			int32 flags = 0;

			switch (c) {
				case '=':
					fCurrentChar--;
				case '+':
				case '-':
				case '*':
				case '/':
				case '%':
					if (*fCurrentChar == '=') {
						fCurrentChar++;
						flags = TOKEN_ASSIGN_FLAG;
					}

				case '(':
				case ')':
				case '[':
				case ']':
				case '{':
				case '}':
				case ';':
				{
					int32 length = fCurrentChar - begin;
					fCurrentToken.SetTo(begin, length, _CurrentPos() - length,
						c | flags);
					break;
				}

				case '"':
				{
					fCurrentChar--;
					_QuotedString();
					break;
				}

				default:
				{
					fCurrentChar--;
					_UnquotedString();
					break;
				}
			}
		}

		return fCurrentToken;
	}

	/**
	 * @brief Scan the next token using command-mode lexical rules.
	 * @return Reference to the newly scanned current token.
	 */
	const Token& _NextTokenCommand()
	{
		switch (*fCurrentChar) {
			case '(':
			case ')':
			case '[':
			case ']':
			case '|':
			case ';':
				fCurrentToken.SetTo(fCurrentChar, 1, _CurrentPos(),
					*fCurrentChar);
				fCurrentChar++;
				return fCurrentToken;
			case '"':
				return _QuotedString();

			default:
				return _UnquotedString();
		}
	}

	/**
	 * @brief Scan a double-quoted string token, handling backslash escapes.
	 * @return Reference to the current token populated as TOKEN_STRING.
	 */
	const Token& _QuotedString()
	{
		const char* begin = fCurrentChar++;
		int32 length = 0;

		while (*fCurrentChar != '\0' && *fCurrentChar != '"') {
			char c = *fCurrentChar;
			fCurrentChar++;

			if (c == '\\') {
				// an escaped char
				c = *fCurrentChar;
				fCurrentChar++;

				if (c == '\0')
					break;
			}

			if ((size_t)length
					>= sizeof(fCurrentToken.string) - 1) {
				parse_exception("quoted string too long", begin - fString);
			}

			fCurrentToken.string[length++] = c;
		}

		if (*fCurrentChar == '\0') {
			parse_exception("unexpected end of line while "
				"parsing quoted string", begin - fString);
		}

		fCurrentChar++;

		fCurrentToken.string[length] = '\0';
		fCurrentToken.value = 0;
		fCurrentToken.type = TOKEN_STRING;
		fCurrentToken.position = begin - fString;

		return fCurrentToken;
	}

	/**
	 * @brief Scan an unquoted string token, terminating at a delimiter.
	 * @return Reference to the current token populated as TOKEN_UNKNOWN.
	 */
	const Token& _UnquotedString()
	{
		const char* begin = fCurrentChar;

		while (*fCurrentChar != 0 && !_IsUnquotedDelimitingChar(*fCurrentChar))
			fCurrentChar++;

		int32 length = fCurrentChar - begin;
		fCurrentToken.SetTo(begin, length, _CurrentPos() - length,
			TOKEN_UNKNOWN);

		return fCurrentToken;
	}

	/**
	 * @brief Decide whether a character ends an unquoted string in the current mode.
	 * @param c Character to classify.
	 * @return true if c terminates an unquoted string, false otherwise.
	 */
	bool _IsUnquotedDelimitingChar(char c)
	{
		if (isspace(c))
			return true;

		switch (c) {
			case '(':
			case ')':
			case '[':
			case ']':
			case '"':
				return true;

			case '|':	// TODO: Move when we support & and | in expressions.
			case ';':
				return fCommandMode;

			case '{':
			case '}':
			case '=':
			case '+':
			case '-':
			case '*':
			case '/':
			case '%':
				return !fCommandMode;

			default:
				return false;
		}
	}

	/**
	 * @brief Compute the scanner's byte offset within the input string.
	 * @return Offset from the start of the input.
	 */
	int32 _CurrentPos() const
	{
		return fCurrentChar - fString;
	}

private:
	const char*	fString;
	const char*	fCurrentChar;
	Token		fCurrentToken;
	bool		fReuseToken;
	bool		fCommandMode;
};


// #pragma mark - ExpressionParser


class ExpressionParser {
 public:
								ExpressionParser();
								~ExpressionParser();

			uint64				EvaluateExpression(
									const char* expressionString);
			uint64				EvaluateCommand(
									const char* expressionString,
									int& returnCode);
			status_t			ParseNextCommandArgument(
									const char** expressionString, char* buffer,
									size_t bufferSize);

 private:
			uint64				_ParseExpression(bool expectAssignment = false);
			uint64				_ParseCommandPipe(int& returnCode);
			void				_ParseCommand(
									debugger_command_pipe_segment& segment);
			bool				_ParseArgument(int& argc, char** argv);
			void				_GetUnparsedArgument(int& argc, char** argv);
			void				_AddArgument(int& argc, char** argv,
									const char* argument, int32 length = -1);
			uint64				_ParseSum(bool useValue, uint64 value);
			uint64				_ParseProduct();
			uint64				_ParsePower();
			uint64				_ParseUnary();
			uint64				_ParseDereference(void** _address,
									uint32* _size);
			uint64				_ParseAtom();

			const Token&		_EatToken(int32 type);

			Tokenizer			fTokenizer;
};


/**
 * @brief Construct an ExpressionParser with an empty tokenizer.
 */
ExpressionParser::ExpressionParser()
	: fTokenizer("")
{
}


/**
 * @brief Destroy the ExpressionParser; owns no external resources.
 */
ExpressionParser::~ExpressionParser()
{
}


/**
 * @brief Evaluate a debugger expression and return its numeric value.
 * @param expressionString NUL-terminated expression in KDL syntax.
 * @return The 64-bit value produced by the expression.
 */
uint64
ExpressionParser::EvaluateExpression(const char* expressionString)
{
	fTokenizer.SetTo(expressionString);

	uint64 value = _ParseExpression();
	const Token& token = fTokenizer.NextToken();
	if (token.type != TOKEN_END_OF_LINE)
		parse_exception("parse error", token.position);

	return value;
}


/**
 * @brief Parse and execute a debugger command line (possibly a pipe/sequence).
 *
 * Accepts a command, an assignment, or a `;`-separated chain of either. Each
 * segment is parsed in the appropriate (command or expression) mode.
 *
 * @param expressionString NUL-terminated command line.
 * @param[out] returnCode Receives the return code of the last executed command,
 *        or zero if the last operation was an assignment.
 * @return The numeric result of the last segment (typically the `_` variable).
 */
uint64
ExpressionParser::EvaluateCommand(const char* expressionString, int& returnCode)
{
	fTokenizer.SetTo(expressionString);

	// Allowed are command or assignment. A command always starts with an
	// identifier, an assignment either with an identifier (variable name) or
	// a dereferenced address.
	const Token& token = fTokenizer.NextToken();
	uint64 value = 0;

	while (true) {
		int32 startPosition = token.position;

		if (token.type == TOKEN_IDENTIFIER) {
			fTokenizer.NextToken();

			if (token.type & TOKEN_ASSIGN_FLAG) {
				// an assignment
				fTokenizer.SetPosition(startPosition);
				value =  _ParseExpression(true);
				returnCode = 0;
			} else {
				// no assignment, so let's assume it's a command
				fTokenizer.SetPosition(startPosition);
				fTokenizer.SetCommandMode(true);
				value = _ParseCommandPipe(returnCode);
			}
		} else if (token.type == TOKEN_STAR) {
			// dereferenced address -- assignment
			fTokenizer.SetPosition(startPosition);
			value =  _ParseExpression(true);
			returnCode = 0;
		} else
			parse_exception("expected command or assignment", token.position);

		// might be chained with ";"
		if (fTokenizer.NextToken().type != TOKEN_SEMICOLON)
			break;

		fTokenizer.SetCommandMode(false);
		fTokenizer.NextToken();
	}

	if (token.type != TOKEN_END_OF_LINE)
		parse_exception("parse error", token.position);

	return value;
}


/**
 * @brief Extract the next whitespace-delimited argument from a command line.
 *
 * Used by callers that want to parse arguments manually after the command has
 * been dispatched. Advances *expressionString past the consumed argument.
 *
 * @param[in,out] expressionString On entry the remaining text; on return either
 *        the position of the next unconsumed character or NULL at end-of-line.
 * @param[out] buffer Destination for the argument text.
 * @param bufferSize Size of @p buffer in bytes (including space for NUL).
 * @return B_OK on success, B_ENTRY_NOT_FOUND when no argument remains, or
 *         B_BAD_VALUE on parse error.
 */
status_t
ExpressionParser::ParseNextCommandArgument(const char** expressionString,
	char* buffer, size_t bufferSize)
{
	fTokenizer.SetTo(*expressionString);
	fTokenizer.SetCommandMode(true);

	if (fTokenizer.NextToken().type == TOKEN_END_OF_LINE)
		return B_ENTRY_NOT_FOUND;

	fTokenizer.RewindToken();

	char* argv[2];
	int argc = 0;
	if (!_ParseArgument(argc, argv))
		return B_BAD_VALUE;

	strlcpy(buffer, argv[0], bufferSize);

	const Token& token = fTokenizer.NextToken();
	if (token.type == TOKEN_END_OF_LINE)
		*expressionString = NULL;
	else
		*expressionString += token.position;

	return B_OK;
}


/**
 * @brief Parse an expression or assignment and return its value.
 *
 * Handles plain assignments to variables, compound assignments (`+=`, `-=`,
 * `*=`, `/=`, `%=`), assignments through a dereferenced address, and falls
 * through to _ParseSum() for plain expressions.
 *
 * @param expectAssignment If true, a non-assignment input raises a parse error.
 * @return The evaluated 64-bit result.
 */
uint64
ExpressionParser::_ParseExpression(bool expectAssignment)
{
	const Token& token = fTokenizer.NextToken();
	int32 position = token.position;
	if (token.type == TOKEN_IDENTIFIER) {
		char variable[MAX_DEBUG_VARIABLE_NAME_LEN];
		strlcpy(variable, token.string, sizeof(variable));

		int32 assignmentType = fTokenizer.NextToken().type;
		if (assignmentType & TOKEN_ASSIGN_FLAG) {
			// an assignment
			uint64 rhs = _ParseExpression();

			// handle the standard assignment separately -- the other kinds
			// need the variable to be defined
			if (assignmentType == TOKEN_ASSIGN) {
				if (!set_debug_variable(variable, rhs)) {
					snprintf(sTempBuffer, sizeof(sTempBuffer),
						"failed to set value for variable \"%s\"",
						variable);
					parse_exception(sTempBuffer, position);
				}

				return rhs;
			}

			// variable must be defined
			if (!is_debug_variable_defined(variable)) {
				snprintf(sTempBuffer, sizeof(sTempBuffer),
					"variable \"%s\" not defined in modifying assignment",
					variable);
				parse_exception(sTempBuffer, position);
			}

			uint64 variableValue = get_debug_variable(variable, 0);

			// check for division by zero for the respective assignment types
			if ((assignmentType == TOKEN_SLASH_ASSIGN
					|| assignmentType == TOKEN_MODULO_ASSIGN)
				&& rhs == 0) {
				parse_exception("division by zero", position);
			}

			// compute the new variable value
			switch (assignmentType) {
				case TOKEN_PLUS_ASSIGN:
					variableValue += rhs;
					break;
				case TOKEN_MINUS_ASSIGN:
					variableValue -= rhs;
					break;
				case TOKEN_STAR_ASSIGN:
					variableValue *= rhs;
					break;
				case TOKEN_SLASH_ASSIGN:
					variableValue /= rhs;
					break;
				case TOKEN_MODULO_ASSIGN:
					variableValue %= rhs;
					break;
				default:
					parse_exception("internal error: unknown assignment token",
						position);
					break;
			}

			set_debug_variable(variable, variableValue);
			return variableValue;
		}
	} else if (token.type == TOKEN_STAR) {
		void* address;
		uint32 size;
		uint64 value = _ParseDereference(&address, &size);

		int32 assignmentType = fTokenizer.NextToken().type;
		if (assignmentType & TOKEN_ASSIGN_FLAG) {
			// an assignment
			uint64 rhs = _ParseExpression();

			// check for division by zero for the respective assignment types
			if ((assignmentType == TOKEN_SLASH_ASSIGN
					|| assignmentType == TOKEN_MODULO_ASSIGN)
				&& rhs == 0) {
				parse_exception("division by zero", position);
			}

			// compute the new value
			switch (assignmentType) {
				case TOKEN_ASSIGN:
					value = rhs;
					break;
				case TOKEN_PLUS_ASSIGN:
					value += rhs;
					break;
				case TOKEN_MINUS_ASSIGN:
					value -= rhs;
					break;
				case TOKEN_STAR_ASSIGN:
					value *= rhs;
					break;
				case TOKEN_SLASH_ASSIGN:
					value /= rhs;
					break;
				case TOKEN_MODULO_ASSIGN:
					value %= rhs;
					break;
				default:
					parse_exception("internal error: unknown assignment token",
						position);
					break;
			}

			// convert the value for writing to the address
			uint64 buffer = 0;
			switch (size) {
				case 1:
					*(uint8*)&buffer = value;
					break;
				case 2:
					*(uint16*)&buffer = value;
					break;
				case 4:
					*(uint32*)&buffer = value;
					break;
				case 8:
					value = buffer;
					break;
			}

			if (debug_memcpy(B_CURRENT_TEAM, address, &buffer, size) != B_OK) {
				snprintf(sTempBuffer, sizeof(sTempBuffer),
					"failed to write to address %p", address);
				parse_exception(sTempBuffer, position);
			}

			return value;
		}
	}

	if (expectAssignment) {
		parse_exception("expected assignment",
			fTokenizer.CurrentToken().position);
	}

	// no assignment -- reset to the identifier position and parse a sum
	fTokenizer.SetPosition(position);
	return _ParseSum(false, 0);
}


/**
 * @brief Parse and invoke a pipeline of `|`-separated debugger commands.
 *
 * Allocates a debugger_command_pipe on the debug heap, populates each segment
 * by calling _ParseCommand(), then invokes the pipe.
 *
 * @param[out] returnCode Receives the pipe invocation's return code.
 * @return The value of the `_` debug variable after the pipe completes.
 */
uint64
ExpressionParser::_ParseCommandPipe(int& returnCode)
{
	debugger_command_pipe* pipe = (debugger_command_pipe*)checked_malloc(
		sizeof(debugger_command_pipe));

	pipe->segment_count = 0;
	pipe->broken = false;

	do {
		if (pipe->segment_count >= MAX_DEBUGGER_COMMAND_PIPE_LENGTH)
			parse_exception("Pipe too long", fTokenizer.NextToken().position);

		debugger_command_pipe_segment& segment
			= pipe->segments[pipe->segment_count];
		segment.index = pipe->segment_count++;

		_ParseCommand(segment);

	} while (fTokenizer.NextToken().type == TOKEN_PIPE);

	fTokenizer.RewindToken();

	// invoke the pipe
	returnCode = invoke_debugger_command_pipe(pipe);

	debug_free(pipe);

	return get_debug_variable("_", 0);
}


/**
 * @brief Parse one command and its argument vector into a pipe segment.
 *
 * Looks up the command by name, then collects arguments according to whether
 * the command has the B_KDEBUG_DONT_PARSE_ARGUMENTS flag.
 *
 * @param[out] segment Segment to populate with the resolved command/argv.
 */
void
ExpressionParser::_ParseCommand(debugger_command_pipe_segment& segment)
{
	fTokenizer.SetCommandMode(false);
	const Token& token = _EatToken(TOKEN_IDENTIFIER);
	fTokenizer.SetCommandMode(true);

	bool ambiguous;
	debugger_command* command = find_debugger_command(token.string, true,
		ambiguous);

	if (command == NULL) {
		if (ambiguous) {
			snprintf(sTempBuffer, sizeof(sTempBuffer),
				"Ambiguous command \"%s\". Use tab completion or enter "
				"\"help %s\" get a list of matching commands.\n", token.string,
				token.string);
		} else {
			snprintf(sTempBuffer, sizeof(sTempBuffer),
				"Unknown command \"%s\". Enter \"help\" to get a list of "
				"all supported commands.\n", token.string);
		}

		parse_exception(sTempBuffer, -1);
	}

	// allocate temporary buffer for the argument vector
	char** argv = (char**)checked_malloc(kMaxArgumentCount * sizeof(char*));
	int argc = 0;
	argv[argc++] = (char*)command->name;

	// get the arguments
	if ((command->flags & B_KDEBUG_DONT_PARSE_ARGUMENTS) != 0) {
		_GetUnparsedArgument(argc, argv);
	} else {
		while (fTokenizer.NextToken().type != TOKEN_END_OF_LINE) {
			fTokenizer.RewindToken();
			if (!_ParseArgument(argc, argv))
				break;
		}
	}

	if (segment.index > 0) {
		if (argc >= kMaxArgumentCount)
			parse_exception("too many arguments for command", 0);
		else
			argc++;
	}

	segment.command = command;
	segment.argc = argc;
	segment.argv = argv;
	segment.invocations = 0;
}


/**
 * @brief Parse one command argument and append it to the argv array.
 *
 * Arguments may be parenthesised expressions, bracketed sub-commands, quoted
 * strings or unquoted strings. Closing brackets/pipes/semicolons are left for
 * the caller and return false.
 *
 * @param[in,out] argc Current argument count; incremented on success.
 * @param[in,out] argv Argument vector (entries are debug_malloc'ed copies).
 * @return true if an argument was consumed, false if a terminator was seen.
 */
bool
ExpressionParser::_ParseArgument(int& argc, char** argv)
{
	const Token& token = fTokenizer.NextToken();
	switch (token.type) {
		case TOKEN_OPENING_PARENTHESIS:
		{
			// this starts an expression
			fTokenizer.SetCommandMode(false);
			uint64 value = _ParseExpression();
			fTokenizer.SetCommandMode(true);
			_EatToken(TOKEN_CLOSING_PARENTHESIS);

			snprintf(sTempBuffer, sizeof(sTempBuffer), "%" B_PRIu64, value);
			_AddArgument(argc, argv, sTempBuffer);
			return true;
		}

		case TOKEN_OPENING_BRACKET:
		{
			// this starts a sub command
			int returnValue;
			uint64 value = _ParseCommandPipe(returnValue);
			_EatToken(TOKEN_CLOSING_BRACKET);

			snprintf(sTempBuffer, sizeof(sTempBuffer), "%" B_PRIu64, value);
			_AddArgument(argc, argv, sTempBuffer);
			return true;
		}

		case TOKEN_STRING:
		case TOKEN_UNKNOWN:
			_AddArgument(argc, argv, token.string);
			return true;

		case TOKEN_CLOSING_PARENTHESIS:
		case TOKEN_CLOSING_BRACKET:
		case TOKEN_PIPE:
		case TOKEN_SEMICOLON:
			// those don't belong to us
			fTokenizer.RewindToken();
			return false;

		default:
		{
			snprintf(sTempBuffer, sizeof(sTempBuffer), "unexpected token "
				"\"%s\"", token.string);
			parse_exception(sTempBuffer, token.position);
			return false;
		}
	}
}


/**
 * @brief Capture the remainder of the line verbatim as a single argument.
 *
 * Used for commands flagged B_KDEBUG_DONT_PARSE_ARGUMENTS: consumes tokens
 * while balancing `()` and `[]` until end-of-line, `|` or `;` at nesting 0.
 *
 * @param[in,out] argc Current argument count; incremented if a non-blank
 *        argument is added.
 * @param[in,out] argv Argument vector to append into.
 */
void
ExpressionParser::_GetUnparsedArgument(int& argc, char** argv)
{
	int32 startPosition = fTokenizer.NextToken().position;
	fTokenizer.RewindToken();

	// match parentheses and brackets, but otherwise skip all tokens
	int32 parentheses = 0;
	int32 brackets = 0;
	bool done = false;
	while (!done) {
		const Token& token = fTokenizer.NextToken();
		switch (token.type) {
			case TOKEN_OPENING_PARENTHESIS:
				parentheses++;
				break;
			case TOKEN_OPENING_BRACKET:
				brackets++;
				break;
			case TOKEN_CLOSING_PARENTHESIS:
				if (parentheses > 0)
					parentheses--;
				else
					done = true;
				break;
			case TOKEN_CLOSING_BRACKET:
				if (brackets > 0)
					brackets--;
				else
					done = true;
				break;
			case TOKEN_PIPE:
			case TOKEN_SEMICOLON:
				if (parentheses == 0 && brackets == 0)
					done = true;
				break;
			case TOKEN_END_OF_LINE:
				done = true;
				break;
		}
	}

	int32 endPosition = fTokenizer.CurrentToken().position;
	fTokenizer.RewindToken();

	// add the argument only, if it's not just all spaces
	const char* arg = fTokenizer.String() + startPosition;
	int32 argLen = endPosition - startPosition;
	bool allSpaces = true;
	for (int32 i = 0; allSpaces && i < argLen; i++)
		allSpaces = isspace(arg[i]);

	if (!allSpaces)
		_AddArgument(argc, argv, arg, argLen);
}


/**
 * @brief Copy a string argument into a fresh debug-heap buffer and append it.
 * @param[in,out] argc Current argument count; incremented on success.
 * @param[in,out] argv Argument vector to append into.
 * @param argument Source text; need not be NUL-terminated when length >= 0.
 * @param length Length to copy, or -1 to use strlen(argument).
 */
void
ExpressionParser::_AddArgument(int& argc, char** argv, const char* argument,
	int32 length)
{
	if (argc == kMaxArgumentCount)
		parse_exception("too many arguments for command", 0);

	if (length < 0)
		length = strlen(argument);
	length++;
	char* buffer = (char*)checked_malloc(length);
	strlcpy(buffer, argument, length);

	argv[argc++] = buffer;
}


/**
 * @brief Parse a sum (a chain of `+` / `-` over products), left-to-right.
 * @param useValue If true, use @p value as the initial left operand instead
 *        of parsing one.
 * @param value Pre-parsed left operand, used only when @p useValue is true.
 * @return The resulting 64-bit value.
 */
uint64
ExpressionParser::_ParseSum(bool useValue, uint64 value)
{
	if (!useValue)
		value = _ParseProduct();

	while (true) {
		const Token& token = fTokenizer.NextToken();
		switch (token.type) {
			case TOKEN_PLUS:
				value = value + _ParseProduct();
				break;
			case TOKEN_MINUS:
				value = value - _ParseProduct();
				break;

			default:
				fTokenizer.RewindToken();
				return value;
		}
	}
}


/**
 * @brief Parse a product (a chain of `*`, `/`, `%` over unary terms).
 *
 * Division and modulo by zero raise a parse exception at the operator's
 * source position.
 *
 * @return The resulting 64-bit value.
 */
uint64
ExpressionParser::_ParseProduct()
{
	uint64 value = _ParseUnary();

	while (true) {
		Token token = fTokenizer.NextToken();
		switch (token.type) {
			case TOKEN_STAR:
				value = value * _ParseUnary();
				break;
			case TOKEN_SLASH: {
				uint64 rhs = _ParseUnary();
				if (rhs == 0)
					parse_exception("division by zero", token.position);
				value = value / rhs;
				break;
			}
			case TOKEN_MODULO: {
				uint64 rhs = _ParseUnary();
				if (rhs == 0)
					parse_exception("modulo by zero", token.position);
				value = value % rhs;
				break;
			}

			default:
				fTokenizer.RewindToken();
				return value;
		}
	}
}


/**
 * @brief Parse a unary term: optional negation, dereference, or atom.
 * @return The resulting 64-bit value.
 */
uint64
ExpressionParser::_ParseUnary()
{
	switch (fTokenizer.NextToken().type) {
		case TOKEN_MINUS:
			return -_ParseUnary();

		case TOKEN_STAR:
			return _ParseDereference(NULL, NULL);

		default:
			fTokenizer.RewindToken();
			return _ParseAtom();
	}

	return 0;
}


/**
 * @brief Parse a memory dereference `*[{size}]expr` and read the target value.
 *
 * The optional `{size}` block specifies an access width of 1, 2, 4 or 8
 * bytes; the default is 4. The memory is read via debug_memcpy() against the
 * current team, and read failures raise a parse exception.
 *
 * @param[out] _address Optional; receives the computed target address.
 * @param[out] _size Optional; receives the access width in bytes.
 * @return The value read from memory, zero-extended to 64 bits.
 */
uint64
ExpressionParser::_ParseDereference(void** _address, uint32* _size)
{
	int32 starPosition = fTokenizer.CurrentToken().position;

	// optional "{ ... }" specifying the size to read
	uint64 size = 4;
	if (fTokenizer.NextToken().type == TOKEN_OPENING_BRACE) {
		int32 position = fTokenizer.CurrentToken().position;
		size = _ParseExpression();

		if (size != 1 && size != 2 && size != 4 && size != 8) {
			snprintf(sTempBuffer, sizeof(sTempBuffer),
				"invalid size (%" B_PRIu64 ") for unary * operator", size);
			parse_exception(sTempBuffer, position);
		}

		_EatToken(TOKEN_CLOSING_BRACE);
	} else
		fTokenizer.RewindToken();

	const void* address = (const void*)(addr_t)_ParseUnary();

	// read bytes from address into a tempory buffer
	uint64 buffer;
	if (debug_memcpy(B_CURRENT_TEAM, &buffer, address, size) != B_OK) {
		snprintf(sTempBuffer, sizeof(sTempBuffer),
			"failed to dereference address %p", address);
		parse_exception(sTempBuffer, starPosition);
	}

	// convert the value to uint64
	uint64 value = 0;
	switch (size) {
		case 1:
			value = *(uint8*)&buffer;
			break;
		case 2:
			value = *(uint16*)&buffer;
			break;
		case 4:
			value = *(uint32*)&buffer;
			break;
		case 8:
			value = buffer;
			break;
	}

	if (_address != NULL)
		*_address = (void*)address;
	if (_size != NULL)
		*_size = size;

	return value;
}


/**
 * @brief Parse an atom: numeric constant, variable, parenthesised expression,
 *        or bracketed `[command]` sub-invocation.
 * @return The resulting 64-bit value.
 */
uint64
ExpressionParser::_ParseAtom()
{
	const Token& token = fTokenizer.NextToken();
	if (token.type == TOKEN_END_OF_LINE)
		parse_exception("unexpected end of expression", token.position);

	if (token.type == TOKEN_CONSTANT)
		return token.value;

	if (token.type == TOKEN_IDENTIFIER) {
		if (!is_debug_variable_defined(token.string)) {
			snprintf(sTempBuffer, sizeof(sTempBuffer),
				"variable '%s' undefined", token.string);
			parse_exception(sTempBuffer, token.position);
		}

		return get_debug_variable(token.string, 0);
	}

	if (token.type == TOKEN_OPENING_PARENTHESIS) {
		uint64 value = _ParseExpression();

		_EatToken(TOKEN_CLOSING_PARENTHESIS);

		return value;
	}

	// it can only be a "[ command ]" expression now
	fTokenizer.RewindToken();

	_EatToken(TOKEN_OPENING_BRACKET);

	fTokenizer.SetCommandMode(true);
	int returnValue;
	uint64 value = _ParseCommandPipe(returnValue);
	fTokenizer.SetCommandMode(false);

	_EatToken(TOKEN_CLOSING_BRACKET);

	return value;
}


/**
 * @brief Consume the next token, requiring it to match an expected type.
 *
 * On mismatch, raises a parse exception describing the expected and actual
 * tokens at the offending source position.
 *
 * @param type Expected token type (see TOKEN_* enum).
 * @return Reference to the consumed token.
 */
const Token&
ExpressionParser::_EatToken(int32 type)
{
	const Token& token = fTokenizer.NextToken();
	if (token.type != type) {
		snprintf(sTempBuffer, sizeof(sTempBuffer), "expected token type '%c', "
			"got token '%s'", char(type & ~TOKEN_FLAGS), token.string);
		parse_exception(sTempBuffer, token.position);
	}

	return token;
}



// #pragma mark -


/**
 * @brief Evaluate a debugger expression under exception protection.
 *
 * Sets up a setjmp buffer so any parse_exception() during evaluation unwinds
 * cleanly. Uses a DebugAllocPoolScope to drop any transient allocations on
 * return, regardless of outcome.
 *
 * @param expression NUL-terminated KDL expression.
 * @param[out] _result Optional; receives the evaluated value on success.
 * @param silent If true, suppress the diagnostic kprintf on parse failure.
 * @return true on successful evaluation, false otherwise.
 */
bool
evaluate_debug_expression(const char* expression, uint64* _result, bool silent)
{
	if (sNextJumpBufferIndex >= kJumpBufferCount) {
		kprintf_unfiltered("evaluate_debug_expression(): Out of jump buffers "
			"for exception handling\n");
		return 0;
	}

	bool success;
	uint64 result;
	DebugAllocPoolScope allocPoolScope;
		// Will clean up all allocations when we return.

	if (setjmp(sJumpBuffers[sNextJumpBufferIndex++]) == 0) {
		result = ExpressionParser().EvaluateExpression(expression);
		success = true;
	} else {
		result = 0;
		success = false;
		if (!silent) {
			if (sExceptionPosition >= 0) {
				kprintf_unfiltered("%s, at position: %d, in expression: %s\n",
					sExceptionMessage, sExceptionPosition, expression);
			} else
				kprintf_unfiltered("%s\n", sExceptionMessage);
		}
	}

	sNextJumpBufferIndex--;

	if (success && _result != NULL)
		*_result = result;

	return success;
}


/**
 * @brief Parse and invoke a KDL command line under exception protection.
 *
 * Handles the full command grammar (commands, pipes, assignments and `;`
 * sequences). Diagnostics are printed through kprintf_unfiltered() on error.
 *
 * @param commandLine NUL-terminated KDL command line.
 * @return The return code of the last command, or 0 on parse failure.
 */
int
evaluate_debug_command(const char* commandLine)
{
	if (sNextJumpBufferIndex >= kJumpBufferCount) {
		kprintf_unfiltered("evaluate_debug_command(): Out of jump buffers for "
			"exception handling\n");
		return 0;
	}

	int returnCode = 0;
	DebugAllocPoolScope allocPoolScope;
		// Will clean up all allocations when we return.

	if (setjmp(sJumpBuffers[sNextJumpBufferIndex++]) == 0) {
		ExpressionParser().EvaluateCommand(commandLine, returnCode);
	} else {
		if (sExceptionPosition >= 0) {
			kprintf_unfiltered("%s, at position: %d, in command line: %s\n",
				sExceptionMessage, sExceptionPosition, commandLine);
		} else
			kprintf_unfiltered("%s\n", sExceptionMessage);
	}

	sNextJumpBufferIndex--;

	return returnCode;
}


/**
 * @brief Public wrapper around ExpressionParser::ParseNextCommandArgument().
 *
 * Extracts one argument from a remaining command-line fragment, with
 * setjmp-based exception protection.
 *
 * @param[in,out] expressionString Remaining command-line text; advanced past
 *        the consumed argument, or set to NULL at end-of-line.
 * @param[out] buffer Destination for the argument text.
 * @param bufferSize Size of @p buffer in bytes.
 * @return B_OK on success, B_ENTRY_NOT_FOUND at end-of-line, B_BAD_VALUE on
 *         parse error, or B_ERROR if no jump buffer is available.
 */
status_t
parse_next_debug_command_argument(const char** expressionString, char* buffer,
	size_t bufferSize)
{
	if (sNextJumpBufferIndex >= kJumpBufferCount) {
		kprintf_unfiltered("parse_next_debug_command_argument(): Out of jump "
			"buffers for exception handling\n");
		return B_ERROR;
	}

	status_t error;
	DebugAllocPoolScope allocPoolScope;
		// Will clean up all allocations when we return.

	if (setjmp(sJumpBuffers[sNextJumpBufferIndex++]) == 0) {
		error = ExpressionParser().ParseNextCommandArgument(expressionString,
			buffer, bufferSize);
	} else {
		if (sExceptionPosition >= 0) {
			kprintf_unfiltered("%s, at position: %d, in command line: %s\n",
				sExceptionMessage, sExceptionPosition, *expressionString);
		} else
			kprintf_unfiltered("%s\n", sExceptionMessage);
		error = B_BAD_VALUE;
	}

	sNextJumpBufferIndex--;

	return error;
}
