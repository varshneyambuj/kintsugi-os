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
 *   Copyright 2004-2015 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file NetworkProfile.cpp
 * @brief Implementation of BNetworkProfile, a stub representation of a
 *        named, on-disk network configuration profile.
 *
 * Profiles bundle a collection of network settings (interfaces, services,
 * DNS) that can be saved, restored, or made current. The class is currently
 * a thin wrapper around a BEntry: most mutating operations are placeholders
 * that callers will eventually drive through the network kit.
 */


#include <NetworkProfile.h>

#include <stdlib.h>


using namespace BNetworkKit;


/**
 * @brief Constructs an empty, unbound profile.
 *
 * The resulting object holds no entry, has no name, and is neither the
 * default nor the current profile.
 */
BNetworkProfile::BNetworkProfile()
	:
	fIsDefault(false),
	fIsCurrent(false),
	fName(NULL)
{
}


/**
 * @brief Constructs a profile pointing at @a path on disk.
 *
 * @param path  Filesystem path of the profile directory or file.
 */
BNetworkProfile::BNetworkProfile(const char* path)
	:
	fIsDefault(false),
	fIsCurrent(false)
{
	SetTo(path);
}


/**
 * @brief Constructs a profile from an entry_ref.
 *
 * @param ref  Reference identifying the on-disk profile.
 */
BNetworkProfile::BNetworkProfile(const entry_ref& ref)
	:
	fIsDefault(false),
	fIsCurrent(false)
{
	SetTo(ref);
}


/**
 * @brief Constructs a profile that wraps an existing BEntry.
 *
 * @param entry  Live BEntry pointing at the profile location.
 */
BNetworkProfile::BNetworkProfile(const BEntry& entry)
	:
	fIsDefault(false),
	fIsCurrent(false)
{
	SetTo(entry);
}


/**
 * @brief Destructor. Owns no heap state of its own.
 */
BNetworkProfile::~BNetworkProfile()
{
}


/**
 * @brief Rebinds the profile to the entry at @a path.
 *
 * Any cached path or leaf name is invalidated so the next Name() call
 * recomputes it.
 *
 * @param path  Filesystem path of the new profile.
 * @return Status of the underlying BEntry::SetTo call.
 * @retval B_OK  On success.
 */
status_t
BNetworkProfile::SetTo(const char* path)
{
	status_t status = fEntry.SetTo(path, true);
	if (status != B_OK)
		return status;

	fPath.Unset();
	fName = NULL;
	return B_OK;
}


/**
 * @brief Rebinds the profile to the supplied entry_ref.
 *
 * @param ref  Reference identifying the new profile.
 * @return Status of the underlying BEntry::SetTo call.
 * @retval B_OK  On success.
 */
status_t
BNetworkProfile::SetTo(const entry_ref& ref)
{
	status_t status = fEntry.SetTo(&ref);
	if (status != B_OK)
		return status;

	fPath.Unset();
	fName = ref.name;
	return B_OK;
}


/**
 * @brief Rebinds the profile to a copy of the supplied BEntry.
 *
 * @param entry  Source entry; copied into the profile.
 * @return Always B_OK.
 */
status_t
BNetworkProfile::SetTo(const BEntry& entry)
{
	fEntry = entry;
	fPath.Unset();
	fName = NULL;
	return B_OK;
}


/**
 * @brief Returns the profile's leaf name, computing it lazily from the entry.
 *
 * @return Pointer to an internal C string owned by this profile, or NULL
 *         if the entry could not be resolved.
 */
const char*
BNetworkProfile::Name()
{
	if (fName == NULL) {
		if (fEntry.GetPath(&fPath) == B_OK)
			fName = fPath.Leaf();
	}

	return fName;
}


/**
 * @brief Renames the profile.
 *
 * @param name  Desired new name; ignored by the current stub.
 * @return Always B_OK.
 * @todo  Persist the rename to disk and update fName.
 */
status_t
BNetworkProfile::SetName(const char* name)
{
	return B_OK;
}


/**
 * @brief Reports whether the underlying entry exists on disk.
 *
 * @return true if the entry exists, false otherwise.
 */
bool
BNetworkProfile::Exists()
{
	return fEntry.Exists();
}


/**
 * @brief Removes the profile from disk.
 *
 * @return Always B_ERROR in the current stub.
 * @todo  Implement deletion via BEntry::Remove plus settings cleanup.
 */
status_t
BNetworkProfile::Delete()
{
	return B_ERROR;
}


/**
 * @brief Reports whether this profile is the system default.
 *
 * @return true if marked as default.
 */
bool
BNetworkProfile::IsDefault()
{
	return fIsDefault;
}


/**
 * @brief Reports whether this profile is currently active.
 *
 * @return true if marked current.
 */
bool
BNetworkProfile::IsCurrent()
{
	return fIsCurrent;
}


/**
 * @brief Activates this profile, making it the system's current profile.
 *
 * @return Always B_ERROR in the current stub.
 * @todo  Push settings to net_server and update the current-profile marker.
 */
status_t
BNetworkProfile::MakeCurrent()
{
	return B_ERROR;
}


// #pragma mark -


/**
 * @brief Returns the system default profile, if any.
 *
 * @return Pointer to the default profile, or NULL when none is configured.
 * @todo  Wire up to persistent profile registry.
 */
BNetworkProfile*
BNetworkProfile::Default()
{
	return NULL;
}


/**
 * @brief Returns the currently active profile, if any.
 *
 * @return Pointer to the current profile, or NULL when none is active.
 * @todo  Wire up to persistent profile registry.
 */
BNetworkProfile*
BNetworkProfile::Current()
{
	return NULL;
}
