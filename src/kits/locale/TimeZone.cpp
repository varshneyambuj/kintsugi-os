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
 *   Copyright (c) 2010, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Adrien Destugues <pulkomandy@pulkomandy.ath.cx>
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file TimeZone.cpp
 * @brief Implementation of BTimeZone, the ICU time zone wrapper.
 *
 * BTimeZone holds an ICU TimeZone object and lazily computes display name
 * strings (long, daylight-saving, short, and short-DST forms), the GMT
 * offset in seconds, and the daylight-saving support flag. Each computed
 * field is cached via a bitmask so it is only fetched from ICU once per
 * object lifetime. An optional BLanguage determines the locale used for
 * display name strings.
 *
 * @see BLocaleRoster, BDurationFormat
 */


#include <unicode/uversion.h>
#include <TimeZone.h>

#include <new>

#include <unicode/locid.h>
#include <unicode/timezone.h>
#include <ICUWrapper.h>

#include <Language.h>


U_NAMESPACE_USE


/** @brief The IANA identifier for the UTC/GMT zone. */
const char* BTimeZone::kNameOfGmtZone = "GMT";


/** @brief Empty string returned when a requested field is not set. */
static const BString skEmptyString;


/** @brief Bitmask field: long (generic-location) display name is cached. */
static const uint32 skNameField 					= 1U << 0;
/** @brief Bitmask field: long DST display name is cached. */
static const uint32 skDaylightSavingNameField 		= 1U << 1;
/** @brief Bitmask field: short (standard) display name is cached. */
static const uint32 skShortNameField 				= 1U << 2;
/** @brief Bitmask field: short DST display name is cached. */
static const uint32 skShortDaylightSavingNameField 	= 1U << 3;
/** @brief Bitmask field: long generic display name is cached. */
static const uint32 skLongGenericNameField 			= 1U << 4;
/** @brief Bitmask field: generic-location display name is cached. */
static const uint32 skGenericLocationNameField 		= 1U << 5;
/** @brief Bitmask field: short commonly-used display name is cached. */
static const uint32 skShortCommonlyUsedNameField	= 1U << 6;
/** @brief Bitmask field: daylight-saving support flag is cached. */
static const uint32 skSupportsDaylightSavingField   = 1U << 7;
/** @brief Bitmask field: GMT offset in seconds is cached. */
static const uint32 skOffsetFromGMTField			= 1U << 8;


/**
 * @brief Construct a BTimeZone for the given IANA zone identifier.
 *
 * If \a zoneID is NULL or empty the system default time zone is used.
 *
 * @param zoneID    IANA time zone identifier (e.g. "America/New_York"), or NULL.
 * @param language  Language for display names, or NULL for the system default.
 */
BTimeZone::BTimeZone(const char* zoneID, const BLanguage* language)
	:
	fICUTimeZone(NULL),
	fICULocale(NULL),
	fInitStatus(B_NO_INIT),
	fInitializedFields(0)
{
	SetTo(zoneID, language);
}


/**
 * @brief Copy-construct a BTimeZone by cloning all ICU objects.
 *
 * @param other  Source BTimeZone to copy.
 */
BTimeZone::BTimeZone(const BTimeZone& other)
	:
	fICUTimeZone(other.fICUTimeZone == NULL
		? NULL
		: other.fICUTimeZone->clone()),
	fICULocale(other.fICULocale == NULL
		? NULL
		: other.fICULocale->clone()),
	fInitStatus(other.fInitStatus),
	fInitializedFields(other.fInitializedFields),
	fZoneID(other.fZoneID),
	fName(other.fName),
	fDaylightSavingName(other.fDaylightSavingName),
	fShortName(other.fShortName),
	fShortDaylightSavingName(other.fShortDaylightSavingName),
	fOffsetFromGMT(other.fOffsetFromGMT),
	fSupportsDaylightSaving(other.fSupportsDaylightSaving)
{
}


/**
 * @brief Destroy the BTimeZone and free ICU objects.
 */
