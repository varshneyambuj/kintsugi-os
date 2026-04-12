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
 *       Rene Gollent <rene@gollent.com>
 */


/**
 * @file PackageManager.cpp
 * @brief Core implementation of BPackageManager, the high-level package lifecycle engine.
 *
 * BPackageManager orchestrates install, uninstall, update, full-sync, and
 * verify operations by building a dependency-resolution job through BSolver,
 * downloading required packages, and committing the resulting activation
 * transaction to the package daemon. It owns a set of installed and remote
 * BSolverRepository objects, a local package repository for files supplied
 * directly by the caller, and the transaction list that tracks in-progress
 * changes.
 *
 * @see BSolver, BRepositoryBuilder, BRequest
 */


#include <package/manager/PackageManager.h>

#include <glob.h>

#include <Catalog.h>
#include <Directory.h>
#include <package/CommitTransactionResult.h>
#include <package/DownloadFileRequest.h>
#include <package/PackageRoster.h>
#include <package/RefreshRepositoryRequest.h>
#include <package/RepositoryCache.h>
#include <package/solver/SolverPackage.h>
#include <package/solver/SolverPackageSpecifier.h>
#include <package/solver/SolverPackageSpecifierList.h>
#include <package/solver/SolverProblem.h>
#include <package/solver/SolverProblemSolution.h>
#include <package/solver/SolverResult.h>

#include <CopyEngine.h>
#include <package/ActivationTransaction.h>
#include <package/DaemonClient.h>
#include <package/manager/RepositoryBuilder.h>

#include "FetchFileJob.h"
#include "FetchUtils.h"
#include "PackageManagerUtils.h"
#include "ValidateChecksumJob.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PackageManagerKit"


using BPackageKit::BPrivate::FetchUtils;
using BPackageKit::BPrivate::FetchFileJob;
using BPackageKit::BPrivate::ValidateChecksumJob;


