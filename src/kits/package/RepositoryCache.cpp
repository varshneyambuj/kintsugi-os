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
 *   Copyright 2011-2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file RepositoryCache.cpp
 * @brief Implementation of BRepositoryCache, the on-disk package repository index.
 *
 * BRepositoryCache wraps a repository cache file (HPKG repository format) and
 * provides access to the repository metadata and the list of packages it
 * contains. Callers can iterate packages via a callback-based GetPackageInfos()
 * interface. The class also tracks whether the cache belongs to the user or
 * the system installation.
 *
 * @see BRepositoryInfo, BPackageInfo, BRepositoryConfig
 */


#include <package/RepositoryCache.h>

#include <stdio.h>

#include <new>

#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <package/hpkg/ErrorOutput.h>
#include <package/hpkg/PackageInfoAttributeValue.h>
#include <package/hpkg/RepositoryContentHandler.h>
#include <package/hpkg/RepositoryReader.h>
#include <package/hpkg/StandardErrorOutput.h>
#include <package/PackageInfo.h>
#include <package/RepositoryInfo.h>

#include <package/PackageInfoContentHandler.h>


namespace BPackageKit {


using namespace BHPKG;


// #pragma mark - RepositoryContentHandler


/**
 * @brief Internal HPKG content handler that collects repository metadata and packages.
 *
 * Feeds each parsed package through fPackageInfoContentHandler and invokes
 * the caller-supplied callback for each valid package.
 */
struct BRepositoryCache::RepositoryContentHandler : BRepositoryContentHandler {
	/**
	 * @brief Construct the content handler.
	 *
	 * @param errorOutput       Sink for parse error messages.
	 * @param repositoryInfo    Reference filled with repository-level metadata.
	 * @param callback          Per-package callback; may be NULL (then stops early).
	 * @param callbackContext   Opaque pointer forwarded to @a callback.
	 */
	RepositoryContentHandler(BErrorOutput* errorOutput, BRepositoryInfo& repositoryInfo,
		GetPackageInfosCallback callback, void* callbackContext)
		:
		fRepositoryInfo(repositoryInfo),
		fPackageInfo(),
		fCallback(callback),
		fCallbackContext(callbackContext),
		fPackageInfoContentHandler(fPackageInfo, errorOutput)
	{
	}

	/**
	 * @brief Called when a new package block begins; resets the accumulator.
	 *
	 * @param packageName  Name of the package being started.
	 * @return B_OK always.
	 */
	virtual status_t HandlePackage(const char* packageName)
	{
		fPackageInfo.Clear();
		return B_OK;
	}

	/**
	 * @brief Forward a package attribute to the inner content handler.
	 *
	 * @param value  The attribute value to process.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual status_t HandlePackageAttribute(
		const BPackageInfoAttributeValue& value)
	{
		return fPackageInfoContentHandler.HandlePackageAttribute(value);
	}

	/**
	 * @brief Called when a package block ends; validates and delivers the info.
	 *
	 * @param packageName  Name of the completed package.
	 * @return B_OK on success, B_CANCELED if the callback returns false or is NULL.
	 */
	virtual status_t HandlePackageDone(const char* packageName)
	{
		status_t result = fPackageInfo.InitCheck();
		if (result != B_OK)
			return result;

		if (fCallback == NULL)
			return B_CANCELED;

		if (!fCallback(fCallbackContext, fPackageInfo))
			return B_CANCELED;

		return B_OK;
	}

	/**
	 * @brief Store repository-level metadata when encountered.
	 *
	 * @param repositoryInfo  The repository info parsed from the cache file.
	 * @return B_OK always.
	 */
	virtual status_t HandleRepositoryInfo(const BRepositoryInfo& repositoryInfo)
	{
		fRepositoryInfo = repositoryInfo;
		return B_OK;
	}

	/**
	 * @brief Called when a parse error occurs; currently a no-op.
	 */
	virtual void HandleErrorOccurred()
	{
	}

private:
	BRepositoryInfo&			fRepositoryInfo;
	BPackageInfo				fPackageInfo;
	GetPackageInfosCallback		fCallback;
	void*						fCallbackContext;

