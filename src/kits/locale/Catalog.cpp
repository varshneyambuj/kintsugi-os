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
 *   Copyright 2003-2004, Axel Dörfler, axeld@pinc-software.de
 *   Copyright 2003-2004,2012, Oliver Tappe, zooey@hirschkaefer.de
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Catalog.cpp
 * @brief Implementation of BCatalog, the public translation catalog interface.
 *
 * BCatalog provides thread-safe access to a chain of BCatalogData add-ons
 * loaded by the MutableLocaleRoster. String and data lookups walk the chain
 * in priority order, returning the first match found. The catalog is identified
 * by a MIME signature, an optional language tag, and a fingerprint used for
 * version matching.
 *
 * @see BCatalogData, BLocaleRoster
 */


#include <Catalog.h>

#include <Application.h>
#include <Autolock.h>
#include <CatalogData.h>
#include <Locale.h>
#include <MutableLocaleRoster.h>
#include <Node.h>
#include <Roster.h>


using BPrivate::MutableLocaleRoster;


//#pragma mark - BCatalog

/**
 * @brief Construct an empty, uninitialized catalog.
 *
 * The catalog holds no data and no catalog data chain until SetTo() is
 * called or a parameterized constructor is used.
 */
BCatalog::BCatalog()
	:
	fCatalogData(NULL),
	fLock("Catalog")
{
}


/**
 * @brief Construct a catalog and load it for the given entry_ref owner.
 *
 * Delegates to SetTo(catalogOwner, language, fingerprint) immediately after
 * construction, so InitCheck() can be used to verify success.
 *
 * @param catalogOwner  entry_ref of the application or add-on that owns this
 *                      catalog.
 * @param language      BCP-47 language tag to load, or NULL for the preferred
 *                      system language.
 * @param fingerprint   Version fingerprint; 0 accepts any version.
 */
BCatalog::BCatalog(const entry_ref& catalogOwner, const char* language,
	uint32 fingerprint)
	:
	fCatalogData(NULL),
	fLock("Catalog")
{
	SetTo(catalogOwner, language, fingerprint);
}


/**
 * @brief Construct a catalog and load it by MIME signature.
 *
 * Delegates to SetTo(signature, language) immediately after construction.
 *
 * @param signature  Application MIME type string (e.g. "application/x-vnd.Foo").
 * @param language   BCP-47 language tag, or NULL for the preferred language.
 */
BCatalog::BCatalog(const char* signature, const char* language)
	:
	fCatalogData(NULL),
	fLock("Catalog")
{
	SetTo(signature, language);
}


/**
 * @brief Destroy the catalog and unload the underlying catalog data chain.
 */
BCatalog::~BCatalog()
{
	MutableLocaleRoster::Default()->UnloadCatalog(fCatalogData);
}


/**
 * @brief Look up a translated string by original text, context, and comment.
 *
 * Walks the catalog data chain and returns the first translation found.
 * If no translation exists the original \a string is returned unchanged so
 * the caller always receives a usable C string.
 *
 * @param string   The original (source) string to translate.
 * @param context  Optional disambiguation context string.
 * @param comment  Optional translator comment.
 * @return The translated string, or \a string if no translation was found.
 */
const char*
BCatalog::GetString(const char* string, const char* context,
	const char* comment)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return string;

	const char* translated;
	for (BCatalogData* cat = fCatalogData; cat != NULL; cat = cat->fNext) {
		translated = cat->GetString(string, context, comment);
		if (translated != NULL)
			return translated;
	}

	return string;
}


/**
 * @brief Look up a translated string by numeric identifier.
 *
 * Walks the catalog data chain and returns the first translation found.
 * An empty string is returned when no translation is available.
 *
 * @param id  Hashed numeric catalog key.
 * @return The translated string, or "" if not found.
 */
const char*
BCatalog::GetString(uint32 id)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return "";

	const char* translated;
	for (BCatalogData* cat = fCatalogData; cat != NULL; cat = cat->fNext) {
		translated = cat->GetString(id);
		if (translated != NULL)
			return translated;
	}

	return "";
}


/**
 * @brief Retrieve binary data associated with a named key.
 *
 * Walks the catalog chain until a catalog reports success or a definitive
 * error. B_NAME_NOT_FOUND is returned when no catalog contains the entry.
 *
 * @param name  Name key to look up.
 * @param msg   BMessage to populate with the data.
 * @return B_OK on success, B_NAME_NOT_FOUND if absent, or another error code.
 */
