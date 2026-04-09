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
 *   Copyright 2012-2024, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file StringForRate.cpp
 *  @brief Implements \c BPrivate::string_for_rate(), which formats a
 *         byte-per-second transfer rate as a human-readable, localised string
 *         (e.g. "1.23 MiB/s").
 */

#include "StringForRate.h"

#include <stdio.h>

#include <Catalog.h>
#include <NumberFormat.h>
#include <StringFormat.h>
#include <SystemCatalog.h>


using BPrivate::gSystemCatalog;


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "StringForRate"


namespace BPrivate {


/**
 * @brief Converts a raw byte-per-second rate to a localised human-readable
 *        string.
 *
 * Repeatedly divides \a rate by 1024 until it is below 1024 or the largest
 * available unit (TiB/s) is reached. The resulting number is formatted with
 * \c BNumberFormat (0 decimal places for bytes/s, 2 for larger units) and
 * then substituted into a translated plural-aware format string via
 * \c BStringFormat.
 *
 * @param rate       The transfer rate in bytes per second.
 * @param string     Caller-supplied output buffer.
 * @param stringSize Size of \a string in bytes.
 * @return \a string, which now holds the formatted result.
 */
const char*
string_for_rate(double rate, char* string, size_t stringSize)
{
	const char* kFormats[] = {
		B_TRANSLATE_MARK_COMMENT("{0, plural, one{%s byte/s} other{%s bytes/s}}",
			"units per second"),
		B_TRANSLATE_MARK_COMMENT("%s KiB/s", "units per second"),
		B_TRANSLATE_MARK_COMMENT("%s MiB/s", "units per second"),
		B_TRANSLATE_MARK_COMMENT("%s GiB/s", "units per second"),
		B_TRANSLATE_MARK_COMMENT("%s TiB/s", "units per second")
	};

	size_t index = 0;
	while (index < B_COUNT_OF(kFormats) - 1 && rate >= 1024.0) {
		rate /= 1024.0;
		index++;
	}

	BString format;
	BStringFormat formatter(
		gSystemCatalog.GetString(kFormats[index], B_TRANSLATION_CONTEXT, "units per second"));
	formatter.Format(format, rate);

	BString printedRate;
	BNumberFormat numberFormat;
	numberFormat.SetPrecision(index == 0 ? 0 : 2);
	numberFormat.Format(printedRate, rate);

	snprintf(string, stringSize, format.String(), printedRate.String());

	return string;
}


}	// namespace BPrivate
