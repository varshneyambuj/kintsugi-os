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
   /*
    * Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
    * Distributed under the terms of the MIT License.
    *
    * Authors:
    *		Ingo Weinhold <ingo_weinhold@gmx.de>
    */
 */

/** @file VolumeState.cpp
 *  @brief Implements VolumeState package tracking, activation changes, and deep cloning */



#include "VolumeState.h"

#include <AutoDeleter.h>
#include <AutoLocker.h>


VolumeState::VolumeState()
	:
	fPackagesByFileName(),
	fPackagesByNodeRef()
{
}


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


bool
VolumeState::Init()
{
	return fPackagesByFileName.Init() == B_OK
		&& fPackagesByNodeRef.Init() == B_OK;
}


void
VolumeState::AddPackage(Package* package)
{
	fPackagesByFileName.Insert(package);
	fPackagesByNodeRef.Insert(package);
}


void
VolumeState::RemovePackage(Package* package)
{
	fPackagesByFileName.Remove(package);
	fPackagesByNodeRef.Remove(package);
}


void
VolumeState::SetPackageActive(Package* package, bool active)
{
	package->SetActive(active);
}


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
