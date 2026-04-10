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

/** @file IndexServer.h
 *  @brief Declaration of the IndexServer application and its helper handler classes. */

#ifndef INDEX_SERVER_H
#define INDEX_SERVER_H


#include <Application.h>
#include <MessageRunner.h>
#include <VolumeRoster.h>

#include <AddOnMonitorHandler.h>
#include <ObjectList.h>

#include "IndexServerAddOn.h"
#include "VolumeWatcher.h"


#define DEBUG_INDEX_SERVER
#ifdef DEBUG_INDEX_SERVER
#include <stdio.h>
#	define STRACE(x...) printf(x)
#else
#	define STRACE(x...) ;
#endif


class IndexServer;


/** @brief Handles B_NODE_MONITOR messages for volume mount/unmount events. */
class VolumeObserverHandler : public BHandler {
public:
								VolumeObserverHandler(IndexServer* indexServer);
	/** @brief Process mount and unmount notifications. */
			void				MessageReceived(BMessage *message);
private:
			IndexServer*		fIndexServer; /**< Owning IndexServer instance. */
};


/** @brief Monitors add-on directories and notifies the IndexServer of enable/disable events. */
class AnalyserMonitorHandler : public AddOnMonitorHandler {
public:
								AnalyserMonitorHandler(
									IndexServer* indexServer);

private:
	/** @brief Called when a new analyser add-on becomes available. */
			void				AddOnEnabled(
									const add_on_entry_info* entryInfo);
	/** @brief Called when an analyser add-on is removed. */
			void				AddOnDisabled(
									const add_on_entry_info* entryInfo);

			IndexServer*		fIndexServer; /**< Owning IndexServer instance. */
};


/** @brief Main BApplication for the index server; manages volumes, watchers, and analyser add-ons. */
class IndexServer : public BApplication {
public:
								IndexServer();
	virtual						~IndexServer();

	/** @brief Called when the application is ready; starts watching volumes and add-ons. */
	virtual void				ReadyToRun();
	/** @brief Process application-level messages. */
	virtual	void				MessageReceived(BMessage *message);

	/** @brief Handle quit request by stopping volume watchers. */
	virtual	bool				QuitRequested();

	/** @brief Add a mounted volume to the watch list. */
			void				AddVolume(const BVolume& volume);
	/** @brief Remove an unmounted volume from the watch list. */
			void				RemoveVolume(const BVolume& volume);

	/** @brief Load and register a new analyser add-on. */
			void				RegisterAddOn(entry_ref ref);
	/** @brief Unregister and unload an analyser add-on. */
			void				UnregisterAddOn(entry_ref ref);

	/** @brief Thread-safe factory method for creating a FileAnalyser by name. */
			FileAnalyser*		CreateFileAnalyser(const BString& name,
									const BVolume& volume);
private:
			void				_StartWatchingVolumes();
			void				_StopWatchingVolumes();

			void				_SetupVolumeWatcher(VolumeWatcher* watcher);
			FileAnalyser*		_SetupFileAnalyser(IndexServerAddOn* addon,
									const BVolume& volume);
			void				_StartWatchingAddOns();

	inline	IndexServerAddOn*	_FindAddon(const BString& name);

			BVolumeRoster		fVolumeRoster;              /**< System volume roster. */
			BObjectList<VolumeWatcher>		fVolumeWatcherList; /**< Active volume watchers. */
			BObjectList<IndexServerAddOn>	fAddOnList;     /**< Loaded analyser add-ons. */

			VolumeObserverHandler	fVolumeObserverHandler; /**< Handler for volume events. */

			AnalyserMonitorHandler	fAddOnMonitorHandler;   /**< Handler for add-on events. */
			BMessageRunner*			fPulseRunner;           /**< Pulse runner for add-on monitor. */
};


#endif
