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
    * Copyright 2013-2021, Haiku, Inc. All Rights Reserved.
    * Distributed under the terms of the MIT License.
    *
    * Authors:
    *		Ingo Weinhold <ingo_weinhold@gmx.de>
    *		Andrew Lindesay <apl@lindesay.co.nz>
    */
 */

/** @file Volume.h
 *  @brief Manages a single packagefs volume including node monitoring and state transitions */

#ifndef VOLUME_H
#define VOLUME_H


#include <Handler.h>
#include <Locker.h>
#include <Message.h>
#include <String.h>

#include <package/ActivationTransaction.h>
#include <package/DaemonClient.h>
#include <package/packagefs.h>
#include <util/DoublyLinkedList.h>

#include "FSUtils.h"
#include "Package.h"


// Locking Policy
// ==============
//
// A Volume object is accessed by two threads:
// 1. The application thread: initially (c'tor and Init()) and when handling a
//    location info request (HandleGetLocationInfoRequest()).
// 2. The corresponding Root object's job thread (any other operation).
//
// The only thread synchronization needed is for the status information accessed
// by HandleGetLocationInfoRequest() and modified by the job thread. The data
// are encapsulated in a VolumeState, which is protected by Volume::fLock. The
// lock must be held by the app thread when accessing the data (it reads only)
// and by the job thread when modifying the data (not needed when reading).


using BPackageKit::BPrivate::BActivationTransaction;
using BPackageKit::BPrivate::BDaemonClient;

class BDirectory;

class CommitTransactionHandler;
class PackageFileManager;
class Root;
class VolumeState;

namespace BPackageKit {
	class BSolver;
	class BSolverRepository;
}

using BPackageKit::BPackageInstallationLocation;
using BPackageKit::BSolver;
using BPackageKit::BSolverRepository;


/** @brief Manages a single packagefs volume: tracks packages, handles node monitoring, and commits transactions */
class Volume : public BHandler {
public:
			class Listener;

public:
	/** @brief Construct a volume and attach it to the given looper */
								Volume(BLooper* looper);
	/** @brief Destructor */
	virtual						~Volume();

	/** @brief Initialize the volume from a root directory reference */
			status_t			Init(const node_ref& rootDirectoryRef,
									node_ref& _packageRootRef);
	/** @brief Scan the packages directory and build the initial package state */
			status_t			InitPackages(Listener* listener);

	/** @brief Add the volume's packages to a solver repository */
			status_t			AddPackagesToRepository(
									BSolverRepository& repository,
									bool activeOnly);
	/** @brief Perform initial verification of package state against adjacent volumes */
			void				InitialVerify(Volume* nextVolume,
									Volume* nextNextVolume);
	/** @brief Reply to a location-info request with current state */
			void				HandleGetLocationInfoRequest(BMessage* message);
	/** @brief Process a commit-transaction request message */
			void				HandleCommitTransactionRequest(
									BMessage* message);

	/** @brief Increment the pending package job counter */
			void				PackageJobPending();
	/** @brief Decrement the pending package job counter */
			void				PackageJobFinished();
	/** @brief Check whether any package jobs are still pending */
			bool				IsPackageJobPending() const;

	/** @brief Handle volume unmount by stopping node monitoring */
			void				Unmounted();

	/** @brief Handle incoming messages (node monitor events and timer ticks) */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Return the absolute path of this volume */
			const BString&		Path() const
									{ return fPath; }
	/** @brief Return the packagefs mount type (system or home) */
			PackageFSMountType	MountType() const
									{ return fMountType; }
	/** @brief Return the package installation location for this volume */
			BPackageInstallationLocation Location() const;

	/** @brief Return the root directory node reference */
			const node_ref&		RootDirectoryRef() const
									{ return fRootDirectoryRef; }
	/** @brief Return the device ID of the root directory */
			dev_t				DeviceID() const
									{ return fRootDirectoryRef.device; }
	/** @brief Return the inode number of the root directory */
			ino_t				RootDirectoryID() const
									{ return fRootDirectoryRef.node; }

	/** @brief Return the packages directory node reference */
			const node_ref&		PackagesDirectoryRef() const;
	/** @brief Return the device ID of the packages directory */
			dev_t				PackagesDeviceID() const
									{ return PackagesDirectoryRef().device; }
	/** @brief Return the inode number of the packages directory */
			ino_t				PackagesDirectoryID() const
									{ return PackagesDirectoryRef().node; }

	/** @brief Return the owning Root object */
			Root*				GetRoot() const
									{ return fRoot; }
	/** @brief Set the owning Root object */
			void				SetRoot(Root* root)
									{ fRoot = root; }

	/** @brief Return the monotonic change counter for this volume */
			int64				ChangeCount() const
									{ return fChangeCount; }

	/** @brief Return an iterator over all packages indexed by filename */
			PackageFileNameHashTable::Iterator PackagesByFileNameIterator()
									const;