BTimeZone::~BTimeZone()
{
	delete fICULocale;
	delete fICUTimeZone;
}


/**
 * @brief Copy-assign from another BTimeZone, replacing all fields.
 *
 * @param source  Source BTimeZone to copy from.
 * @return Reference to this BTimeZone.
 */
BTimeZone& BTimeZone::operator=(const BTimeZone& source)
{
	delete fICUTimeZone;
	fICUTimeZone = source.fICUTimeZone == NULL
		? NULL
		: source.fICUTimeZone->clone();
	fICULocale = source.fICULocale == NULL
		? NULL
		: source.fICULocale->clone();
	fInitStatus = source.fInitStatus;
	fInitializedFields = source.fInitializedFields;
	fZoneID = source.fZoneID;
	fName = source.fName;
	fDaylightSavingName = source.fDaylightSavingName;
	fShortName = source.fShortName;
	fShortDaylightSavingName = source.fShortDaylightSavingName;
	fOffsetFromGMT = source.fOffsetFromGMT;
	fSupportsDaylightSaving = source.fSupportsDaylightSaving;

	return *this;
}


/**
 * @brief Return the IANA zone identifier string.
 *
 * @return Reference to the cached zone ID BString.
 */
const BString&
BTimeZone::ID() const
{
	return fZoneID;
}


/**
 * @brief Return the long generic-location display name, lazily fetched.
 *
 * @return Reference to the cached display name BString.
 */
const BString&
BTimeZone::Name() const
{
	if ((fInitializedFields & skNameField) == 0) {
		UnicodeString unicodeString;
		if (fICULocale != NULL) {
			fICUTimeZone->getDisplayName(false, TimeZone::GENERIC_LOCATION,
				*fICULocale, unicodeString);
		} else {
			fICUTimeZone->getDisplayName(false, TimeZone::GENERIC_LOCATION,
				unicodeString);
		}
		BStringByteSink sink(&fName);
		unicodeString.toUTF8(sink);
		fInitializedFields |= skNameField;
	}

	return fName;
}


/**
 * @brief Return the long DST generic-location display name, lazily fetched.
 *
 * @return Reference to the cached DST display name BString.
 */
const BString&
BTimeZone::DaylightSavingName() const
{
	if ((fInitializedFields & skDaylightSavingNameField) == 0) {
		UnicodeString unicodeString;
		if (fICULocale != NULL) {
			fICUTimeZone->getDisplayName(true, TimeZone::GENERIC_LOCATION,
				*fICULocale, unicodeString);
		} else {
			fICUTimeZone->getDisplayName(true, TimeZone::GENERIC_LOCATION,
				unicodeString);
		}
		BStringByteSink sink(&fDaylightSavingName);
		unicodeString.toUTF8(sink);
		fInitializedFields |= skDaylightSavingNameField;
	}

	return fDaylightSavingName;
}


/**
 * @brief Return the short (abbreviation) standard-time display name, lazily fetched.
 *
 * @return Reference to the cached short name BString.
 */
const BString&
BTimeZone::ShortName() const
{
	if ((fInitializedFields & skShortNameField) == 0) {
		UnicodeString unicodeString;
		if (fICULocale != NULL) {
			fICUTimeZone->getDisplayName(false, TimeZone::SHORT, *fICULocale,
				unicodeString);
		} else {
			fICUTimeZone->getDisplayName(false, TimeZone::SHORT, unicodeString);
		}
		BStringByteSink sink(&fShortName);
		unicodeString.toUTF8(sink);
		fInitializedFields |= skShortNameField;
	}

	return fShortName;
}


/**
 * @brief Return the short DST display name (abbreviation), lazily fetched.
 *
 * @return Reference to the cached short DST name BString.
 */
