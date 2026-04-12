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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file NodePreloader.cpp
 * @brief Background icon pre-loader for the Tracker Applications and Prefs folders.
 *
 * NodePreloader is a BHandler that enumerates well-known directories on a
 * worker thread, loading each entry's icon into IconCache before it is needed
 * for display.  It also subscribes to node-monitor notifications so the cache
 * stays up to date when files are added, removed, or modified.
 *
 * @see IconCache, TTracker
 */

#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <Path.h>

#include "AutoLock.h"
#include "IconCache.h"
#include "NodePreloader.h"
#include "Thread.h"
#include "Tracker.h"


/**
 * @brief Create a NodePreloader, add it to @p host, and start the preload thread.
 *
 * Allocates the preloader, adds it as a handler to @p host under the looper
 * lock, then calls Run() to start background preloading.
 *
 * @param name  Handler name for the new preloader.
 * @param host  The BLooper that will own and route messages to this handler.
 * @return Pointer to the new NodePreloader, or NULL if @p host could not be locked.
 */
NodePreloader*
NodePreloader::InstallNodePreloader(const char* name, BLooper* host)
{
	NodePreloader* result = new NodePreloader(name);
	{
		AutoLock<BLooper> lock(host);
		if (!lock) {
			delete result;
			return NULL;
		}

		host->AddHandler(result);
	}
	result->Run();

	return result;
}


/**
 * @brief Construct a NodePreloader handler with the given name.
 *
 * @param name  The BHandler name string.
 */
NodePreloader::NodePreloader(const char* name)
	:
	BHandler(name),
	fModelList(20),
	fQuitRequested(false)
{
}


/**
 * @brief Destroy the NodePreloader, blocking until the Preload thread exits.
 */
NodePreloader::~NodePreloader()
{
	// block deletion while we are locked
	fQuitRequested = true;
	fLock.Lock();
}


/**
 * @brief Start the background Preload() worker thread.
 */
void
NodePreloader::Run()
{
	fLock.Lock();
	Thread::Launch(NewMemberFunctionObject(&NodePreloader::Preload, this));
}


/**
 * @brief Search the preloaded model list for a model matching @p itemNode.
 *
 * @param itemNode  The node_ref to search for.
 * @return Pointer to the matching Model, or NULL if not found.
 */
Model*
NodePreloader::FindModel(node_ref itemNode) const
{
	for (int32 count = fModelList.CountItems() - 1; count >= 0; count--) {
		Model* model = fModelList.ItemAt(count);
		if (*model->NodeRef() == itemNode)
			return model;
	}

	return NULL;
}


/**
 * @brief Respond to node-monitor notifications to keep the icon cache current.
 *
 * Handles B_ENTRY_REMOVED (evict from cache and model list) and
 * B_ATTR_CHANGED/B_STAT_CHANGED (refresh icon in cache).
 *
 * @param message  The incoming BMessage.
 */
void
NodePreloader::MessageReceived(BMessage* message)
{
	// respond to node monitor notifications

	node_ref itemNode;
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			switch (message->GetInt32("opcode", 0)) {
				case B_ENTRY_REMOVED:
				{
					AutoLock<Benaphore> locker(fLock);
					message->FindInt32("device", &itemNode.device);
					message->FindInt64("node", &itemNode.node);
					Model* model = FindModel(itemNode);
					if (model == NULL)
						break;

					//PRINT(("preloader removing file %s\n", model->Name()));
					IconCache::sIconCache->Removing(model);
					fModelList.RemoveItem(model);
					break;
				}

				case B_ATTR_CHANGED:
				case B_STAT_CHANGED:
				{
					AutoLock<Benaphore> locker(fLock);
					message->FindInt32("device", &itemNode.device);
					message->FindInt64("node", &itemNode.node);

					const char* attrName;
					message->FindString("attr", &attrName);
					Model* model = FindModel(itemNode);
					if (model == NULL)
						break;

					BModelOpener opener(model);
					IconCache::sIconCache->IconChanged(model->ResolveIfLink());
					//PRINT(("preloader updating file %s\n", model->Name()));
					break;
				}
			}
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Preload icon data for every entry inside @p dirPath.
 *
 * Opens the directory, sets up a node monitor on it, then iterates each
 * entry to load its icon into IconCache.  Called from the Preload() thread.
 *
 * @param dirPath  Absolute path of the directory to preload.
 */
void
NodePreloader::PreloadOne(const char* dirPath)
{
	//PRINT(("preloading directory %s\n", dirPath));
	BDirectory dir(dirPath);
	if (dir.InitCheck() != B_OK)
		return;

	node_ref nodeRef;
	dir.GetNodeRef(&nodeRef);

	// have to node monitor the whole directory
	TTracker::WatchNode(&nodeRef, B_WATCH_DIRECTORY, this);

	dir.Rewind();
	for (;;) {
		entry_ref ref;
		if (dir.GetNextRef(&ref) != B_OK)
			break;

		BEntry entry(&ref);
		if (!entry.IsFile())
			// only interrested in files
			continue;

		Model* model = new Model(&ref, true);
		if (model->InitCheck() == B_OK && model->IconFrom() == kUnknownSource) {
			TTracker::WatchNode(model->NodeRef(),
				B_WATCH_STAT | B_WATCH_ATTR, this);
			IconCache::sIconCache->Preload(model, kNormalIcon,
				IconCache::sMiniIconSize, true);
			fModelList.AddItem(model);
			model->CloseNode();
		} else
			delete model;
	}
}


void
NodePreloader::Preload()
{
	for (int32 count = 100; count >= 0; count--) {
		// wait for a little bit before going ahead to reduce disk access
		// contention
		snooze(100000);
		if (fQuitRequested) {
			fLock.Unlock();
			return;
		}
	}

	BMessenger messenger(kTrackerSignature);
	if (!messenger.IsValid()) {
		// put out some message here!
		return;
	}

	ASSERT(fLock.IsLocked());
	BPath path;
	if (find_directory(B_BEOS_APPS_DIRECTORY, &path) == B_OK)
		PreloadOne(path.Path());

	if (find_directory(B_BEOS_PREFERENCES_DIRECTORY, &path) == B_OK)
		PreloadOne(path.Path());

	fLock.Unlock();
}
