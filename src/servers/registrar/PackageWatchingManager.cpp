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
   Copyright 2014, Ingo Weinhold, ingo_weinhold@gmx.de.
   Distributed under the terms of the MIT License.
 */

/** @file PackageWatchingManager.cpp
 *  @brief Manages package installation/removal watchers and broadcasts notifications. */
#include "PackageWatchingManager.h"

#include <new>

#include <package/PackageRoster.h>

#include <RegistrarDefs.h>

#include "Debug.h"
#include "EventMaskWatcher.h"


using namespace BPackageKit;
using namespace BPrivate;


PackageWatchingManager::PackageWatchingManager()
{
}


PackageWatchingManager::~PackageWatchingManager()
{
}


void
PackageWatchingManager::HandleStartStopWatching(BMessage* request)
{
	status_t error = request->what == B_REG_PACKAGE_START_WATCHING
		? _AddWatcher(request) : _RemoveWatcher(request);

	if (error == B_OK) {
		BMessage reply(B_REG_SUCCESS);
		request->SendReply(&reply);
	} else {
		BMessage reply(B_REG_ERROR);
		reply.AddInt32("error", error);
		request->SendReply(&reply);
	}
}


void
PackageWatchingManager::NotifyWatchers(BMessage* message)
{
	int32 event;
	if (message->FindInt32("event", &event) != B_OK) {
		WARNING("No event field in notification message\n");
		return;
	}

	uint32 eventMask;
	switch (event) {
		case B_INSTALLATION_LOCATION_PACKAGES_CHANGED:
			eventMask = B_WATCH_PACKAGE_INSTALLATION_LOCATIONS;
			break;
		default:
			WARNING("Invalid event: %" B_PRId32 "\n", event);
			return;
	}

	EventMaskWatcherFilter filter(eventMask);
    fWatchingService.NotifyWatchers(message, &filter);
}


status_t
PackageWatchingManager::_AddWatcher(const BMessage* request)
{
	BMessenger target;
	uint32 eventMask;
	status_t error;
	if ((error = request->FindMessenger("target", &target)) != B_OK
		|| (error = request->FindUInt32("events", &eventMask)) != B_OK) {
		return error;
	}

	Watcher* watcher = new(std::nothrow) EventMaskWatcher(target, eventMask);
	if (watcher == NULL || !fWatchingService.AddWatcher(watcher)) {
		delete watcher;
		return B_NO_MEMORY;
	}

	return B_OK;
}


status_t
PackageWatchingManager::_RemoveWatcher(const BMessage* request)
{
	BMessenger target;
	status_t error;
	if ((error = request->FindMessenger("target", &target)) != B_OK)
		return error;

	if (!fWatchingService.RemoveWatcher(target))
		return B_BAD_VALUE;

	return B_OK;
}
