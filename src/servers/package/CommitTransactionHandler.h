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

/** @file CommitTransactionHandler.h
 *  @brief Manages the commit phase of package activation transactions */

#ifndef COMMIT_TRANSACTION_HANDLER_H
#define COMMIT_TRANSACTION_HANDLER_H


#include <set>
#include <string>

#include <Directory.h>

#include "FSTransaction.h"
#include "FSUtils.h"
#include "Volume.h"


typedef std::set<std::string> StringSet;


namespace BPackageKit {
	class BCommitTransactionResult;
}

using BPackageKit::BCommitTransactionResult;


/** @brief Executes the commit phase of a package activation transaction on a volume */
class CommitTransactionHandler {
public:
	/** @brief Construct a handler for the given volume and result object */
								CommitTransactionHandler(Volume* volume,
									PackageFileManager* packageFileManager,
									BCommitTransactionResult& result);
	/** @brief Destructor */
								~CommitTransactionHandler();

	/** @brief Initialize with volume state and the sets of pre-existing package changes */
			void				Init(VolumeState* volumeState,
									bool isActiveVolumeState,
									const PackageSet& packagesAlreadyAdded,
									const PackageSet& packagesAlreadyRemoved);

	/** @brief Process a commit request from a BMessage */
			void				HandleRequest(BMessage* request);
	/** @brief Process a commit request from an activation transaction */
			void				HandleRequest(
									const BActivationTransaction& transaction);
	/** @brief Process a commit using the pre-existing add/remove sets from Init() */
			void				HandleRequest();
									// uses packagesAlreadyAdded and
									// packagesAlreadyRemoved from Init()

	/** @brief Roll back all changes made during this transaction */
			void				Revert();

	/** @brief Return the name of the old-state snapshot directory */
			const BString&		OldStateDirectoryName() const
									{ return fOldStateDirectoryName; }

	/** @brief Return the package currently being processed, if any */
			Package*			CurrentPackage() const
									{ return fCurrentPackage; }

	/** @brief Detach and return the owned VolumeState (caller takes ownership) */
			VolumeState*		DetachVolumeState();
	/** @brief Return whether the volume state is the currently active one */
			bool				IsActiveVolumeState() const
									{ return fVolumeStateIsActive; }

private:
			typedef BObjectList<Package> PackageList;
			typedef FSUtils::RelativePath RelativePath;

			struct TransactionIssueBuilder;

private:
			void				_GetPackagesToDeactivate(
									const BActivationTransaction& transaction);
			void				_ReadPackagesToActivate(
									const BActivationTransaction& transaction);
			void				_ApplyChanges();
			void				_CreateOldStateDirectory();
			void				_RemovePackagesToDeactivate();
			void				_AddPackagesToActivate();

			void				_PreparePackageToActivate(Package* package);
			void				_AddGroup(Package* package,
									const BString& groupName);
			void				_AddUser(Package* package, const BUser& user);
			void				_AddGlobalWritableFiles(Package* package);
			void				_AddGlobalWritableFile(Package* package,
									const BGlobalWritableFileInfo& file,
									const BDirectory& rootDirectory,
									const BDirectory& extractedFilesDirectory);
			void				_AddGlobalWritableFileRecurse(Package* package,
									const BDirectory& sourceDirectory,
									FSUtils::Path& relativeSourcePath,
									const BDirectory& targetDirectory,
									const char* targetName,
									BWritableFileUpdateType updateType);

			void				_RevertAddPackagesToActivate();
			void				_RevertRemovePackagesToDeactivate();
			void				_RevertUserGroupChanges();

			void				_RunPostInstallScripts();
			void				_RunPreUninstallScripts();
			void				_RunPostOrPreScript(Package* package,
									const BString& script,
									bool postNotPre);

			void				_QueuePostInstallScripts();

			void				_ExtractPackageContent(Package* package,
									const BStringList& contentPaths,
									BDirectory& targetDirectory,
									BDirectory& _extractedFilesDirectory);

			status_t			_OpenPackagesSubDirectory(
									const RelativePath& path, bool create,
									BDirectory& _directory);
			status_t			_OpenPackagesFile(
									const RelativePath& subDirectoryPath,
									const char* fileName, uint32 openMode,
									BFile& _file, BEntry* _entry = NULL);

			void				_WriteActivationFile(
									const RelativePath& directoryPath,
									const char* fileName,
									const PackageSet& toActivate,
									const PackageSet& toDeactivate,
									BEntry& _entry);
			void				_CreateActivationFileContent(
									const PackageSet& toActivate,
									const PackageSet& toDeactivate,
									BString& _content);
			status_t			_WriteTextFile(
									const RelativePath& directoryPath,
									const char* fileName,
									const BString& content, BEntry& _entry);
			void				_ChangePackageActivation(
									const PackageSet& packagesToActivate,
									const PackageSet& packagesToDeactivate);
									// throws Exception
			void				_ChangePackageActivationIOCtl(
									const PackageSet& packagesToActivate,
									const PackageSet& packagesToDeactivate);
									// throws Exception
			void				_PrepareFirstBootPackages();
			void				_FillInActivationChangeItem(
									PackageFSActivationChangeItem* item,
									PackageFSActivationChangeType type,
									Package* package, char*& nameBuffer);

			bool				_IsSystemPackage(Package* package);

			void				_AddIssue(
									const TransactionIssueBuilder& builder);

	static	BString				_GetPath(const FSUtils::Entry& entry,
									const BString& fallback);

	static	void				_TagPackageEntriesRecursively(
									BDirectory& directory, const BString& value,
									bool nonDirectoriesOnly);

	static	status_t			_AssertEntriesAreEqual(const BEntry& entry,
									const BDirectory* directory);

private:
			Volume*				fVolume;                /**< Volume being committed */
			PackageFileManager*	fPackageFileManager;    /**< Shared package-file pool */
			VolumeState*		fVolumeState;           /**< Working copy of the volume state */
			bool				fVolumeStateIsActive;   /**< Whether fVolumeState is the active state */
			PackageList			fPackagesToActivate;    /**< Packages to be activated */
			PackageSet			fPackagesToDeactivate;  /**< Packages to be deactivated */
			PackageSet			fAddedPackages;         /**< Packages added during this transaction */
			PackageSet			fRemovedPackages;       /**< Packages removed during this transaction */
			PackageSet			fPackagesAlreadyAdded;  /**< Pre-existing packages added by user */
			PackageSet			fPackagesAlreadyRemoved;/**< Pre-existing packages removed by user */
			BDirectory			fOldStateDirectory;     /**< Directory holding the old state backup */
			node_ref			fOldStateDirectoryRef;
			BString				fOldStateDirectoryName; /**< Name of the old-state directory */
			node_ref			fTransactionDirectoryRef;
			bool				fFirstBootProcessing;   /**< Whether this is a first-boot commit */
			BDirectory			fWritableFilesDirectory;/**< Target for global writable files */
			StringSet			fAddedGroups;           /**< Groups created during this transaction */
			StringSet			fAddedUsers;            /**< Users created during this transaction */
			FSTransaction		fFSTransaction;         /**< Filesystem rollback tracker */
			BCommitTransactionResult& fResult;          /**< Output result of the commit */
			Package*			fCurrentPackage;        /**< Package currently being processed */
};


#endif	// COMMIT_TRANSACTION_HANDLER_H
