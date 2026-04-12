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
 *   Copyright 2013-2020, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 *       Andrew Lindesay <apl@lindesay.co.nz>
 */


/**
 * @file RepositoryBuilder.cpp
 * @brief Implementation of BRepositoryBuilder, the fluent helper for populating solver repositories.
 *
 * BRepositoryBuilder wraps a BSolverRepository and provides a chainable API for
 * adding packages from individual BPackageInfo objects, local .hpkg or .PackageInfo
 * files, a directory of package files, or all packages at an installation location.
 * It also handles adding the finished repository to a BSolver. Fatal errors abort
 * via DIE() rather than returning error codes.
 *
 * @see BSolverRepository, BSolver, BPackageManager
 */


#include <package/manager/RepositoryBuilder.h>

#include <errno.h>
#include <dirent.h>

#include <Entry.h>
#include <package/RepositoryCache.h>
#include <Path.h>

#include <AutoDeleter.h>
#include <AutoDeleterPosix.h>

#include "PackageManagerUtils.h"


namespace BPackageKit {

namespace BManager {

namespace BPrivate {


namespace {


/**
 * @brief ParseErrorListener implementation that accumulates parse-error messages.
 *
 * Used internally by BRepositoryBuilder::AddPackage(const char*) to collect
 * parse errors encountered while reading .PackageInfo files.
 */
class PackageInfoErrorListener : public BPackageInfo::ParseErrorListener {
public:
	/**
	 * @brief Construct the listener with a context label for error messages.
	 *
	 * @param context  Human-readable label prepended to each error message.
	 */
	PackageInfoErrorListener(const char* context)
		:
		fContext(context)
	{
	}

	/**
	 * @brief Append a formatted parse-error message to the error accumulator.
	 *
	 * @param message  The error description from the parser.
	 * @param line     Line number where the error occurred.
	 * @param column   Column number where the error occurred.
	 */
	virtual void OnError(const BString& message, int line, int column)
	{
		fErrors << BString().SetToFormat("%s: parse error in line %d:%d: %s\n",
			fContext, line, column, message.String());
	}

