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
 *
 * Copyright 2013, Haiku, Inc. All Rights Reserved.
  * Distributed under the terms of the MIT License.
  *
  * Authors:
  *		Ingo Weinhold <ingo_weinhold@gmx.de>
 */

/** @file Root.h
 *  @brief Represents a package-filesystem root that owns system and home volumes */

#ifndef ROOT_H
#define ROOT_H


#include <Locker.h>
#include <Node.h>
#include <ObjectList.h>
#include <OS.h>
#include <package/PackageDefs.h>
#include <String.h>

#include <Referenceable.h>

#include <package/packagefs.h>

#include "JobQueue.h"
#include "Volume.h"


/** @brief Represents a packagefs root directory with system and home volumes and a job runner thread */
class Root : public BReferenceable, private Volume::Listener {
public:
	/** @brief Construct an uninitialized root */
								Root();
	/** @brief Destructor */
	virtual						~Root();

	/** @brief Initialize the root from a directory node reference */
			status_t			Init(const node_ref& nodeRef,
									bool isSystemRoot);

	/** @brief Return the root directory node reference */
			const node_ref&		NodeRef() const		{ return fNodeRef; }
	/** @brief Return the device ID of the root directory */
			dev_t				DeviceID() const	{ return fNodeRef.device; }
	/** @brief Return the inode number of the root directory */
			ino_t				NodeID() const		{ return fNodeRef.node; }
	/** @brief Return the absolute path of the root directory */
			const BString&		Path() const		{ return fPath; }

	/** @brief Check whether this is the system root */
			bool				IsSystemRoot() const
									{ return fIsSystemRoot; }

	/** @brief Register a volume with this root and initialize its packages */
			status_t			RegisterVolume(Volume* volume);
	/** @brief Unregister and eventually delete a volume */
			void				UnregisterVolume(Volume* volume);
									// deletes the volume (eventually)

	/** @brief Find a registered volume by device ID */
			Volume*				FindVolume(dev_t deviceID) const;
	/** @brief Return the volume for the given installation location */
			Volume*				GetVolume(
									BPackageInstallationLocation location);

	/** @brief Dispatch a commit-transaction request to the job queue */
			void				HandleRequest(BMessage* message);

private:
	// Volume::Listener
	virtual	void				VolumeNodeMonitorEventOccurred(Volume* volume);

protected:
	virtual	void				LastReferenceReleased();

private:
			struct AbstractVolumeJob;
			struct VolumeJob;
			struct ProcessNodeMonitorEventsJob;
			struct CommitTransactionJob;
			struct VolumeJobFilter;

			friend struct CommitTransactionJob;

private:
			Volume**			_GetVolume(PackageFSMountType mountType);
			Volume*				_NextVolumeFor(Volume* volume);

			void				_InitPackages(Volume* volume);
			void				_DeleteVolume(Volume* volume);
			void				_ProcessNodeMonitorEvents(Volume* volume);
			void				_CommitTransaction(Volume* volume,
									BMessage* message);

			status_t			_QueueJob(Job* job);

	static	status_t			_JobRunnerEntry(void* data);
			status_t			_JobRunner();

	static	void				_ShowError(const char* errorMessage);

private:
	mutable	BLocker				fLock;          /**< Protects volume pointers and root state */
			node_ref			fNodeRef;       /**< Node reference for the root directory */
			bool				fIsSystemRoot;  /**< True if this is the primary system root */
			BString				fPath;          /**< Absolute filesystem path */
			Volume*				fSystemVolume;  /**< System packages volume, if registered */
			Volume*				fHomeVolume;    /**< Home packages volume, if registered */
			JobQueue			fJobQueue;      /**< Queue for background volume jobs */
			thread_id			fJobRunner;     /**< Thread executing queued jobs */
};


#endif	// ROOT_H
