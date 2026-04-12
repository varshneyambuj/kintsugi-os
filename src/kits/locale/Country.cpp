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
 *   Copyright 2009-2019, Adrien Destugues, pulkomandy@gmail.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Country.cpp
 * @brief Implementation of BCountry, the ISO 3166 country representation.
 *
 * BCountry wraps an ICU icu::Locale restricted to a country component so it
 * can provide localized country names, the preferred language for that country,
 * flag icons, and the list of time zones associated with the country.
 *
 * @see BLanguage, BTimeZone, BLocaleRoster
 */


#include <unicode/uversion.h>
#include <Country.h>

#include <AutoDeleter.h>
#include <IconUtils.h>
#include <List.h>
#include <Language.h>
#include <LocaleRoster.h>
#include <Resources.h>
#include <String.h>

#include <unicode/locid.h>
#include <unicode/ulocdata.h>
#include <ICUWrapper.h>

#include <iostream>
#include <map>
#include <monetary.h>
#include <new>
#include <stdarg.h>
#include <stdlib.h>


U_NAMESPACE_USE


/**
 * @brief Construct a BCountry for the given ISO 3166-1 alpha-2 country code.
 *
 * @param countryCode  Two-letter country code (e.g. "DE", "JP").
 */
BCountry::BCountry(const char* countryCode)
	:
	fICULocale(NULL)
{
	SetTo(countryCode);
}


/**
 * @brief Copy-construct a BCountry by cloning the source's ICU locale.
 *
 * @param other  The BCountry to copy.
 */
BCountry::BCountry(const BCountry& other)
	:
	fICULocale(new icu::Locale(*other.fICULocale))
{
}


/**
 * @brief Copy-assign another BCountry, replacing or creating the ICU locale.
 *
 * @param other  The source BCountry to assign from.
 * @return Reference to this BCountry.
 */
BCountry&
BCountry::operator=(const BCountry& other)
{
	if (this == &other)
		return *this;

	if (!fICULocale)
		fICULocale = new icu::Locale(*other.fICULocale);
	else
		*fICULocale = *other.fICULocale;

	return *this;
}


/**
 * @brief Destroy the BCountry and free the ICU locale object.
 */
BCountry::~BCountry()
{
	delete fICULocale;
}


/**
 * @brief Set the country to the given ISO 3166-1 alpha-2 code.
 *
 * @param countryCode  Two-letter country code.
 * @return B_OK on success, B_NO_MEMORY if allocation fails, B_BAD_DATA if
 *         the code produces a bogus ICU locale.
 */
status_t
BCountry::SetTo(const char* countryCode)
{
	delete fICULocale;
	fICULocale = new icu::Locale("", countryCode);

	return InitCheck();
}


/**
 * @brief Check whether this BCountry object was initialized successfully.
 *
 * @return B_OK if valid, B_NO_MEMORY if the locale could not be allocated,
 *         B_BAD_DATA if the country code produced a bogus ICU locale.
 */
status_t
BCountry::InitCheck() const
{
	if (fICULocale == NULL)
		return B_NO_MEMORY;

	if (fICULocale->isBogus())
		return B_BAD_DATA;

	return B_OK;
}


/**
 * @brief Get the country name in its own native language, title-cased.
 *
 * @param name  Output BString that receives the native country name.
 * @return B_OK on success, or an error code from InitCheck().
 */
status_t
BCountry::GetNativeName(BString& name) const
{
	status_t valid = InitCheck();
	if (valid != B_OK)
		return valid;

	UnicodeString string;
	fICULocale->getDisplayCountry(*fICULocale, string);
	string.toTitle(NULL, *fICULocale);

	name.Truncate(0);
	BStringByteSink converter(&name);
	string.toUTF8(converter);

	return B_OK;
}


/**
 * @brief Get the country name in the specified display language.
 *
 * If \a displayLanguage is NULL the system preferred language is used.
 *
 * @param name             Output BString for the localized country name.
 * @param displayLanguage  Language to use for the name, or NULL for default.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCountry::GetName(BString& name, const BLanguage* displayLanguage) const
{
	status_t status = InitCheck();
	if (status != B_OK)
		return status;

	BString appLanguage;
	if (displayLanguage == NULL) {
		BMessage preferredLanguages;
		status = BLocaleRoster::Default()->GetPreferredLanguages(
			&preferredLanguages);
		if (status == B_OK)
			status = preferredLanguages.FindString("language", 0, &appLanguage);
	} else {
		appLanguage = displayLanguage->Code();
	}

	if (status == B_OK) {
		UnicodeString uString;
		fICULocale->getDisplayCountry(Locale(appLanguage), uString);
		name.Truncate(0);
		BStringByteSink stringConverter(&name);
		uString.toUTF8(stringConverter);
	}

	return status;
}


/**
 * @brief Get the preferred language spoken in this country.
 *
 * Uses ICU likely-subtags expansion to determine the most probable language
 * for the country. Requires ICU 63 or later; returns ENOSYS on older builds.
 *
 * @param language  Output BLanguage populated with the preferred language.
 * @return B_OK on success, ENOSYS if ICU is too old, or another error code.
 */
status_t
BCountry::GetPreferredLanguage(BLanguage& language) const
{
#if U_ICU_VERSION_MAJOR_NUM < 63
	return ENOSYS;
#else
	status_t status = InitCheck();
	if (status != B_OK)
		return status;

	icu::Locale* languageLocale = fICULocale->clone();
	if (languageLocale == NULL)
		return B_NO_MEMORY;

	UErrorCode icuError = U_ZERO_ERROR;
	languageLocale->addLikelySubtags(icuError);

	if (U_FAILURE(icuError))
		return B_ERROR;

	status = language.SetTo(languageLocale->getLanguage());

	delete languageLocale;

	return status;
#endif
}


/**
 * @brief Return the ISO 3166-1 alpha-2 country code string.
 *
 * @return The country code (e.g. "DE"), or NULL if InitCheck() fails.
 */
const char*
BCountry::Code() const
{
	status_t status = InitCheck();
	if (status != B_OK)
		return NULL;

	return fICULocale->getCountry();
}


/**
 * @brief Retrieve the flag icon bitmap for this country.
 *
 * @param result  Pre-allocated BBitmap to receive the flag icon data.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCountry::GetIcon(BBitmap* result) const
{
	status_t status = InitCheck();
	if (status != B_OK)
		return status;

	return BLocaleRoster::Default()->GetFlagIconForCountry(result, Code());
}


/**
 * @brief Populate a BMessage with the time zone IDs available for this country.
 *
 * @param timeZones  Output BMessage that will have "timeZone" string fields
 *                   added for each available zone.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BCountry::GetAvailableTimeZones(BMessage* timeZones) const
{
	status_t status = InitCheck();
	if (status != B_OK)
		return status;

	return BLocaleRoster::Default()->GetAvailableTimeZonesForCountry(timeZones,
		Code());
}
