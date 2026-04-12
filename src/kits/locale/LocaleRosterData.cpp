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
 * @file LocaleRosterData.cpp
 * @brief Implementation of LocaleRosterData and CatalogAddOnInfo.
 *
 * LocaleRosterData holds the mutable state shared by BLocaleRoster and
 * MutableLocaleRoster: the list of catalog add-on descriptors, the default
 * locale, time zone, and preferred language list, and the resource loader
 * for flag icons. CatalogAddOnInfo describes a single catalog add-on (either
 * the built-in default or a plugin found in the system add-ons directories).
 *
 * @see BLocaleRoster, MutableLocaleRoster, DefaultCatalog
 */


#include <unicode/uversion.h>
#include <LocaleRosterData.h>

#include <Autolock.h>
#include <Catalog.h>
#include <Collator.h>
#include <Debug.h>
#include <DefaultCatalog.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <FormattingConventions.h>
#include <Language.h>
#include <Locale.h>
#include <Node.h>
#include <Path.h>
#include <PathFinder.h>
#include <Roster.h>
#include <String.h>
#include <StringList.h>
#include <TimeZone.h>

// ICU includes
#include <unicode/locid.h>
#include <unicode/timezone.h>


U_NAMESPACE_USE


namespace BPrivate {


// #pragma mark - CatalogAddOnInfo


/**
 * @brief Construct a CatalogAddOnInfo descriptor.
 *
 * @param name      Filename of the catalog add-on shared object.
 * @param path      Directory path containing the add-on, or "" for embedded.
 * @param priority  Loading priority; higher = checked first.
 */
CatalogAddOnInfo::CatalogAddOnInfo(const BString& name, const BString& path,
	uint8 priority)
	:
	fInstantiateFunc(NULL),
	fCreateFunc(NULL),
	fLanguagesFunc(NULL),
	fName(name),
	fPath(path),
	fAddOnImage(B_NO_INIT),
	fPriority(priority),
	fIsEmbedded(path.Length()==0)
{
}


/**
 * @brief Destroy the CatalogAddOnInfo, deleting all loaded catalogs and unloading.
 */
CatalogAddOnInfo::~CatalogAddOnInfo()
{
	int32 count = fLoadedCatalogs.CountItems();
	for (int32 i = 0; i < count; ++i) {
		BCatalogData* cat
			= static_cast<BCatalogData*>(fLoadedCatalogs.ItemAt(i));
		delete cat;
	}
	fLoadedCatalogs.MakeEmpty();
	UnloadIfPossible();
}


/**
 * @brief Load the catalog add-on image if it has not been loaded yet.
 *
 * For embedded add-ons this is a no-op (they are always "loaded").
 * For external add-ons the shared object is dlopen'd and the three
 * well-known symbols are resolved.
 *
 * @return true if the add-on is ready to use, false if loading failed.
 */
bool
CatalogAddOnInfo::MakeSureItsLoaded()
{
	if (!fIsEmbedded && fAddOnImage < B_OK) {
		// add-on has not been loaded yet, so we try to load it:
		BString fullAddOnPath(fPath);
		fullAddOnPath << "/" << fName;
		fAddOnImage = load_add_on(fullAddOnPath.String());
		if (fAddOnImage >= B_OK) {
			get_image_symbol(fAddOnImage, "instantiate_catalog",
				B_SYMBOL_TYPE_TEXT, (void**)&fInstantiateFunc);
			get_image_symbol(fAddOnImage, "create_catalog",
				B_SYMBOL_TYPE_TEXT, (void**)&fCreateFunc);
			get_image_symbol(fAddOnImage, "get_available_languages",
				B_SYMBOL_TYPE_TEXT, (void**)&fLanguagesFunc);
		} else
			return false;
	} else if (fIsEmbedded) {
		// The built-in catalog still has to provide this function
		fLanguagesFunc = default_catalog_get_available_languages;
	}
	return true;
}


/**
 * @brief Unload the add-on image if no catalogs remain loaded from it.
 *
 * This is a no-op for embedded add-ons.
 */
void
CatalogAddOnInfo::UnloadIfPossible()
{
	if (!fIsEmbedded && fLoadedCatalogs.IsEmpty()) {
		unload_add_on(fAddOnImage);
		fAddOnImage = B_NO_INIT;
		fInstantiateFunc = NULL;
		fCreateFunc = NULL;
		fLanguagesFunc = NULL;
	}
}


// #pragma mark - LocaleRosterData


namespace {


/** @brief Filesystem attribute name that stores a catalog add-on's priority. */
static const char* kPriorityAttr = "ADDON:priority";

/** @brief BMessage field name for preferred language entries. */
static const char* kLanguageField = "language";
/** @brief BMessage field name for the default time zone. */
static const char* kTimezoneField = "timezone";
/** @brief BMessage field name for the filesystem translation preference. */
static const char* kTranslateFilesystemField = "filesys";


}	// anonymous namespace


/**
 * @brief Construct a LocaleRosterData with the given default language and conventions.
 *
 * Calls _Initialize() to discover catalog add-ons and load the user's locale
 * settings from disk.
 *
 * @param language     Initial default language.
 * @param conventions  Initial formatting conventions.
 */
LocaleRosterData::LocaleRosterData(const BLanguage& language,
	const BFormattingConventions& conventions)
	:
	fLock("LocaleRosterData"),
	fDefaultLocale(&language, &conventions),
	fIsFilesystemTranslationPreferred(true),
	fAreResourcesLoaded(false)
{
	fInitStatus = _Initialize();
}


/**
 * @brief Destroy the LocaleRosterData, cleaning up all catalog add-on state.
 */
LocaleRosterData::~LocaleRosterData()
{
	BAutolock lock(fLock);

	_CleanupCatalogAddOns();
}


/**
 * @brief Return the initialization status of this object.
 *
 * @return B_OK if resources are loaded, B_NO_INIT otherwise.
 */
status_t
LocaleRosterData::InitCheck() const
{
	return fAreResourcesLoaded ? B_OK : B_NO_INIT;
}


/**
 * @brief Reload locale and time settings from disk.
 *
 * @return B_OK on success, B_ERROR on lock failure.
 */
status_t
LocaleRosterData::Refresh()
{
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	_LoadLocaleSettings();
	_LoadTimeSettings();

	return B_OK;
}


/**
 * @brief Comparator for sorting CatalogAddOnInfo pointers by priority.
 *
 * @param left   Pointer to a const CatalogAddOnInfo pointer.
 * @param right  Pointer to a const CatalogAddOnInfo pointer.
 * @return Negative if left has lower priority, zero if equal, positive if higher.
 */
int
LocaleRosterData::CompareInfos(const void* left, const void* right)
{
	const CatalogAddOnInfo* leftInfo
		= * static_cast<const CatalogAddOnInfo* const *>(left);
	const CatalogAddOnInfo* rightInfo
		= * static_cast<const CatalogAddOnInfo* const *>(right);

	return leftInfo->fPriority - rightInfo->fPriority;
}


/**
 * @brief Set the default formatting conventions and persist them to disk.
 *
 * Broadcasts B_LOCALE_CHANGED to all running applications after saving.
 *
 * @param newFormattingConventions  The new BFormattingConventions to use.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LocaleRosterData::SetDefaultFormattingConventions(
	const BFormattingConventions& newFormattingConventions)
{
	status_t status = B_OK;

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	status = _SetDefaultFormattingConventions(newFormattingConventions);

	if (status == B_OK)
		status = _SaveLocaleSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddDefaultFormattingConventionsToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}

	return status;
}


/**
 * @brief Set the default time zone and persist it to disk.
 *
 * Broadcasts B_LOCALE_CHANGED to all running applications after saving.
 *
 * @param newZone  The new BTimeZone to use.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LocaleRosterData::SetDefaultTimeZone(const BTimeZone& newZone)
{
	status_t status = B_OK;

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	status = _SetDefaultTimeZone(newZone);

	if (status == B_OK)
		status = _SaveTimeSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddDefaultTimeZoneToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}

	return status;
}


/**
 * @brief Set the preferred language list and persist it to disk.
 *
 * Broadcasts B_LOCALE_CHANGED to all running applications after saving.
 *
 * @param languages  BMessage containing "language" string fields in priority order.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LocaleRosterData::SetPreferredLanguages(const BMessage* languages)
{
	status_t status = B_OK;

	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	status = _SetPreferredLanguages(languages);

	if (status == B_OK)
		status = _SaveLocaleSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddPreferredLanguagesToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}

	return status;
}


/**
 * @brief Set the filesystem translation preference and persist it to disk.
 *
 * Broadcasts B_LOCALE_CHANGED to all running applications after saving.
 *
 * @param preferred  true to translate filesystem entry names, false to leave them as-is.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LocaleRosterData::SetFilesystemTranslationPreferred(bool preferred)
{
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	_SetFilesystemTranslationPreferred(preferred);

	status_t status = _SaveLocaleSettings();

	if (status == B_OK) {
		BMessage updateMessage(B_LOCALE_CHANGED);
		status = _AddFilesystemTranslationPreferenceToMessage(&updateMessage);
		if (status == B_OK)
			status = be_roster->Broadcast(&updateMessage);
	}

	return status;
}


/**
 * @brief Lazily load the liblocale.so BResources object and return it.
 *
 * On first call the resource file is opened from the image containing
 * BLocaleRoster::Default and all vector icon resources are preloaded.
 *
 * @param resources  Output pointer that receives the BResources pointer.
 * @return B_OK on success, or an error code on failure.
 */
status_t
LocaleRosterData::GetResources(BResources** resources)
{
	if (resources == NULL)
		return B_BAD_VALUE;

	if (!fAreResourcesLoaded) {
		status_t result
			= fResources.SetToImage((const void*)&BLocaleRoster::Default);
		if (result != B_OK)
			return result;

		result = fResources.PreloadResourceType();
		if (result != B_OK)
			return result;

		fAreResourcesLoaded = true;
	}

	*resources = &fResources;
	return B_OK;
}


/**
 * @brief Run full initialization: discover add-ons and load settings.
 *
 * @return B_OK on success, or an error code from _InitializeCatalogAddOns()
 *         or Refresh().
 */
status_t
LocaleRosterData::_Initialize()
{
	status_t result = _InitializeCatalogAddOns();
	if (result != B_OK)
		return result;

	if ((result = Refresh()) != B_OK)
		return result;

	fInitStatus = B_OK;
	return B_OK;
}


/*
iterate over add-on-folders and collect information about each
catalog-add-ons (types of catalogs) into fCatalogAddOnInfos.
*/
/**
 * @brief Scan catalog add-on directories and populate fCatalogAddOnInfos.
 *
 * Registers the embedded DefaultCatalog first, then scans all add-on
 * directories under B_FIND_PATH_ADD_ONS_DIRECTORY/locale/catalogs/ for
 * add-on shared objects, reading their priority from a filesystem attribute
 * (loading them temporarily if needed).
 *
 * @return B_OK on success, B_NO_MEMORY on allocation failure, B_ERROR on lock
 *         failure.
 */
status_t
LocaleRosterData::_InitializeCatalogAddOns()
{
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	// add info about embedded default catalog:
	CatalogAddOnInfo* defaultCatalogAddOnInfo
		= new(std::nothrow) CatalogAddOnInfo("Default", "",
			DefaultCatalog::kDefaultCatalogAddOnPriority);
	if (!defaultCatalogAddOnInfo)
		return B_NO_MEMORY;

	defaultCatalogAddOnInfo->MakeSureItsLoaded();
	defaultCatalogAddOnInfo->fInstantiateFunc = DefaultCatalog::Instantiate;
	defaultCatalogAddOnInfo->fCreateFunc = DefaultCatalog::Create;
	fCatalogAddOnInfos.AddItem((void*)defaultCatalogAddOnInfo);

	BStringList folders;
	BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "locale/catalogs/",
		B_FIND_PATH_EXISTING_ONLY, folders);

