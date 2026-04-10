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
 * @file Err.cpp
 * @brief MIME sniffer error class implementation.
 *
 * Implements the Err class, which carries diagnostic information (a descriptive
 * message and a stream position) for errors encountered during MIME sniffer rule
 * parsing and pattern matching. Err objects are typically thrown as pointers and
 * caught by the Parser to generate human-readable error output.
 *
 * @see BPrivate::Storage::Sniffer::Parser
 */

#include <sniffer/Err.h>
#include <new>
#include <string.h>

using namespace BPrivate::Storage::Sniffer;

//------------------------------------------------------------------------------
// Err
//------------------------------------------------------------------------------

/**
 * @brief Constructs an Err with a C-string message and stream position.
 *
 * @param msg  A null-terminated string describing the error.
 * @param pos  The position in the input stream where the error occurred.
 */
Err::Err(const char *msg, const ssize_t pos)
	: fMsg(NULL)
	, fPos(-1)
{
	SetTo(msg, pos);
}

/**
 * @brief Constructs an Err with a std::string message and stream position.
 *
 * @param msg  A std::string describing the error.
 * @param pos  The position in the input stream where the error occurred.
 */
Err::Err(const std::string &msg, const ssize_t pos)
	: fMsg(NULL)
	, fPos(-1)
{
	SetTo(msg, pos);
}

/**
 * @brief Copy-constructs an Err from an existing Err object.
 *
 * @param ref The source Err object to copy.
 */
Err::Err(const Err &ref)
	: fMsg(NULL)
	, fPos(-1)
{
	*this = ref;
}

/**
 * @brief Destroys the Err, releasing the internally allocated message buffer.
 */
Err::~Err() {
	Unset();
}

/**
 * @brief Assigns the contents of another Err to this object.
 *
 * @param ref The source Err object to copy from.
 * @return A reference to this Err object.
 */
Err&
Err::operator=(const Err &ref) {
	SetTo(ref.Msg(), ref.Pos());
	return *this;
}

/**
 * @brief Reinitializes the error with a C-string message and position.
 *
 * @param msg  A null-terminated string describing the error.
 * @param pos  The position in the input stream where the error occurred.
 * @return B_OK on success.
 */
status_t
Err::SetTo(const char *msg, const ssize_t pos) {
	SetMsg(msg);
	SetPos(pos);
	return B_OK;
}

/**
 * @brief Reinitializes the error with a std::string message and position.
 *
 * @param msg  A std::string describing the error.
 * @param pos  The position in the input stream where the error occurred.
 * @return B_OK on success.
 */
status_t
Err::SetTo(const std::string &msg, const ssize_t pos) {
	return SetTo(msg.c_str(), pos);
}

/**
 * @brief Resets the error to an uninitialized state, freeing the message buffer.
 */
void
Err::Unset() {
	delete[] fMsg;
	fMsg = NULL;
	fPos = -1;
}

/**
 * @brief Returns the error message string.
 *
 * @return A pointer to the null-terminated error message, or NULL if not set.
 */
const char*
Err::Msg() const {
	return fMsg;
}

/**
 * @brief Returns the stream position associated with the error.
 *
 * @return The zero-based position in the input stream, or -1 if not applicable.
 */
ssize_t
Err::Pos() const {
	return fPos;
}

/**
 * @brief Sets the internal message by copying the given C-string.
 *
 * Frees any previously held message before allocating a new buffer.
 *
 * @param msg A null-terminated string to store as the error message, or NULL to clear.
 */
void
Err::SetMsg(const char *msg) {
	if (fMsg) {
		delete[] fMsg;
		fMsg = NULL;
	}
	if (msg) {
		fMsg = new(std::nothrow) char[strlen(msg)+1];
		if (fMsg)
			strcpy(fMsg, msg);
	}
}

/**
 * @brief Sets the stream position stored in this error.
 *
 * @param pos The zero-based position in the input stream.
 */
void
Err::SetPos(ssize_t pos) {
	fPos = pos;
}
