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
 */


/**
 * @file Format.cpp
 * @brief Base class implementation for locale-aware formatters.
 *
 * BFormat is the common base for BDateFormat, BTimeFormat, BNumberFormat, and
 * related classes. It holds a BFormattingConventions and a BLanguage that
 * together describe how values should be rendered for a given locale. The
 * constructors resolve a BLocale into its constituent parts, ensuring that all
 * concrete formatters start from a consistent state.
 *
 * @see BDateFormat, BTimeFormat, BNumberFormat, BLocale
 */


#include <Format.h>

#include <new>

#include <Autolock.h>
#include <Locale.h>
#include <LocaleRoster.h>


/**
 * @brief Construct a BFormat from a BLocale pointer.
 *
 * Resolves the locale's formatting conventions and language into the
 * corresponding member fields. If \a locale is NULL, the system default
 * locale is used instead.
 *
 * @param locale  Pointer to the locale to use, or NULL for the default.
 */
BFormat::BFormat(const BLocale* locale)
{
	if (locale == NULL)
		locale = BLocaleRoster::Default()->GetDefaultLocale();

	if (locale == NULL) {
		fInitStatus = B_BAD_DATA;
		return;
	}

	_Initialize(*locale);
}


/**
 * @brief Construct a BFormat from explicit language and conventions objects.
 *
 * @param language     Language that provides name strings and locale code.
 * @param conventions  Formatting conventions that supply patterns and settings.
 */
BFormat::BFormat(const BLanguage& language,
	const BFormattingConventions& conventions)
{
	_Initialize(language, conventions);
}


/**
 * @brief Copy-construct a BFormat from another instance.
 *
 * @param other  Source BFormat whose conventions, language, and status are copied.
 */
BFormat::BFormat(const BFormat &other)
	:
	fConventions(other.fConventions),
	fLanguage(other.fLanguage),
	fInitStatus(other.fInitStatus)
{
}


/**
 * @brief Destroy the BFormat base object.
 */
BFormat::~BFormat()
{
}


/**
 * @brief Return the initialization status of this formatter.
 *
 * @return B_OK if the formatter was initialized successfully, or an error
 *         code describing the failure.
 */
status_t
BFormat::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Initialize formatter fields from a BLocale by decomposing it.
 *
 * Extracts the BFormattingConventions and BLanguage from \a locale, then
 * delegates to _Initialize(language, conventions).
 *
 * @param locale  The locale to decompose.
 * @return B_OK on success, or an error code from GetFormattingConventions()
 *         or GetLanguage().
 */
status_t
BFormat::_Initialize(const BLocale& locale)
{
	BFormattingConventions conventions;
	BLanguage language;

	fInitStatus = locale.GetFormattingConventions(&conventions);
	if (fInitStatus != B_OK)
		return fInitStatus;

	fInitStatus = locale.GetLanguage(&language);
	if (fInitStatus != B_OK)
		return fInitStatus;

	return _Initialize(language, conventions);
}


/**
 * @brief Store the given language and conventions and mark the object ready.
 *
 * @param language     Language to use for name strings.
 * @param conventions  Formatting conventions to use for patterns.
 * @return B_OK always.
 */
status_t
BFormat::_Initialize(const BLanguage& language,
	const BFormattingConventions& conventions)
{
	fConventions = conventions;
	fLanguage = language;
	fInitStatus = B_OK;
	return fInitStatus;
}
