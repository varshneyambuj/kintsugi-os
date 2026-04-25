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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2012, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file IntegerFormatter.cpp
 * @brief Static helpers that format BVariant integer values for display.
 *
 * Maps the requested integer_format together with the BVariant's type code
 * to a printf-style format string and width-correct integer accessor, so
 * the debugger UI can render the same integer as signed decimal, unsigned
 * decimal, or hex of any width.
 */


#include "IntegerFormatter.h"

#include <stdio.h>
#include <string.h>

#include <TypeConstants.h>


/**
 * @brief Build the printf-style format string for a given type and integer format.
 *
 * For hex defaults the integer_format is upgraded to a width-specific value
 * (HEX_8/16/32/64). For signed and unsigned formats the corresponding
 * B_PRId/B_PRIu width macro is appended into @a _formatString.
 *
 * @param type           BVariant type code of the value to format.
 * @param format         Caller-requested format.
 * @param _formatString  Buffer the printf format string is written into.
 * @param formatSize     Capacity of @a _formatString in bytes.
 * @return The (possibly upgraded) integer_format that should be used by the caller.
 */
static integer_format
GetFormatForTypeAndFormat(type_code type, integer_format format,
	char* _formatString, int formatSize)
{
	integer_format result = format;
	_formatString[0] = '%';
	++_formatString;
	formatSize -= 1;

	switch (type) {
		case B_INT8_TYPE:
			switch (format) {
				case INTEGER_FORMAT_HEX_DEFAULT:
					result = INTEGER_FORMAT_HEX_8;
					break;
				case INTEGER_FORMAT_SIGNED:
					strlcpy(_formatString, B_PRId8, formatSize);
					break;
				case INTEGER_FORMAT_UNSIGNED:
					strlcpy(_formatString, B_PRIu8, formatSize);
					break;
				default:
					break;
			}
			break;
		case B_INT16_TYPE:
			switch (format) {
				case INTEGER_FORMAT_HEX_DEFAULT:
					result = INTEGER_FORMAT_HEX_16;
					break;
				case INTEGER_FORMAT_SIGNED:
					strlcpy(_formatString, B_PRId16, formatSize);
					break;
				case INTEGER_FORMAT_UNSIGNED:
					strlcpy(_formatString, B_PRIu16, formatSize);
					break;
				default:
					break;
			}
			break;
		case B_INT32_TYPE:
			switch (format) {
				case INTEGER_FORMAT_HEX_DEFAULT:
					result = INTEGER_FORMAT_HEX_32;
					break;
				case INTEGER_FORMAT_SIGNED:
					strlcpy(_formatString, B_PRId32, formatSize);
					break;
				case INTEGER_FORMAT_UNSIGNED:
					strlcpy(_formatString, B_PRIu32, formatSize);
					break;
				default:
					break;
			}
			break;
		case B_INT64_TYPE:
		default:
			switch (format) {
				case INTEGER_FORMAT_HEX_DEFAULT:
					result = INTEGER_FORMAT_HEX_64;
					break;
				case INTEGER_FORMAT_SIGNED:
					strlcpy(_formatString, B_PRId64, formatSize);
					break;
				case INTEGER_FORMAT_UNSIGNED:
					strlcpy(_formatString, B_PRIu64, formatSize);
					break;
				default:
					break;
			}
			break;
	}

	return result;
}


/**
 * @brief Format an integer BVariant into a textual representation.
 *
 * Falls back to signed/unsigned decimal when @a format is INTEGER_FORMAT_DEFAULT,
 * picking the variant based on the value's signedness.
 *
 * @param value       Integer-typed BVariant. Must report IsInteger().
 * @param format      Desired representation; INTEGER_FORMAT_HEX_DEFAULT is
 *                    promoted to a width-specific hex format.
 * @param buffer      Output buffer that receives the formatted string.
 * @param bufferSize  Capacity of @a buffer in bytes.
 * @return true on success, false if @a value is not an integer type.
 */
/*static*/ bool
IntegerFormatter::FormatValue(const BVariant& value, integer_format format,
	char* buffer, size_t bufferSize)
{
	bool isSigned;
	if (!value.IsInteger(&isSigned))
		return false;

	char formatString[10];

	if (format == INTEGER_FORMAT_DEFAULT) {
		format = isSigned ? INTEGER_FORMAT_SIGNED : INTEGER_FORMAT_UNSIGNED;
	}

	format = GetFormatForTypeAndFormat(value.Type(), format, formatString,
		sizeof(formatString));

	// format the value
	switch (format) {
		case INTEGER_FORMAT_SIGNED:
			snprintf(buffer, bufferSize, formatString,
				value.Type() == B_INT8_TYPE ? value.ToInt8() :
					value.Type() == B_INT16_TYPE ? value.ToInt16() :
						value.Type() == B_INT32_TYPE ? value.ToInt32() :
							value.ToInt64());
			break;
		case INTEGER_FORMAT_UNSIGNED:
			snprintf(buffer, bufferSize, formatString,
				value.Type() == B_INT8_TYPE ? value.ToUInt8() :
					value.Type() == B_INT16_TYPE ? value.ToUInt16() :
						value.Type() == B_INT32_TYPE ? value.ToUInt32() :
							value.ToUInt64());
			break;
		case INTEGER_FORMAT_HEX_8:
			snprintf(buffer, bufferSize, "%#x", (uint8)value.ToUInt64());
			break;
		case INTEGER_FORMAT_HEX_16:
			snprintf(buffer, bufferSize, "%#x", (uint16)value.ToUInt64());
			break;
		case INTEGER_FORMAT_HEX_32:
			snprintf(buffer, bufferSize, "%#" B_PRIx32,
				(uint32)value.ToUInt64());
			break;
		case INTEGER_FORMAT_HEX_64:
		default:
			snprintf(buffer, bufferSize, "%#" B_PRIx64, value.ToUInt64());
			break;
	}

	return true;
}
