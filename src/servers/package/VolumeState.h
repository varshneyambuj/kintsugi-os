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

/** @file VolumeState.h
 *  @brief Maintains the set of packages on a volume indexed by filename and node ref */

#ifndef VOLUME_STATE_H
#define VOLUME_STATE_H


#include "Package.h"


/** @brief Snapshot of the packages on a volume, dual-indexed by filename and node reference */
class VolumeState {
public:
	/** @brief Construct an empty volume state */
								VolumeState();
	/** @brief Destroy the state and delete all owned Package objects */
								~VolumeState();

	/** @brief Initialize the internal hash tables */
			bool				Init();

	/** @brief Look up a package by its on-disk filename */
			Package*			FindPackage(const char* name) const;
	/** @brief Look up a package by its node reference */
			Package*			FindPackage(const node_ref& nodeRef) const;

	/** @brief Return an iterator over packages indexed by filename */
			PackageFileNameHashTable::Iterator ByFileNameIterator() const;
	/** @brief Return an iterator over packages indexed by node reference */
			PackageNodeRefHashTable::Iterator ByNodeRefIterator() const;

	/** @brief Insert a package into both hash tables */
			void				AddPackage(Package* package);
	/** @brief Remove a package from both hash tables */
			void				RemovePackage(Package* package);

	/** @brief Set the activation flag on a package */
			void				SetPackageActive(Package* package, bool active);

	/** @brief Apply a batch of activation and deactivation changes */
			void				ActivationChanged(
									const PackageSet& activatedPackage,
									const PackageSet& deactivatePackages);

	/** @brief Create a deep copy of this state sharing the same PackageFile objects */
			VolumeState*		Clone() const;

private:
			void				_RemovePackage(Package* package);

private:
			PackageFileNameHashTable fPackagesByFileName; /**< Packages indexed by filename */
			PackageNodeRefHashTable fPackagesByNodeRef;   /**< Packages indexed by node ref */
};


inline Package*
VolumeState::FindPackage(const char* name) const
{
	return fPackagesByFileName.Lookup(name);
}


inline Package*
VolumeState::FindPackage(const node_ref& nodeRef) const
{
	return fPackagesByNodeRef.Lookup(nodeRef);
}


inline PackageFileNameHashTable::Iterator
VolumeState::ByFileNameIterator() const
{
	return fPackagesByFileName.GetIterator();
}


inline PackageNodeRefHashTable::Iterator
VolumeState::ByNodeRefIterator() const
{
	return fPackagesByNodeRef.GetIterator();
}


#endif	// VOLUME_STATE_H
