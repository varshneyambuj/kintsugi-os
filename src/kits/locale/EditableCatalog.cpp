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
 * @file EditableCatalog.cpp
 * @brief Implementation of EditableCatalog for read-write catalog access.
 *
 * EditableCatalog extends BCatalog with write operations. It is used by
 * catalog editing tools (e.g. CatalogTest, CollectCatkeys) to create new
 * catalogs, add translated strings, and serialize them to files, attributes,
 * or resources. The backing store is always a BCatalogData add-on selected
 * by the \a type name passed to the constructor.
 *
 * @see BCatalog, BCatalogData, MutableLocaleRoster
 */


#include <EditableCatalog.h>

#include <CatalogData.h>
#include <MutableLocaleRoster.h>


using BPrivate::MutableLocaleRoster;


namespace BPrivate {

/**
 * @brief Construct an EditableCatalog by creating a new catalog of the given type.
 *
 * Asks MutableLocaleRoster to instantiate a fresh BCatalogData of the
 * specified add-on type, identified by signature and language.
 *
 * @param type       Name of the catalog add-on type (e.g. "Default").
 * @param signature  MIME application signature for the new catalog.
 * @param language   BCP-47 language tag for the new catalog.
 */
EditableCatalog::EditableCatalog(const char* type, const char* signature,
	const char* language)
{
	fCatalogData = MutableLocaleRoster::Default()->CreateCatalog(type,
		signature, language);
}


/**
 * @brief Destroy the EditableCatalog.
 */
EditableCatalog::~EditableCatalog()
{
}


/**
 * @brief Add or replace a translated string keyed by original text and context.
 *
 * @param string      Original source string.
 * @param translated  Translated replacement string.
 * @param context     Optional disambiguation context.
 * @param comment     Optional translator comment.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error code.
 */
status_t
EditableCatalog::SetString(const char* string, const char* translated,
	const char* context, const char* comment)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->SetString(string, translated, context, comment);
}


/**
 * @brief Add or replace a translated string keyed by numeric identifier.
 *
 * @param id          Numeric catalog key.
 * @param translated  Translated replacement string.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error code.
 */
status_t
EditableCatalog::SetString(int32 id, const char* translated)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->SetString(id, translated);
}


/**
 * @brief Indicate whether the backing catalog data supports writing binary data.
 *
 * @return true if write data operations are supported, false otherwise.
 */
bool
EditableCatalog::CanWriteData() const
{
	if (fCatalogData == NULL)
		return false;

	return fCatalogData->CanWriteData();
}


/**
 * @brief Store binary data by name key into the backing catalog.
 *
 * @param name  Name key.
 * @param msg   BMessage containing the data to store.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::SetData(const char* name, BMessage* msg)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->SetData(name, msg);
}


/**
 * @brief Store binary data by numeric identifier into the backing catalog.
 *
 * @param id   Numeric catalog key.
 * @param msg  BMessage containing the data to store.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::SetData(uint32 id, BMessage* msg)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->SetData(id, msg);
}


/**
 * @brief Load catalog contents from a file on disk.
 *
 * @param path  Path to the catalog file, or NULL to use the stored path.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::ReadFromFile(const char* path)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->ReadFromFile(path);
}


/**
 * @brief Load catalog contents from a file attribute on the given entry.
 *
 * @param appOrAddOnRef  entry_ref of the application or add-on file.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::ReadFromAttribute(const entry_ref& appOrAddOnRef)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->ReadFromAttribute(appOrAddOnRef);
}


/**
 * @brief Load catalog contents from an embedded resource in the given entry.
 *
 * @param appOrAddOnRef  entry_ref of the application or add-on file.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::ReadFromResource(const entry_ref& appOrAddOnRef)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->ReadFromResource(appOrAddOnRef);
}


/**
 * @brief Write catalog contents to a file on disk.
 *
 * @param path  Destination path, or NULL to use the stored path.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::WriteToFile(const char* path)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->WriteToFile(path);
}


/**
 * @brief Write catalog contents to a file attribute on the given entry.
 *
 * @param appOrAddOnRef  entry_ref of the target application or add-on file.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::WriteToAttribute(const entry_ref& appOrAddOnRef)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->WriteToAttribute(appOrAddOnRef);
}


/**
 * @brief Write catalog contents to an embedded resource in the given entry.
 *
 * @param appOrAddOnRef  entry_ref of the target application or add-on file.
 * @return B_OK on success, B_NO_INIT if no catalog data, or another error.
 */
status_t
EditableCatalog::WriteToResource(const entry_ref& appOrAddOnRef)
{
	if (fCatalogData == NULL)
		return B_NO_INIT;

	return fCatalogData->WriteToResource(appOrAddOnRef);
}


/**
 * @brief Remove all entries from the backing catalog data object.
 */
void EditableCatalog::MakeEmpty()
{
	if (fCatalogData != NULL)
		fCatalogData->MakeEmpty();
}


/**
 * @brief Return a direct pointer to the backing BCatalogData object.
 *
 * @return Pointer to the BCatalogData, or NULL if none was created.
 */
BCatalogData*
EditableCatalog::CatalogData()
{
	return fCatalogData;
}


} // namespace BPrivate