	BPackageInfoContentHandler	fPackageInfoContentHandler;
};


// #pragma mark - BRepositoryCache


/**
 * @brief Default-construct an empty BRepositoryCache.
 */
BRepositoryCache::BRepositoryCache()
	:
	fIsUserSpecific(false)
{
}


/**
 * @brief Destroy the BRepositoryCache.
 */
BRepositoryCache::~BRepositoryCache()
{
}


/**
 * @brief Return the filesystem entry of the cache file.
 *
 * @return Reference to the BEntry representing the cache file.
 */
const BEntry&
BRepositoryCache::Entry() const
{
	return fEntry;
}


/**
 * @brief Return the repository metadata stored in the cache.
 *
 * @return Reference to the BRepositoryInfo describing the repository.
 */
const BRepositoryInfo&
BRepositoryCache::Info() const
{
	return fInfo;
}


/**
 * @brief Check whether this cache belongs to the per-user installation.
 *
 * @return True if the cache file lives under the user settings directory.
 */
bool
BRepositoryCache::IsUserSpecific() const
{
	return fIsUserSpecific;
}


/**
 * @brief Initialise the cache from a filesystem entry and read its metadata.
 *
 * Only the repository info is read at this point; package data is deferred
 * to GetPackageInfos(). The fIsUserSpecific flag is set based on whether
 * the entry resides under the user settings directory.
 *
 * @param entry  The BEntry pointing to the repository cache file.
 * @return B_OK on success, or an error code if the file cannot be read.
 */
status_t
BRepositoryCache::SetTo(const BEntry& entry)
{
	fEntry = entry;

	BPath repositoryCachePath;
	status_t result;
	if ((result = entry.GetPath(&repositoryCachePath)) != B_OK)
		return result;

	// read only info from repository cache
	if ((result = _ReadCache(repositoryCachePath, fInfo, NULL, NULL) != B_OK))
		return result;

	BPath userSettingsPath;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &userSettingsPath) == B_OK) {
		BDirectory userSettingsDir(userSettingsPath.Path());
		fIsUserSpecific = userSettingsDir.Contains(&entry);
	} else
		fIsUserSpecific = false;

	return B_OK;
}


/**
 * @brief Iterate over all packages in the cache and invoke the callback for each.
 *
 * @param callback  Function called once per valid BPackageInfo; returning false
 *                  stops iteration.
 * @param context   Opaque pointer forwarded unchanged to @a callback.
 * @return B_OK on success, or an error code if reading fails.
 */
status_t
BRepositoryCache::GetPackageInfos(GetPackageInfosCallback callback, void* context) const
{
	BPath repositoryCachePath;
	status_t result;
	if ((result = fEntry.GetPath(&repositoryCachePath)) != B_OK)
		return result;

	BRepositoryInfo dummy;
	return _ReadCache(repositoryCachePath, dummy, callback, context);
}


/**
 * @brief Open and parse a repository cache file, delivering data via handler callbacks.
 *
 * @param repositoryCachePath  Path to the cache file to read.
 * @param repositoryInfo       Reference filled with repository-level metadata.
 * @param callback             Per-package callback; may be NULL.
 * @param context              Opaque pointer forwarded to @a callback.
 * @return B_OK on success, or an error code if the file cannot be parsed.
 */
status_t
BRepositoryCache::_ReadCache(const BPath& repositoryCachePath,
	BRepositoryInfo& repositoryInfo, GetPackageInfosCallback callback, void* context) const
{
	status_t result;

	BStandardErrorOutput errorOutput;
	BRepositoryReader repositoryReader(&errorOutput);
	if ((result = repositoryReader.Init(repositoryCachePath.Path())) != B_OK)
		return result;

	RepositoryContentHandler handler(&errorOutput, repositoryInfo, callback, context);
	if ((result = repositoryReader.ParseContent(&handler)) != B_OK && result != B_CANCELED)
		return result;

	return B_OK;
}


}	// namespace BPackageKit
