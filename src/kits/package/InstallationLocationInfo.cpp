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
 *   Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file InstallationLocationInfo.cpp
 * @brief Snapshot of a package installation location as reported by the daemon.
 *
 * BInstallationLocationInfo aggregates the node references for the base and
 * packages directories, three package-info sets (latest active, latest
 * inactive, and currently active), the active state name, and the change count
 * used for optimistic concurrency in BActivationTransaction.
 *
 * @see BDaemonClient, BActivationTransaction
 */


#include <package/InstallationLocationInfo.h>


namespace BPackageKit {


/**
 * @brief Default constructor; creates an uninitialised location-info object.
 */
BInstallationLocationInfo::BInstallationLocationInfo()
	:
	fLocation(B_PACKAGE_INSTALLATION_LOCATION_ENUM_COUNT),
	fBaseDirectoryRef(),
	fPackageDirectoryRef(),
	fLatestActivePackageInfos(),
	fLatestInactivePackageInfos(),
	fCurrentlyActivePackageInfos(),
	fActiveStateName(),
	fChangeCount(0)
{
}


/**
 * @brief Destructor.
 */
BInstallationLocationInfo::~BInstallationLocationInfo()
{
}


/**
 * @brief Reset all fields to their uninitialised defaults.
 */
void
BInstallationLocationInfo::Unset()
{
	fLocation = B_PACKAGE_INSTALLATION_LOCATION_ENUM_COUNT;
	fBaseDirectoryRef = node_ref();
	fPackageDirectoryRef = node_ref();
	fLatestActivePackageInfos.MakeEmpty();
	fLatestInactivePackageInfos.MakeEmpty();
	fCurrentlyActivePackageInfos.MakeEmpty();
	fActiveStateName.Truncate(0);
	fChangeCount = 0;
}


/**
 * @brief Return the installation location this info object describes.
 *
 * @return The BPackageInstallationLocation enum value.
 */
BPackageInstallationLocation
BInstallationLocationInfo::Location() const
{
	return fLocation;
}


/**
 * @brief Set the installation location.
 *
 * @param location  New BPackageInstallationLocation value.
 */
void
BInstallationLocationInfo::SetLocation(BPackageInstallationLocation location)
{
	fLocation = location;
}


/**
 * @brief Return the node_ref of the base directory for this location.
 *
 * @return Const reference to the base directory node_ref.
 */
const node_ref&
BInstallationLocationInfo::BaseDirectoryRef() const
{
	return fBaseDirectoryRef;
}


/**
 * @brief Set the node_ref of the base directory.
 *
 * @param ref  New base directory node_ref.
 * @return B_OK on success, or B_NO_MEMORY if the assignment failed.
 */
status_t
BInstallationLocationInfo::SetBaseDirectoryRef(const node_ref& ref)
{
	fBaseDirectoryRef = ref;
	return fBaseDirectoryRef == ref ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Return the node_ref of the packages subdirectory.
 *
 * @return Const reference to the packages directory node_ref.
 */
const node_ref&
BInstallationLocationInfo::PackagesDirectoryRef() const
{
	return fPackageDirectoryRef;
}


/**
 * @brief Set the node_ref of the packages subdirectory.
 *
 * @param ref  New packages directory node_ref.
 * @return B_OK on success, or B_NO_MEMORY if the assignment failed.
 */
status_t
BInstallationLocationInfo::SetPackagesDirectoryRef(const node_ref& ref)
{
	fPackageDirectoryRef = ref;
	return fPackageDirectoryRef == ref ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Return the set of packages that will be active after the next boot.
 *
 * @return Const reference to the latest-active BPackageInfoSet.
 */
const BPackageInfoSet&
BInstallationLocationInfo::LatestActivePackageInfos() const
{
	return fLatestActivePackageInfos;
}


/**
 * @brief Replace the latest-active package info set.
 *
 * @param infos  New BPackageInfoSet to store.
 */
void
BInstallationLocationInfo::SetLatestActivePackageInfos(
	const BPackageInfoSet& infos)
{
	fLatestActivePackageInfos = infos;
}


/**
 * @brief Return the set of packages that were inactive at the latest state.
 *
 * @return Const reference to the latest-inactive BPackageInfoSet.
 */
const BPackageInfoSet&
BInstallationLocationInfo::LatestInactivePackageInfos() const
{
	return fLatestInactivePackageInfos;
}


/**
 * @brief Replace the latest-inactive package info set.
 *
 * @param infos  New BPackageInfoSet to store.
 */
void
BInstallationLocationInfo::SetLatestInactivePackageInfos(
	const BPackageInfoSet& infos)
{
	fLatestInactivePackageInfos = infos;
}


/**
 * @brief Return the set of packages currently active in packagefs.
 *
 * @return Const reference to the currently-active BPackageInfoSet.
 */
const BPackageInfoSet&
BInstallationLocationInfo::CurrentlyActivePackageInfos() const
{
	return fCurrentlyActivePackageInfos;
}


/**
 * @brief Replace the currently-active package info set.
 *
 * @param infos  New BPackageInfoSet to store.
 */
void
BInstallationLocationInfo::SetCurrentlyActivePackageInfos(
	const BPackageInfoSet& infos)
{
	fCurrentlyActivePackageInfos = infos;
}


/**
 * @brief Return the name of the currently active state directory.
 *
 * @return Const reference to the active state name string.
 */
const BString&
BInstallationLocationInfo::ActiveStateName() const
{
	return fActiveStateName;
}


/**
 * @brief Set the name of the active state directory.
 *
 * @param name  New active state name.
 */
void
BInstallationLocationInfo::SetActiveStateName(const BString& name)
{
	fActiveStateName = name;
}


/**
 * @brief Return the change count for optimistic concurrency.
 *
 * @return The 64-bit change count from the daemon.
 */
int64
BInstallationLocationInfo::ChangeCount() const
{
	return fChangeCount;
}


/**
 * @brief Set the change count.
 *
 * @param changeCount  New change count value.
 */
void
BInstallationLocationInfo::SetChangeCount(int64 changeCount)
{
	fChangeCount = changeCount;
}


}	// namespace BPackageKit
