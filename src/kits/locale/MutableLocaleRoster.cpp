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
 * @file MutableLocaleRoster.cpp
 * @brief Implementation of MutableLocaleRoster, the write-capable singleton.
 *
 * MutableLocaleRoster extends BLocaleRoster with methods for changing the
 * system locale, loading and unloading catalogs, and loading the system-wide
 * translation catalog for libbe. The singleton is created on first access via
 * Default() using pthread_once to ensure thread safety.
 *
 * @see BLocaleRoster, LocaleRosterData
 */


#include <MutableLocaleRoster.h>

#include <pthread.h>

#include <Application.h>
#include <Autolock.h>
#include <Catalog.h>
#include <CatalogData.h>
#include <DefaultCatalog.h>
#include <Debug.h>
#include <Entry.h>
#include <FormattingConventions.h>
#include <Language.h>
#include <LocaleRosterData.h>
#include <String.h>


namespace BPrivate {


namespace {


/** @brief The process-wide MutableLocaleRoster singleton instance. */
static MutableLocaleRoster* sLocaleRoster;

/** @brief pthread_once guard for singleton initialization. */
static pthread_once_t sLocaleRosterInitOnce = PTHREAD_ONCE_INIT;


}	// anonymous namespace


/**
 * @brief Allocate the process-wide MutableLocaleRoster singleton.
 *
 * Called exactly once by pthread_once from Default().
 */
static void
InitializeLocaleRoster()
{
	sLocaleRoster = new (std::nothrow) MutableLocaleRoster();
}


/**
 * @brief Construct a MutableLocaleRoster (private; use Default()).
 */
MutableLocaleRoster::MutableLocaleRoster()
{
}


/**
 * @brief Destroy the MutableLocaleRoster.
 */
MutableLocaleRoster::~MutableLocaleRoster()
{
}


/**
 * @brief Return the process-wide MutableLocaleRoster singleton.
 *
 * Initializes the singleton on first call using pthread_once.
 *
 * @return Pointer to the singleton; never NULL after successful init.
 */
/*static*/ MutableLocaleRoster*
MutableLocaleRoster::Default()
{
	if (sLocaleRoster == NULL)
		pthread_once(&sLocaleRosterInitOnce, &InitializeLocaleRoster);

	return sLocaleRoster;
}


/**
 * @brief Set the default formatting conventions, persisting the change to disk.
 *
 * @param newFormattingConventions  New BFormattingConventions to adopt.
 * @return B_OK on success, or an error code from LocaleRosterData.
 */
status_t
MutableLocaleRoster::SetDefaultFormattingConventions(
	const BFormattingConventions& newFormattingConventions)
{
	return fData->SetDefaultFormattingConventions(newFormattingConventions);
}


/**
 * @brief Set the default time zone, persisting the change to disk.
 *
 * @param newZone  New BTimeZone to adopt.
 * @return B_OK on success, or an error code from LocaleRosterData.
 */
status_t
MutableLocaleRoster::SetDefaultTimeZone(const BTimeZone& newZone)
{
	return fData->SetDefaultTimeZone(newZone);
}


/**
 * @brief Set the preferred language list, persisting the change to disk.
 *
 * @param languages  BMessage with ordered "language" string fields.
 * @return B_OK on success, or an error code from LocaleRosterData.
 */
status_t
MutableLocaleRoster::SetPreferredLanguages(const BMessage* languages)
{
	return fData->SetPreferredLanguages(languages);
}


/**
 * @brief Set whether filesystem entry names should be translated.
 *
 * @param preferred  true to enable translation, false to disable.
 * @return B_OK on success, or an error code from LocaleRosterData.
 */
status_t
MutableLocaleRoster::SetFilesystemTranslationPreferred(bool preferred)
{
	return fData->SetFilesystemTranslationPreferred(preferred);
}


/**
 * @brief Load the libbe system catalog into the given BCatalog.
 *
 * Scans the running process's image list to find the libbe shared object
 * (identified by the address of be_app), then calls catalog->SetTo() with
 * the library's entry_ref.
 *
 * @param catalog  Output BCatalog to populate with libbe's translations.
 * @return B_OK on success, B_BAD_VALUE if catalog is NULL, B_ERROR if the
 *         library image cannot be found.
 */
status_t
MutableLocaleRoster::LoadSystemCatalog(BCatalog* catalog) const
{
	if (!catalog)
		return B_BAD_VALUE;

	// figure out libbe-image (shared object) by name
	image_info info;
	int32 cookie = 0;
	bool found = false;

	while (get_next_image_info(0, &cookie, &info) == B_OK) {
		if (info.data < (void*)&be_app
			&& (char*)info.data + info.data_size > (void*)&be_app) {
			found = true;
			break;
		}
	}

	if (!found)
		return B_ERROR;

	// load the catalog for libbe into the given catalog
	entry_ref ref;
	status_t status = BEntry(info.name).GetRef(&ref);
	if (status != B_OK)
		return status;

	return catalog->SetTo(ref);
}


/*
 * creates a new (empty) catalog of the given type (the request is dispatched
 * to the appropriate add-on).
 * If the add-on doesn't support catalog-creation or if the creation fails,
 * NULL is returned, otherwise a pointer to the freshly created catalog.
 * Any created catalog will be initialized with the given signature and
 * language-name.
 */
/**
 * @brief Create a new empty catalog of the given add-on type.
 *
 * Dispatches the creation request to the first add-on whose name matches
 * \a type and that exposes a create_catalog symbol.
 *
 * @param type       Name of the catalog add-on type (e.g. "Default").
 * @param signature  MIME application signature for the new catalog.
 * @param language   BCP-47 language tag for the new catalog.
 * @return Pointer to the new BCatalogData on success, or NULL on failure.
 */
BCatalogData*
MutableLocaleRoster::CreateCatalog(const char* type, const char* signature,
	const char* language)
{
	if (!type || !signature || !language)
		return NULL;

	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return NULL;

	int32 count = fData->fCatalogAddOnInfos.CountItems();
	for (int32 i = 0; i < count; ++i) {
		CatalogAddOnInfo* info = (CatalogAddOnInfo*)
			fData->fCatalogAddOnInfos.ItemAt(i);
		if (info->fName.ICompare(type) != 0 || !info->fCreateFunc)
			continue;

		BCatalogData* catalog = info->fCreateFunc(signature, language);
		if (catalog != NULL) {
			info->fLoadedCatalogs.AddItem(catalog);
			return catalog;
		}
	}

	return NULL;
}


/*
 * Loads a catalog for the given signature, language and fingerprint.
 * The request to load this catalog is dispatched to all add-ons in turn,
 * until an add-on reports success.
 * If a catalog depends on another language (as 'english-british' depends
 * on 'english') the dependant catalogs are automatically loaded, too.
 * So it is perfectly possible that this method returns a catalog-chain
 * instead of a single catalog.
 * NULL is returned if no matching catalog could be found.
 */
/**
 * @brief Load a catalog for the given entry_ref, language, and fingerprint.
 *
 * Tries each catalog add-on in priority order. For each preferred language
 * (or the given \a language) a chain of parent-language fallback catalogs is
 * automatically built by truncating the language tag at each underscore.
 *
 * @param catalogOwner  entry_ref of the owning application.
 * @param language      BCP-47 language tag, or NULL for the preferred list.
 * @param fingerprint   Version fingerprint; 0 accepts any version.
 * @return Head of the loaded BCatalogData chain, or NULL if not found.
 */
BCatalogData*
MutableLocaleRoster::LoadCatalog(const entry_ref& catalogOwner,
	const char* language, int32 fingerprint) const
{
	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return NULL;

	int32 count = fData->fCatalogAddOnInfos.CountItems();
	for (int32 i = 0; i < count; ++i) {
		CatalogAddOnInfo* info = (CatalogAddOnInfo*)
			fData->fCatalogAddOnInfos.ItemAt(i);

		if (!info->fInstantiateFunc)
			continue;
		BMessage languages;
		if (language != NULL) {
			// try to load catalogs for the given language:
			languages.AddString("language", language);
		} else {
			// try to load catalogs for one of the preferred languages:
			GetPreferredLanguages(&languages);
		}

		BCatalogData* catalog = NULL;
		const char* lang;
		for (int32 l = 0; languages.FindString("language", l, &lang) == B_OK;
			++l) {
			catalog = info->fInstantiateFunc(catalogOwner, lang, fingerprint);
			if (catalog != NULL)
				info->fLoadedCatalogs.AddItem(catalog);
			// Chain-load catalogs for languages that depend on
			// other languages.
			// The current implementation uses the filename in order to
			// detect dependencies (parenthood) between languages (it
			// traverses from "english_british_oxford" to "english_british"
			// to "english"):
			int32 pos;
			BString langName(lang);
			BCatalogData* currentCatalog = catalog;
			BCatalogData* nextCatalog = NULL;
			while ((pos = langName.FindLast('_')) >= 0) {
				// language is based on parent, so we load that, too:
				// (even if the parent catalog was not found)
				langName.Truncate(pos);
				nextCatalog = info->fInstantiateFunc(catalogOwner,
					langName.String(), fingerprint);
				if (nextCatalog != NULL) {
					info->fLoadedCatalogs.AddItem(nextCatalog);
					if (currentCatalog != NULL)
						currentCatalog->SetNext(nextCatalog);
					else
						catalog = nextCatalog;
					currentCatalog = nextCatalog;
				}
			}
			if (catalog != NULL)
				return catalog;
		}
	}

	return NULL;
}


/*
 * Loads a catalog for the given signature and language.
 *
 * Only the default catalog type is searched, and only the standard system
 * directories.
 *
 * If a catalog depends on another language (as 'english-british' depends
 * on 'english') the dependant catalogs are automatically loaded, too.
 * So it is perfectly possible that this method returns a catalog-chain
 * instead of a single catalog.
 * NULL is returned if no matching catalog could be found.
 */
/**
 * @brief Load a catalog from standard directories for the given signature/language.
 *
 * Only searches the built-in DefaultCatalog add-on; unlike the entry_ref
 * overload this does not scan application-local directories.
 *
 * @param signature  MIME application signature.
 * @param language   BCP-47 language tag, or NULL for the preferred list.
 * @return Head of the loaded BCatalogData chain, or NULL if not found.
 */
BCatalogData*
MutableLocaleRoster::LoadCatalog(const char* signature,
	const char* language) const
{
	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return NULL;

	BMessage languages;
	if (language != NULL) {
		// try to load catalogs for the given language:
		languages.AddString("language", language);
	} else {
		// try to load catalogs for one of the preferred languages:
		GetPreferredLanguages(&languages);
	}


	int32 count = fData->fCatalogAddOnInfos.CountItems();
	CatalogAddOnInfo* defaultCatalogInfo = NULL;
	for (int32 i = 0; i < count; ++i) {
		CatalogAddOnInfo* info = (CatalogAddOnInfo*)
			fData->fCatalogAddOnInfos.ItemAt(i);
		if (info->fInstantiateFunc
				== BPrivate::DefaultCatalog::Instantiate) {
			defaultCatalogInfo = info;
			break;
		}
	}

	if (defaultCatalogInfo == NULL)
		return NULL;

	BPrivate::DefaultCatalog* catalog = NULL;
	const char* lang;
	for (int32 l = 0; languages.FindString("language", l, &lang) == B_OK; ++l) {
		catalog = new (std::nothrow) BPrivate::DefaultCatalog(NULL, signature,
			lang);

		if (catalog != NULL) {
			if (catalog->InitCheck() != B_OK
				|| catalog->ReadFromStandardLocations() != B_OK) {
				delete catalog;
				catalog = NULL;
			} else {
				defaultCatalogInfo->fLoadedCatalogs.AddItem(catalog);
			}
		}

		// Chain-load catalogs for languages that depend on
		// other languages.
		// The current implementation uses the filename in order to
		// detect dependencies (parenthood) between languages (it
		// traverses from "english_british_oxford" to "english_british"
		// to "english"):
		int32 pos;
		BString langName(lang);
		BCatalogData* currentCatalog = catalog;
		BPrivate::DefaultCatalog* nextCatalog = NULL;
		while ((pos = langName.FindLast('_')) >= 0) {
			// language is based on parent, so we load that, too:
			// (even if the parent catalog was not found)
			langName.Truncate(pos);
			nextCatalog = new (std::nothrow) BPrivate::DefaultCatalog(NULL,
				signature, langName.String());

			if (nextCatalog == NULL)
				continue;

			if (nextCatalog->InitCheck() != B_OK
				|| nextCatalog->ReadFromStandardLocations() != B_OK) {
				delete nextCatalog;
				continue;
			}

			defaultCatalogInfo->fLoadedCatalogs.AddItem(nextCatalog);

			if (currentCatalog != NULL)
				currentCatalog->SetNext(nextCatalog);
			else
				catalog = nextCatalog;
			currentCatalog = nextCatalog;
		}
		if (catalog != NULL)
			return catalog;
	}

	return NULL;
}


/*
 * unloads the given catalog (or rather: catalog-chain).
 * Every single catalog of the chain will be deleted automatically.
 */
/**
 * @brief Unload and delete the given catalog chain.
 *
 * Walks the fNext chain and removes each entry from its owning add-on's
 * fLoadedCatalogs list before deleting it.
 *
 * @param catalog  Head of the BCatalogData chain to unload.
 * @return B_OK if at least one catalog was found and deleted, B_BAD_VALUE if
 *         catalog is NULL, B_ERROR if none could be found.
 */
status_t
MutableLocaleRoster::UnloadCatalog(BCatalogData* catalog)
{
	if (!catalog)
		return B_BAD_VALUE;

	BAutolock lock(fData->fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	status_t res = B_ERROR;
	BCatalogData* nextCatalog;

	while (catalog != NULL) {
		nextCatalog = catalog->Next();
		int32 count = fData->fCatalogAddOnInfos.CountItems();
		for (int32 i = 0; i < count; ++i) {
			CatalogAddOnInfo* info = static_cast<CatalogAddOnInfo*>(
				fData->fCatalogAddOnInfos.ItemAt(i));
			if (info->fLoadedCatalogs.HasItem(catalog)) {
				info->fLoadedCatalogs.RemoveItem(catalog);
				delete catalog;
				res = B_OK;
				break;
			}
		}
		catalog = nextCatalog;
	}
	return res;
}


}	// namespace BPrivate