	/** @brief Open and return a file descriptor for the root directory */
			int					OpenRootDirectory() const;

	/** @brief Process all queued node monitor events */
			void				ProcessPendingNodeMonitorEvents();

	/** @brief Check whether there are pending package activation changes */
			bool				HasPendingPackageActivationChanges() const;
	/** @brief Apply pending user-initiated package activation changes via the solver */
			void				ProcessPendingPackageActivationChanges();
	/** @brief Clear the pending activation and deactivation sets */
			void				ClearPackageActivationChanges();
	/** @brief Return the set of packages pending activation */
			const PackageSet&	PackagesToBeActivated() const
									{ return fPackagesToBeActivated; }
	/** @brief Return the set of packages pending deactivation */
			const PackageSet&	PackagesToBeDeactivated() const
									{ return fPackagesToBeDeactivated; }

	/** @brief Create a new activation transaction and its working directory */
			status_t			CreateTransaction(
									BPackageInstallationLocation location,
									BActivationTransaction& _transaction,
									BDirectory& _transactionDirectory);
	/** @brief Commit an activation transaction, applying package changes */
			void				CommitTransaction(
									const BActivationTransaction& transaction,
									const PackageSet& packagesAlreadyAdded,
									const PackageSet& packagesAlreadyRemoved,
									BCommitTransactionResult& _result);

private:
			struct NodeMonitorEvent;
			struct PackagesDirectory;

			typedef FSUtils::RelativePath RelativePath;
			typedef DoublyLinkedList<NodeMonitorEvent> NodeMonitorEventList;

private:
			void				_HandleEntryCreatedOrRemoved(
									const BMessage* message, bool created);
			void				_HandleEntryMoved(const BMessage* message);
			void				_QueueNodeMonitorEvent(const BString& name,
									bool wasCreated);

			void				_PackagesEntryCreated(const char* name);
			void				_PackagesEntryRemoved(const char* name);

			status_t			_ReadPackagesDirectory();
			status_t			_InitLatestState();
			status_t			_InitLatestStateFromActivatedPackages();
			status_t			_GetActivePackages(int fd);
			void				_RunQueuedScripts(); // TODO: Never called, fix?
			bool				_CheckActivePackagesMatchLatestState(
									PackageFSGetPackageInfosRequest* request);
			void				_SetLatestState(VolumeState* state,
									bool isActive);
			void				_DumpState(VolumeState* state);

			status_t			_AddRepository(BSolver* solver,
									BSolverRepository& repository,
							 		bool activeOnly, bool installed);

			status_t			_OpenPackagesSubDirectory(
									const RelativePath& path, bool create,
									BDirectory& _directory);

			void				_CommitTransaction(BMessage* message,
									const BActivationTransaction* transaction,
									const PackageSet& packagesAlreadyAdded,
									const PackageSet& packagesAlreadyRemoved,
									BCommitTransactionResult& _result);

	static	void				_CollectPackageNamesAdded(
									const VolumeState* oldState,
									const VolumeState* newState,
									BStringList& addedPackageNames);

private:
			BString				fPath;                  /**< Absolute path of the volume root */
			PackageFSMountType	fMountType;             /**< System or home mount type */
			node_ref			fRootDirectoryRef;      /**< Node reference for the volume root directory */
			PackagesDirectory*	fPackagesDirectories;   /**< Array of watched packages directories */
			uint32				fPackagesDirectoryCount;/**< Number of packages directories */
			Root*				fRoot;                  /**< Owning Root object */
			Listener*			fListener;              /**< Callback for node monitor events */
			PackageFileManager*	fPackageFileManager;    /**< Shared package file cache */
			VolumeState*		fLatestState;           /**< Most recently computed package state */
			VolumeState*		fActiveState;           /**< Currently active package state */
			int64				fChangeCount;           /**< Monotonic counter incremented on state changes */
			BLocker				fLock;                  /**< Protects volume state for app-thread reads */
			BLocker				fPendingNodeMonitorEventsLock; /**< Guards the pending events list */
			NodeMonitorEventList fPendingNodeMonitorEvents;    /**< Queued node monitor events */
			bigtime_t			fNodeMonitorEventHandleTime;   /**< Scheduled time for processing events */
			PackageSet			fPackagesToBeActivated;  /**< Packages pending user-initiated activation */
			PackageSet			fPackagesToBeDeactivated;/**< Packages pending user-initiated deactivation */
			BMessage			fLocationInfoReply;     /**< Cached location info reply (app thread only) */
			int32				fPendingPackageJobCount; /**< Number of pending package jobs */
};


/** @brief Callback interface notified when node monitor events occur on a volume */
class Volume::Listener {
public:
	/** @brief Destructor */
	virtual						~Listener();

	/** @brief Called when a node monitor event is received for the given volume */
	virtual	void				VolumeNodeMonitorEventOccurred(Volume* volume)
									= 0;
};


#endif	// VOLUME_H
