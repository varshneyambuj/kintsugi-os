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
 *   Copyright 2003-2012, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Oliver Tappe, zooey@hirschkaefer.de
 */


/**
 * @file LocaleRoster.cpp
 * @brief Implementation of BLocaleRoster, the Locale Kit service locator.
 *
 * BLocaleRoster is the public facade over LocaleRosterData. It exposes methods
 * for querying available languages, countries, time zones, and catalogs, as
 * well as for retrieving flag icons and looking up localized filenames via the
 * SYS:NAME filesystem attribute. The singleton is accessed through Default()
 * which delegates to MutableLocaleRoster.
 *
 * @see MutableLocaleRoster, LocaleRosterData, BCatalog
 */


#include <unicode/uversion.h>
#include <LocaleRoster.h>

#include <assert.h>
#include <ctype.h>

#include <new>

#include <Autolock.h>
#include <Bitmap.h>
#include <Catalog.h>
#include <Entry.h>
#include <FormattingConventions.h>
#include <fs_attr.h>
#include <IconUtils.h>
#include <Language.h>
#include <Locale.h>
#include <LocaleRosterData.h>
#include <MutableLocaleRoster.h>
#include <Node.h>
#include <Roster.h>
#include <String.h>
#include <TimeZone.h>

#include <ICUWrapper.h>
#include <locks.h>

// ICU includes
#include <unicode/locdspnm.h>
#include <unicode/locid.h>
#include <unicode/timezone.h>


using BPrivate::CatalogAddOnInfo;
using BPrivate::MutableLocaleRoster;
U_NAMESPACE_USE


/*
 * several attributes/resource-IDs used within the Locale Kit:
 */
/** @brief Filesystem attribute name that stores the catalog's language tag. */
const char* BLocaleRoster::kCatLangAttr = "BEOS:LOCALE_LANGUAGE";
	// name of catalog language, lives in every catalog file
/** @brief Filesystem attribute name that stores the catalog's MIME signature. */
const char* BLocaleRoster::kCatSigAttr = "BEOS:LOCALE_SIGNATURE";
	// catalog signature, lives in every catalog file
/** @brief Filesystem attribute name that stores the catalog's fingerprint. */
const char* BLocaleRoster::kCatFingerprintAttr = "BEOS:LOCALE_FINGERPRINT";
	// catalog fingerprint, may live in catalog file

/** @brief Filesystem attribute name for an embedded flattened catalog. */
const char* BLocaleRoster::kEmbeddedCatAttr = "BEOS:LOCALE_EMBEDDED_CATALOG";
	// attribute which contains flattened data of embedded catalog
	// this may live in an app- or add-on-file
/** @brief Resource ID used to embed a catalog inside an application binary. */
int32 BLocaleRoster::kEmbeddedCatResId = 0xCADA;
	// a unique value used to identify the resource (=> embedded CAtalog DAta)
	// which contains flattened data of embedded catalog.
	// this may live in an app- or add-on-file


/**
 * @brief Map a language to a representative ISO 3166 country code.
 *
 * For country-specific languages the country code is returned directly.
 * For other well-known languages a hard-coded mapping is used as a fallback
 * so that FirstBootPrompt and similar tools can show a flag.
 *
 * @param language  The BLanguage to map.
 * @return Two-letter country code string, or NULL if unknown.
 */
