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


/** @brief Constructs the PackageWatchingManager with default state. */
PackageWatchingManager::PackageWatchingManager()
{
}


/** @brief Destroys the PackageWatchingManager. */
PackageWatchingManager::~PackageWatchingManager()
{
}


/**
 * @brief Handles a start-watching or stop-watching request for package events.
 *
 * Dispatches to _AddWatcher() or _RemoveWatcher() depending on the message's
 * what code, then sends a success or error reply.
 *
 * @param request The incoming B_REG_PACKAGE_START_WATCHING or
 *                B_REG_PACKAGE_STOP_WATCHING request message.
 */
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


/**
 * @brief Broadcasts a package event notification to all matching watchers.
 *
 * Reads the "event" field from the message, maps it to an event mask, and
 * uses the watching service to deliver the message to watchers whose masks
 * include that event.
 *
 * @param message The notification message containing an "event" int32 field.
 */
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


/**
 * @brief Registers a new package event watcher.
 *
 * Extracts the "target" messenger and "events" mask from the request and
 * creates an EventMaskWatcher in the watching service.
 *
 * @param request The request message containing "target" and "events" fields.
 * @return @c B_OK on success, @c B_NO_MEMORY if allocation fails, or another
 *         error code if required fields are missing.
 */
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


/**
 * @brief Unregisters an existing package event watcher.
 *
 * Extracts the "target" messenger from the request and removes the
 * corresponding watcher from the watching service.
 *
 * @param request The request message containing the "target" field.
 * @return @c B_OK on success, @c B_BAD_VALUE if the target is not found,
 *         or another error if the field is missing.
 */
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
