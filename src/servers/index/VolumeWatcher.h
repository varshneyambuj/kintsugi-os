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
 *   Copyright 2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 */

/** @file VolumeWatcher.h
 *  @brief Watches mounted volumes for file-system changes and delegates them to analyser workers. */

#ifndef VOLUME_WATCHER_H
#define VOLUME_WATCHER_H


#include <vector>

#include <Debug.h>
#include <Handler.h>
#include <NodeMonitorHandler.h>
#include <Volume.h>

#include <ObjectList.h>

#include "AnalyserDispatcher.h"
#include "CatchUpManager.h"
#include "IndexServerAddOn.h"


class VolumeWatcher;


/** @brief Receives node-monitor messages and forwards file-system events to a VolumeWatcher. */
class WatchNameHandler : public NodeMonitorHandler {
public:
								WatchNameHandler(VolumeWatcher* volumeWatcher);

	/** @brief Called when a new entry is created on the watched volume. */
			void				EntryCreated(const char *name, ino_t directory,
									dev_t device, ino_t node);
	/** @brief Called when an entry is removed from the watched volume. */
			void				EntryRemoved(const char *name, ino_t directory,
									dev_t device, ino_t node);
	/** @brief Called when an entry is moved or renamed on the watched volume. */
			void				EntryMoved(const char *name,
									const char *fromName, ino_t from_directory,
									ino_t to_directory, dev_t device,
									ino_t node, dev_t nodeDevice);
	/** @brief Called when stat information of a node changes. */
			void				StatChanged(ino_t node, dev_t device,
									int32 statFields);

	/** @brief Process raw B_NODE_MONITOR messages. */
			void				MessageReceived(BMessage* msg);
private:
			VolumeWatcher*		fVolumeWatcher; /**< The VolumeWatcher that owns this handler. */
};


typedef std::vector<entry_ref> EntryRefVector;


class VolumeWatcher;


const uint32 kTriggerWork = '&twk';	// what a bad message


/** @brief Worker looper that processes queued file-system changes from a VolumeWatcher. */
class VolumeWorker : public AnalyserDispatcher
{
public:
								VolumeWorker(VolumeWatcher* watcher);

	/** @brief Handle kTriggerWork messages to process queued entries. */
			void				MessageReceived(BMessage *message);

	/** @brief Return whether the worker is currently processing entries. */
			bool				IsBusy();

private:
			void				_Work();

			void				_SetBusy(bool busy = true);

			VolumeWatcher*		fVolumeWatcher; /**< The VolumeWatcher that feeds this worker. */
			int32				fBusy; /**< Atomic busy flag. */
};


/** @brief Base class holding per-volume state such as enabled flag and last-updated timestamp. */
class VolumeWatcherBase {
public:
								VolumeWatcherBase(const BVolume& volume);

	/** @brief Return the volume being watched. */
			const BVolume&		Volume() { return fVolume; }

	/** @brief Return whether watching is enabled for this volume. */
			bool				Enabled() { return fEnabled; }
	/** @brief Return the timestamp of the last update. */
			bigtime_t			GetLastUpdated() { return fLastUpdated; }

protected:
			bool				ReadSettings();
			bool				WriteSettings();

			BVolume				fVolume;    /**< The volume being watched. */

			bool				fEnabled;   /**< Whether watching is enabled. */

			bigtime_t			fLastUpdated; /**< Timestamp of last successful update. */
};


/** @brief Thread-safe double-buffered vector for exchanging entry_ref lists between threads. */
class SwapEntryRefVector {
public:
								SwapEntryRefVector();

	/** @brief Swap the current and next list, returning the previously current list. */
			EntryRefVector*		SwapList();
	/** @brief Return the current list being filled. */
			EntryRefVector*		CurrentList();
private:
			EntryRefVector		fFirstList;
			EntryRefVector		fSecondList;
			EntryRefVector*		fCurrentList;
			EntryRefVector*		fNextList;
};


/** @brief Collection of entry_ref vectors representing different types of file-system changes. */
struct list_collection
{
			EntryRefVector*		createdList;    /**< Newly created entries. */
			EntryRefVector*		deletedList;    /**< Deleted entries. */
			EntryRefVector*		modifiedList;   /**< Modified entries. */
			EntryRefVector*		movedList;      /**< Destination refs of moved entries. */
			EntryRefVector*		movedFromList;  /**< Source refs of moved entries. */
};


/** @brief Watches a single volume for file-system changes and delegates them to a VolumeWorker. */
class VolumeWatcher : public VolumeWatcherBase, public BLooper {
public:
								VolumeWatcher(const BVolume& volume);
								~VolumeWatcher();

	/** @brief Begin watching the volume for file-system changes. */
			bool				StartWatching();
	/** @brief Stop watching and persist analyser settings. */
			void				Stop();

	/** @brief Thread-safe addition of a FileAnalyser to the volume worker. */
			bool				AddAnalyser(FileAnalyser* analyser);
	/** @brief Remove an analyser by name from the volume worker. */
			bool				RemoveAnalyser(const BString& name);

	/** @brief Atomically swap and return the pending entry lists. */
			void				GetSecureEntries(list_collection& collection);

	/** @brief Look up an entry_ref by node and device. */
			bool				FindEntryRef(ino_t node, dev_t device,
									entry_ref& entry);

private:
	friend class WatchNameHandler;

			void				_NewEntriesArrived();

			bool				fWatching; /**< Whether the watcher is actively monitoring. */

			WatchNameHandler	fWatchNameHandler; /**< Node-monitor message handler. */

			SwapEntryRefVector	fCreatedList;   /**< Double-buffered created entries. */
			SwapEntryRefVector	fDeleteList;    /**< Double-buffered deleted entries. */
			SwapEntryRefVector	fModifiedList;  /**< Double-buffered modified entries. */
			SwapEntryRefVector	fMovedList;     /**< Double-buffered move-destination entries. */
			SwapEntryRefVector	fMovedFromList; /**< Double-buffered move-source entries. */

			VolumeWorker*		fVolumeWorker;  /**< Worker looper processing changes. */
			CatchUpManager		fCatchUpManager; /**< Manager for catch-up indexing. */
};


#endif
