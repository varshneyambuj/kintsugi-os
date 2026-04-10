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
   Copyright (c) 2001-2002, Haiku
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   File Name:		WatchingService.cpp
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	Features everything needed to provide a watching service.
 */

/** @file WatchingService.cpp
 *  @brief Manages a set of Watchers and dispatches filtered notification messages. */
#include <List.h>

#include "Watcher.h"
#include "WatchingService.h"

using namespace std;

/**
 * @brief Creates a new watching service with an empty watcher set.
 */
WatchingService::WatchingService()
	: fWatchers()
{
}

/**
 * @brief Destroys the watching service and deletes all registered watchers.
 */
WatchingService::~WatchingService()
{
	// delete the watchers
	for (watcher_map::iterator it = fWatchers.begin();
		 it != fWatchers.end();
		 ++it) {
		delete it->second;
	}
}

/**
 * @brief Registers a watcher, taking ownership.
 *
 * If a watcher with the same target already exists, it is removed and deleted
 * before the new one is added.
 *
 * @param watcher The watcher to register. Ownership transfers to the service.
 * @return @c true on success, @c false if @a watcher is NULL.
 */
bool
WatchingService::AddWatcher(Watcher *watcher)
{
	bool result = (watcher);
	if (result) {
		RemoveWatcher(watcher->Target(), true);
		fWatchers[watcher->Target()] = watcher;
	}
	return result;
}

/**
 * @brief Creates and registers a new watcher for the given target.
 *
 * Allocates a new Watcher with @a target and adds it to the service. Any
 * existing watcher with the same target is replaced.
 *
 * @param target The BMessenger identifying the watcher's target.
 * @return @c true on success, @c false on allocation failure.
 */
bool
WatchingService::AddWatcher(const BMessenger &target)
{
	return AddWatcher(new(nothrow) Watcher(target));
}

/**
 * @brief Unregisters a watcher by pointer and optionally deletes it.
 *
 * If @a deleteWatcher is @c false, ownership transfers to the caller.
 *
 * @param watcher       The watcher to unregister.
 * @param deleteWatcher If @c true, the watcher is deleted after removal.
 * @return @c true if the watcher was found and removed, @c false otherwise.
 */
bool
WatchingService::RemoveWatcher(Watcher *watcher, bool deleteWatcher)
{
	watcher_map::iterator it = fWatchers.find(watcher->Target());
	bool result = (it != fWatchers.end() && it->second == watcher);
	if (result) {
		if (deleteWatcher)
			delete it->second;
		fWatchers.erase(it);
	}
	return result;
}

/**
 * @brief Unregisters a watcher identified by its target messenger.
 *
 * If @a deleteWatcher is @c false, ownership transfers to the caller.
 *
 * @param target        The BMessenger identifying the watcher to remove.
 * @param deleteWatcher If @c true, the watcher is deleted after removal.
 * @return @c true if a matching watcher was found and removed, @c false
 *         otherwise.
 */
bool
WatchingService::RemoveWatcher(const BMessenger &target, bool deleteWatcher)
{
	watcher_map::iterator it = fWatchers.find(target);
	bool result = (it != fWatchers.end());
	if (result) {
		if (deleteWatcher)
			delete it->second;
		fWatchers.erase(it);
	}
	return result;
}

/**
 * @brief Sends a notification message to all watchers that pass the filter.
 *
 * If @a filter is NULL, the message is sent to every registered watcher.
 * Watchers whose targets have become invalid are automatically removed and
 * deleted after delivery.
 *
 * @param message The message to deliver.
 * @param filter  Optional filter selecting which watchers receive the message;
 *                may be NULL.
 */
void
WatchingService::NotifyWatchers(BMessage *message, WatcherFilter *filter)
{
	if (message) {
		BList staleWatchers;
		// deliver the message
		for (watcher_map::iterator it = fWatchers.begin();
			 it != fWatchers.end();
			 ++it) {
			Watcher *watcher = it->second;
// TODO: If a watcher is invalid, but the filter never selects it, it will
// not be removed.
			if (!filter || filter->Filter(watcher, message)) {
				status_t error = watcher->SendMessage(message);
				if (error != B_OK && !watcher->Target().IsValid())
					staleWatchers.AddItem(watcher);
			}
		}
		// remove the stale watchers
		for (int32 i = 0;
			 Watcher *watcher = (Watcher*)staleWatchers.ItemAt(i);
			 i++) {
			RemoveWatcher(watcher, true);
		}
	}
}

