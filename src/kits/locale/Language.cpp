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
 *   Copyright 2003-2011, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Language.cpp
 * @brief Implementation of BLanguage, the BCP-47 language descriptor.
 *
 * BLanguage wraps an ICU icu::Locale to expose language, country, script, and
 * variant subtags, text direction, and localized/native display names. It is
 * used throughout the Locale Kit wherever a language identity is needed,
 * including by BFormattingConventions and BCatalog.
 *
 * @see BFormattingConventions, BLocale, BCountry
 */


#include <unicode/uversion.h>
#include <Language.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#include <iostream>

#include <Catalog.h>
#include <Locale.h>
#include <LocaleRoster.h>
#include <Path.h>
#include <String.h>
#include <FindDirectory.h>

#include <ICUWrapper.h>

#include <unicode/locid.h>


U_NAMESPACE_USE


/**
 * @brief Construct a BLanguage representing the default locale.
 *
 * Sets the direction to B_LEFT_TO_RIGHT and calls SetTo(NULL) to initialize
 * the ICU locale to the system default.
 */
BLanguage::BLanguage()
	:
	fDirection(B_LEFT_TO_RIGHT),
	fICULocale(NULL)
{
	SetTo(NULL);
}


/**
 * @brief Construct a BLanguage from a BCP-47 language tag string.
 *
 * @param language  BCP-47 tag such as "en", "de_DE", or "zh_Hans_CN".
 */
BLanguage::BLanguage(const char* language)
	:
	fDirection(B_LEFT_TO_RIGHT),
	fICULocale(NULL)
{
	SetTo(language);
}


/**
 * @brief Copy-construct a BLanguage by cloning the source's ICU locale.
 *
 * @param other  The BLanguage to copy.
 */
BLanguage::BLanguage(const BLanguage& other)
	:
	fICULocale(NULL)
{
	*this = other;
}


/**
 * @brief Destroy the BLanguage and free the ICU locale object.
 */
BLanguage::~BLanguage()
{
	delete fICULocale;
}


/**
 * @brief Set this BLanguage to the given BCP-47 tag.
 *
 * Replaces the current ICU locale with a new one constructed from \a language.
 *
 * @param language  BCP-47 tag, or NULL for the system default.
 * @return B_OK on success, B_NO_MEMORY if allocation fails, B_BAD_VALUE if the
 *         tag produces a bogus ICU locale.
 */
status_t
BLanguage::SetTo(const char* language)
{
	delete fICULocale;
	fICULocale = new icu::Locale(language);
	if (fICULocale == NULL)
		return B_NO_MEMORY;

	if (fICULocale->isBogus())
		return B_BAD_VALUE;

	return B_OK;
}


/**
 * @brief Get the language's name in its own native script and language.
 *
 * @param name  Output BString that receives the title-cased native name.
 * @return B_OK always.
 */
status_t
BLanguage::GetNativeName(BString& name) const
{
	UnicodeString string;
	fICULocale->getDisplayName(*fICULocale, string);
	string.toTitle(NULL, *fICULocale);

	name.Truncate(0);
	BStringByteSink converter(&name);
	string.toUTF8(converter);

	return B_OK;
}


/**
 * @brief Get the language's name in the specified display language.
 *
 * If \a displayLanguage is NULL, the system preferred language is used.
 *
 * @param name             Output BString for the localized language name.
 * @param displayLanguage  Language in which to express the name, or NULL.
 * @return B_OK on success, or an error code from GetPreferredLanguages().
 */
status_t
BLanguage::GetName(BString& name, const BLanguage* displayLanguage) const
{
	status_t status = B_OK;

	BString appLanguage;
	if (displayLanguage == NULL) {
		BMessage preferredLanguage;
		status = BLocaleRoster::Default()->GetPreferredLanguages(
			&preferredLanguage);
		if (status == B_OK)
			status = preferredLanguage.FindString("language", 0, &appLanguage);
	} else {
		appLanguage = displayLanguage->Code();
	}

	if (status == B_OK) {
		UnicodeString string;
		fICULocale->getDisplayName(Locale(appLanguage), string);

		name.Truncate(0);
		BStringByteSink converter(&name);
		string.toUTF8(converter);
	}

	return status;
}