	BPath addOnPath;
	BDirectory addOnFolder;
	char buf[4096];
	status_t err;
	for (int32 f = 0; f < folders.CountStrings(); f++) {
		BString addOnFolderName = folders.StringAt(f);
		err = addOnFolder.SetTo(addOnFolderName.String());
		if (err != B_OK)
			continue;

		// scan through all the folder's entries for catalog add-ons:
		int32 count;
		int8 priority;
		entry_ref eref;
		BNode node;
		BEntry entry;
		dirent* dent;
		while ((count = addOnFolder.GetNextDirents((dirent*)buf, sizeof(buf)))
				> 0) {
			dent = (dirent*)buf;
			while (count-- > 0) {
				if (strcmp(dent->d_name, ".") != 0
						&& strcmp(dent->d_name, "..") != 0
						&& strcmp(dent->d_name, "x86") != 0
						&& strcmp(dent->d_name, "x86_gcc2") != 0) {
					// we have found (what should be) a catalog-add-on:
					eref.device = dent->d_pdev;
					eref.directory = dent->d_pino;
					eref.set_name(dent->d_name);
					entry.SetTo(&eref, true);
						// traverse through any links to get to the real thang!
					node.SetTo(&entry);
					priority = -1;
					if (node.ReadAttr(kPriorityAttr, B_INT8_TYPE, 0,
						&priority, sizeof(int8)) <= 0) {
						// add-on has no priority-attribute yet, so we load it
						// to fetch the priority from the corresponding
						// symbol...
						BString fullAddOnPath(addOnFolderName);
						fullAddOnPath << "/" << dent->d_name;
						image_id image = load_add_on(fullAddOnPath.String());
						if (image >= B_OK) {
							uint8* prioPtr;
							if (get_image_symbol(image, "gCatalogAddOnPriority",
								B_SYMBOL_TYPE_DATA,
								(void**)&prioPtr) == B_OK) {
								priority = *prioPtr;
								node.WriteAttr(kPriorityAttr, B_INT8_TYPE, 0,
									&priority, sizeof(int8));
							}
							unload_add_on(image);
						}
					}

					if (priority >= 0) {
						// add-ons with priority < 0 will be ignored
						CatalogAddOnInfo* addOnInfo
							= new(std::nothrow) CatalogAddOnInfo(dent->d_name,
								addOnFolderName, priority);
						if (addOnInfo != NULL) {
							if (addOnInfo->MakeSureItsLoaded())
								fCatalogAddOnInfos.AddItem((void*)addOnInfo);
							else
								delete addOnInfo;
						}
					}
				}
				// Bump the dirent-pointer by length of the dirent just handled:
				dent = (dirent*)((char*)dent + dent->d_reclen);
			}
		}
	}
	fCatalogAddOnInfos.SortItems(CompareInfos);