static const char*
country_code_for_language(const BLanguage& language)
{
	if (language.IsCountrySpecific())
		return language.CountryCode();

	// TODO: implement for real! For now, we just map some well known
	// languages to countries to make FirstBootPrompt happy.
	switch ((tolower(language.Code()[0]) << 8) | tolower(language.Code()[1])) {
		case 'be':	// Belarus
			return "BY";
		case 'cs':	// Czech Republic
			return "CZ";
		case 'da':	// Denmark
			return "DK";
		case 'el':	// Greece
			return "GR";
		case 'en':	// United Kingdom
			return "GB";
		case 'hi':	// India
			return "IN";
		case 'ja':	// Japan
			return "JP";
		case 'ko':	// South Korea
			return "KR";
		case 'nb':	// Norway
			return "NO";
		case 'pa':	// Pakistan
			return "PK";
		case 'sv':	// Sweden
			return "SE";
		case 'uk':	// Ukraine
			return "UA";
		case 'zh':	// China
			return "CN";

		// Languages with a matching country name
		case 'de':	// Germany
		case 'es':	// Spain
		case 'fi':	// Finland
		case 'fr':	// France
		case 'hr':	// Croatia
		case 'hu':	// Hungary
		case 'it':	// Italy
		case 'lt':	// Lithuania
		case 'nl':	// Netherlands
		case 'pl':	// Poland
		case 'pt':	// Portugal
		case 'ro':	// Romania
		case 'ru':	// Russia
		case 'sk':	// Slovakia
			return language.Code();
	}

	return NULL;
}


// #pragma mark -


/**
 * @brief Construct a BLocaleRoster initialized to the "en_US" locale.
 */
BLocaleRoster::BLocaleRoster()
	:
	fData(new(std::nothrow) BPrivate::LocaleRosterData(BLanguage("en_US"),
		BFormattingConventions("en_US")))
{
}


/**
 * @brief Destroy the BLocaleRoster and free its data.
 */
BLocaleRoster::~BLocaleRoster()
{
	delete fData;
}


/**
 * @brief Return the process-wide BLocaleRoster singleton.
 *
 * @return Pointer to the MutableLocaleRoster singleton; never NULL after init.
 */
/*static*/ BLocaleRoster*
BLocaleRoster::Default()
{
	return MutableLocaleRoster::Default();
}


/**
 * @brief Reload locale and time settings from disk, refreshing all caches.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
BLocaleRoster::Refresh()
{
	return fData->Refresh();
}


/**
 * @brief Copy the default time zone into the caller's BTimeZone object.
 *
 * @param timezone  Output BTimeZone to populate.
 * @return B_OK on success, B_BAD_VALUE if NULL, B_ERROR on lock failure.
 */
status_t
BLocaleRoster::GetDefaultTimeZone(BTimeZone* timezone) const
{
	if (!timezone)
		return B_BAD_VALUE;

	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	*timezone = fData->fDefaultTimeZone;

	return B_OK;
}


/**
 * @brief Return a pointer to the process-wide default BLocale.
 *
 * @return Pointer to the default locale; valid for the roster's lifetime.
 */
const BLocale*
BLocaleRoster::GetDefaultLocale() const
{
	return &fData->fDefaultLocale;
}


/**
 * @brief Allocate a BLanguage object for the given language code.
 *
 * The caller takes ownership of the returned BLanguage and must delete it.
 *
 * @param languageCode  BCP-47 language tag string.
 * @param _language     Output pointer that receives the allocated BLanguage.
 * @return B_OK on success, B_BAD_VALUE for NULL or empty input, B_NO_MEMORY
 *         on allocation failure.
 */
status_t
BLocaleRoster::GetLanguage(const char* languageCode,
	BLanguage** _language) const
{
	if (_language == NULL || languageCode == NULL || languageCode[0] == '\0')
		return B_BAD_VALUE;

	BLanguage* language = new(std::nothrow) BLanguage(languageCode);
	if (language == NULL)
		return B_NO_MEMORY;

	*_language = language;
	return B_OK;
}


/**
 * @brief Populate a BMessage with the user's ordered preferred language list.
 *
 * @param languages  Output BMessage; "language" string fields are added.
 * @return B_OK on success, B_BAD_VALUE if NULL, B_ERROR on lock failure.
 */
status_t
BLocaleRoster::GetPreferredLanguages(BMessage* languages) const
{
	if (!languages)
		return B_BAD_VALUE;

	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	*languages = fData->fPreferredLanguages;

	return B_OK;
}


