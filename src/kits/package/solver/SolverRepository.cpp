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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file SolverRepository.cpp
 * @brief Implementation of BSolverRepository, the package container seen by the solver.
 *
 * BSolverRepository owns a list of BSolverPackage objects and presents them to
 * the solver. It can be initialised from a name, an installation location, all
 * installation locations, a BRepositoryConfig, or a BRepositoryCache. A monotonically
 * increasing change counter lets the solver detect stale pool data.
 *
 * @see BSolverPackage, BSolver, BPackageManager
 */


#include <package/solver/SolverRepository.h>

#include <package/PackageDefs.h>
#include <package/PackageRoster.h>
#include <package/RepositoryCache.h>
#include <package/RepositoryConfig.h>
#include <package/solver/SolverPackage.h>


/** @brief Initial capacity of the package list. */
static const int32 kInitialPackageListSize = 40;


namespace BPackageKit {


/**
 * @brief Default-construct an uninitialised BSolverRepository.
 */
BSolverRepository::BSolverRepository()
	:
	fName(),
	fPriority(0),
	fIsInstalled(false),
	fPackages(kInitialPackageListSize),
	fChangeCount(0)
{
}


/**
 * @brief Construct and immediately initialise a BSolverRepository by name.
 *
 * @param name  The name to assign; must be non-empty.
 */
BSolverRepository::BSolverRepository(const BString& name)
	:
	fName(),
	fPriority(0),
	fIsInstalled(false),
	fPackages(kInitialPackageListSize),
	fChangeCount(0)
{
	SetTo(name);
}


/**
 * @brief Construct and initialise a repository from an installation location.
 *
 * @param location  The installation location whose packages are loaded.
 */
BSolverRepository::BSolverRepository(BPackageInstallationLocation location)
	:
	fName(),
	fPriority(0),
	fIsInstalled(false),
	fPackages(kInitialPackageListSize),
	fChangeCount(0)
{
	SetTo(location);
}


/**
 * @brief Construct and initialise a repository for all installation locations.
 *
 * @param  Sentinel tag value; use B_ALL_INSTALLATION_LOCATIONS.
 */
BSolverRepository::BSolverRepository(BAllInstallationLocations)
	:
	fName(),
	fPriority(0),
	fIsInstalled(false),
	fPackages(kInitialPackageListSize),
	fChangeCount(0)
{
	SetTo(B_ALL_INSTALLATION_LOCATIONS);
}


/**
 * @brief Construct and initialise a repository from a BRepositoryConfig.
 *
 * @param config  The repository configuration to load packages from.
 */
BSolverRepository::BSolverRepository(const BRepositoryConfig& config)
	:
	fName(),
	fPriority(0),
	fIsInstalled(false),
	fPackages(kInitialPackageListSize),
	fChangeCount(0)
{
	SetTo(config);
}


/**
 * @brief Destroy the repository and free all owned BSolverPackage objects.
 */
BSolverRepository::~BSolverRepository()
{
}


/**
 * @brief Reset the repository and set its name.
 *
 * @param name  The new name; must be non-empty.
 * @return B_OK on success, B_BAD_VALUE if @a name is empty.
 */
status_t
BSolverRepository::SetTo(const BString& name)
{
	Unset();

	fName = name;
	return fName.IsEmpty() ? B_BAD_VALUE : B_OK;
}


/**
 * @brief Reset and load all active packages from the given installation location.
 *
 * Sets fIsInstalled to true on success.
 *
 * @param location  The installation location to query via BPackageRoster.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSolverRepository::SetTo(BPackageInstallationLocation location)
{
	Unset();

	fName = "Installed";

	status_t error = AddPackages(location);
	if (error != B_OK) {
		Unset();
		return error;
	}

	fIsInstalled = true;
	return B_OK;
}


/**
 * @brief Reset and load packages from all installation locations.
 *
 * Loads the system location first, then adds packages from the home location.
 *
 * @param  Sentinel tag value; use B_ALL_INSTALLATION_LOCATIONS.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSolverRepository::SetTo(BAllInstallationLocations)
{
	status_t error = SetTo(B_PACKAGE_INSTALLATION_LOCATION_SYSTEM);
	if (error != B_OK)
		return error;

	error = AddPackages(B_PACKAGE_INSTALLATION_LOCATION_HOME);
	if (error != B_OK) {
		Unset();
		return error;
	}

	return B_OK;
}


/**
 * @brief Internal callback used when reading package infos from a repository cache.
 *
 * @param context      Pointer to the BSolverRepository to populate.
 * @param packageInfo  The BPackageInfo parsed from the cache.
 * @return True to continue, false if AddPackage() failed.
 */
static bool
SolverRepositoryAddPackageCallback(void* context, const BPackageInfo& packageInfo)
{
	BSolverRepository* repo = (BSolverRepository*)context;

	status_t error = repo->AddPackage(packageInfo);
	if (error != B_OK) {
		repo->Unset();
		return false;
	}
	return true;
}


/**
 * @brief Reset and load packages from a BRepositoryConfig.
 *
 * Looks up the repository cache via BPackageRoster and populates the package list.
 *
 * @param config  The repository configuration to load.
 * @return B_OK on success, B_BAD_VALUE if the config is invalid, or an error
 *         code if the cache cannot be obtained.
 */
status_t
BSolverRepository::SetTo(const BRepositoryConfig& config)
{
	Unset();

	if (config.InitCheck() != B_OK)
		return B_BAD_VALUE;

	fName = config.Name();
	fPriority = config.Priority();

	BPackageRoster roster;
	BRepositoryCache cache;
	status_t error = roster.GetRepositoryCache(config.Name(), &cache);
	if (error != B_OK) {
		Unset();
		return error;
	}

	if ((error = cache.GetPackageInfos(SolverRepositoryAddPackageCallback, this)) != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Reset and load packages from a BRepositoryCache.
 *
 * @param cache  The repository cache to load packages from.
 * @return B_OK on success, B_BAD_VALUE if the cache's info is invalid, or
 *         an error code if the packages cannot be read.
 */
status_t
BSolverRepository::SetTo(const BRepositoryCache& cache)
{
	Unset();

	const BRepositoryInfo& info = cache.Info();
	if (info.InitCheck() != B_OK)
		return B_BAD_VALUE;

	fName = info.Name();
	fPriority = info.Priority();

	status_t error;
	if ((error = cache.GetPackageInfos(SolverRepositoryAddPackageCallback, this)) != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Reset the repository to an empty, uninitialised state.
 *
 * Clears the name, resets priority and installed flag, empties the package
 * list, and increments the change counter.
 */
void
BSolverRepository::Unset()
{
	fName = BString();
	fPriority = 0;
	fIsInstalled = false;
	fPackages.MakeEmpty();
	fChangeCount++;
}


/**
 * @brief Check whether the repository has been initialised.
 *
 * @return B_OK if the name is non-empty, B_NO_INIT otherwise.
 */
status_t
BSolverRepository::InitCheck()
{
	return fName.IsEmpty() ? B_NO_INIT : B_OK;
}


/**
 * @brief Check whether this repository represents currently installed packages.
 *
 * @return True if the repository holds the installed package set.
 */
bool
BSolverRepository::IsInstalled() const
{
	return fIsInstalled;
}


/**
 * @brief Mark the repository as installed or not and increment the change counter.
 *
 * @param isInstalled  True if this repository holds installed packages.
 */
void
BSolverRepository::SetInstalled(bool isInstalled)
{
	fIsInstalled = isInstalled;
	fChangeCount++;
}


/**
 * @brief Return the display name of this repository.
 *
 * @return The repository name string.
 */
BString
BSolverRepository::Name() const
{
	return fName;
}


/**
 * @brief Return the solver priority of this repository.
 *
 * @return Priority value; lower values indicate higher preference.
 */
int32
BSolverRepository::Priority() const
{
	return fPriority;
}


/**
 * @brief Set the solver priority and increment the change counter.
 *
 * @param priority  The new priority value.
 */
void
BSolverRepository::SetPriority(int32 priority)
{
	fPriority = priority;
	fChangeCount++;
}


/**
 * @brief Check whether the repository contains no packages.
 *
 * @return True if the package list is empty.
 */
bool
BSolverRepository::IsEmpty() const
{
	return fPackages.IsEmpty();
}


/**
 * @brief Return the number of packages in the repository.
 *
 * @return Count of BSolverPackage objects.
 */
int32
BSolverRepository::CountPackages() const
{
	return fPackages.CountItems();
}


/**
 * @brief Return the package at the given index.
 *
 * @param index  Zero-based index.
 * @return Pointer to the BSolverPackage, or NULL if out of range.
 */
BSolverPackage*
BSolverRepository::PackageAt(int32 index) const
{
	return fPackages.ItemAt(index);
}


/**
 * @brief Create a BSolverPackage from @a info and add it to the repository.
 *
 * @param info      The package metadata.
 * @param _package  Optional output pointer set to the new BSolverPackage.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BSolverRepository::AddPackage(const BPackageInfo& info,
	BSolverPackage** _package)
{
	BSolverPackage* package = new(std::nothrow) BSolverPackage(this, info);
	if (package == NULL || !fPackages.AddItem(package)) {
		delete package;
		return B_NO_MEMORY;
	}

	fChangeCount++;

	if (_package != NULL)
		*_package = package;

	return B_OK;
}


/**
 * @brief Add all active packages from the given installation location.
 *
 * @param location  The installation location to query.
 * @return B_OK on success, or an error code if the roster query fails.
 */
status_t
BSolverRepository::AddPackages(BPackageInstallationLocation location)
{
	BPackageRoster roster;
	BPackageInfoSet packageInfos;
	status_t error = roster.GetActivePackages(location, packageInfos);
	if (error != B_OK)
		return error;

	BPackageInfoSet::Iterator it = packageInfos.GetIterator();
	while (const BPackageInfo* packageInfo = it.Next()) {
		error = AddPackage(*packageInfo);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Remove a package from the repository without deleting it.
 *
 * @param package  The package to remove.
 * @return True if the package was found and removed.
 */
bool
BSolverRepository::RemovePackage(BSolverPackage* package)
{
	if (!fPackages.RemoveItem(package, false))
		return false;

	fChangeCount++;
	return true;
}


/**
 * @brief Remove and delete a package from the repository.
 *
 * @param package  The package to remove and destroy.
 * @return True if the package was found and deleted.
 */
bool
BSolverRepository::DeletePackage(BSolverPackage* package)
{
	if (!RemovePackage(package))
		return false;

	delete package;
	return true;
}


/**
 * @brief Return the monotonically increasing change counter.
 *
 * The solver uses this to detect when a repository's contents have changed
 * since the pool was last built.
 *
 * @return The current change count.
 */
uint64
BSolverRepository::ChangeCount() const
{
	return fChangeCount;
}


}	// namespace BPackageKit