	return B_OK;
}


/*
 * unloads all catalog-add-ons (which will throw away all loaded catalogs, too)
 */
/**
 * @brief Delete all CatalogAddOnInfo objects and clear the add-on list.
 */
void
LocaleRosterData::_CleanupCatalogAddOns()
{
	BAutolock lock(fLock);
	if (!lock.IsLocked())
		return;

	int32 count = fCatalogAddOnInfos.CountItems();
	for (int32 i = 0; i<count; ++i) {
		CatalogAddOnInfo* info
			= static_cast<CatalogAddOnInfo*>(fCatalogAddOnInfos.ItemAt(i));
		delete info;
	}
	fCatalogAddOnInfos.MakeEmpty();
}


/**
 * @brief Load the user's locale settings from B_USER_SETTINGS_DIRECTORY.
 *
 * Falls back to English defaults if the settings file is absent or invalid.
 *
 * @return B_OK on success, or an error code if the file cannot be read.
 */
status_t
LocaleRosterData::_LoadLocaleSettings()
{
	BPath path;
	BFile file;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status == B_OK) {
		path.Append("Locale settings");
		status = file.SetTo(path.Path(), B_READ_ONLY);
	}
	BMessage settings;
	if (status == B_OK)
		status = settings.Unflatten(&file);

	if (status == B_OK) {
		BFormattingConventions conventions(&settings);
		fDefaultLocale.SetFormattingConventions(conventions);

		_SetPreferredLanguages(&settings);

		bool preferred;
		if (settings.FindBool(kTranslateFilesystemField, &preferred) == B_OK)
			_SetFilesystemTranslationPreferred(preferred);

		return B_OK;
	}


	// Something went wrong (no settings file or invalid BMessage), so we
	// set everything to default values

	fPreferredLanguages.MakeEmpty();
	fPreferredLanguages.AddString(kLanguageField, "en");
	BLanguage defaultLanguage("en_US");
	fDefaultLocale.SetLanguage(defaultLanguage);
	BFormattingConventions conventions("en_US");
	fDefaultLocale.SetFormattingConventions(conventions);

	return status;
}


