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
 * @file User.cpp
 * @brief Implementation of BUser, the package-declared system user descriptor.
 *
 * BUser represents a system user account that a package requests be created
 * during installation. It stores the login name, real name, home directory,
 * login shell, and supplementary group list. The class validates that user
 * names contain only alphanumeric characters and underscores.
 *
 * @see BPackageInfo, BGroup
 */


#include <package/User.h>

#include <ctype.h>

#include <package/hpkg/PackageInfoAttributeValue.h>


namespace BPackageKit {


/**
 * @brief Default-construct an empty BUser.
 */
BUser::BUser()
	:
	fName(),
	fRealName(),
	fHome(),
	fShell(),
	fGroups()
{
}


/**
 * @brief Construct a BUser from a low-level HPKG attribute data struct.
 *
 * @param userData  Raw attribute data containing name, realName, home, shell,
 *                  and a groups array.
 */
BUser::BUser(const BHPKG::BUserData& userData)
	:
	fName(userData.name),
	fRealName(userData.realName),
	fHome(userData.home),
	fShell(userData.shell),
	fGroups()
{
	for (size_t i =	0; i < userData.groupCount; i++)
		fGroups.Add(userData.groups[i]);
}


/**
 * @brief Construct a BUser with explicit field values.
 *
 * @param name      UNIX login name.
 * @param realName  Human-readable display name (GECOS).
 * @param home      Absolute path to the home directory.
 * @param shell     Absolute path to the login shell.
 * @param groups    List of supplementary group names.
 */
BUser::BUser(const BString& name, const BString& realName, const BString& home,
	const BString& shell, const BStringList& groups)
	:
	fName(name),
	fRealName(realName),
	fHome(home),
	fShell(shell),
	fGroups(groups)
{
}


/**
 * @brief Destroy the BUser.
 */
BUser::~BUser()
{
}


/**
 * @brief Check whether this user descriptor is valid.
 *
 * The user name must be non-empty and contain only alphanumeric characters
 * and underscores.
 *
 * @return B_OK if valid, B_NO_INIT if the name is empty, B_BAD_VALUE if the
 *         name contains invalid characters.
 */
status_t
BUser::InitCheck() const
{
	if (fName.IsEmpty())
		return B_NO_INIT;
	if (!IsValidUserName(fName))
		return B_BAD_VALUE;
	return B_OK;
}


/**
 * @brief Return the UNIX login name.
 *
 * @return Reference to the login name string.
 */
const BString&
BUser::Name() const
{
	return fName;
}


/**
 * @brief Return the human-readable display name.
 *
 * @return Reference to the real-name (GECOS) string.
 */
const BString&
BUser::RealName() const
{
	return fRealName;
}


/**
 * @brief Return the absolute path to the home directory.
 *
 * @return Reference to the home-directory path string.
 */
const BString&
BUser::Home() const
{
	return fHome;
}


/**
 * @brief Return the absolute path to the login shell.
 *
 * @return Reference to the shell path string.
 */
const BString&
BUser::Shell() const
{
	return fShell;
}


/**
 * @brief Return the list of supplementary group names.
 *
 * @return Reference to the groups string list.
 */
const BStringList&
BUser::Groups() const
{
	return fGroups;
}


/**
 * @brief Replace all fields of this user descriptor.
 *
 * @param name      UNIX login name.
 * @param realName  Human-readable display name.
 * @param home      Absolute path to the home directory.
 * @param shell     Absolute path to the login shell.
 * @param groups    List of supplementary group names.
 * @return B_OK on success, B_NO_MEMORY if the groups list could not be copied.
 */
status_t
BUser::SetTo(const BString& name, const BString& realName, const BString& home,
	const BString& shell, const BStringList& groups)
{
	fName = name;
	fRealName = realName;
	fHome = home;
	fShell = shell;
	fGroups = groups;

	return fGroups.CountStrings() == groups.CountStrings() ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Check whether a C-string is a valid UNIX user name.
 *
 * A valid name is non-empty and consists solely of alphanumeric characters
 * and underscores.
 *
 * @param name  The name to validate.
 * @return True if the name is valid, false otherwise.
 */
/*static*/ bool
BUser::IsValidUserName(const char* name)
{
	if (name[0] == '\0')
		return false;

	for (; name[0] != '\0'; name++) {
		if (!isalnum(name[0]) && name[0] != '_')
			return false;
	}

	return true;
}


}	// namespace BPackageKit