namespace BPackageKit {

namespace BManager {

namespace BPrivate {


// #pragma mark - BPackageManager


/**
 * @brief Construct a BPackageManager for the given installation location.
 *
 * Allocates the system, home, and local solver repositories. The solver is
 * created lazily by the first call to Init().
 *
 * @param location               The target installation location.
 * @param installationInterface  Interface to the package-activation backend.
 * @param userInteractionHandler Handler for progress callbacks and user prompts.
 */
BPackageManager::BPackageManager(BPackageInstallationLocation location,
	InstallationInterface* installationInterface,
	UserInteractionHandler* userInteractionHandler)
	:
	fDebugLevel(0),
	fLocation(location),
	fSolver(NULL),
	fSystemRepository(new (std::nothrow) InstalledRepository("system",
		B_PACKAGE_INSTALLATION_LOCATION_SYSTEM, -1)),
	fHomeRepository(new (std::nothrow) InstalledRepository("home",
		B_PACKAGE_INSTALLATION_LOCATION_HOME, -3)),
	fInstalledRepositories(10),
	fOtherRepositories(10),
	fLocalRepository(new (std::nothrow) MiscLocalRepository),
	fTransactions(5),
	fInstallationInterface(installationInterface),
	fUserInteractionHandler(userInteractionHandler)
{
}


/**
 * @brief Destroy the package manager, releasing the solver and all repositories.
 */
BPackageManager::~BPackageManager()
{
	delete fSolver;
	delete fSystemRepository;
	delete fHomeRepository;
	delete fLocalRepository;
}


/**
 * @brief Initialise the solver and populate it with the requested set of repositories.
 *
 * Creates the BSolver instance, optionally adds installed repositories and/or
 * remote repositories (refreshing them when requested). Calling Init() when
 * the solver already exists is a no-op.
 *
 * @param flags  Bitmask of B_ADD_INSTALLED_REPOSITORIES, B_ADD_REMOTE_REPOSITORIES,
 *               and B_REFRESH_REPOSITORIES.
 */
void
BPackageManager::Init(uint32 flags)
{
	if (fSolver != NULL)
		return;

	// create the solver
	status_t error = BSolver::Create(fSolver);
	if (error != B_OK)
		DIE(error, "Failed to create solver");

	if (fSystemRepository == NULL || fHomeRepository == NULL
		|| fLocalRepository == NULL) {
		throw std::bad_alloc();
	}

	fSolver->SetDebugLevel(fDebugLevel);

	BRepositoryBuilder(*fLocalRepository).AddToSolver(fSolver, false);

	// add installation location repositories
	if ((flags & B_ADD_INSTALLED_REPOSITORIES) != 0) {
		// We add only the repository of our actual installation location as the
		// "installed" repository. The repositories for the more general
		// installation locations are added as regular repositories, but with
		// better priorities than the actual (remote) repositories. This
		// prevents the solver from showing conflicts when a package in a more
		// specific installation location overrides a package in a more general
		// one. Instead any requirement that is already installed in a more
		// general installation location will turn up as to be installed as
		// well. But we can easily filter those out.
		_AddInstalledRepository(fSystemRepository);

		if (!fSystemRepository->IsInstalled()) {
			// Only add the home repository if the directory exists
			BPath path;
			status_t error = find_directory(B_USER_PACKAGES_DIRECTORY, &path);
			if (error == B_OK && BEntry(path.Path()).Exists())
				_AddInstalledRepository(fHomeRepository);
		}
	}

	// add other repositories
	if ((flags & B_ADD_REMOTE_REPOSITORIES) != 0) {
		BPackageRoster roster;
		BStringList repositoryNames;
		error = roster.GetRepositoryNames(repositoryNames);
		if (error != B_OK) {
			fUserInteractionHandler->Warn(error,
				B_TRANSLATE("Failed to get repository names"));
		}

		int32 repositoryNameCount = repositoryNames.CountStrings();
		for (int32 i = 0; i < repositoryNameCount; i++) {
			_AddRemoteRepository(roster, repositoryNames.StringAt(i),
				(flags & B_REFRESH_REPOSITORIES) != 0);
		}
	}
}


/**
 * @brief Set the libsolv debug verbosity level.
 *
 * @param level  Debug level; 0 disables debug output.
 */
void
BPackageManager::SetDebugLevel(int32 level)
{
	fDebugLevel = level;

	if (fSolver != NULL)
		fSolver->SetDebugLevel(fDebugLevel);
}


/**
 * @brief Install packages given as an array of name/specifier strings.
 *
 * Converts the strings to a BSolverPackageSpecifierList and delegates to
 * Install(const BSolverPackageSpecifierList&, bool).
 *
 * @param packages      Array of package name strings.
 * @param packageCount  Number of entries in @a packages.
 * @param refresh       If true, remote repositories are refreshed first.
 */
void
BPackageManager::Install(const char* const* packages, int packageCount, bool refresh)
{
	BSolverPackageSpecifierList packagesToInstall;
	_AddPackageSpecifiers(packages, packageCount, packagesToInstall);
	Install(packagesToInstall, refresh);
}


/**
 * @brief Install packages given as a solver specifier list.
 *
 * Initialises the solver, resolves dependencies, handles any problems
 * interactively, confirms changes with the user, then applies them.
 *
 * @param packages  The list of packages to install.
 * @param refresh   If true, remote repositories are refreshed first.
 */
void
BPackageManager::Install(const BSolverPackageSpecifierList& packages, bool refresh)
{
	uint32 flags = B_ADD_INSTALLED_REPOSITORIES | B_ADD_REMOTE_REPOSITORIES;
	if (refresh)
		flags |= B_REFRESH_REPOSITORIES;

	Init(flags);

	// solve
	const BSolverPackageSpecifier* unmatchedSpecifier;
	status_t error = fSolver->Install(packages, &unmatchedSpecifier);
	if (error != B_OK) {
		if (unmatchedSpecifier != NULL) {
			DIE(error, "Failed to find a match for \"%s\"",
				unmatchedSpecifier->SelectString().String());
		} else
			DIE(error, "Failed to compute packages to install");
	}

	_HandleProblems();

	// install/uninstall packages
	_AnalyzeResult();
	_ConfirmChanges();
	_ApplyPackageChanges();
}


/**
 * @brief Uninstall packages given as an array of name strings.
 *
 * @param packages      Array of package name strings.
 * @param packageCount  Number of entries in @a packages.
 */
void
BPackageManager::Uninstall(const char* const* packages, int packageCount)
{
	BSolverPackageSpecifierList packagesToUninstall;
	if (!packagesToUninstall.AppendSpecifiers(packages, packageCount))
		throw std::bad_alloc();
	Uninstall(packagesToUninstall);
}


/**
 * @brief Uninstall packages given as a solver specifier list.
 *
 * Resolves the inverse base-package closure, removes the packages from the
 * solver's installed repository, verifies installation consistency, and
 * applies the changes across all installation locations from most specific
 * to least specific.
 *
 * @param packages  The list of packages to uninstall.
 */
void
BPackageManager::Uninstall(const BSolverPackageSpecifierList& packages)
{
	Init(B_ADD_INSTALLED_REPOSITORIES);

	// find the packages that match the specification
	const BSolverPackageSpecifier* unmatchedSpecifier;
	PackageList foundPackages;
	status_t error = fSolver->FindPackages(packages,
		BSolver::B_FIND_INSTALLED_ONLY, foundPackages, &unmatchedSpecifier);
	if (error != B_OK) {
		if (unmatchedSpecifier != NULL) {
			DIE(error, "Failed to find a match for \"%s\"",
				unmatchedSpecifier->SelectString().String());
		} else
			DIE(error, "Failed to compute packages to uninstall");
	}

	// determine the inverse base package closure for the found packages
// TODO: Optimize!
	InstalledRepository& installationRepository = InstallationRepository();
	bool foundAnotherPackage;
	do {
		foundAnotherPackage = false;
		int32 count = installationRepository.CountPackages();
		for (int32 i = 0; i < count; i++) {
			BSolverPackage* package = installationRepository.PackageAt(i);
			if (foundPackages.HasItem(package))
				continue;

			if (_FindBasePackage(foundPackages, package->Info()) >= 0) {
				foundPackages.AddItem(package);
				foundAnotherPackage = true;
			}
		}
	} while (foundAnotherPackage);

	// remove the packages from the repository
	for (int32 i = 0; BSolverPackage* package = foundPackages.ItemAt(i); i++)
		installationRepository.DisablePackage(package);

	for (;;) {
		error = fSolver->VerifyInstallation(BSolver::B_VERIFY_ALLOW_UNINSTALL);
		if (error != B_OK)
			DIE(error, "Failed to compute packages to uninstall");

		_HandleProblems();

		// (virtually) apply the result to this repository
		_AnalyzeResult();

		for (int32 i = foundPackages.CountItems() - 1; i >= 0; i--) {
			if (!installationRepository.PackagesToDeactivate()
					.AddItem(foundPackages.ItemAt(i))) {
				throw std::bad_alloc();
			}
		}

		installationRepository.ApplyChanges();

		// verify the next specific respository
		if (!_NextSpecificInstallationLocation())
			break;

		foundPackages.MakeEmpty();

		// NOTE: In theory, after verifying a more specific location, it would
		// be more correct to compute the inverse base package closure for the
		// packages we need to uninstall and (if anything changed) verify again.
		// In practice, however, base packages are always required with an exact
		// version (ATM). If that base package still exist in a more general
		// location (the only reason why the package requiring the base package
		// wouldn't be marked to be uninstalled as well) there shouldn't have
		// been any reason to remove it from the more specific location in the
		// first place.
	}

	_ConfirmChanges(true);
	_ApplyPackageChanges(true);
}


/**
 * @brief Update packages given as an array of name strings.
 *
 * @param packages      Array of package name strings.
 * @param packageCount  Number of entries in @a packages.
 */
void
BPackageManager::Update(const char* const* packages, int packageCount)
{
	BSolverPackageSpecifierList packagesToUpdate;
	_AddPackageSpecifiers(packages, packageCount, packagesToUpdate);
	Update(packagesToUpdate);
}


/**
 * @brief Update packages given as a solver specifier list.
 *
 * Initialises the solver with remote repositories (refreshing them), runs the
 * update resolution, and applies the resulting changes.
 *
 * @param packages  The list of packages to update.
 */
void
BPackageManager::Update(const BSolverPackageSpecifierList& packages)
{
	Init(B_ADD_INSTALLED_REPOSITORIES | B_ADD_REMOTE_REPOSITORIES
		| B_REFRESH_REPOSITORIES);

	// solve
	const BSolverPackageSpecifier* unmatchedSpecifier;
	status_t error = fSolver->Update(packages, true,
		&unmatchedSpecifier);
	if (error != B_OK) {
		if (unmatchedSpecifier != NULL) {
			DIE(error, "Failed to find a match for \"%s\"",
				unmatchedSpecifier->SelectString().String());
		} else
			DIE(error, "Failed to compute packages to update");
	}

	_HandleProblems();

	// install/uninstall packages
	_AnalyzeResult();
	_ConfirmChanges();
	_ApplyPackageChanges();
}


/**
 * @brief Perform a full distribution-upgrade sync against all remote repositories.
 *
 * Refreshes all remote repositories, runs a DISTUPGRADE solve, and applies changes.
 */
void
BPackageManager::FullSync()
{
	Init(B_ADD_INSTALLED_REPOSITORIES | B_ADD_REMOTE_REPOSITORIES
		| B_REFRESH_REPOSITORIES);

	// solve
	status_t error = fSolver->FullSync();
	if (error != B_OK)
		DIE(error, "Failed to compute packages to synchronize");

	_HandleProblems();

	// install/uninstall packages
	_AnalyzeResult();
	_ConfirmChanges();
	_ApplyPackageChanges();
}


/**
 * @brief Verify that the current installation is self-consistent.
 *
 * Refreshes remote repositories, iterates over all installation locations
 * from most to least specific, verifying and applying corrective changes
 * at each level.
 */
void
BPackageManager::VerifyInstallation()
{
	Init(B_ADD_INSTALLED_REPOSITORIES | B_ADD_REMOTE_REPOSITORIES
		| B_REFRESH_REPOSITORIES);

	for (;;) {
		status_t error = fSolver->VerifyInstallation();
		if (error != B_OK)
			DIE(error, "Failed to compute package dependencies");

		_HandleProblems();

		// (virtually) apply the result to this repository
		_AnalyzeResult();
		InstallationRepository().ApplyChanges();

		// verify the next specific respository
		if (!_NextSpecificInstallationLocation())
			break;
	}

	_ConfirmChanges();
	_ApplyPackageChanges();
}


/**
 * @brief Return the most-specific installed repository managed by this instance.
 *
 * @return Reference to the last InstalledRepository in fInstalledRepositories.
 */
BPackageManager::InstalledRepository&
BPackageManager::InstallationRepository()
{
	if (fInstalledRepositories.IsEmpty())
		DIE("No installation repository");

	return *fInstalledRepositories.LastItem();
}


/**
 * @brief Job-started callback; notifies the UI layer about download or checksum starts.
 *
 * @param job  The job that just started.
 */
void
BPackageManager::JobStarted(BSupportKit::BJob* job)
{
	if (dynamic_cast<FetchFileJob*>(job) != NULL) {
		FetchFileJob* fetchJob = (FetchFileJob*)job;
		fUserInteractionHandler->ProgressPackageDownloadStarted(
			fetchJob->DownloadFileName());
	} else if (dynamic_cast<ValidateChecksumJob*>(job) != NULL) {
		fUserInteractionHandler->ProgressPackageChecksumStarted(
			job->Title().String());
	}
}


/**
 * @brief Job-progress callback; forwards download progress to the UI layer.
 *
 * @param job  The job that reported progress.
 */
void
BPackageManager::JobProgress(BSupportKit::BJob* job)
{
	if (dynamic_cast<FetchFileJob*>(job) != NULL) {
		FetchFileJob* fetchJob = (FetchFileJob*)job;
		fUserInteractionHandler->ProgressPackageDownloadActive(
			fetchJob->DownloadFileName(), fetchJob->DownloadProgress(),
			fetchJob->DownloadBytes(), fetchJob->DownloadTotalBytes());
	}
}


/**
 * @brief Job-succeeded callback; notifies the UI layer about download or checksum completion.
 *
 * @param job  The job that just completed successfully.
 */
void
BPackageManager::JobSucceeded(BSupportKit::BJob* job)
{
	if (dynamic_cast<FetchFileJob*>(job) != NULL) {
		FetchFileJob* fetchJob = (FetchFileJob*)job;
		fUserInteractionHandler->ProgressPackageDownloadComplete(
			fetchJob->DownloadFileName());
	} else if (dynamic_cast<ValidateChecksumJob*>(job) != NULL) {
		fUserInteractionHandler->ProgressPackageChecksumComplete(
			job->Title().String());
	}
}


/**
 * @brief Loop until the solver has no more unresolved problems.
 *
 * Delegates to the user-interaction handler which may apply user-selected
 * solutions before calling SolveAgain().
 */
void
BPackageManager::_HandleProblems()
{
	while (fSolver->HasProblems()) {
		fUserInteractionHandler->HandleProblems();

		status_t error = fSolver->SolveAgain();
		if (error != B_OK)
			DIE(error, "Failed to recompute packages to un/-install");
	}
}


/**
 * @brief Translate the solver result into activate/deactivate package lists.
 *
 * Also ensures that base packages are placed in the same installation
 * location as the packages that require them.
 */
void
BPackageManager::_AnalyzeResult()
{
	BSolverResult result;
	status_t error = fSolver->GetResult(result);
	if (error != B_OK)
		DIE(error, "Failed to compute packages to un/-install");

	InstalledRepository& installationRepository = InstallationRepository();
	PackageList& packagesToActivate
		= installationRepository.PackagesToActivate();
	PackageList& packagesToDeactivate
		= installationRepository.PackagesToDeactivate();

	PackageList potentialBasePackages;

	for (int32 i = 0; const BSolverResultElement* element = result.ElementAt(i);
			i++) {
		BSolverPackage* package = element->Package();

		switch (element->Type()) {
			case BSolverResultElement::B_TYPE_INSTALL:
			{
				PackageList& packageList
					= dynamic_cast<InstalledRepository*>(package->Repository())
							!= NULL
						? potentialBasePackages
						: packagesToActivate;
				if (!packageList.AddItem(package))
					throw std::bad_alloc();
				break;
			}

			case BSolverResultElement::B_TYPE_UNINSTALL:
				if (!packagesToDeactivate.AddItem(package))
					throw std::bad_alloc();
				break;
		}
	}

	// Make sure base packages are installed in the same location.
	for (int32 i = 0; i < packagesToActivate.CountItems(); i++) {
		BSolverPackage* package = packagesToActivate.ItemAt(i);
		int32 index = _FindBasePackage(potentialBasePackages, package->Info());
		if (index < 0)
			continue;

		BSolverPackage* basePackage = potentialBasePackages.RemoveItemAt(index);
		if (!packagesToActivate.AddItem(basePackage))
			throw std::bad_alloc();
	}

	fInstallationInterface->ResultComputed(installationRepository);
}


/**
 * @brief Ask the user to confirm the computed package changes.
 *
 * Throws BNothingToDoException when there are no changes at all. Delegates
 * the actual confirmation prompt to the UserInteractionHandler.
 *
 * @param fromMostSpecific  If true, changes are presented from the most
 *                          specific location outward.
 */
void
BPackageManager::_ConfirmChanges(bool fromMostSpecific)
{
	// check, if there are any changes at all
	int32 count = fInstalledRepositories.CountItems();
	bool hasChanges = false;
	for (int32 i = 0; i < count; i++) {
		if (fInstalledRepositories.ItemAt(i)->HasChanges()) {
			hasChanges = true;
			break;
		}
	}

	if (!hasChanges)
		throw BNothingToDoException();

	fUserInteractionHandler->ConfirmChanges(fromMostSpecific);
}


/**
 * @brief Download, verify, and commit all queued package changes.
 *
 * Prepares and commits one transaction per InstalledRepository that has
 * pending changes. When @a fromMostSpecific is true the most specific
 * location is processed first.
 *
 * @param fromMostSpecific  Process locations from most to least specific when true.
 */
void
BPackageManager::_ApplyPackageChanges(bool fromMostSpecific)
{
	int32 count = fInstalledRepositories.CountItems();
	if (fromMostSpecific) {
		for (int32 i = count - 1; i >= 0; i--)
			_PreparePackageChanges(*fInstalledRepositories.ItemAt(i));
	} else {
		for (int32 i = 0; i < count; i++)
			_PreparePackageChanges(*fInstalledRepositories.ItemAt(i));
	}

	for (int32 i = 0; Transaction* transaction = fTransactions.ItemAt(i); i++)
		_CommitPackageChanges(*transaction);

// TODO: Clean up the transaction directories on error!
}


/**
 * @brief Download packages to activate and build the activation transaction.
 *
 * Creates a transaction directory, downloads each package to activate
 * (reusing previous partial downloads when available), and populates
 * the ActivationTransaction with the names of files to activate/deactivate.
 *
 * @param installationRepository  The repository whose changes are being prepared.
 */
void
BPackageManager::_PreparePackageChanges(
	InstalledRepository& installationRepository)
{
	if (!installationRepository.HasChanges())
		return;

	PackageList& packagesToActivate
		= installationRepository.PackagesToActivate();
	PackageList& packagesToDeactivate
		= installationRepository.PackagesToDeactivate();

	// create the transaction
	Transaction* transaction = new Transaction(installationRepository);
	if (!fTransactions.AddItem(transaction)) {
		delete transaction;
		throw std::bad_alloc();
	}

	status_t error = fInstallationInterface->PrepareTransaction(*transaction);
	if (error != B_OK)
		DIE(error, "Failed to create transaction");

	// download the new packages and prepare the transaction
	for (int32 i = 0; BSolverPackage* package = packagesToActivate.ItemAt(i);
		i++) {
		// get package URL and target entry

		BString fileName(package->Info().FileName());
		if (fileName.IsEmpty())
			throw std::bad_alloc();

		BEntry entry;
		error = entry.SetTo(&transaction->TransactionDirectory(), fileName);
		if (error != B_OK)
			DIE(error, "Failed to create package entry");

		RemoteRepository* remoteRepository
			= dynamic_cast<RemoteRepository*>(package->Repository());
		if (remoteRepository != NULL) {
			bool reusingDownload = false;

			// Check for matching files in already existing transaction
			// directories
			BPath path(&transaction->TransactionDirectory());
			BPath parent;
			if (path.GetParent(&parent) == B_OK) {
				BString globPath = parent.Path();
				globPath << "/*/" << fileName;
				glob_t globbuf;
				if (glob(globPath.String(), GLOB_NOSORT, NULL, &globbuf) == 0) {
					off_t bestSize = 0;
					const char* bestFile = NULL;

					// If there are multiple matching files, pick the largest
					// one (the others are most likely partial downloads)
					for (size_t i = 0; i < globbuf.gl_pathc; i++) {
						off_t size = 0;
						BNode node(globbuf.gl_pathv[i]);
						if (node.GetSize(&size) == B_OK && size > bestSize) {
							bestSize = size;
							bestFile = globbuf.gl_pathv[i];
						}
					}

					// Copy the selected file into our own transaction directory
					path.Append(fileName);
					if (bestFile != NULL && BCopyEngine().CopyEntry(bestFile,
						path.Path()) == B_OK) {
						reusingDownload = true;
						printf("Re-using download '%s' from previous "
							"transaction%s\n", bestFile,
							FetchUtils::IsDownloadCompleted(
								path.Path()) ? "" : " (partial)");
					}
					globfree(&globbuf);
				}
			}

			// download the package (this will resume the download if the
			// file already exists)
			BString url = remoteRepository->Config().PackagesURL();
			url << '/' << fileName;

			status_t error;
retryDownload:
			error = DownloadPackage(url, entry,
				package->Info().Checksum());
			if (error != B_OK) {
				if (error == B_BAD_DATA || error == ERANGE) {
					// B_BAD_DATA is returned when there is a checksum
					// mismatch. Make sure this download is not re-used.
					entry.Remove();

					if (reusingDownload) {
						// Maybe the download we reused had some problem.
						// Try again, this time without reusing the download.
						printf("\nPrevious download '%s' was invalid. Redownloading.\n",
							path.Path());
						reusingDownload = false;
						goto retryDownload;
					}
				}
				DIE(error, "Failed to download package %s",
					package->Info().Name().String());
			}
		} else if (package->Repository() != &installationRepository) {
			// clone the existing package
			LocalRepository* localRepository
				= dynamic_cast<LocalRepository*>(package->Repository());
			if (localRepository == NULL) {
				DIE("Internal error: repository %s is not a local repository",
					package->Repository()->Name().String());
			}
			_ClonePackageFile(localRepository, package, entry);
		}

		// add package to transaction
		if (!transaction->ActivationTransaction().AddPackageToActivate(
				fileName)) {
			throw std::bad_alloc();
		}
	}

	for (int32 i = 0; BSolverPackage* package = packagesToDeactivate.ItemAt(i);
		i++) {
		// add package to transaction
		if (!transaction->ActivationTransaction().AddPackageToDeactivate(
				package->Info().FileName())) {
			throw std::bad_alloc();
		}
	}
}


/**
 * @brief Commit a prepared transaction to the package daemon.
 *
 * Notifies the UI layer at start, commits, reports the result, and removes
 * the transaction directory when complete.
 *
 * @param transaction  The transaction to commit.
 */
void
BPackageManager::_CommitPackageChanges(Transaction& transaction)
{
	InstalledRepository& installationRepository = transaction.Repository();

	fUserInteractionHandler->ProgressStartApplyingChanges(
		installationRepository);

	// commit the transaction
	BCommitTransactionResult transactionResult;
	status_t error = fInstallationInterface->CommitTransaction(transaction,
		transactionResult);
	if (error != B_OK)
		DIE(error, "Failed to commit transaction");
	if (transactionResult.Error() != B_TRANSACTION_OK)
		DIE(transactionResult);

	fUserInteractionHandler->ProgressTransactionCommitted(
		installationRepository, transactionResult);

	BEntry transactionDirectoryEntry;
	if ((error = transaction.TransactionDirectory()
			.GetEntry(&transactionDirectoryEntry)) != B_OK
		|| (error = transactionDirectoryEntry.Remove()) != B_OK) {
		fUserInteractionHandler->Warn(error,
			B_TRANSLATE("Failed to remove transaction directory"));
	}

	fUserInteractionHandler->ProgressApplyingChangesDone(
		installationRepository);
}


/**
 * @brief Copy a package file from a local repository to the transaction directory.
 *
 * @param repository  The local repository that owns the file.
 * @param package     The package whose file is to be copied.
 * @param entry       The destination BEntry in the transaction directory.
 */
void
BPackageManager::_ClonePackageFile(LocalRepository* repository,
	BSolverPackage* package, const BEntry& entry)
{
	// get source and destination path
	BPath sourcePath;
	repository->GetPackagePath(package, sourcePath);

	BPath destinationPath;
	status_t error = entry.GetPath(&destinationPath);
	if (error != B_OK) {
		DIE(error, "Failed to entry path of package file to install \"%s\"",
			package->Info().FileName().String());
	}

	// Copy the package. Ideally we would just hard-link it, but BFS doesn't
	// support that.
	error = BCopyEngine().CopyEntry(sourcePath.Path(), destinationPath.Path());
	if (error != B_OK)
		DIE(error, "Failed to copy package file \"%s\"", sourcePath.Path());
}


/**
 * @brief Search @a packages for the base package required by @a info.
 *
 * @param packages  The list of candidate packages to search.
 * @param info      The package info whose base-package requirement is used.
 * @return Index of the matching package in @a packages, or -1 if not found.
 */
int32
BPackageManager::_FindBasePackage(const PackageList& packages,
	const BPackageInfo& info)
{
	if (info.BasePackage().IsEmpty())
		return -1;

	// find the requirement matching the base package
	BPackageResolvableExpression* basePackage = NULL;
	int32 count = info.RequiresList().CountItems();
	for (int32 i = 0; i < count; i++) {
		BPackageResolvableExpression* require = info.RequiresList().ItemAt(i);
		if (require->Name() == info.BasePackage()) {
			basePackage = require;
			break;
		}
	}

	if (basePackage == NULL) {
		fUserInteractionHandler->Warn(B_OK, B_TRANSLATE("Package %s-%s "
			"doesn't have a matching requires for its base package \"%s\""),
			info.Name().String(), info.Version().ToString().String(),
			info.BasePackage().String());
		return -1;
	}

	// find the first package matching the base package requires
	count = packages.CountItems();
	for (int32 i = 0; i < count; i++) {
		BSolverPackage* package = packages.ItemAt(i);
		if (package->Name() == basePackage->Name()
			&& package->Info().Matches(*basePackage)) {
			return i;
		}
	}

	return -1;
}


/**
 * @brief Add an installed repository to the solver and track it.
 *
 * Initialises the repository via the installation interface, adds it to the
 * solver, and appends it to fInstalledRepositories.
 *
 * @param repository  The InstalledRepository to add.
 */
void
BPackageManager::_AddInstalledRepository(InstalledRepository* repository)
{
	fInstallationInterface->InitInstalledRepository(*repository);

	BRepositoryBuilder(*repository)
		.AddToSolver(fSolver, repository->Location() == fLocation);
	repository->SetPriority(repository->InitialPriority());

	if (!fInstalledRepositories.AddItem(repository))
		throw std::bad_alloc();
}


/**
 * @brief Load (and optionally refresh) a remote repository and add it to the solver.
 *
 * Skips the repository with a warning when its config or cache cannot be
 * obtained.
 *
 * @param roster  BPackageRoster used to retrieve the config and cache.
 * @param name    Name of the repository to load.
 * @param refresh If true, the repository cache is refreshed from the network.
 */
void
BPackageManager::_AddRemoteRepository(BPackageRoster& roster, const char* name,
	bool refresh)
{
	BRepositoryConfig config;
	status_t error = roster.GetRepositoryConfig(name, &config);
	if (error != B_OK) {
		fUserInteractionHandler->Warn(error, B_TRANSLATE(
			"Failed to get config for repository \"%s\". Skipping."), name);
		return;
	}

	BRepositoryCache cache;
	error = _GetRepositoryCache(roster, config, refresh, cache);
	if (error != B_OK) {
		fUserInteractionHandler->Warn(error, B_TRANSLATE(
			"Failed to get cache for repository \"%s\". Skipping."), name);
		return;
	}

	RemoteRepository* repository = new RemoteRepository(config);
	if (!fOtherRepositories.AddItem(repository)) {
		delete repository;
		throw std::bad_alloc();
	}

	BRepositoryBuilder(*repository, cache, config.Name())
		.AddToSolver(fSolver, false);
}


/**
 * @brief Retrieve the repository cache, refreshing it when needed or requested.
 *
 * @param roster   BPackageRoster used to look up and refresh the cache.
 * @param config   The repository configuration.
 * @param refresh  If true, always attempt a network refresh.
 * @param _cache   Output BRepositoryCache filled on success.
 * @return B_OK on success, or an error code if the cache cannot be obtained.
 */
status_t
BPackageManager::_GetRepositoryCache(BPackageRoster& roster,
	const BRepositoryConfig& config, bool refresh, BRepositoryCache& _cache)
{
	if (!refresh && roster.GetRepositoryCache(config.Name(), &_cache) == B_OK)
		return B_OK;

	status_t error = RefreshRepository(config);
	if (error != B_OK) {
		fUserInteractionHandler->Warn(error, B_TRANSLATE(
			"Refreshing repository \"%s\" failed"), config.Name().String());
	}

	return roster.GetRepositoryCache(config.Name(), &_cache);
}


/**
 * @brief Convert an array of name/path strings into package specifiers.
 *
 * Strings ending in ".hpkg" that refer to existing files are treated as
 * local package files and added to fLocalRepository; other strings are
 * treated as package-name selectors.
 *
 * @param searchStrings      Array of package names or file paths.
 * @param searchStringCount  Number of entries in @a searchStrings.
 * @param specifierList      The specifier list to populate.
 */
void
BPackageManager::_AddPackageSpecifiers(const char* const* searchStrings,
	int searchStringCount, BSolverPackageSpecifierList& specifierList)
{
	for (int i = 0; i < searchStringCount; i++) {
		const char* searchString = searchStrings[i];
		if (_IsLocalPackage(searchString)) {
			BSolverPackage* package = _AddLocalPackage(searchString);
			if (!specifierList.AppendSpecifier(package))
				throw std::bad_alloc();
		} else {
			if (!specifierList.AppendSpecifier(searchString))
				throw std::bad_alloc();
		}
	}
}


/**
 * @brief Determine whether a file name refers to a local .hpkg package file.
 *
 * @param fileName  The file name to inspect.
 * @return True if @a fileName contains ".hpkg" and the file exists as a
 *         regular file.
 */
bool
BPackageManager::_IsLocalPackage(const char* fileName)
{
	// Simple heuristic: fileName contains ".hpkg" and there's actually a file
	// it refers to.
	struct stat st;
	return strstr(fileName, ".hpkg") != NULL && stat(fileName, &st) == 0
		&& S_ISREG(st.st_mode);
}


/**
 * @brief Load a local package file into fLocalRepository and return the solver package.
 *
 * @param fileName  Path to the .hpkg file.
 * @return Pointer to the BSolverPackage added to fLocalRepository.
 */
BSolverPackage*
BPackageManager::_AddLocalPackage(const char* fileName)
{
	if (fLocalRepository == NULL)
		throw std::bad_alloc();
	return fLocalRepository->AddLocalPackage(fileName);
}


/**
 * @brief Advance the active installation location to the next more general one.
 *
 * Switches from system to home and adds the home repository to the solver.
 *
 * @return True if the location was advanced, false if already at the most
 *         general location.
 */
bool
BPackageManager::_NextSpecificInstallationLocation()
{
	try {
		if (fLocation == B_PACKAGE_INSTALLATION_LOCATION_SYSTEM) {
			fLocation = B_PACKAGE_INSTALLATION_LOCATION_HOME;
			fSystemRepository->SetInstalled(false);
			_AddInstalledRepository(fHomeRepository);
			return true;
		}
	} catch (BFatalErrorException& e) {
		// No home repo. This is acceptable for example when we are in an haikuporter chroot.
	}

	return false;
}


/**
 * @brief Download a single package file from a URL to a local entry, verifying the checksum.
 *
 * @param fileURL     The URL of the package to download.
 * @param targetEntry The local BEntry where the file will be saved.
 * @param checksum    The expected SHA-256 checksum of the file.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageManager::DownloadPackage(const BString& fileURL,
	const BEntry& targetEntry, const BString& checksum)
{
	BDecisionProvider provider;
	BContext context(provider, *this);
	return DownloadFileRequest(context, fileURL, targetEntry, checksum)
		.Process();
}


/**
 * @brief Refresh a repository cache by running a BRefreshRepositoryRequest.
 *
 * @param repoConfig  The configuration of the repository to refresh.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageManager::RefreshRepository(const BRepositoryConfig& repoConfig)
{
	BDecisionProvider provider;
	BContext context(provider, *this);
	return BRefreshRepositoryRequest(context, repoConfig).Process();
}


// #pragma mark - RemoteRepository


/**
 * @brief Construct a RemoteRepository wrapping the given config.
 *
 * @param config  The BRepositoryConfig describing the remote repository.
 */
BPackageManager::RemoteRepository::RemoteRepository(
	const BRepositoryConfig& config)
	:
	BSolverRepository(),
	fConfig(config)
{
}


/**
 * @brief Return the repository configuration for this remote repository.
 *
 * @return Reference to the BRepositoryConfig.
 */
const BRepositoryConfig&
BPackageManager::RemoteRepository::Config() const
{
	return fConfig;
}


// #pragma mark - LocalRepository


/**
 * @brief Default-construct an unnamed LocalRepository.
 */
BPackageManager::LocalRepository::LocalRepository()
	:
	BSolverRepository()
{
}


/**
 * @brief Construct a named LocalRepository.
 *
 * @param name  The display name for this repository.
 */
BPackageManager::LocalRepository::LocalRepository(const BString& name)
	:
	BSolverRepository(name)
{
}


// #pragma mark - MiscLocalRepository


/**
 * @brief Construct a MiscLocalRepository with lowest solver priority.
 *
 * The repository is named "local" and assigned priority -127 so that it
 * is only used when no remote or installed package matches.
 */
BPackageManager::MiscLocalRepository::MiscLocalRepository()
	:
	LocalRepository("local"),
	fPackagePaths()
{
	SetPriority(-127);
}


/**
 * @brief Load a local package file, add it to this repository, and record its path.
 *
 * @param fileName  Path to the .hpkg file.
 * @return Pointer to the BSolverPackage added to this repository.
 */
BSolverPackage*
BPackageManager::MiscLocalRepository::AddLocalPackage(const char* fileName)
{
	BSolverPackage* package;
	BRepositoryBuilder(*this).AddPackage(fileName, &package);

	fPackagePaths[package] = fileName;

	return package;
}


/**
 * @brief Retrieve the on-disk path of a package owned by this repository.
 *
 * @param package  The package whose path is requested.
 * @param _path    Output BPath filled with the package file path.
 */
void
BPackageManager::MiscLocalRepository::GetPackagePath(BSolverPackage* package,
	BPath& _path)
{
	PackagePathMap::const_iterator it = fPackagePaths.find(package);
	if (it == fPackagePaths.end()) {
		DIE("Package %s not in local repository",
			package->VersionedName().String());
	}

	status_t error = _path.SetTo(it->second.c_str());
	if (error != B_OK)
		DIE(error, "Failed to init package path %s", it->second.c_str());
}


// #pragma mark - InstalledRepository


/**
 * @brief Construct an InstalledRepository for a given installation location.
 *
 * @param name      Initial display name of the repository.
 * @param location  The BPackageInstallationLocation this repository represents.
 * @param priority  Initial solver priority for this repository.
 */
BPackageManager::InstalledRepository::InstalledRepository(const char* name,
	BPackageInstallationLocation location, int32 priority)
	:
	LocalRepository(),
	fDisabledPackages(10),
	fPackagesToActivate(),
	fPackagesToDeactivate(),
	fInitialName(name),
	fLocation(location),
	fInitialPriority(priority)
{
}


/**
 * @brief Resolve the on-disk path of a package in this installed repository.
 *
 * @param package  The package whose path is needed.
 * @param _path    Output BPath filled with the full package file path.
 */
void
BPackageManager::InstalledRepository::GetPackagePath(BSolverPackage* package,
	BPath& _path)
{
	directory_which packagesWhich;
	switch (fLocation) {
		case B_PACKAGE_INSTALLATION_LOCATION_SYSTEM:
			packagesWhich = B_SYSTEM_PACKAGES_DIRECTORY;
			break;
		case B_PACKAGE_INSTALLATION_LOCATION_HOME:
			packagesWhich = B_USER_PACKAGES_DIRECTORY;
			break;
		default:
			DIE("Don't know packages directory path for installation location "
				"\"%s\"", Name().String());
	}

	BString fileName(package->Info().FileName());
	status_t error = find_directory(packagesWhich, &_path);
	if (error != B_OK || (error = _path.Append(fileName)) != B_OK) {
		DIE(error, "Failed to get path of package file \"%s\" in installation "
			"location \"%s\"", fileName.String(), Name().String());
	}
}


/**
 * @brief Move a package to the disabled list and remove it from the solver.
 *
 * @param package  The package to disable; must belong to this repository.
 */
void
BPackageManager::InstalledRepository::DisablePackage(BSolverPackage* package)
{
	if (fDisabledPackages.HasItem(package))
		DIE("Package %s already disabled", package->VersionedName().String());

	if (package->Repository() != this) {
		DIE("Package %s not in repository %s",
			package->VersionedName().String(), Name().String());
	}

	// move to disabled list
	if (!fDisabledPackages.AddItem(package))
		throw std::bad_alloc();

	RemovePackage(package);
}


/**
 * @brief Remove a package from the disabled list.
 *
 * @param package  The package to re-enable.
 * @return True if the package was found and removed from the disabled list.
 */
bool
BPackageManager::InstalledRepository::EnablePackage(BSolverPackage* package)
{
	return fDisabledPackages.RemoveItem(package);
}


/**
 * @brief Check whether this repository has pending activation or deactivation changes.
 *
 * @return True if either PackagesToActivate or PackagesToDeactivate is non-empty.
 */
bool
BPackageManager::InstalledRepository::HasChanges() const
{
	return !fPackagesToActivate.IsEmpty() || !fPackagesToDeactivate.IsEmpty();
}


/**
 * @brief Apply pending changes to the repository's in-memory package list.
 *
 * Disables packages marked for deactivation and adds packages marked for
 * activation so that the solver sees the updated state.
 */
void
BPackageManager::InstalledRepository::ApplyChanges()
{
	// disable packages to deactivate
	for (int32 i = 0; BSolverPackage* package = fPackagesToDeactivate.ItemAt(i);
		i++) {
		if (!fDisabledPackages.HasItem(package))
			DisablePackage(package);
	}

	// add packages to activate
	for (int32 i = 0; BSolverPackage* package = fPackagesToActivate.ItemAt(i);
		i++) {
		status_t error = AddPackage(package->Info());
		if (error != B_OK) {
			DIE(error, "Failed to add package %s to %s repository",
				package->Name().String(), Name().String());
		}
	}
}


// #pragma mark - Transaction


/**
 * @brief Construct a Transaction for the given installed repository.
 *
 * @param repository  The repository whose packages are being transacted.
 */
BPackageManager::Transaction::Transaction(InstalledRepository& repository)
	:
	fRepository(repository),
	fTransaction(),
	fTransactionDirectory()
{
}


/**
 * @brief Destroy the transaction.
 */
BPackageManager::Transaction::~Transaction()
{
}


// #pragma mark - InstallationInterface


/**
 * @brief Destroy the InstallationInterface.
 */
BPackageManager::InstallationInterface::~InstallationInterface()
{
}


/**
 * @brief Called when the solver result has been computed for a repository.
 *
 * The default implementation is a no-op; subclasses may override.
 *
 * @param repository  The repository for which the result was computed.
 */
void
BPackageManager::InstallationInterface::ResultComputed(
	InstalledRepository& repository)
{
}


// #pragma mark - ClientInstallationInterface


/**
 * @brief Default-construct a ClientInstallationInterface.
 */
BPackageManager::ClientInstallationInterface::ClientInstallationInterface()
	:
	fDaemonClient()
{
}


/**
 * @brief Destroy the ClientInstallationInterface.
 */
BPackageManager::ClientInstallationInterface::~ClientInstallationInterface()
{
}


/**
 * @brief Populate an InstalledRepository with the currently active packages.
 *
 * @param repository  The repository to populate; its location is used to
 *                    determine which packages to load.
 */
void
BPackageManager::ClientInstallationInterface::InitInstalledRepository(
	InstalledRepository& repository)
{
	const char* name = repository.InitialName();
	BRepositoryBuilder(repository, name)
		.AddPackages(repository.Location(), name);
}


/**
 * @brief Create a daemon transaction directory for staging package changes.
 *
 * @param transaction  The transaction to prepare.
 * @return B_OK on success, or an error code if the daemon cannot create
 *         the transaction.
 */
status_t
BPackageManager::ClientInstallationInterface::PrepareTransaction(
	Transaction& transaction)
{
	return fDaemonClient.CreateTransaction(transaction.Repository().Location(),
		transaction.ActivationTransaction(),
		transaction.TransactionDirectory());
}


/**
 * @brief Commit the activation transaction to the package daemon.
 *
 * @param transaction  The transaction to commit.
 * @param _result      Output object filled with the commit result details.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BPackageManager::ClientInstallationInterface::CommitTransaction(
	Transaction& transaction, BCommitTransactionResult& _result)
{
	return fDaemonClient.CommitTransaction(transaction.ActivationTransaction(),
		_result);
}


// #pragma mark - UserInteractionHandler


/**
 * @brief Destroy the UserInteractionHandler.
 */
BPackageManager::UserInteractionHandler::~UserInteractionHandler()
{
}


/**
 * @brief Default handler for solver problems: abort by throwing BAbortedByUserException.
 */
void
BPackageManager::UserInteractionHandler::HandleProblems()
{
	throw BAbortedByUserException();
}


/**
 * @brief Default change-confirmation handler: abort by throwing BAbortedByUserException.
 *
 * @param fromMostSpecific  Unused; present for API compatibility.
 */
void
BPackageManager::UserInteractionHandler::ConfirmChanges(bool fromMostSpecific)
{
	throw BAbortedByUserException();
}


/**
 * @brief Default warning handler: silently ignores all warnings.
 *
 * @param error   The error code associated with the warning.
 * @param format  printf-style format string describing the warning.
 * @param ...     Arguments for the format string.
 */
void
BPackageManager::UserInteractionHandler::Warn(status_t error,
	const char* format, ...)
{
}


/**
 * @brief Called when a package download begins; default is a no-op.
 *
 * @param packageName  Name of the package being downloaded.
 */
void
BPackageManager::UserInteractionHandler::ProgressPackageDownloadStarted(
	const char* packageName)
{
}


/**
 * @brief Called periodically during a package download; default is a no-op.
 *
 * @param packageName          Name of the package being downloaded.
 * @param completionPercentage Fraction (0..1) of the download completed.
 * @param bytes                Bytes received so far.
 * @param totalBytes           Total expected bytes.
 */
void
BPackageManager::UserInteractionHandler::ProgressPackageDownloadActive(
	const char* packageName, float completionPercentage, off_t bytes,
	off_t totalBytes)
{
}


/**
 * @brief Called when a package download finishes; default is a no-op.
 *
 * @param packageName  Name of the package that was downloaded.
 */
void
BPackageManager::UserInteractionHandler::ProgressPackageDownloadComplete(
	const char* packageName)
{
}


/**
 * @brief Called when a checksum validation job starts; default is a no-op.
 *
 * @param title  Human-readable description of the checksum operation.
 */
void
BPackageManager::UserInteractionHandler::ProgressPackageChecksumStarted(
	const char* title)
{
}


/**
 * @brief Called when a checksum validation job finishes; default is a no-op.
 *
 * @param title  Human-readable description of the checksum operation.
 */
void
BPackageManager::UserInteractionHandler::ProgressPackageChecksumComplete(
	const char* title)
{
}


/**
 * @brief Called when the activation phase begins for a repository; default is a no-op.
 *
 * @param repository  The repository whose changes are being applied.
 */
void
BPackageManager::UserInteractionHandler::ProgressStartApplyingChanges(
	InstalledRepository& repository)
{
}


/**
 * @brief Called when a transaction has been committed; default is a no-op.
 *
 * @param repository  The repository that was modified.
 * @param result      The commit result from the package daemon.
 */
void
BPackageManager::UserInteractionHandler::ProgressTransactionCommitted(
	InstalledRepository& repository, const BCommitTransactionResult& result)
{
}


/**
 * @brief Called when the activation phase is complete for a repository; default is a no-op.
 *
 * @param repository  The repository whose changes have been applied.
 */
void
BPackageManager::UserInteractionHandler::ProgressApplyingChangesDone(
	InstalledRepository& repository)
{
}


}	// namespace BPrivate

}	// namespace BManager

}	// namespace BPackageKit
