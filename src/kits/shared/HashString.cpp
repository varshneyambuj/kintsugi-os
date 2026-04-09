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
 *   Copyright 2004-2007, Ingo Weinhold, bonefish@users.sf.net.
 *   All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file HashString.cpp
 *  @brief Implements \c HashString, a lightweight heap-managed string class
 *         designed for use as hash-table keys. It stores a \c NUL-terminated
 *         character buffer and tracks the string length explicitly.
 */

#include <new>
#include <string.h>

#include "HashString.h"

/*!
	\class HashString
	\brief A very simple string class.
*/

/**
 * @brief Default constructor. Creates an empty \c HashString.
 */
HashString::HashString()
	: fLength(0),
	  fString(NULL)
{
}

/**
 * @brief Copy constructor. Performs a deep copy of \a string.
 *
 * @param string The source \c HashString to copy from.
 */
HashString::HashString(const HashString &string)
	: fLength(0),
	  fString(NULL)
{
	*this = string;
}

/**
 * @brief Constructs a \c HashString from a C string with an optional length
 *        limit.
 *
 * Delegates to \c SetTo(string, length).
 *
 * @param string The source C string (may be \c NULL).
 * @param length Maximum number of characters to copy, or \c -1 to use the
 *               full length of \a string.
 */
HashString::HashString(const char *string, int32 length)
	: fLength(0),
	  fString(NULL)
{
	SetTo(string, length);
}

/**
 * @brief Destructor. Releases the internally allocated string buffer.
 */
HashString::~HashString()
{
	Unset();
}

/**
 * @brief Assigns the content of a C string to this \c HashString.
 *
 * When \a maxLength is positive the string is clamped to at most
 * \a maxLength characters using \c strnlen(). When negative the full
 * \c strlen() is used.
 *
 * @param string    The source C string (may be \c NULL to clear).
 * @param maxLength Maximum character count to accept, or \c -1 for unlimited.
 * @return \c true on success, \c false if memory allocation failed.
 */
bool
HashString::SetTo(const char *string, int32 maxLength)
{
	if (string) {
		if (maxLength > 0)
			maxLength = strnlen(string, maxLength);
		else if (maxLength < 0)
			maxLength = strlen(string);
	}
	return _SetTo(string, maxLength);
}

/**
 * @brief Clears the string, freeing any allocated memory.
 *
 * After this call \c GetString() returns an empty C string and
 * \c Length() returns zero.
 */
void
HashString::Unset()
{
	if (fString) {
		delete[] fString;
		fString = NULL;
	}
	fLength = 0;
}

/**
 * @brief Shortens the string to at most \a newLength characters in-place.
 *
 * If \a newLength is negative it is clamped to zero. If it is greater than
 * or equal to the current length the call is a no-op. The function attempts
 * to reallocate a smaller buffer; if reallocation fails the existing buffer
 * is truncated with a \c NUL byte at the new boundary.
 *
 * @param newLength The desired maximum length.
 */
void
HashString::Truncate(int32 newLength)
{
	if (newLength < 0)
		newLength = 0;
	if (newLength < fLength) {
		char *string = fString;
		fString = NULL;
		if (!_SetTo(string, newLength)) {
			fString = string;
			fLength = newLength;
			fString[fLength] = '\0';
		} else
			delete[] string;
	}
}

/**
 * @brief Returns a pointer to the \c NUL-terminated string data.
 *
 * @return The internal C string, or \c "" if the \c HashString is empty.
 */
const char *
HashString::GetString() const
{
	if (fString)
		return fString;
	return "";
}

/**
 * @brief Assignment operator. Performs a deep copy of \a string.
 *
 * @param string The source \c HashString.
 * @return A reference to \c *this.
 */
HashString &
HashString::operator=(const HashString &string)
{
	if (&string != this)
		_SetTo(string.fString, string.fLength);
	return *this;
}

/**
 * @brief Equality comparison operator.
 *
 * Two \c HashString objects are equal when they have the same length and
 * identical content.
 *
 * @param string The \c HashString to compare against.
 * @return \c true if the strings are equal, \c false otherwise.
 */
bool
HashString::operator==(const HashString &string) const
{
	return (fLength == string.fLength
			&& (fLength == 0 || !strcmp(fString, string.fString)));
}

/**
 * @brief Internal helper that allocates a new buffer and copies \a length
 *        bytes from \a string into it.
 *
 * Any existing buffer is released via \c Unset() before the new one is
 * allocated.
 *
 * @param string The source data (may be \c NULL when \a length is zero).
 * @param length Number of characters to copy (not including the \c NUL).
 * @return \c true on success, \c false if allocation failed.
 */
bool
HashString::_SetTo(const char *string, int32 length)
{
	bool result = true;
	Unset();
	if (string && length > 0) {
		fString = new(std::nothrow) char[length + 1];
		if (fString) {
			memcpy(fString, string, length);
			fString[length] = '\0';
			fLength = length;
		} else
			result = false;
	}
	return result;
}
