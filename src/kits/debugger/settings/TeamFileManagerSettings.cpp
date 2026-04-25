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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TeamFileManagerSettings.cpp
 * @brief Per-team source-path remapping configuration.
 *
 * TeamFileManagerSettings stores user-supplied mappings from a source path
 * embedded in debug info to the path on disk where the file actually lives.
 * Mappings are persisted in a BMessage that round-trips through TeamSettings.
 */


#include "TeamFileManagerSettings.h"

/**
 * @brief Construct an empty file-manager settings object.
 */
TeamFileManagerSettings::TeamFileManagerSettings()
	:
	fValues()
{
}


/**
 * @brief Destructor.
 */
TeamFileManagerSettings::~TeamFileManagerSettings()
{
}


/**
 * @brief Copy values from @a other.
 *
 * @param other  Source settings whose value message is copied.
 * @return Reference to @c *this.
 */
TeamFileManagerSettings&
TeamFileManagerSettings::operator=(const TeamFileManagerSettings& other)
{
	fValues = other.fValues;

	return *this;
}


/**
 * @brief Returns the stable identifier for these settings.
 *
 * @return The constant string @c "FileManager".
 */
const char*
TeamFileManagerSettings::ID() const
{
	return "FileManager";
}


/**
 * @brief Loads the settings from a BMessage archive.
 *
 * @param archive  Source archive previously produced by WriteTo().
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When BMessage assignment throws.
 */
status_t
TeamFileManagerSettings::SetTo(const BMessage& archive)
{
	try {
		fValues = archive;
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Serialises the settings into @a archive.
 *
 * @param archive  Out: receives a copy of the underlying BMessage.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When BMessage assignment throws.
 */
status_t
TeamFileManagerSettings::WriteTo(BMessage& archive) const
{
	try {
		archive = fValues;
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Returns the number of source-path mappings configured.
 *
 * @return Count of @c source:mapping entries currently stored.
 */
int32
TeamFileManagerSettings::CountSourceMappings() const
{
	type_code type;
	int32 count = 0;

	if (fValues.GetInfo("source:mapping", &type, &count) == B_OK)
		return count;

	return 0;
}


/**
 * @brief Appends a source-path remap.
 *
 * @param sourcePath   Path embedded in the original debug info.
 * @param locatedPath  Path on disk where the file is actually located.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When the mapping could not be stored.
 */
status_t
TeamFileManagerSettings::AddSourceMapping(const BString& sourcePath,
	const BString& locatedPath)
{
	BMessage mapping;
	if (mapping.AddString("source:path", sourcePath) != B_OK
		|| mapping.AddString("source:locatedpath", locatedPath) != B_OK
		|| fValues.AddMessage("source:mapping", &mapping) != B_OK) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Removes the mapping at the given index.
 *
 * @param index  Zero-based index of the mapping to remove.
 * @return B_OK on success or BMessage::RemoveData() error.
 */
status_t
TeamFileManagerSettings::RemoveSourceMappingAt(int32 index)
{
	return fValues.RemoveData("source:mapping", index);
}


/**
 * @brief Reads back the mapping at @a index.
 *
 * @param index        Zero-based index to query.
 * @param sourcePath   Out: source path stored at @a index.
 * @param locatedPath  Out: located path stored at @a index.
 * @return B_OK on success or the underlying BMessage error.
 */
status_t
TeamFileManagerSettings::GetSourceMappingAt(int32 index, BString& sourcePath,
	BString& locatedPath)
{
	BMessage mapping;
	status_t error = fValues.FindMessage("source:mapping", index, &mapping);
	if (error != B_OK)
		return error;

	error = mapping.FindString("source:path", &sourcePath);
	if (error != B_OK)
		return error;

	return mapping.FindString("source:locatedpath", &locatedPath);
}


/**
 * @brief Produces a deep copy of these settings on the heap.
 *
 * @return Newly allocated copy, or @c NULL on allocation/assignment failure.
 *         Caller takes ownership.
 */
TeamFileManagerSettings*
TeamFileManagerSettings::Clone() const
{
	TeamFileManagerSettings* settings = new(std::nothrow)
		TeamFileManagerSettings();

	if (settings == NULL)
		return NULL;

	if (settings->SetTo(fValues) != B_OK) {
		delete settings;
		return NULL;
	}

	return settings;
}
