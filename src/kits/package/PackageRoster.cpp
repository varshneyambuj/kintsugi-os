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
 *   Copyright 2011-2014, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file PackageRoster.cpp
 * @brief Implementation of BPackageRoster, the central package management facade.
 *
 * BPackageRoster gives applications a single entry point for querying installed
 * packages, locating repository configuration and cache files, enumerating
 * repository names, and subscribing to package-change notifications. On Haiku
 * it communicates with the package daemon and the registrar; on non-Haiku
 * platforms many operations return B_NOT_SUPPORTED.
 *
 * @see BRepositoryConfig, BRepositoryCache, BPackageInfoSet
 */


#include <package/PackageRoster.h>

#include <errno.h>
#include <sys/stat.h>

#include <Directory.h>
#include <Entry.h>
#include <Messenger.h>
#include <Path.h>
#include <String.h>
#include <StringList.h>

#include <package/InstallationLocationInfo.h>
#include <package/PackageInfo.h>
#include <package/PackageInfoContentHandler.h>
#include <package/PackageInfoSet.h>
#include <package/RepositoryCache.h>
#include <package/RepositoryConfig.h>

#include <package/hpkg/PackageReader.h>


#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
#	include <package/DaemonClient.h>
#	include <RegistrarDefs.h>
#	include <RosterPrivate.h>
#endif


