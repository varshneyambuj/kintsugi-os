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
 * @file CatalogData.cpp
 * @brief Base class implementation for catalog data add-on backends.
 *
 * BCatalogData is the abstract backend interface that catalog add-on shared
 * libraries must implement. Each concrete subclass stores and retrieves
 * translated strings in its own format (e.g. hash map, plain text, resource).
 * The default implementations return EOPNOTSUPP, signalling that the
 * operation is not supported by the base class.
 *
 * @see BCatalog, HashMapCatalog, DefaultCatalog
 */


#include <CatalogData.h>


/**
 * @brief Construct a BCatalogData with the given identity fields.
 *
 * @param signature    MIME application signature that owns this catalog.
 * @param language     BCP-47 language tag for the translated strings.
 * @param fingerprint  Version fingerprint; 0 means no version checking.
 */
BCatalogData::BCatalogData(const char* signature, const char* language,
	uint32 fingerprint)
	:
	fInitCheck(B_NO_INIT),
	fSignature(signature),
	fLanguageName(language),
	fFingerprint(fingerprint),
	fNext(NULL)
{
}


/**
 * @brief Destroy the catalog data object.
 */
BCatalogData::~BCatalogData()
{
}


/**
 * @brief Recompute and store the catalog fingerprint.
 *
 * The base implementation always sets the fingerprint to 0, effectively
 * disabling version-mismatch detection. Subclasses should override this
 * to compute a meaningful checksum over all catalog keys.
 */
void
BCatalogData::UpdateFingerprint()
{
	fFingerprint = 0;
		// base implementation always yields the same fingerprint,
		// which means that no version-mismatch detection is possible.
}


/**
 * @brief Return the initialization status of this catalog data object.
 *
 * @return B_OK if the object was successfully initialized, B_NO_INIT
 *         otherwise.
 */
status_t
BCatalogData::InitCheck() const
{
	return fInitCheck;
}


/**
 * @brief Indicate whether this catalog backend supports binary data storage.
 *
 * @return false in the base implementation; override to return true.
 */
bool
BCatalogData::CanHaveData() const
{
	return false;
}


/**
 * @brief Retrieve binary data by name key (unsupported in base class).
 *
 * @param name  Name key to look up.
 * @param msg   BMessage to populate.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::GetData(const char* name, BMessage* msg)
{
	return EOPNOTSUPP;
}


/**
 * @brief Retrieve binary data by numeric identifier (unsupported in base class).
 *
 * @param id   Numeric catalog key.
 * @param msg  BMessage to populate.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::GetData(uint32 id, BMessage* msg)
{
	return EOPNOTSUPP;
}


/**
 * @brief Store a translated string by original text and context (unsupported).
 *
 * @param string      Original source string.
 * @param translated  Translated replacement string.
 * @param context     Optional disambiguation context.
 * @param comment     Optional translator comment.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::SetString(const char* string, const char* translated,
	const char* context, const char* comment)
{
	return EOPNOTSUPP;
}


/**
 * @brief Store a translated string by numeric identifier (unsupported).
 *
 * @param id          Numeric catalog key.
 * @param translated  Translated replacement string.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::SetString(int32 id, const char* translated)
{
	return EOPNOTSUPP;
}


/**
 * @brief Indicate whether this catalog backend supports writing data.
 *
 * @return false in the base implementation; override to return true.
 */
bool
BCatalogData::CanWriteData() const
{
	return false;
}


/**
 * @brief Store binary data by name key (unsupported in base class).
 *
 * @param name  Name key.
 * @param msg   BMessage containing the data to store.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::SetData(const char* name, BMessage* msg)
{
	return EOPNOTSUPP;
}


/**
 * @brief Store binary data by numeric identifier (unsupported in base class).
 *
 * @param id   Numeric catalog key.
 * @param msg  BMessage containing the data to store.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::SetData(uint32 id, BMessage* msg)
{
	return EOPNOTSUPP;
}


/**
 * @brief Load catalog contents from a file on disk (unsupported in base class).
 *
 * @param path  File system path to the catalog file.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::ReadFromFile(const char* path)
{
	return EOPNOTSUPP;
}


/**
 * @brief Load catalog contents from a file attribute (unsupported in base).
 *
 * @param appOrAddOnRef  entry_ref of the application or add-on file.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::ReadFromAttribute(const entry_ref& appOrAddOnRef)
{
	return EOPNOTSUPP;
}


/**
 * @brief Load catalog contents from an embedded resource (unsupported in base).
 *
 * @param appOrAddOnRef  entry_ref of the application or add-on file.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::ReadFromResource(const entry_ref& appOrAddOnRef)
{
	return EOPNOTSUPP;
}


/**
 * @brief Write catalog contents to a file on disk (unsupported in base class).
 *
 * @param path  File system path for the output catalog file.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::WriteToFile(const char* path)
{
	return EOPNOTSUPP;
}


/**
 * @brief Write catalog contents to a file attribute (unsupported in base).
 *
 * @param appOrAddOnRef  entry_ref of the target application or add-on file.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::WriteToAttribute(const entry_ref& appOrAddOnRef)
{
	return EOPNOTSUPP;
}


/**
 * @brief Write catalog contents to an embedded resource (unsupported in base).
 *
 * @param appOrAddOnRef  entry_ref of the target application or add-on file.
 * @return EOPNOTSUPP always in the base implementation.
 */
status_t
BCatalogData::WriteToResource(const entry_ref& appOrAddOnRef)
{
	return EOPNOTSUPP;
}


/**
 * @brief Remove all catalog entries (no-op in base class).
 */
void BCatalogData::MakeEmpty()
{
}


/**
 * @brief Return the number of entries in this catalog data object.
 *
 * @return 0 in the base implementation; subclasses return the real count.
 */
int32
BCatalogData::CountItems() const
{
	return 0;
}


/**
 * @brief Link another catalog data object as the next element in the chain.
 *
 * The BCatalog walk-chain mechanism uses this to fall back to lower-priority
 * catalogs when a string is not found in the current one.
 *
 * @param next  Pointer to the next BCatalogData in the chain, or NULL.
 */
void
BCatalogData::SetNext(BCatalogData* next)
{
	fNext = next;
}
