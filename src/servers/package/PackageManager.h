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
    * Copyright 2013-2014, Ingo Weinhold, ingo_weinhold@gmx.de.
    * Distributed under the terms of the MIT License.
    */
 */

/** @file PackageManager.h
 *  @brief Coordinates user-initiated package changes through solver-based dependency resolution */

#ifndef PACKAGE_MANAGER_H
#define PACKAGE_MANAGER_H


#include <map>
#include <set>

#include <package/Context.h>
#include <package/Job.h>

#include <package/DaemonClient.h>
#include <package/manager/PackageManager.h>


using BPackageKit::BCommitTransactionResult;
using BPackageKit::BContext;
using BPackageKit::BPackageInstallationLocation;
using BPackageKit::BRepositoryConfig;
using BPackageKit::BSolverPackage;
using BSupportKit::BJob;
using BSupportKit::BJobStateListener;

using BPackageKit::BPrivate::BDaemonClient;
using BPackageKit::BManager::BPrivate::BPackageManager;


class Package;
class ProblemWindow;
class ResultWindow;
class Root;
class Volume;


/** @brief Drives solver-based dependency resolution for user package additions and removals */
class PackageManager : public BPackageManager,
	private BPackageManager::InstallationInterface,
	private BPackageManager::UserInteractionHandler {
public:
	/** @brief Construct a manager for the given root and volume */
								PackageManager(Root* root, Volume* volume);
	/** @brief Destructor */
								~PackageManager();

	/** @brief Resolve and commit user-initiated package activation changes */
			void				HandleUserChanges();

private:
	// InstallationInterface
	virtual	void				InitInstalledRepository(
									InstalledRepository& repository);
	virtual	void				ResultComputed(InstalledRepository& repository);

	virtual	status_t			PrepareTransaction(Transaction& transaction);
	virtual	status_t			CommitTransaction(Transaction& transaction,
									BCommitTransactionResult& _result);

private:
	// UserInteractionHandler
	virtual	void				HandleProblems();
	virtual	void				ConfirmChanges(bool fromMostSpecific);

	virtual	void				Warn(status_t error, const char* format, ...);

	virtual	void				ProgressPackageDownloadStarted(
									const char* packageName);
	virtual	void				ProgressPackageDownloadActive(
									const char* packageName,
									float completionPercentage,
									off_t bytes, off_t totalBytes);
	virtual	void				ProgressPackageDownloadComplete(
									const char* packageName);
	virtual	void				ProgressPackageChecksumStarted(
									const char* title);
	virtual	void				ProgressPackageChecksumComplete(
									const char* title);

	virtual	void				ProgressStartApplyingChanges(
									InstalledRepository& repository);
	virtual	void				ProgressTransactionCommitted(
									InstalledRepository& repository,
									const BCommitTransactionResult& result);
	virtual	void				ProgressApplyingChangesDone(
									InstalledRepository& repository);

private:
	// BJobStateListener
	virtual	void				JobFailed(BSupportKit::BJob* job);
	virtual	void				JobAborted(BSupportKit::BJob* job);

private:
			typedef std::set<BSolverPackage*> SolverPackageSet;
			typedef std::map<Package*, BSolverPackage*> SolverPackageMap;

private:
			bool				_AddResults(InstalledRepository& repository,
									ResultWindow* window);

			BSolverPackage*		_SolverPackageFor(Package* package) const;

			void				_InitGui();

private:
			Root*				fRoot;                  /**< Root owning the target volume */
			Volume*				fVolume;                /**< Volume whose packages are being managed */
			SolverPackageMap	fSolverPackages;        /**< Maps Package to BSolverPackage */
			SolverPackageSet	fPackagesAddedByUser;   /**< Solver packages the user added */
			SolverPackageSet	fPackagesRemovedByUser; /**< Solver packages the user removed */
			ProblemWindow*		fProblemWindow;         /**< Lazily created problem dialog */
};


#endif	// PACKAGE_MANAGER_H
