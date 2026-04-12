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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file GlobalWritableFileInfo.cpp
 * @brief Describes a globally writable file or directory declared by a package.
 *
 * BGlobalWritableFileInfo stores the path, update policy (keep-old, manual,
 * auto-merge), and whether the entry is a directory.  It is decoded from HPKG
 * package attributes and consulted by the package daemon when updating
 * writable files during package activation or deactivation.
 *
 * @see BPackageInfo, BUserSettingsFileInfo
 */


#include <package/GlobalWritableFileInfo.h>

#include <package/hpkg/PackageInfoAttributeValue.h>


namespace BPackageKit {


/**
 * @brief Default constructor; creates an uninitialised info object.
 */
BGlobalWritableFileInfo::BGlobalWritableFileInfo()
	:
	fPath(),
	fUpdateType(B_WRITABLE_FILE_UPDATE_TYPE_ENUM_COUNT)
{
}


/**
 * @brief Construct from a low-level HPKG attribute value structure.
 *
 * @param infoData  The decoded HPKG attribute data containing path, update
 *                  type, and directory flag.
 */
BGlobalWritableFileInfo::BGlobalWritableFileInfo(
	const BHPKG::BGlobalWritableFileInfoData& infoData)
	:
	fPath(infoData.path),
	fUpdateType(infoData.updateType),
	fIsDirectory(infoData.isDirectory)
{
}


/**
 * @brief Construct from individual fields.
 *
 * @param path        Repository-relative path of the writable file or directory.
 * @param updateType  Policy controlling how this file is handled on package update.
 * @param isDirectory true if the path refers to a directory rather than a file.
 */
BGlobalWritableFileInfo::BGlobalWritableFileInfo(const BString& path,
	BWritableFileUpdateType updateType, bool isDirectory)
	:
	fPath(path),
	fUpdateType(updateType),
	fIsDirectory(isDirectory)
{
}


/**
 * @brief Destructor.
 */
BGlobalWritableFileInfo::~BGlobalWritableFileInfo()
{
}


/**
 * @brief Check whether this object has been properly initialised.
 *
 * @return B_OK if a non-empty path is set, or B_NO_INIT otherwise.
 */
status_t
BGlobalWritableFileInfo::InitCheck() const
{
	if (fPath.IsEmpty())
		return B_NO_INIT;
	return B_OK;
}


/**
 * @brief Return the path of the globally writable file or directory.
 *
 * @return Const reference to the path string.
 */
const BString&
BGlobalWritableFileInfo::Path() const
{
	return fPath;
}


/**
 * @brief Return whether this entry participates in update handling.
 *
 * An entry is considered "included" when its update type is a valid value
 * (i.e. not the sentinel B_WRITABLE_FILE_UPDATE_TYPE_ENUM_COUNT).
 *
 * @return true if the update type is valid, false otherwise.
 */
bool
BGlobalWritableFileInfo::IsIncluded() const
{
	return fUpdateType != B_WRITABLE_FILE_UPDATE_TYPE_ENUM_COUNT;
}


/**
 * @brief Return the update policy for this writable entry.
 *
 * @return The BWritableFileUpdateType enum value.
 */
BWritableFileUpdateType
BGlobalWritableFileInfo::UpdateType() const
{
	return fUpdateType;
}


/**
 * @brief Return whether the path refers to a directory.
 *
 * @return true if the path is a directory, false if it is a regular file.
 */
bool
BGlobalWritableFileInfo::IsDirectory() const
{
	return fIsDirectory;
}


/**
 * @brief Replace the stored path, update type, and directory flag.
 *
 * @param path        New path for the writable entry.
 * @param updateType  New update policy.
 * @param isDirectory true if the new path refers to a directory.
 */
void
BGlobalWritableFileInfo::SetTo(const BString& path,
	BWritableFileUpdateType updateType, bool isDirectory)
{
	fPath = path;
	fUpdateType = updateType;
	fIsDirectory = isDirectory;
}


}	// namespace BPackageKit