const BString&
BTimeZone::ShortDaylightSavingName() const
{
	if ((fInitializedFields & skShortDaylightSavingNameField) == 0) {
		UnicodeString unicodeString;
		if (fICULocale != NULL) {
			fICUTimeZone->getDisplayName(true, TimeZone::SHORT, *fICULocale,
				unicodeString);
		} else {
			fICUTimeZone->getDisplayName(true, TimeZone::SHORT, unicodeString);
		}
		BStringByteSink sink(&fShortDaylightSavingName);
		unicodeString.toUTF8(sink);
		fInitializedFields |= skShortDaylightSavingNameField;
	}

	return fShortDaylightSavingName;
}


/**
 * @brief Return the total offset from GMT in seconds, lazily computed.
 *
 * Queries the ICU time zone for the raw and DST offsets at the current time
 * and sums them. Positive values are east of GMT.
 *
 * @return Offset in seconds from UTC.
 */
int
BTimeZone::OffsetFromGMT() const
{
	if ((fInitializedFields & skOffsetFromGMTField) == 0) {
		int32_t rawOffset;
		int32_t dstOffset;
		UDate nowMillis = 1000 * (double)time(NULL);

		UErrorCode error = U_ZERO_ERROR;
		fICUTimeZone->getOffset(nowMillis, FALSE, rawOffset, dstOffset, error);
		if (!U_SUCCESS(error))
			fOffsetFromGMT = 0;
		else {
			fOffsetFromGMT = (rawOffset + dstOffset) / 1000;
				// we want seconds, not ms (which ICU gives us)
		}
		fInitializedFields |= skOffsetFromGMTField;
	}

	return fOffsetFromGMT;
}


/**
 * @brief Return whether this time zone observes daylight saving time.
 *
 * @return true if DST is supported, false otherwise.
 */
bool
BTimeZone::SupportsDaylightSaving() const
{
	if ((fInitializedFields & skSupportsDaylightSavingField) == 0) {
		fSupportsDaylightSaving = fICUTimeZone->useDaylightTime();
		fInitializedFields |= skSupportsDaylightSavingField;
	}

	return fSupportsDaylightSaving;
}


/**
 * @brief Check whether this BTimeZone was initialized successfully.
 *
 * @return B_OK on success, B_NAME_NOT_FOUND if the zone ID was not recognized,
 *         B_NO_MEMORY if allocation failed.
 */
status_t
BTimeZone::InitCheck() const
{
	return fInitStatus;
}


/**
 * @brief Change the display language without changing the time zone.
 *
 * @param language  New language for display names, or NULL for the system default.
 * @return B_OK on success, or an error code from SetTo().
 */
status_t
BTimeZone::SetLanguage(const BLanguage* language)
{
	return SetTo(fZoneID, language);
}


/**
 * @brief Set this BTimeZone to a new IANA zone identifier and optional language.
 *
 * Clears all cached display-name fields. If \a zoneID is NULL or empty, the
 * system default time zone is used.
 *
 * @param zoneID    IANA time zone identifier, or NULL for the system default.
 * @param language  Language for display names, or NULL for the system default.
 * @return B_OK on success, B_NAME_NOT_FOUND if the zone is unrecognized,
 *         B_NO_MEMORY on allocation failure.
 */
status_t
BTimeZone::SetTo(const char* zoneID, const BLanguage* language)
{
	delete fICULocale;
	fICULocale = NULL;
	delete fICUTimeZone;
	fInitializedFields = 0;

	if (zoneID == NULL || zoneID[0] == '\0')
		fICUTimeZone = TimeZone::createDefault();
	else
		fICUTimeZone = TimeZone::createTimeZone(zoneID);

	if (fICUTimeZone == NULL) {
		fInitStatus = B_NAME_NOT_FOUND;
		return fInitStatus;
	}

	if (language != NULL) {
		fICULocale = new Locale(language->Code());
		if (fICULocale == NULL) {
			fInitStatus = B_NO_MEMORY;
			return fInitStatus;
		}
	}

	UnicodeString unicodeString;
	fICUTimeZone->getID(unicodeString);
	BStringByteSink sink(&fZoneID);
	unicodeString.toUTF8(sink);

	fInitStatus = B_OK;

	return fInitStatus;
}