namespace BPackageKit {


using namespace BHPKG;


/**
 * @brief Default-construct a BPackageRoster.
 */
BPackageRoster::BPackageRoster()
{
}


/**
 * @brief Destroy the BPackageRoster.
 */
BPackageRoster::~BPackageRoster()
{
}


/**
 * @brief Determine whether a system reboot is required to apply pending package changes.
 *
 * Only meaningful on an installed Haiku system; always returns false elsewhere.
 *
 * @return True if there are packages waiting to be activated on next boot.
 */
bool
BPackageRoster::IsRebootNeeded()
{
// This method makes sense only on an installed Haiku, but not for the build
// tools.
#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
	BInstallationLocationInfo info;

	// We get information on the system package installation location.
	// If we fail, we just have to assume a reboot is not needed.
	if (GetInstallationLocationInfo(B_PACKAGE_INSTALLATION_LOCATION_SYSTEM,
		info) != B_OK)
		return false;

	// CurrentlyActivePackageInfos() will return 0 if no packages need to be
	// activated with a reboot. Otherwise, the method will return the total
	// number of packages in the system package directory.
	if (info.CurrentlyActivePackageInfos().CountInfos() != 0)
		return true;
#endif

	return false;
}


/**
 * @brief Retrieve the path to the system-wide repository configuration directory.
 *
 * @param path    Output path object to fill in.
 * @param create  If true, create the directory if it does not yet exist.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::GetCommonRepositoryConfigPath(BPath* path, bool create) const
{
	return _GetRepositoryPath(path, create, B_SYSTEM_SETTINGS_DIRECTORY);
}


/**
 * @brief Retrieve the path to the per-user repository configuration directory.
 *
 * @param path    Output path object to fill in.
 * @param create  If true, create the directory if it does not yet exist.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::GetUserRepositoryConfigPath(BPath* path, bool create) const
{
	return _GetRepositoryPath(path, create, B_USER_SETTINGS_DIRECTORY);
}


/**
 * @brief Retrieve the path to the system-wide repository cache directory.
 *
 * @param path    Output path object to fill in.
 * @param create  If true, create the directory if it does not yet exist.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::GetCommonRepositoryCachePath(BPath* path, bool create) const
{
	return _GetRepositoryPath(path, create, B_SYSTEM_CACHE_DIRECTORY);
}


/**
 * @brief Retrieve the path to the per-user repository cache directory.
 *
 * @param path    Output path object to fill in.
 * @param create  If true, create the directory if it does not yet exist.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::GetUserRepositoryCachePath(BPath* path, bool create) const
{
	return _GetRepositoryPath(path, create, B_USER_CACHE_DIRECTORY);
}


/**
 * @brief Visit all repository configuration files in the system-wide location.
 *
 * @param visitor  Callback object invoked once per config file entry.
 * @return B_OK on success, or the first error returned by @a visitor.
 */
status_t
BPackageRoster::VisitCommonRepositoryConfigs(BRepositoryConfigVisitor& visitor)
{
	BPath commonRepositoryConfigPath;
	status_t result
		= GetCommonRepositoryConfigPath(&commonRepositoryConfigPath);
	if (result != B_OK)
		return result;

	return _VisitRepositoryConfigs(commonRepositoryConfigPath, visitor);
}


/**
 * @brief Visit all repository configuration files in the per-user location.
 *
 * @param visitor  Callback object invoked once per config file entry.
 * @return B_OK on success, or the first error returned by @a visitor.
 */
status_t
BPackageRoster::VisitUserRepositoryConfigs(BRepositoryConfigVisitor& visitor)
{
	BPath userRepositoryConfigPath;
	status_t result = GetUserRepositoryConfigPath(&userRepositoryConfigPath);
	if (result != B_OK)
		return result;

	return _VisitRepositoryConfigs(userRepositoryConfigPath, visitor);
}


/**
 * @brief Collect the names of all known repositories (user and system).
 *
 * Duplicate names are suppressed so each repository appears only once.
 *
 * @param names  List to which repository names are appended.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::GetRepositoryNames(BStringList& names)
{
	struct RepositoryNameCollector : public BRepositoryConfigVisitor {
		RepositoryNameCollector(BStringList& _names)
			: names(_names)
		{
		}
		status_t operator()(const BEntry& entry)
		{
			char name[B_FILE_NAME_LENGTH];
			status_t result = entry.GetName(name);
			if (result != B_OK)
				return result;
			int32 count = names.CountStrings();
			for (int i = 0; i < count; ++i) {
				if (names.StringAt(i).Compare(name) == 0)
					return B_OK;
			}
			names.Add(name);
			return B_OK;
		}
		BStringList& names;
	};
	RepositoryNameCollector repositoryNameCollector(names);
	status_t result = VisitUserRepositoryConfigs(repositoryNameCollector);
	if (result != B_OK)
		return result;

	return VisitCommonRepositoryConfigs(repositoryNameCollector);
}


/**
 * @brief Load the repository cache for a named repository.
 *
 * The user cache path takes precedence over the system cache path.
 *
 * @param name             Name of the repository to look up.
 * @param repositoryCache  Output object filled with the cache data.
 * @return B_OK on success, B_BAD_VALUE if @a repositoryCache is NULL, or
 *         an error code if the cache cannot be found or read.
 */
status_t
BPackageRoster::GetRepositoryCache(const BString& name,
	BRepositoryCache* repositoryCache)
{
	if (repositoryCache == NULL)
		return B_BAD_VALUE;

	// user path has higher precedence than common path
	BPath path;
	status_t result = GetUserRepositoryCachePath(&path);
	if (result != B_OK)
		return result;
	path.Append(name.String());

	BEntry repoCacheEntry(path.Path());
	if (repoCacheEntry.Exists())
		return repositoryCache->SetTo(repoCacheEntry);

	if ((result = GetCommonRepositoryCachePath(&path, true)) != B_OK)
		return result;
	path.Append(name.String());

	result = repoCacheEntry.SetTo(path.Path());
	if (result != B_OK)
		return result;
	return repositoryCache->SetTo(repoCacheEntry);
}


/**
 * @brief Load the repository configuration for a named repository.
 *
 * The user config path takes precedence over the system config path.
 *
 * @param name               Name of the repository to look up.
 * @param repositoryConfig   Output object filled with the configuration data.
 * @return B_OK on success, B_BAD_VALUE if @a repositoryConfig is NULL, or
 *         an error code if the config cannot be found or read.
 */
status_t
BPackageRoster::GetRepositoryConfig(const BString& name,
	BRepositoryConfig* repositoryConfig)
{
	if (repositoryConfig == NULL)
		return B_BAD_VALUE;

	// user path has higher precedence than common path
	BPath path;
	status_t result = GetUserRepositoryConfigPath(&path);
	if (result != B_OK)
		return result;
	path.Append(name.String());

	BEntry repoConfigEntry(path.Path());
	if (repoConfigEntry.Exists())
		return repositoryConfig->SetTo(repoConfigEntry);

	if ((result = GetCommonRepositoryConfigPath(&path, true)) != B_OK)
		return result;
	path.Append(name.String());

	result = repoConfigEntry.SetTo(path.Path());
	if (result != B_OK)
		return result;
	return repositoryConfig->SetTo(repoConfigEntry);
}


/**
 * @brief Query the package daemon for information about an installation location.
 *
 * Only supported on an installed Haiku system; returns B_NOT_SUPPORTED elsewhere.
 *
 * @param location  The installation location to query.
 * @param _info     Output object filled with location information.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::GetInstallationLocationInfo(
	BPackageInstallationLocation location, BInstallationLocationInfo& _info)
{
// This method makes sense only on an installed Haiku, but not for the build
// tools.
#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
	return BPackageKit::BPrivate::BDaemonClient().GetInstallationLocationInfo(
		location, _info);
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Retrieve the set of currently active packages at an installation location.
 *
 * Only supported on an installed Haiku system; returns B_NOT_SUPPORTED elsewhere.
 *
 * @param location      The installation location to query.
 * @param packageInfos  Output set populated with active package info objects.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::GetActivePackages(BPackageInstallationLocation location,
	BPackageInfoSet& packageInfos)
{
// This method makes sense only on an installed Haiku, but not for the build
// tools.
#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
	BInstallationLocationInfo info;
	status_t error = GetInstallationLocationInfo(location, info);
	if (error != B_OK)
		return error;

	packageInfos = info.LatestActivePackageInfos();
	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Check whether a specific package is currently active at a location.
 *
 * Only supported on an installed Haiku system; returns B_NOT_SUPPORTED elsewhere.
 *
 * @param location  The installation location to search.
 * @param info      The package to look for (matched by name and version).
 * @param active    Output boolean set to true if the package is active.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::IsPackageActive(BPackageInstallationLocation location,
	const BPackageInfo info, bool* active)
{
// This method makes sense only on an installed Haiku, but not for the build
// tools.
#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
	BPackageInfoSet packageInfos;
	status_t error = GetActivePackages(location, packageInfos);
	if (error != B_OK)
		return error;

	BPackageInfoSet::Iterator it = packageInfos.GetIterator();
	while (const BPackageInfo* packageInfo = it.Next()) {
		if (info.Name() == packageInfo->Name() &&
			info.Version().Compare(packageInfo->Version()) == 0) {
			*active = true;
			break;
		}
	}

	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Register a messenger to receive package-change notifications.
 *
 * Only supported on an installed Haiku system; returns B_NOT_SUPPORTED elsewhere.
 *
 * @param target     The messenger that will receive notification messages.
 * @param eventMask  Bitmask of event types to subscribe to.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::StartWatching(const BMessenger& target, uint32 eventMask)
{
// This method makes sense only on an installed Haiku, but not for the build
// tools.
#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
	// compose the registrar request
	BMessage request(::BPrivate::B_REG_PACKAGE_START_WATCHING);
	status_t error;
	if ((error = request.AddMessenger("target", target)) != B_OK
		|| (error = request.AddUInt32("events", eventMask)) != B_OK) {
		return error;
	}

	// send it
	BMessage reply;
	error = BRoster::Private().SendTo(&request, &reply, false);
	if (error != B_OK)
		return error;

	// get result
	if (reply.what != ::BPrivate::B_REG_SUCCESS) {
		int32 result;
		if (reply.FindInt32("error", &result) != B_OK)
			result = B_ERROR;
		return (status_t)error;
	}

	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Unregister a messenger from package-change notifications.
 *
 * Only supported on an installed Haiku system; returns B_NOT_SUPPORTED elsewhere.
 *
 * @param target  The messenger to unregister.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::StopWatching(const BMessenger& target)
{
// This method makes sense only on an installed Haiku, but not for the build
// tools.
#if defined(__HAIKU__) && !defined(HAIKU_HOST_PLATFORM_HAIKU)
	// compose the registrar request
	BMessage request(::BPrivate::B_REG_PACKAGE_STOP_WATCHING);
	status_t error = request.AddMessenger("target", target);
	if (error  != B_OK)
		return error;

	// send it
	BMessage reply;
	error = BRoster::Private().SendTo(&request, &reply, false);
	if (error != B_OK)
		return error;

	// get result
	if (reply.what != ::BPrivate::B_REG_SUCCESS) {
		int32 result;
		if (reply.FindInt32("error", &result) != B_OK)
			result = B_ERROR;
		return (status_t)error;
	}

	return B_OK;
#else
	return B_NOT_SUPPORTED;
#endif
}


/**
 * @brief Build the path to the "package-repositories" subdirectory under a known directory.
 *
 * @param path      Output path object to fill.
 * @param create    If true, create the directory when it does not exist.
 * @param whichDir  The well-known base directory constant (e.g. B_SYSTEM_SETTINGS_DIRECTORY).
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageRoster::_GetRepositoryPath(BPath* path, bool create,
	directory_which whichDir) const
{
	if (path == NULL)
		return B_BAD_VALUE;

	status_t result = find_directory(whichDir, path);
	if (result != B_OK)
		return result;
	if ((result = path->Append("package-repositories")) != B_OK)
		return result;

	if (create) {
		BEntry entry(path->Path(), true);
		if (!entry.Exists()) {
			if (mkdir(path->Path(), 0755) != 0)
				return errno;
		}
	}

	return B_OK;
}


/**
 * @brief Iterate over all entries in a repository config directory and invoke the visitor.
 *
 * @param path     Path to the directory to scan.
 * @param visitor  Callback invoked for each BEntry found.
 * @return B_OK on success, or the first error returned by @a visitor.
 */
status_t
BPackageRoster::_VisitRepositoryConfigs(const BPath& path,
	BRepositoryConfigVisitor& visitor)
{
	BDirectory directory(path.Path());
	status_t result = directory.InitCheck();
	if (result == B_ENTRY_NOT_FOUND)
		return B_OK;
	if (result != B_OK)
		return result;

	BEntry entry;
	while (directory.GetNextEntry(&entry, true) == B_OK) {
		if ((result = visitor(entry)) != B_OK)
			return result;
	}

	return B_OK;
}


}	// namespace BPackageKit
