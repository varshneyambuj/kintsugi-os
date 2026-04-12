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
 *   Copyright 2014-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file StringFormat.cpp
 * @brief Implementation of BStringFormat for pluralization and message patterns.
 *
 * BStringFormat wraps ICU MessageFormat to apply ICU message-format patterns
 * that support plural rules (e.g. "{0, plural, one{# file} other{# files}}").
 * The formatter is initialized from a BString pattern and an optional BLanguage;
 * Format() substitutes a single int64 argument into the pattern.
 *
 * @see BFormat
 */


#include <unicode/uversion.h>
#include <StringFormat.h>

#include <Autolock.h>
#include <FormattingConventionsPrivate.h>
#include <LanguagePrivate.h>

#include <ICUWrapper.h>

#include <unicode/msgfmt.h>


U_NAMESPACE_USE


/**
 * @brief Construct a BStringFormat with an explicit language and ICU pattern.
 *
 * @param language  Language whose plural rules are used.
 * @param pattern   ICU MessageFormat pattern string.
 */
BStringFormat::BStringFormat(const BLanguage& language, const BString pattern)
	: BFormat(language, BFormattingConventions())
{
	_Initialize(UnicodeString::fromUTF8(pattern.String()));
}


/**
 * @brief Construct a BStringFormat using the default locale and given pattern.
 *
 * @param pattern  ICU MessageFormat pattern string.
 */
BStringFormat::BStringFormat(const BString pattern)
	: BFormat()
{
	_Initialize(UnicodeString::fromUTF8(pattern.String()));
}


/**
 * @brief Destroy the BStringFormat and free the ICU MessageFormat.
 */
BStringFormat::~BStringFormat()
{
	delete fFormatter;
}


/**
 * @brief Return the initialization status.
 *
 * @return B_OK if the ICU MessageFormat was created successfully, or an error
 *         code if pattern parsing failed.
 */
status_t
BStringFormat::InitCheck()
{
	return fInitStatus;
}


/**
 * @brief Format the pattern with the given int64 argument.
 *
 * Substitutes \a arg into the ICU message pattern (which may use plural rules)
 * and appends the result to \a output.
 *
 * @param output  Output BString that receives the formatted string.
 * @param arg     The int64 argument substituted into the pattern.
 * @return B_OK on success, the stored init status if not initialized, B_ERROR
 *         on ICU formatting failure.
 */
status_t
BStringFormat::Format(BString& output, const int64 arg) const
{
	if (fInitStatus != B_OK)
		return fInitStatus;

	UnicodeString buffer;
	UErrorCode error = U_ZERO_ERROR;

	Formattable arguments[] = {
		(int64)arg
	};

	FieldPosition pos;
	buffer = fFormatter->format(arguments, 1, buffer, pos, error);
	if (!U_SUCCESS(error))
		return B_ERROR;

	BStringByteSink byteSink(&output);
	buffer.toUTF8(byteSink);

	return B_OK;
}


/**
 * @brief Create and store the ICU MessageFormat from the given pattern.
 *
 * Called by both constructors. Sets fInitStatus to B_NO_MEMORY if allocation
 * fails or B_ERROR if ICU reports a pattern parse error.
 *
 * @param pattern  ICU UnicodeString pattern to compile.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BStringFormat::_Initialize(const UnicodeString& pattern)
{
	fInitStatus = B_OK;
	UErrorCode error = U_ZERO_ERROR;
	Locale* icuLocale
		= BLanguage::Private(&fLanguage).ICULocale();

	fFormatter = new MessageFormat(pattern, *icuLocale, error);

	if (fFormatter == NULL)
		fInitStatus = B_NO_MEMORY;

	if (!U_SUCCESS(error)) {
		delete fFormatter;
		fInitStatus = B_ERROR;
		fFormatter = NULL;
	}

	return fInitStatus;
}