/**
 * \brief Fills \c message with 'language'-fields containing the language-
 * ID(s) of all available languages.
 */
/**
 * @brief Populate a BMessage with all ICU-available language identifiers.
 *
 * @param languages  Output BMessage; "language" string fields are added.
 * @return B_OK on success, B_BAD_VALUE if NULL.
 */
status_t
BLocaleRoster::GetAvailableLanguages(BMessage* languages) const
{
	if (!languages)
		return B_BAD_VALUE;

	int32_t localeCount;
	const Locale* icuLocaleList = Locale::getAvailableLocales(localeCount);

	for (int i = 0; i < localeCount; i++)
		languages->AddString("language", icuLocaleList[i].getName());

	return B_OK;
}


/**
 * @brief Populate a BMessage with all ISO 3166 country codes.
 *
 * @param countries  Output BMessage; "country" string fields are added.
 * @return B_OK on success, B_BAD_VALUE if NULL.
 */
status_t
BLocaleRoster::GetAvailableCountries(BMessage* countries) const
{
	if (!countries)
		return B_BAD_VALUE;

	int32 i;
	const char* const* countryList = uloc_getISOCountries();

	for (i = 0; countryList[i] != NULL; i++)
		countries->AddString("country", countryList[i]);

	return B_OK;
}


/**
 * @brief Populate a BMessage with all ICU time zone identifiers.
 *
 * @param timeZones  Output BMessage; "timeZone" string fields are added.
 * @return B_OK on success, B_BAD_VALUE if NULL, B_ERROR on ICU failure.
 */
status_t
BLocaleRoster::GetAvailableTimeZones(BMessage* timeZones) const
{
	if (!timeZones)
		return B_BAD_VALUE;

	status_t status = B_OK;

	StringEnumeration* zoneList = TimeZone::createEnumeration();

	UErrorCode icuStatus = U_ZERO_ERROR;
	int32 count = zoneList->count(icuStatus);
	if (U_SUCCESS(icuStatus)) {
		for (int i = 0; i < count; ++i) {
			const char* zoneID = zoneList->next(NULL, icuStatus);
			if (zoneID == NULL || !U_SUCCESS(icuStatus)) {
				status = B_ERROR;
				break;
			}
 			timeZones->AddString("timeZone", zoneID);
		}
	} else
		status = B_ERROR;

	delete zoneList;

	return status;
}


/**
 * @brief Populate a BMessage with canonical time zone IDs and their region codes.
 *
 * Each time zone adds both a "timeZone" string and a corresponding "region"
 * string to \a timeZones.
 *
 * @param timeZones  Output BMessage.
 * @return B_OK on success, B_BAD_VALUE if NULL, B_ERROR on ICU failure.
 */
status_t
BLocaleRoster::GetAvailableTimeZonesWithRegionInfo(BMessage* timeZones) const
{
	if (!timeZones)
		return B_BAD_VALUE;

	status_t status = B_OK;

	UErrorCode icuStatus = U_ZERO_ERROR;

	StringEnumeration* zoneList = TimeZone::createTimeZoneIDEnumeration(
		UCAL_ZONE_TYPE_CANONICAL, NULL, NULL, icuStatus);

	int32 count = zoneList->count(icuStatus);
	if (U_SUCCESS(icuStatus)) {
		for (int i = 0; i < count; ++i) {
			const char* zoneID = zoneList->next(NULL, icuStatus);
			if (zoneID == NULL || !U_SUCCESS(icuStatus)) {
				status = B_ERROR;
				break;
			}
			timeZones->AddString("timeZone", zoneID);

			char region[5];
			icuStatus = U_ZERO_ERROR;
			TimeZone::getRegion(zoneID, region, 5, icuStatus);
			if (!U_SUCCESS(icuStatus)) {
				status = B_ERROR;
				break;
			}
			timeZones->AddString("region", region);
		}
	} else
		status = B_ERROR;

	delete zoneList;

	return status;
}