/**
 * @brief Retrieve a flag icon bitmap for the country associated with this language.
 *
 * @param result  Pre-allocated BBitmap to receive the flag icon.
 * @return B_OK on success, or an error code from GetFlagIconForCountry().
 */
status_t
BLanguage::GetIcon(BBitmap* result) const
{
	return BLocaleRoster::Default()->GetFlagIconForCountry(result, Code());
}


/**
 * @brief Look up a locale-kit string constant by numeric identifier (stub).
 *
 * Currently returns NULL for all IDs; ICU-based string lookup is not yet
 * implemented.
 *
 * @param id  String constant identifier in the range
 *            [B_LANGUAGE_STRINGS_BASE, B_LANGUAGE_STRINGS_BASE + B_NUM_LANGUAGE_STRINGS).
 * @return NULL always (not yet implemented).
 */
const char*
BLanguage::GetString(uint32 id) const
{
	if (id < B_LANGUAGE_STRINGS_BASE
		|| id >= B_LANGUAGE_STRINGS_BASE + B_NUM_LANGUAGE_STRINGS)
		return NULL;

	return NULL;

	// TODO: fetch string from ICU

//	return fStrings[id - B_LANGUAGE_STRINGS_BASE];
}


/**
 * @brief Return the language subtag (e.g. "en", "de").
 *
 * @return Pointer to the language code string; valid for this object's lifetime.
 */
const char*
BLanguage::Code() const
{
	return fICULocale->getLanguage();
}


/**
 * @brief Return the country/region subtag, or NULL if none is present.
 *
 * @return Country code (e.g. "US"), or NULL.
 */
const char*
BLanguage::CountryCode() const
{
	const char* country = fICULocale->getCountry();
	if (country == NULL || country[0] == '\0')
		return NULL;

	return country;
}


/**
 * @brief Return the script subtag (e.g. "Hans", "Latn"), or NULL if absent.
 *
 * @return Script subtag string, or NULL.
 */
const char*
BLanguage::ScriptCode() const
{
	const char* script = fICULocale->getScript();
	if (script == NULL || script[0] == '\0')
		return NULL;

	return script;
}


/**
 * @brief Return the variant subtag, or NULL if none is present.
 *
 * @return Variant string, or NULL.
 */
const char*
BLanguage::Variant() const
{
	const char* variant = fICULocale->getVariant();
	if (variant == NULL || variant[0] == '\0')
		return NULL;

	return variant;
}


/**
 * @brief Return the full ICU locale name including all subtags.
 *
 * @return Full locale identifier (e.g. "en_US", "zh_Hans_CN").
 */
const char*
BLanguage::ID() const
{
	return fICULocale->getName();
}


/**
 * @brief Indicate whether this language has a country subtag.
 *
 * @return true if CountryCode() is non-NULL.
 */
bool
BLanguage::IsCountrySpecific() const
{
	return CountryCode() != NULL;
}


/**
 * @brief Indicate whether this language has a variant subtag.
 *
 * @return true if Variant() is non-NULL.
 */
bool
BLanguage::IsVariant() const
{
	return Variant() != NULL;
}


/**
 * @brief Return the text direction for this language.
 *
 * @return B_LEFT_TO_RIGHT or B_RIGHT_TO_LEFT.
 */
uint8
BLanguage::Direction() const
{
	return fDirection;
}


/**
 * @brief Copy-assign from another BLanguage, cloning the ICU locale.
 *
 * @param source  The source BLanguage to copy from.
 * @return Reference to this BLanguage.
 */
BLanguage&
BLanguage::operator=(const BLanguage& source)
{
	if (&source != this) {
		delete fICULocale;

		fICULocale = source.fICULocale != NULL
			? source.fICULocale->clone()
			: NULL;
		fDirection = source.fDirection;
	}

	return *this;
}
