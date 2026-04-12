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
 * @file UserSettingsFileInfo.cpp
 * @brief Implementation of BUserSettingsFileInfo, describing a package-provided settings file.
 *
 * BUserSettingsFileInfo records the path of a user settings file or directory
 * that a package installs, together with an optional template path used when
 * creating the file for the first time. When the entry is a directory the
 * template path is left empty and fIsDirectory is set.
 *
 * @see BPackageInfo, BUser
 */


#include <package/UserSettingsFileInfo.h>

#include <package/hpkg/PackageInfoAttributeValue.h>


namespace BPackageKit {


/**
 * @brief Default-construct an empty BUserSettingsFileInfo.
 */
BUserSettingsFileInfo::BUserSettingsFileInfo()
	:
	fPath(),
	fTemplatePath()
{
}


/**
 * @brief Construct from a low-level HPKG attribute data struct.
 *
 * @param infoData  Raw attribute data containing path, templatePath, and
 *                  isDirectory fields.
 */
BUserSettingsFileInfo::BUserSettingsFileInfo(
	const BHPKG::BUserSettingsFileInfoData& infoData)
	:
	fPath(infoData.path),
	fTemplatePath(infoData.templatePath),
	fIsDirectory(infoData.isDirectory)
{
}


/**
 * @brief Construct from a file path and a template path.
 *
 * @param path          Relative path of the settings file to manage.
 * @param templatePath  Path to the template used to seed the file on first run.
 */
BUserSettingsFileInfo::BUserSettingsFileInfo(const BString& path,
	const BString& templatePath)
	:
	fPath(path),
	fTemplatePath(templatePath),
	fIsDirectory(false)
{
}


/**
 * @brief Construct from a path that may represent either a file or a directory.
 *
 * @param path         Relative path of the settings entry.
 * @param isDirectory  True if the entry is a directory rather than a file.
 */
BUserSettingsFileInfo::BUserSettingsFileInfo(const BString& path,
	bool isDirectory)
	:
	fPath(path),
	fTemplatePath(),
	fIsDirectory(isDirectory)
{
}


/**
 * @brief Destroy the BUserSettingsFileInfo.
 */
BUserSettingsFileInfo::~BUserSettingsFileInfo()
{
}


/**
 * @brief Check whether this info has been properly initialised.
 *
 * @return B_OK if the path is non-empty, B_NO_INIT otherwise.
 */
status_t
BUserSettingsFileInfo::InitCheck() const
{
	return fPath.IsEmpty() ? B_NO_INIT : B_OK;
}


/**
 * @brief Return the relative path of the settings entry.
 *
 * @return Reference to the path string.
 */
const BString&
BUserSettingsFileInfo::Path() const
{
	return fPath;
}


/**
 * @brief Return the template path used to seed the settings file on first use.
 *
 * @return Reference to the template-path string; empty for directory entries.
 */
const BString&
BUserSettingsFileInfo::TemplatePath() const
{
	return fTemplatePath;
}


/**
 * @brief Return whether this entry represents a directory.
 *
 * @return True if fIsDirectory is set, false for regular file entries.
 */
bool
BUserSettingsFileInfo::IsDirectory() const
{
	return fIsDirectory;
}


/**
 * @brief Reset to a file entry with the given path and template path.
 *
 * @param path          The new settings file path.
 * @param templatePath  The new template file path.
 */
void
BUserSettingsFileInfo::SetTo(const BString& path, const BString& templatePath)
{
	fPath = path;
	fTemplatePath = templatePath;
	fIsDirectory = false;
}


/**
 * @brief Reset to a path that may be either a file or directory entry.
 *
 * @param path         The new settings entry path.
 * @param isDirectory  True if the entry is a directory.
 */
void
BUserSettingsFileInfo::SetTo(const BString& path, bool isDirectory)
{
	fPath = path;
	fTemplatePath.Truncate(0);
	fIsDirectory = isDirectory;
}


}	// namespace BPackageKit