status_t
BCatalog::GetData(const char* name, BMessage* msg)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	if (fCatalogData == NULL)
		return B_NO_INIT;

	status_t res;
	for (BCatalogData* cat = fCatalogData; cat != NULL; cat = cat->fNext) {
		res = cat->GetData(name, msg);
		if (res != B_NAME_NOT_FOUND && res != EOPNOTSUPP)
			return res;	// return B_OK if found, or specific error-code
	}

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Retrieve binary data associated with a numeric identifier.
 *
 * @param id   Numeric catalog key.
 * @param msg  BMessage to populate with the data.
 * @return B_OK on success, B_NAME_NOT_FOUND if absent, or another error code.
 */
status_t
BCatalog::GetData(uint32 id, BMessage* msg)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	if (fCatalogData == NULL)
		return B_NO_INIT;

	status_t res;
	for (BCatalogData* cat = fCatalogData; cat != NULL; cat = cat->fNext) {
		res = cat->GetData(id, msg);
		if (res != B_NAME_NOT_FOUND && res != EOPNOTSUPP)
			return res;	// return B_OK if found, or specific error-code
	}

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Return the MIME signature of the currently loaded catalog.
 *
 * @param sig  Output BString that receives the signature.
 * @return B_OK on success, B_BAD_VALUE if \a sig is NULL, B_NO_INIT if no
 *         catalog is loaded.
 */
status_t
BCatalog::GetSignature(BString* sig)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	if (sig == NULL)
		return B_BAD_VALUE;

	if (fCatalogData == NULL)
		return B_NO_INIT;

	*sig = fCatalogData->fSignature;

	return B_OK;
}


/**
 * @brief Return the language tag of the currently loaded catalog.
 *
 * @param lang  Output BString that receives the BCP-47 language tag.
 * @return B_OK on success, B_BAD_VALUE if \a lang is NULL, B_NO_INIT if no
 *         catalog is loaded.
 */
status_t
BCatalog::GetLanguage(BString* lang)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	if (lang == NULL)
		return B_BAD_VALUE;

	if (fCatalogData == NULL)
		return B_NO_INIT;

	*lang = fCatalogData->fLanguageName;

	return B_OK;
}


/**
 * @brief Return the fingerprint of the currently loaded catalog.
 *
 * @param fp  Output pointer that receives the 32-bit fingerprint value.
 * @return B_OK on success, B_BAD_VALUE if \a fp is NULL, B_NO_INIT if no
 *         catalog is loaded.
 */
status_t
BCatalog::GetFingerprint(uint32* fp)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	if (fp == NULL)
		return B_BAD_VALUE;

	if (fCatalogData == NULL)
		return B_NO_INIT;

	*fp = fCatalogData->fFingerprint;

	return B_OK;
}


/**
 * @brief Load a new catalog for the given entry_ref, replacing any current one.
 *
 * Unloads the previously held catalog data before loading the new one.
 *
 * @param catalogOwner  entry_ref of the owning application or add-on.
 * @param language      BCP-47 language tag, or NULL for the preferred language.
 * @param fingerprint   Version fingerprint; 0 accepts any version.
 * @return B_OK on success, or an error code if the lock cannot be acquired.
 */
status_t
BCatalog::SetTo(const entry_ref& catalogOwner, const char* language,
	uint32 fingerprint)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	MutableLocaleRoster::Default()->UnloadCatalog(fCatalogData);
	fCatalogData = MutableLocaleRoster::Default()->LoadCatalog(catalogOwner,
		language, fingerprint);

	return B_OK;
}


/**
 * @brief Load a new catalog by MIME signature, replacing any current one.
 *
 * @param signature  Application MIME type string.
 * @param language   BCP-47 language tag, or NULL for the preferred language.
 * @return B_OK on success, or an error code if the lock cannot be acquired.
 */
status_t
BCatalog::SetTo(const char* signature, const char* language)
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	MutableLocaleRoster::Default()->UnloadCatalog(fCatalogData);
	fCatalogData = MutableLocaleRoster::Default()->LoadCatalog(signature,
		language);

	return B_OK;
}


/**
 * @brief Check whether the catalog was loaded successfully.
 *
 * @return B_OK if the underlying catalog data is valid, B_NO_INIT if no
 *         catalog has been loaded, or a more specific error code.
 */
status_t
BCatalog::InitCheck() const
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return B_ERROR;

	return fCatalogData != NULL	? fCatalogData->InitCheck() : B_NO_INIT;
}


/**
 * @brief Return the total number of translation entries across the catalog chain.
 *
 * @return The entry count, or 0 if no catalog is loaded or the lock fails.
 */
int32
BCatalog::CountItems() const
{
	BAutolock lock(&fLock);
	if (!lock.IsLocked())
		return 0;

	return fCatalogData != NULL ? fCatalogData->CountItems() : 0;
}