/**
 * @brief Populate a BMessage with time zone IDs available for the given country.
 *
 * Passing NULL for \a countryCode enumerates time zones not bound to any
 * country.
 *
 * @param timeZones   Output BMessage; "timeZone" strings are added.
 * @param countryCode ISO 3166 alpha-2 country code, or NULL.
 * @return B_OK on success, B_BAD_VALUE if timeZones is NULL, B_ERROR on
 *         ICU failure.
 */
status_t
BLocaleRoster::GetAvailableTimeZonesForCountry(BMessage* timeZones,
	const char* countryCode) const
{
	if (!timeZones)
		return B_BAD_VALUE;

	status_t status = B_OK;

	StringEnumeration* zoneList = TimeZone::createEnumeration(countryCode);
		// countryCode == NULL will yield all timezones not bound to a country

	UErrorCode icuStatus = U_ZERO_ERROR;
	int32 count = zoneList->count(icuStatus);
	if (U_SUCCESS(icuStatus)) {
		for (int i = 0; i < count; ++i) {
			const char* zoneID = zoneList->next(NULL, icuStatus);
			if (zoneID == NULL || !U_SUCCESS(icuStatus)) {
				status = B_ERROR;
				break;
			}
			timeZones->AddString("timeZone", zoneID);
		}
	} else
		status = B_ERROR;

	delete zoneList;

	return status;
}


/**
 * @brief Load and render the SVG flag icon for the given country code.
 *
 * Normalizes \a countryCode to lowercase and constructs the resource name
 * "flag-xx". The last two characters of the code are used so that
 * "pt_BR" yields the Brazilian flag.
 *
 * @param flagIcon    Pre-allocated BBitmap to receive the rendered icon.
 * @param countryCode ISO 3166 alpha-2 country code string (at least 2 chars).
 * @return B_OK on success, B_BAD_VALUE for NULL or too-short codes,
 *         B_NAME_NOT_FOUND if the icon resource is absent.
 */
