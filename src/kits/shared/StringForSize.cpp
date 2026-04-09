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
 *   Copyright 2010-2024 Haiku Inc. All rights reserved.
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file StringForSize.cpp
 *  @brief Implements \c BPrivate::string_for_size() and
 *         \c BPrivate::parse_size(), utilities for formatting byte counts as
 *         human-readable strings (e.g. "1.50 GiB") and parsing size strings
 *         with optional SI-like suffixes back into raw byte counts.
 */

#include "StringForSize.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include <NumberFormat.h>
#include <StringFormat.h>
#include <SystemCatalog.h>


using BPrivate::gSystemCatalog;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StringForSize"


namespace BPrivate {


/**
 * @brief Converts a byte count to a localised human-readable size string.
 *
 * Repeatedly divides \a size by 1024 while the value is >= 1000 and a
 * larger unit is available (up to TiB). The result is formatted with
 * \c BNumberFormat (0 decimal places for bytes, 2 for larger units) and
 * embedded in a translated plural-aware format string via \c BStringFormat.
 *
 * @param size       The size in bytes.
 * @param string     Caller-supplied output buffer.
 * @param stringSize Size of \a string in bytes.
 * @return \a string, which now holds the formatted result.
 */
const char*
string_for_size(double size, char* string, size_t stringSize)
{
	const char* kFormats[] = {
		B_TRANSLATE_MARK_COMMENT("{0, plural, one{%s byte} other{%s bytes}}", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s KiB", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s MiB", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s GiB", "size unit"),
		B_TRANSLATE_MARK_COMMENT("%s TiB", "size unit")
	};

	size_t index = 0;
	while (index < B_COUNT_OF(kFormats) - 1 && size >= 1000.0) {
		size /= 1024.0;
		index++;
	}

	BString format;
	BStringFormat formatter(
		gSystemCatalog.GetString(kFormats[index], B_TRANSLATION_CONTEXT, "size unit"));
	formatter.Format(format, size);

	BString printedSize;
	BNumberFormat numberFormat;
	numberFormat.SetPrecision(index == 0 ? 0 : 2);
	numberFormat.Format(printedSize, size);

	snprintf(string, stringSize, format.String(), printedSize.String());

	return string;
}


/**
 * @brief Parses a size string with an optional binary unit suffix.
 *
 * Reads a leading integer using \c strtoll() and then inspects the
 * immediately following character for a case-insensitive suffix:
 * - \c 'k' / \c 'K' — multiply by 1024
 * - \c 'm' / \c 'M' — multiply by 1024^2
 * - \c 'g' / \c 'G' — multiply by 1024^3
 * - \c 't' / \c 'T' — multiply by 1024^4
 *
 * The suffixes are applied via fall-through multiplication (T -> G -> M -> K).
 * Overflow is detected by checking that the scaled value remains larger than
 * the original.
 *
 * @param sizeString A \c NUL-terminated string such as \c "512", \c "4k",
 *                   or \c "2G".
 * @return The parsed byte count, or \c -1 if the string is invalid, the
 *         suffix is unrecognised, or an overflow is detected.
 */
int64
parse_size(const char* sizeString)
{
	int64 parsedSize = -1;
	char* end;
	parsedSize = strtoll(sizeString, &end, 0);
	if (end != sizeString && parsedSize > 0) {
		int64 rawSize = parsedSize;
		switch (tolower(*end)) {
			case 't':
				parsedSize *= 1024;
			case 'g':
				parsedSize *= 1024;
			case 'm':
				parsedSize *= 1024;
			case 'k':
				parsedSize *= 1024;
				end++;
				break;
			case '\0':
				break;
			default:
				parsedSize = -1;
				break;
		}

		// Check for overflow
		if (parsedSize > 0 && rawSize > parsedSize)
			parsedSize = -1;
	}

	return parsedSize;
}


}	// namespace BPrivate