	/**
	 * @brief Return all accumulated error messages.
	 *
	 * @return Reference to the error string.
	 */
	const BString& Errors() const
	{
		return fErrors;
	}

private:
	const char*	fContext;
	BString		fErrors;
};


} // unnamed namespace


/**
 * @brief Construct a BRepositoryBuilder for an existing BSolverRepository.
 *
 * The repository is assumed to be already initialised; no SetTo() is called.
 *
 * @param repository  The solver repository to wrap.
 */
BRepositoryBuilder::BRepositoryBuilder(BSolverRepository& repository)
	:
	fRepository(repository),
	fErrorName(repository.Name()),
	fPackagePaths(NULL)
{
}


/**
 * @brief Construct a BRepositoryBuilder and initialise the repository by name.
 *
 * Calls fRepository.SetTo(name); aborts via DIE() on failure.
 *
 * @param repository  The solver repository to initialise and wrap.
 * @param name        The name to assign to the repository.
 * @param errorName   Human-readable label for error messages; defaults to @a name.
 */
BRepositoryBuilder::BRepositoryBuilder(BSolverRepository& repository,
	const BString& name, const BString& errorName)
	:
	fRepository(repository),
	fErrorName(errorName.IsEmpty() ? name : errorName),
	fPackagePaths(NULL)
{
	status_t error = fRepository.SetTo(name);
	if (error != B_OK)
		DIE(error, "failed to init %s repository", fErrorName.String());
}


/**
 * @brief Construct a BRepositoryBuilder and initialise the repository from a config.
 *
 * Calls fRepository.SetTo(config); aborts via DIE() on failure.
 *
 * @param repository  The solver repository to initialise and wrap.
 * @param config      The BRepositoryConfig describing the repository.
 */
BRepositoryBuilder::BRepositoryBuilder(BSolverRepository& repository,
	const BRepositoryConfig& config)
	:
	fRepository(repository),
	fErrorName(fRepository.Name()),
	fPackagePaths(NULL)
{
	status_t error = fRepository.SetTo(config);
	if (error != B_OK)
		DIE(error, "failed to init %s repository", fErrorName.String());
}


/**
 * @brief Construct a BRepositoryBuilder and initialise the repository from a cache.
 *
 * Calls fRepository.SetTo(cache); aborts via DIE() on failure.
 *
 * @param repository  The solver repository to initialise and wrap.
 * @param cache       The BRepositoryCache to load packages from.
 * @param errorName   Human-readable label for error messages; defaults to the cache name.
 */
BRepositoryBuilder::BRepositoryBuilder(BSolverRepository& repository,
	const BRepositoryCache& cache, const BString& errorName)
	:
	fRepository(repository),
	fErrorName(errorName.IsEmpty() ? cache.Info().Name() : errorName),
	fPackagePaths(NULL)
{
	status_t error = fRepository.SetTo(cache);
	if (error != B_OK)
		DIE(error, "failed to init %s repository", fErrorName.String());
	fErrorName = fRepository.Name();
}


/**
 * @brief Attach a package-path map for recording the on-disk location of added packages.
 *
 * @param packagePaths  Map to populate; NULL disables path recording.
 * @return Reference to this builder for chaining.
 */
BRepositoryBuilder&
BRepositoryBuilder::SetPackagePathMap(BPackagePathMap* packagePaths)
{
	fPackagePaths = packagePaths;
	return *this;
}


/**
 * @brief Add a package described by a BPackageInfo to the repository.
 *
 * Aborts via DIE() if the addition fails.
 *
 * @param info              The package metadata to add.
 * @param packageErrorName  Optional human-readable label for error messages.
 * @param _package          Optional output pointer set to the new BSolverPackage.
 * @return Reference to this builder for chaining.
 */
BRepositoryBuilder&
BRepositoryBuilder::AddPackage(const BPackageInfo& info,
	const char* packageErrorName, BSolverPackage** _package)
{
	status_t error = fRepository.AddPackage(info, _package);
	if (error != B_OK) {
		DIE(error, "failed to add %s to %s repository",
			packageErrorName != NULL
				? packageErrorName
				: (BString("package ") << info.Name()).String(),
			fErrorName.String());
	}
	return *this;
}


/**
 * @brief Add a package by reading its metadata from an .hpkg or .PackageInfo file.
 *
 * Aborts via DIE_DETAILS() if the file does not exist, cannot be stat'd,
 * is empty, or cannot be parsed.
 *
 * @param path      Path to the .hpkg or .PackageInfo file.
 * @param _package  Optional output pointer set to the new BSolverPackage.
 * @return Reference to this builder for chaining.
 */
BRepositoryBuilder&
BRepositoryBuilder::AddPackage(const char* path, BSolverPackage** _package)
{
	// read a package info from the (HPKG or package info) file
	BPackageInfo packageInfo;

	size_t pathLength = strlen(path);
	status_t error;
	PackageInfoErrorListener errorListener(path);
	BEntry entry(path, true);

	if (!entry.Exists()) {
		DIE_DETAILS(errorListener.Errors(), B_ENTRY_NOT_FOUND,
			"the package data file does not exist at \"%s\"", path);
	}

	struct stat entryStat;
	error = entry.GetStat(&entryStat);

	if (error != B_OK) {
		DIE_DETAILS(errorListener.Errors(), error,
			"failed to access the package data file at \"%s\"", path);
	}

	if (entryStat.st_size == 0) {
		DIE_DETAILS(errorListener.Errors(), B_BAD_DATA,
			"empty package data file at \"%s\"", path);
	}

	if (pathLength > 5 && strcmp(path + pathLength - 5, ".hpkg") == 0) {
		// a package file
		error = packageInfo.ReadFromPackageFile(path);
	} else {
		// a package info file (supposedly)
		error = packageInfo.ReadFromConfigFile(entry, &errorListener);
	}

	if (error != B_OK) {
		DIE_DETAILS(errorListener.Errors(), error,
			"failed to read package data file at \"%s\"", path);
	}

	// add the package
	BSolverPackage* package;
	AddPackage(packageInfo, path, &package);

	// enter the package path in the path map, if given
	if (fPackagePaths != NULL)
		(*fPackagePaths)[package] = path;

	if (_package != NULL)
		*_package = package;

	return *this;
}


/**
 * @brief Add all packages at the given installation location to the repository.
 *
 * Aborts via DIE() if the packages cannot be loaded.
 *
 * @param location          The BPackageInstallationLocation to load from.
 * @param locationErrorName Human-readable label for error messages.
 * @return Reference to this builder for chaining.
 */
BRepositoryBuilder&
BRepositoryBuilder::AddPackages(BPackageInstallationLocation location,
	const char* locationErrorName)
{
	status_t error = fRepository.AddPackages(location);
	if (error != B_OK) {
		DIE(error, "failed to add %s packages to %s repository",
			locationErrorName, fErrorName.String());
	}
	return *this;
}


/**
 * @brief Scan a directory and add every regular file as a package.
 *
 * Non-regular-file entries (directories, symlinks, etc.) are silently skipped.
 * Aborts via DIE() if the directory cannot be opened.
 *
 * @param path  Path to the directory to scan.
 * @return Reference to this builder for chaining.
 */
BRepositoryBuilder&
BRepositoryBuilder::AddPackagesDirectory(const char* path)
{
	// open directory
	DirCloser dir(opendir(path));
	if (!dir.IsSet())
		DIE(errno, "failed to open package directory \"%s\"", path);

	// iterate through directory entries
	while (dirent* entry = readdir(dir.Get())) {
		// skip "." and ".."
		const char* name = entry->d_name;
		if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
			continue;

		// stat() the entry and skip any non-file
		BPath entryPath;
		status_t error = entryPath.SetTo(path, name);
		if (error != B_OK)
			DIE(errno, "failed to construct path");

		struct stat st;
		if (stat(entryPath.Path(), &st) != 0)
			DIE(errno, "failed to stat() %s", entryPath.Path());

		if (!S_ISREG(st.st_mode))
			continue;

		AddPackage(entryPath.Path());
	}

	return *this;
}


/**
 * @brief Mark the repository as installed/not-installed and add it to the solver.
 *
 * Aborts via DIE() if the solver rejects the repository.
 *
 * @param solver       The BSolver instance to add this repository to.
 * @param isInstalled  True if this repository represents currently-active packages.
 * @return Reference to this builder for chaining.
 */
BRepositoryBuilder&
BRepositoryBuilder::AddToSolver(BSolver* solver, bool isInstalled)
{
	fRepository.SetInstalled(isInstalled);

	status_t error = solver->AddRepository(&fRepository);
	if (error != B_OK) {
		DIE(error, "failed to add %s repository to solver",
			fErrorName.String());
	}
	return *this;
}


}	// namespace BPrivate

}	// namespace BManager

}	// namespace BPackageKit
