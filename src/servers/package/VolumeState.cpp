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
 *
 * Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
  * Distributed under the terms of the MIT License.
  *
  * Authors:
  *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */

/** @file VolumeState.cpp
 *  @brief Implements VolumeState package tracking, activation changes, and deep cloning */



#include "VolumeState.h"

#include <AutoDeleter.h>
#include <AutoLocker.h>


/**
 * @brief Constructs an empty VolumeState with uninitialized hash tables.
 *
 * Init() must be called before using this object.
 */
VolumeState::VolumeState()
	:
	fPackagesByFileName(),
	fPackagesByNodeRef()
{
}


/**
 * @brief Destroys the VolumeState, clearing hash tables and deleting all owned packages.
 */
VolumeState::~VolumeState()
{
	fPackagesByFileName.Clear();

	Package* package = fPackagesByNodeRef.Clear(true);
	while (package != NULL) {
		Package* next = package->NodeRefHashTableNext();
		delete package;
		package = next;
	}
}


/**
 * @brief Initializes the internal hash tables for file-name and node-ref lookups.
 *
 * @return @c true if both hash tables initialized successfully, @c false otherwise.
 */
bool
VolumeState::Init()
{
	return fPackagesByFileName.Init() == B_OK
		&& fPackagesByNodeRef.Init() == B_OK;
}


/**
 * @brief Adds a package to both the file-name and node-ref hash tables.
 *
 * @param package The package to insert; must not already be present.
 */
void
VolumeState::AddPackage(Package* package)
{
	fPackagesByFileName.Insert(package);
	fPackagesByNodeRef.Insert(package);
}


/**
 * @brief Removes a package from both the file-name and node-ref hash tables.
 *
 * The caller is responsible for deleting the package after removal.
 *
 * @param package The package to remove.
 */
void
VolumeState::RemovePackage(Package* package)
{
	fPackagesByFileName.Remove(package);
	fPackagesByNodeRef.Remove(package);
}


/**
 * @brief Marks a package as active or inactive.
 *
 * @param package The package whose activation state to change.
 * @param active  @c true to activate, @c false to deactivate.
 */
void
VolumeState::SetPackageActive(Package* package, bool active)
{
	package->SetActive(active);
}


/**
 * @brief Applies an activation change by marking packages active and deleting deactivated ones.
 *
 * Activated packages are marked active. Deactivated packages are removed
 * from the hash tables and deleted.
 *
 * @param activatedPackage   Set of packages to mark as active.
 * @param deactivatePackages Set of packages to remove and delete.
 */
void
VolumeState::ActivationChanged(const PackageSet& activatedPackage,
	const PackageSet& deactivatePackages)
{
	for (PackageSet::iterator it = activatedPackage.begin();
			it != activatedPackage.end(); ++it) {
		(*it)->SetActive(true);
	}

	for (PackageSet::iterator it = deactivatePackages.begin();
			it != deactivatePackages.end(); ++it) {
		Package* package = *it;
		RemovePackage(package);
		delete package;
	}
}


/**
 * @brief Creates a deep copy of this VolumeState, cloning every contained package.
 *
 * Each Package is cloned individually, so the new VolumeState is
 * completely independent of the original.
 *
 * @return A new VolumeState, or NULL if any allocation fails.
 */
VolumeState*
VolumeState::Clone() const
{
	VolumeState* clone = new(std::nothrow) VolumeState;
	if (clone == NULL)
		return NULL;
	ObjectDeleter<VolumeState> cloneDeleter(clone);

	for (PackageFileNameHashTable::Iterator it
				= fPackagesByFileName.GetIterator();
			Package* package = it.Next();) {
		Package* clonedPackage = package->Clone();
		if (clonedPackage == NULL)
			return NULL;
		clone->AddPackage(clonedPackage);
	}

	return cloneDeleter.Detach();
}
