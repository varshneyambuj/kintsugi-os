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
 *   Copyright 2007-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file ArgumentVector.cpp
 *  @brief Implements \c ArgumentVector, which tokenises a shell-style command
 *         line string into a \c NULL-terminated \c argv array, honouring
 *         single-quoted strings, double-quoted strings, and backslash escaping.
 */

#include <ArgumentVector.h>

#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>


/**
 * @brief Internal helper that tokenises a raw command-line string.
 *
 * The \c Parser struct holds all temporary state needed during a single
 * parse pass and is used exclusively by \c ArgumentVector::Parse().
 */
struct ArgumentVector::Parser {
	/**
	 * @brief Parses \a commandLine into discrete argument tokens.
	 *
	 * Iterates over every character in \a commandLine, applying shell-like
	 * quoting rules:
	 * - Single-quoted regions are copied verbatim (no escaping).
	 * - Double-quoted regions honour \c \\ and \c \" escapes only.
	 * - A lone backslash outside quotes escapes the next character.
	 * - Whitespace delimits arguments.
	 *
	 * @param commandLine    The raw command-line string to parse.
	 * @param _errorLocation Set to the position within \a commandLine where
	 *                       the error was detected when a non-\c NO_ERROR
	 *                       code is returned.
	 * @return \c NO_ERROR on success, \c UNTERMINATED_QUOTED_STRING if a
	 *         quote is never closed, or \c TRAILING_BACKSPACE if the string
	 *         ends with an unescaped backslash.
	 */
	ParseError Parse(const char* commandLine, const char*& _errorLocation)
	{
		// init temporary arg/argv storage
		fCurrentArg.clear();
		fCurrentArgStarted = false;
		fArgVector.clear();
		fTotalStringSize = 0;

		for (; *commandLine; commandLine++) {
			char c = *commandLine;

			// whitespace delimits args and is otherwise ignored
			if (isspace(c)) {
				_PushCurrentArg();
				continue;
			}

			const char* errorBase = commandLine;

			switch (c) {
				case '\'':
					// quoted string -- no quoting
					while (*++commandLine != '\'') {
						c = *commandLine;
						if (c == '\0') {
							_errorLocation = errorBase;
							return UNTERMINATED_QUOTED_STRING;
						}
						_PushCharacter(c);
					}
					break;

				case '"':
					// quoted string -- some quoting
					while (*++commandLine != '"') {
						c = *commandLine;
						if (c == '\0') {
							_errorLocation = errorBase;
							return UNTERMINATED_QUOTED_STRING;
						}

						if (c == '\\') {
							c = *++commandLine;
							if (c == '\0') {
								_errorLocation = errorBase;
								return UNTERMINATED_QUOTED_STRING;
							}

							// only '\' and '"' can be quoted, otherwise the
							// the '\' is treated as a normal char
							if (c != '\\' && c != '"')
								_PushCharacter('\\');
						}

						_PushCharacter(c);
					}
					break;

				case '\\':
					// quoted char
					c = *++commandLine;
					if (c == '\0') {
						_errorLocation = errorBase;
						return TRAILING_BACKSPACE;
					}
					_PushCharacter(c);
					break;

				default:
					// normal char
					_PushCharacter(c);
					break;
			}
		}

		// commit last arg
		_PushCurrentArg();

		return NO_ERROR;
	}

	/**
	 * @brief Returns the vector of parsed argument strings.
	 * @return A const reference to the internal argument vector.
	 */
	const std::vector<std::string>& ArgVector() const
	{
		return fArgVector;
	}

	/**
	 * @brief Returns the total byte size needed to store all argument strings.
	 *
	 * Each argument contributes its character count plus one byte for the
	 * terminating \c NUL.
	 *
	 * @return Total string storage size in bytes.
	 */
	size_t TotalStringSize() const
	{
		return fTotalStringSize;
	}

private:
	/**
	 * @brief Finalises the current argument and appends it to the vector.
	 *
	 * Does nothing if no characters have been accumulated for the current
	 * argument (i.e. \c fCurrentArgStarted is \c false).
	 */
	void _PushCurrentArg()
	{
		if (fCurrentArgStarted) {
			fArgVector.push_back(fCurrentArg);
			fTotalStringSize += fCurrentArg.length() + 1;
			fCurrentArgStarted = false;
		}
	}

	/**
	 * @brief Appends a single character to the argument being accumulated.
	 *
	 * Marks the current argument as started on the first call so that
	 * \c _PushCurrentArg() knows there is something to commit.
	 *
	 * @param c The character to append.
	 */
	void _PushCharacter(char c)
	{
		if (!fCurrentArgStarted) {
			fCurrentArg = "";
			fCurrentArgStarted = true;
		}

		fCurrentArg += c;
	}

private:
	// temporaries
	std::string					fCurrentArg;
	bool						fCurrentArgStarted;
	std::vector<std::string>	fArgVector;
	size_t						fTotalStringSize;
};


/**
 * @brief Default constructor. Initialises an empty \c ArgumentVector.
 */
ArgumentVector::ArgumentVector()
	:
	fArguments(NULL),
	fCount(0)
{
}


/**
 * @brief Destructor. Frees the internal argument array allocation.
 */
ArgumentVector::~ArgumentVector()
{
	free(fArguments);
}


/**
 * @brief Transfers ownership of the internal \c argv array to the caller.
 *
 * After this call the \c ArgumentVector is reset to an empty state. The
 * caller is responsible for freeing the returned pointer with \c free().
 *
 * @return A \c NULL-terminated \c char** array, or \c NULL if none exists.
 */
char**
ArgumentVector::DetachArguments()
{
	char** arguments = fArguments;
	fArguments = NULL;
	fCount = 0;
	return arguments;
}


/**
 * @brief Parses a shell-style command line into an \c argv array.
 *
 * Any previously held argument array is freed before parsing begins. On
 * success the resulting \c NULL-terminated array can be accessed via
 * \c Arguments() and \c ArgumentCount(), or ownership transferred via
 * \c DetachArguments().
 *
 * @param commandLine    The command-line string to parse.
 * @param _errorLocation If non-\c NULL and an error occurs, set to the
 *                       character position within \a commandLine where the
 *                       problem was detected.
 * @return \c NO_ERROR on success; \c UNTERMINATED_QUOTED_STRING,
 *         \c TRAILING_BACKSPACE, or \c NO_MEMORY on failure.
 */
ArgumentVector::ParseError
ArgumentVector::Parse(const char* commandLine, const char** _errorLocation)
{
	free(DetachArguments());

	ParseError error;
	const char* errorLocation = commandLine;

	try {
		Parser parser;
		error = parser.Parse(commandLine, errorLocation);

		if (error == NO_ERROR) {
			// Create a char* array and copy everything into a single
			// allocation.
			int count = parser.ArgVector().size();
			size_t arraySize = (count + 1) * sizeof(char*);
			fArguments = (char**)malloc(
				arraySize + parser.TotalStringSize());
			if (fArguments != 0) {
				char* argument = (char*)(fArguments + count + 1);
				for (int i = 0; i < count; i++) {
					fArguments[i] = argument;
					const std::string& sourceArgument = parser.ArgVector()[i];
					size_t argumentSize = sourceArgument.length() + 1;
					memcpy(argument, sourceArgument.c_str(), argumentSize);
					argument += argumentSize;
				}

				fArguments[count] = NULL;
				fCount = count;
			} else
				error = NO_MEMORY;
		}
	} catch (...) {
		error = NO_MEMORY;
	}

	if (error != NO_ERROR && _errorLocation != NULL)
		*_errorLocation = errorLocation;

	return error;
}
