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
 * @file PathFinder.cpp
 * @brief Package-kit portion of BPathFinder for resolving paths via installed packages.
 *
 * This file provides the package-kit-specific implementation of BPathFinder.
 * It uses the dependency-resolution solver to locate the package that satisfies
 * a given BResolvableExpression and then constructs a path rooted at that
 * package's symlink inside the system package-links directory. The remainder
 * of BPathFinder is implemented in the storage kit.
 *
 * @see BPackageManager, BSolver, BResolvableExpression
 */


#include <PathFinder.h>

#include <package/PackageResolvableExpression.h>
#include <package/solver/SolverPackage.h>

#include <directories.h>
#include <package/manager/PackageManager.h>


// NOTE: This is only the package kit specific part of BPathFinder. Everything
// else is implemented in the storage kit.


using namespace BPackageKit;
using namespace BPackageKit::BPrivate;
using namespace BPackageKit::BManager::BPrivate;


/**
 * @brief Locate the newest installed package that satisfies the given expression.
 *
 * Initialises a BPackageManager for the home installation location, runs the
 * solver over all installed repositories, and selects the highest-versioned
 * package whose info matches @a expression.
 *
 * @param expression             The resolvable expression to satisfy.
 * @param _versionedPackageName  Output string set to "name-version" of the
 *                               matching package on success.
 * @return B_OK on success, B_BAD_VALUE if the expression is invalid,
 *         B_ENTRY_NOT_FOUND if no matching package exists, or B_NO_MEMORY.
 */
static status_t
find_package(const BPackageResolvableExpression& expression,
	BString& _versionedPackageName)
{
	if (expression.InitCheck() != B_OK)
		return B_BAD_VALUE;

	// create the package manager -- we only want to use its solver
	BPackageManager::ClientInstallationInterface installationInterface;
	BPackageManager::UserInteractionHandler userInteractionHandler;
	BPackageManager packageManager(B_PACKAGE_INSTALLATION_LOCATION_HOME,
		&installationInterface, &userInteractionHandler);
	packageManager.Init(BPackageManager::B_ADD_INSTALLED_REPOSITORIES);

	// search
	BObjectList<BSolverPackage> packages;
	status_t error = packageManager.Solver()->FindPackages(expression.Name(),
		BSolver::B_FIND_IN_NAME | BSolver::B_FIND_IN_PROVIDES, packages);
	if (error != B_OK)
		return B_ENTRY_NOT_FOUND;

	// find the newest matching package
	BSolverPackage* foundPackage = NULL;
	for (int32 i = 0; BSolverPackage* package = packages.ItemAt(i); i++) {
		if (package->Info().Matches(expression)
			&& (foundPackage == NULL
				|| package->Info().Version().Compare(
					foundPackage->Info().Version()) > 0)) {
			foundPackage = package;
		}
	}

	if (foundPackage == NULL)
		return B_ENTRY_NOT_FOUND;

	BString version = foundPackage->Info().Version().ToString();
	_versionedPackageName = foundPackage->VersionedName();
	return _versionedPackageName.IsEmpty() ? B_NO_MEMORY : B_OK;
}


/**
 * @brief Construct a BPathFinder rooted at the package satisfying @a expression.
 *
 * @param expression  The resolvable expression identifying the required package.
 * @param dependency  Optional dependency name hint passed to the base class.
 */
BPathFinder::BPathFinder(const BResolvableExpression& expression,
	const char* dependency)
{
	SetTo(expression, dependency);
}


/**
 * @brief Reinitialise the path finder for a new resolvable expression.
 *
 * Resolves the package that satisfies @a expression and sets the root path to
 * the corresponding entry inside the system package-links directory.
 *
 * @param expression  The resolvable expression to satisfy.
 * @param dependency  Optional dependency name hint forwarded to the base class.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no matching package is installed,
 *         or another error code on failure.
 */
status_t
BPathFinder::SetTo(const BResolvableExpression& expression,
	const char* dependency)
{
	BString versionedPackageName;
	fInitStatus = find_package(expression, versionedPackageName);
	if (fInitStatus != B_OK)
		return fInitStatus;

	BString packageLinksPath;
	packageLinksPath.SetToFormat(kSystemPackageLinksDirectory "/%s/.self",
		versionedPackageName.String());
	if (packageLinksPath.IsEmpty())
		return fInitStatus = B_NO_MEMORY;

	struct stat st;
	if (lstat(packageLinksPath, &st) < 0)
		return fInitStatus = B_ENTRY_NOT_FOUND;

	return _SetTo(NULL, packageLinksPath, dependency);
}
