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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file CharStream.cpp
 * @brief Character stream implementation for the MIME sniffer parser.
 *
 * Provides a simple sequential character stream over a std::string, used by
 * the MIME sniffer's tokenizer to read and unread individual characters while
 * scanning sniffer rule strings. The stream tracks the current position and
 * returns an end-of-text marker when the string is exhausted.
 *
 * @see BPrivate::Storage::Sniffer::TokenStream
 */

#include "sniffer/CharStream.h"

#include <sniffer/Err.h>

using namespace BPrivate::Storage::Sniffer;

//------------------------------------------------------------------------------
// CharStream
//------------------------------------------------------------------------------

/**
 * @brief Creates a new, initialized character stream.
 *
 * @param string The character string to be streamed.
 */
CharStream::CharStream(const std::string &string)
	: fString(string)
	, fPos(0)
	, fCStatus(B_OK)
{
}

/**
 * @brief Creates a new, uninitialized character stream.
 *
 * Call SetTo() to initialize the stream before use.
 */
CharStream::CharStream()
	: fString("")
	, fPos(0)
	, fCStatus(B_NO_INIT)
{
}

/**
 * @brief Destroys the character stream and releases resources.
 */
CharStream::~CharStream() {
	Unset();
}

/**
 * @brief Reinitializes the character stream to the given string.
 *
 * The stream position is reset to the beginning of the stream.
 *
 * @param string The new character string to be streamed.
 * @return B_OK on success.
 */
status_t
CharStream::SetTo(const std::string &string) {
	fString = string;
	fPos = 0;
	fCStatus = B_OK;
	return fCStatus;
}

/**
 * @brief Uninitializes the stream, clearing its content and position.
 */
void
CharStream::Unset() {
	fString = "";
	fPos = 0;
	fCStatus = B_NO_INIT;
}

/**
 * @brief Returns the current initialization status of the stream.
 *
 * @return B_OK if the stream is ready and initialized, B_NO_INIT if uninitialized.
 */
status_t
CharStream::InitCheck() const {
	return fCStatus;
}

/**
 * @brief Returns true if there are no more characters in the stream.
 *
 * Also returns true when the stream is uninitialized.
 *
 * @return true if the stream is empty or uninitialized, false otherwise.
 */
bool
CharStream::IsEmpty() const {
	return fPos >= fString.length();
}

/**
 * @brief Returns the current offset of the stream into the original string.
 *
 * @return The current zero-based character position; zero if uninitialized.
 */
size_t
CharStream::Pos() const {
	return fPos;
}

/**
 * @brief Returns the entire string being streamed.
 *
 * @return A const reference to the underlying string.
 */
const std::string&
CharStream::String() const {
	return fString;
}

/**
 * @brief Returns the next character in the stream and advances the position.
 *
 * Call Unget() to undo this operation. Throws a Sniffer::Err exception if the
 * stream is uninitialized. Returns end-of-text (0x03) when the stream is exhausted
 * while still incrementing the position to keep Unget() calls consistent.
 *
 * @return The next character, or 0x03 if the end of the stream has been reached.
 */
char
CharStream::Get() {
	if (fCStatus != B_OK)
		throw new Err("Sniffer parser error: CharStream::Get() called on uninitialized CharStream object", -1);
	if (fPos < fString.length())
		return fString[fPos++];
	else {
		fPos++;		// Increment fPos to keep Unget()s consistent
		return 0x3;	// Return End-Of-Text char
	}
}

/**
 * @brief Shifts the stream position back by one character.
 *
 * Throws a Sniffer::Err exception if the stream is uninitialized or if there
 * are no characters to unget (i.e., already at the beginning of the stream).
 */
void
CharStream::Unget() {
	if (fCStatus != B_OK)
		throw new Err("Sniffer parser error: CharStream::Unget() called on uninitialized CharStream object", -1);
	if (fPos > 0)
		fPos--;
	else
		throw new Err("Sniffer parser error: CharStream::Unget() called at beginning of character stream", -1);
}