status_t
BLocaleRoster::GetFlagIconForCountry(BBitmap* flagIcon, const char* countryCode)
{
	if (countryCode == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	BResources* resources;
	status_t status = fData->GetResources(&resources);
	if (status != B_OK)
		return status;

	// Normalize the country code: 2 letters uppercase
	// filter things out so that "pt_BR" gives the flag for brazil

	int codeLength = strlen(countryCode);
	if (codeLength < 2)
		return B_BAD_VALUE;

	char normalizedCode[8];
	strcpy(normalizedCode, "flag-");
	normalizedCode[5] = tolower(countryCode[codeLength - 2]);
	normalizedCode[6] = tolower(countryCode[codeLength - 1]);
	normalizedCode[7] = '\0';

	size_t size;
	const void* buffer = resources->LoadResource(B_VECTOR_ICON_TYPE,
		normalizedCode, &size);
	if (buffer == NULL || size == 0)
		return B_NAME_NOT_FOUND;

	return BIconUtils::GetVectorIcon(static_cast<const uint8*>(buffer), size,
		flagIcon);
}


/**
 * @brief Load and render the flag icon associated with a language code.
 *
 * First tries to find an icon named with the first two lowercase characters
 * of \a languageCode. If none exists, falls back to GetFlagIconForCountry()
 * using the default country for the language.
 *
 * @param flagIcon      Pre-allocated BBitmap for the rendered icon.
 * @param languageCode  BCP-47 language tag (at least 2 characters).
 * @return B_OK on success, B_BAD_VALUE for invalid input, B_NAME_NOT_FOUND
 *         if no icon or country mapping exists.
 */
status_t
BLocaleRoster::GetFlagIconForLanguage(BBitmap* flagIcon,
	const char* languageCode)
{
	if (languageCode == NULL || languageCode[0] == '\0'
		|| languageCode[1] == '\0')
		return B_BAD_VALUE;

	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	BResources* resources;
	status_t status = fData->GetResources(&resources);
	if (status != B_OK)
		return status;

	// Normalize the language code: first two letters, lowercase

	char normalizedCode[3];
	normalizedCode[0] = tolower(languageCode[0]);
	normalizedCode[1] = tolower(languageCode[1]);
	normalizedCode[2] = '\0';

	size_t size;
	const void* buffer = resources->LoadResource(B_VECTOR_ICON_TYPE,
		normalizedCode, &size);
	if (buffer != NULL && size != 0) {
		return BIconUtils::GetVectorIcon(static_cast<const uint8*>(buffer),
			size, flagIcon);
	}

	// There is no language flag, try to get the default country's flag for
	// the language instead.

	BLanguage language(languageCode);
	const char* countryCode = country_code_for_language(language);
	if (countryCode == NULL)
		return B_NAME_NOT_FOUND;

	return GetFlagIconForCountry(flagIcon, countryCode);
}


/**
 * @brief Populate a BMessage with language identifiers from all catalog add-ons.
 *
 * Iterates over loaded catalog add-ons and calls each one's
 * get_available_languages function, filtered by signature and language patterns.
 *
 * @param languageList  Output BMessage; "language" strings are added.
 * @param sigPattern    Signature pattern to filter catalogs (required).
 * @param langPattern   Language pattern filter (passed through to add-ons).
 * @param fingerprint   Fingerprint filter (passed through to add-ons).
 * @return B_OK on success, B_BAD_VALUE if languageList is NULL, B_ERROR on
 *         lock failure.
 */
status_t
BLocaleRoster::GetAvailableCatalogs(BMessage*  languageList,
	const char* sigPattern,	const char* langPattern, int32 fingerprint) const
{
	if (languageList == NULL)
		return B_BAD_VALUE;

	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	int32 count = fData->fCatalogAddOnInfos.CountItems();
	for (int32 i = 0; i < count; ++i) {
		CatalogAddOnInfo* info
			= (CatalogAddOnInfo*)fData->fCatalogAddOnInfos.ItemAt(i);

		if (!info->fLanguagesFunc)
			continue;

		info->fLanguagesFunc(languageList, sigPattern, langPattern,
			fingerprint);
	}

	return B_OK;
}


/**
 * @brief Return whether filesystem entry names should be translated.
 *
 * @return true if the user prefers translated filenames.
 */
bool
BLocaleRoster::IsFilesystemTranslationPreferred() const
{
	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	return fData->fIsFilesystemTranslationPreferred;
}


/*!	\brief Looks up a localized filename from a catalog.
	\param localizedFileName A pre-allocated BString object for the result
		of the lookup.
	\param ref An entry_ref with an attribute holding data for catalog lookup.
	\param traverse A boolean to decide if symlinks are to be traversed.
	\return
	- \c B_OK: success
	- \c B_ENTRY_NOT_FOUND: failure. Attribute not found, entry not found
		in catalog, etc
	- other error codes: failure

	Attribute format:  "signature:context:string"
	(no colon in any of signature, context and string)

	Lookup is done for the top preferred language, only.
	Lookup fails if a comment is present in the catalog entry.
*/
/**
 * @brief Look up the localized filename for a filesystem entry.
 *
 * Reads the "SYS:NAME" attribute from \a ref (format: "sig:context:string"),
 * locates the catalog for that signature, and returns the translated string.
 *
 * @param localizedFileName  Output BString for the translated filename.
 * @param ref                entry_ref of the file to look up.
 * @param traverse           Whether to traverse symlinks.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if not found, or another error.
 */
status_t
BLocaleRoster::GetLocalizedFileName(BString& localizedFileName,
	const entry_ref& ref, bool traverse)
{
	BString signature;
	BString context;
	BString string;

	status_t status = _PrepareCatalogEntry(ref, signature, context, string,
		traverse);

	if (status != B_OK)
		return status;

	// Try to get entry_ref for signature from above
	BRoster roster;
	entry_ref catalogRef;
	// The signature is missing application/
	signature.Prepend("application/");
	status = roster.FindApp(signature, &catalogRef);
	if (status != B_OK)
		return status;

	BCatalog catalog(catalogRef);
	const char* temp = catalog.GetString(string, context);

	if (temp == NULL)
		return B_ENTRY_NOT_FOUND;

	localizedFileName = temp;
	return B_OK;
}


/**
 * @brief One-shot init callback for a per-library static BCatalog.
 *
 * Identifies the shared library that contains \a catalog by scanning image
 * info, then calls catalog->SetTo() with the library's entry_ref.
 *
 * @param param  Pointer to the BCatalog to initialize.
 * @return B_OK on success, B_NAME_NOT_FOUND if the image is not found.
 */
static status_t
_InitializeCatalog(void* param)
{
	BCatalog* catalog = (BCatalog*)param;

	// figure out image (shared object) from catalog address
	image_info info;
	int32 cookie = 0;
	bool found = false;

	while (get_next_image_info(0, &cookie, &info) == B_OK) {
		if ((char*)info.data < (char*)catalog && (char*)info.data
				+ info.data_size > (char*)catalog) {
			found = true;
			break;
		}
	}

	if (!found)
		return B_NAME_NOT_FOUND;

	// load the catalog for this mimetype
	entry_ref ref;
	if (BEntry(info.name).GetRef(&ref) == B_OK && catalog->SetTo(ref) == B_OK)
		return B_OK;

	return B_ERROR;
}


/**
 * @brief Ensure a per-library BCatalog is initialized exactly once and return it.
 *
 * Uses the init-once mechanism to call _InitializeCatalog() on first access.
 * Used internally by the translation macros.
 *
 * @param catalog            The static BCatalog to initialize.
 * @param catalogInitStatus  Init-once guard variable for this catalog.
 * @return Pointer to \a catalog after initialization.
 */
BCatalog*
BLocaleRoster::_GetCatalog(BCatalog* catalog, int32* catalogInitStatus)
{
	// This function is used in the translation macros, so it can't return a
	// status_t. Maybe it could throw exceptions ?

	__init_once(catalogInitStatus, _InitializeCatalog, catalog);
	return catalog;
}


/**
 * @brief Parse the "SYS:NAME" attribute of a filesystem entry into components.
 *
 * The attribute must have the format "signature:context:string" with exactly
 * two colons and no empty components.
 *
 * @param ref        entry_ref of the file to read from.
 * @param signature  Output BString for the signature component.
 * @param context    Output BString for the context component.
 * @param string     Output BString for the string component.
 * @param traverse   Whether to traverse symlinks when opening the entry.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the entry or attribute is
 *         absent or malformed.
 */
status_t
BLocaleRoster::_PrepareCatalogEntry(const entry_ref& ref, BString& signature,
	BString& context, BString& string, bool traverse)
{
	BEntry entry(&ref, traverse);
	if (!entry.Exists())
		return B_ENTRY_NOT_FOUND;

	BNode node(&entry);
	status_t status = node.InitCheck();
	if (status != B_OK)
		return status;

	status = node.ReadAttrString("SYS:NAME", &signature);
	if (status != B_OK)
		return status;

	int32 first = signature.FindFirst(':');
	int32 last = signature.FindLast(':');
	if (first == last)
		return B_ENTRY_NOT_FOUND;

	context = signature;
	string = signature;

	signature.Truncate(first);
	context.Truncate(last);
	context.Remove(0, first + 1);
	string.Remove(0, last + 1);

	if (signature.Length() == 0 || context.Length() == 0
		|| string.Length() == 0)
		return B_ENTRY_NOT_FOUND;

	return B_OK;
}