/**
 * @brief Load the user's time settings from B_USER_SETTINGS_DIRECTORY.
 *
 * Falls back to GMT if the settings file is absent or invalid.
 *
 * @return B_OK on success, or an error code if the file cannot be read.
 */
status_t
LocaleRosterData::_LoadTimeSettings()
{
	BPath path;
	BFile file;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status == B_OK) {
		path.Append("Time settings");
		status = file.SetTo(path.Path(), B_READ_ONLY);
	}
	BMessage settings;
	if (status == B_OK)
		status = settings.Unflatten(&file);
	if (status == B_OK) {
		BString timeZoneID;
		if (settings.FindString(kTimezoneField, &timeZoneID) == B_OK)
			_SetDefaultTimeZone(BTimeZone(timeZoneID.String()));
		else
			_SetDefaultTimeZone(BTimeZone(BTimeZone::kNameOfGmtZone));

		return B_OK;
	}

	// Something went wrong (no settings file or invalid BMessage), so we
	// set everything to default values
	_SetDefaultTimeZone(BTimeZone(BTimeZone::kNameOfGmtZone));

	return status;
}


/**
 * @brief Persist the current locale settings to B_USER_SETTINGS_DIRECTORY.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
LocaleRosterData::_SaveLocaleSettings()
{
	BMessage settings;
	status_t status = _AddDefaultFormattingConventionsToMessage(&settings);
	if (status == B_OK)
		_AddPreferredLanguagesToMessage(&settings);
	if (status == B_OK)
		_AddFilesystemTranslationPreferenceToMessage(&settings);

	BPath path;
	if (status == B_OK)
		status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);

	BFile file;
	if (status == B_OK) {
		path.Append("Locale settings");
		status = file.SetTo(path.Path(),
			B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	}
	if (status == B_OK)
		status = settings.Flatten(&file);
	if (status == B_OK)
		status = file.Sync();

	return status;
}


/**
 * @brief Persist the current time settings to B_USER_SETTINGS_DIRECTORY.
 *
 * @return B_OK on success, or an error code on failure.
 */
