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
 *   Copyright 2017, Andrew Lindesay <apl@lindesay.co.nz>
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Andrew Lindesay
 */

/** @file JsonWriter.cpp
 *  @brief Base class for JSON writers that translate typed C++ values into
 *         BJsonEvent calls consumed by a BJsonEventListener.
 */


#include "JsonWriter.h"

#include <stdio.h>
#include <stdlib.h>

#include <UnicodeChar.h>


/** @brief Constructs a BJsonWriter in the initial (no-error) state. */
BJsonWriter::BJsonWriter()
	:
	fErrorStatus(B_OK)
{
}


/** @brief Destroys the writer. */
BJsonWriter::~BJsonWriter()
{
}


/** @brief Records a write error and prints a diagnostic to stderr.
 *
 *  Only the first error is recorded; subsequent errors are silently ignored
 *  to allow callers to check ErrorStatus() once at the end of a sequence of
 *  writes.
 *
 *  @param status   The error code to record (e.g. B_IO_ERROR).
 *  @param line     Source line where the error occurred, or -1 if unknown.
 *  @param message  Human-readable description of the error; "?" is used
 *                  when NULL is passed.
 */
void
BJsonWriter::HandleError(status_t status, int32 line,
	const char* message)
{
	if(fErrorStatus == B_OK) {
		if (message == NULL)
			message = "?";
		fErrorStatus = status;
		fprintf(stderr, "! json err @line %" B_PRIi32 " - %s : %s\n", line,
			strerror(status), message);
	}
}


/** @brief Returns the first error encountered since construction, or B_OK.
 *  @return The stored error status code.
 */
status_t
BJsonWriter::ErrorStatus()
{
	return fErrorStatus;
}


/** @brief Writes a boolean value by delegating to WriteTrue() or WriteFalse().
 *  @param value  The boolean to write.
 *  @return B_OK on success, or the first error status if a prior write failed.
 */
status_t
BJsonWriter::WriteBoolean(bool value)
{
	if (value)
		return WriteTrue();

	return WriteFalse();
}


/** @brief Writes the JSON literal @c true.
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteTrue()
{
	Handle(BJsonEvent(B_JSON_TRUE));
	return fErrorStatus;
}


/** @brief Writes the JSON literal @c false.
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteFalse()
{
	Handle(BJsonEvent(B_JSON_FALSE));
	return fErrorStatus;
}


/** @brief Writes the JSON literal @c null.
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteNull()
{
	Handle(BJsonEvent(B_JSON_NULL));
	return fErrorStatus;
}


/** @brief Writes a 64-bit signed integer as a JSON number.
 *  @param value  The integer to write.
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteInteger(int64 value)
{
	Handle(BJsonEvent(value));
	return fErrorStatus;
}


/** @brief Writes a double-precision floating-point value as a JSON number.
 *  @param value  The double to write.
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteDouble(double value)
{
	Handle(BJsonEvent(value));
	return fErrorStatus;
}


/** @brief Writes a NUL-terminated string as a JSON string value.
 *  @param value  The string to write (will be JSON-encoded by the sink).
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteString(const char* value)
{
	Handle(BJsonEvent(value));
	return fErrorStatus;
}


/** @brief Emits the start of a JSON object ('{').
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteObjectStart()
{
	Handle(BJsonEvent(B_JSON_OBJECT_START));
	return fErrorStatus;
}


/** @brief Writes a JSON object member name.
 *  @param value  The NUL-terminated name string.
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteObjectName(const char* value)
{
	Handle(BJsonEvent(B_JSON_OBJECT_NAME, value));
	return fErrorStatus;
}


/** @brief Emits the end of a JSON object ('}').
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteObjectEnd()
{
	Handle(BJsonEvent(B_JSON_OBJECT_END));
	return fErrorStatus;
}


/** @brief Emits the start of a JSON array ('[').
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteArrayStart()
{
	Handle(BJsonEvent(B_JSON_ARRAY_START));
	return fErrorStatus;
}


/** @brief Emits the end of a JSON array (']').
 *  @return B_OK on success, or the stored error status on failure.
 */
status_t
BJsonWriter::WriteArrayEnd()
{
	Handle(BJsonEvent(B_JSON_ARRAY_END));
	return fErrorStatus;
}