status_t
LocaleRosterData::_SaveTimeSettings()
{
	BMessage settings;
	status_t status = _AddDefaultTimeZoneToMessage(&settings);

	BPath path;
	if (status == B_OK)
		status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);

	BFile file;
	if (status == B_OK) {
		path.Append("Time settings");
		status = file.SetTo(path.Path(),
			B_CREATE_FILE | B_ERASE_FILE | B_WRITE_ONLY);
	}
	if (status == B_OK)
		status = settings.Flatten(&file);
	if (status == B_OK)
		status = file.Sync();

	return status;
}


/**
 * @brief Apply new formatting conventions to the default locale and ICU global state.
 *
 * @param newFormattingConventions  The new conventions to apply.
 * @return B_OK on success, B_ERROR if the ICU canonical locale is bogus.
 */
status_t
LocaleRosterData::_SetDefaultFormattingConventions(
	const BFormattingConventions& newFormattingConventions)
{
	fDefaultLocale.SetFormattingConventions(newFormattingConventions);

	UErrorCode icuError = U_ZERO_ERROR;
	Locale icuLocale = Locale::createCanonical(newFormattingConventions.ID());
	if (icuLocale.isBogus())
		return B_ERROR;

	Locale::setDefault(icuLocale, icuError);
	if (!U_SUCCESS(icuError))
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Set the ICU default time zone from a BTimeZone and store it.
 *
 * @param newZone  The BTimeZone to adopt as the global ICU default.
 * @return B_OK on success, B_ERROR if the ICU time zone cannot be created.
 */
status_t
LocaleRosterData::_SetDefaultTimeZone(const BTimeZone& newZone)
{
	fDefaultTimeZone = newZone;

	TimeZone* timeZone = TimeZone::createTimeZone(newZone.ID().String());
	if (timeZone == NULL)
		return B_ERROR;
	TimeZone::adoptDefault(timeZone);

	return B_OK;
}


/**
 * @brief Update the preferred language list from a BMessage.
 *
 * Also updates the default locale's language and collator to match the first
 * preferred language. Falls back to English if the message is empty or NULL.
 *
 * @param languages  BMessage with "language" string fields, or NULL.
 * @return B_OK always.
 */
status_t
LocaleRosterData::_SetPreferredLanguages(const BMessage* languages)
{
	BString langName;
	if (languages != NULL
		&& languages->FindString(kLanguageField, &langName) == B_OK) {
		fDefaultLocale.SetCollator(BCollator(langName.String()));
		fDefaultLocale.SetLanguage(BLanguage(langName.String()));

		fPreferredLanguages.RemoveName(kLanguageField);
		for (int i = 0; languages->FindString(kLanguageField, i, &langName)
				== B_OK; i++) {
			fPreferredLanguages.AddString(kLanguageField, langName);
		}
	} else {
		fPreferredLanguages.MakeEmpty();
		fPreferredLanguages.AddString(kLanguageField, "en");
		fDefaultLocale.SetCollator(BCollator("en"));
	}

	return B_OK;
}


/**
 * @brief Store the filesystem translation preference flag.
 *
 * @param preferred  true to translate filesystem entry names.
 */
void
LocaleRosterData::_SetFilesystemTranslationPreferred(bool preferred)
{
	fIsFilesystemTranslationPreferred = preferred;
}


/**
 * @brief Add the current formatting conventions to a BMessage.
 *
 * @param message  Output BMessage to populate.
 * @return B_OK on success, or an error code from Archive().
 */
status_t
LocaleRosterData::_AddDefaultFormattingConventionsToMessage(
	BMessage* message) const
{
	BFormattingConventions conventions;
	fDefaultLocale.GetFormattingConventions(&conventions);

	return conventions.Archive(message);
}


/**
 * @brief Add the current time zone ID to a BMessage.
 *
 * @param message  Output BMessage to populate.
 * @return B_OK on success, or an error from AddString().
 */
status_t
LocaleRosterData::_AddDefaultTimeZoneToMessage(BMessage* message) const
{
	return message->AddString(kTimezoneField, fDefaultTimeZone.ID());
}


/**
 * @brief Add all preferred language tags to a BMessage.
 *
 * @param message  Output BMessage; "language" string fields are added.
 * @return B_OK on success, or an error code from AddString().
 */
status_t
LocaleRosterData::_AddPreferredLanguagesToMessage(BMessage* message) const
{
	status_t status = B_OK;

	BString langName;
	for (int i = 0; fPreferredLanguages.FindString("language", i,
			&langName) == B_OK; i++) {
		status = message->AddString(kLanguageField, langName);
		if (status != B_OK)
			break;
	}

	return status;
}


/**
 * @brief Add the filesystem translation preference flag to a BMessage.
 *
 * @param message  Output BMessage to populate.
 * @return B_OK on success, or an error from AddBool().
 */
status_t
LocaleRosterData::_AddFilesystemTranslationPreferenceToMessage(
	BMessage* message) const
{
	return message->AddBool(kTranslateFilesystemField,
		fIsFilesystemTranslationPreferred);
}


}	// namespace BPrivate
