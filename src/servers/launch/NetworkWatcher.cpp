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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file NetworkWatcher.cpp
 *  @brief Implements network availability monitoring and listener notification for launch conditions and events. */


#include "NetworkWatcher.h"

#include <Application.h>
#include <Autolock.h>
#include <NetworkDevice.h>
#include <NetworkInterface.h>
#include <NetworkRoster.h>

#include "Utility.h"


/** @brief Minimum interval between network availability re-checks (one second). */
static const bigtime_t kNetworkUpdateInterval = 1000000;
	// Update network availability every second

/** @brief Global lock protecting the shared NetworkWatcher singleton and listener list. */
static BLocker sLocker("network watcher");
/** @brief Singleton NetworkWatcher instance; created on first Register() call. */
static NetworkWatcher* sWatcher;

/** @brief Cached result of the last network availability check. */
static bool sLastNetworkAvailable;
/** @brief Timestamp of the last network availability check. */
static bigtime_t sLastNetworkUpdate;


/** @brief Destructor for the NetworkListener interface. */
NetworkListener::~NetworkListener()
{
}


// #pragma mark -


/**
 * @brief Constructs the network watcher and begins monitoring interface and link changes.
 *
 * Registers itself as a BHandler with the application and subscribes to
 * B_WATCH_NETWORK_INTERFACE_CHANGES and B_WATCH_NETWORK_LINK_CHANGES events.
 */
NetworkWatcher::NetworkWatcher()
	:
	BHandler("network watcher"),
	fAvailable(false)
{
	if (be_app->Lock()) {
		be_app->AddHandler(this);

		start_watching_network(B_WATCH_NETWORK_INTERFACE_CHANGES
			| B_WATCH_NETWORK_LINK_CHANGES, this);
		be_app->Unlock();
	}
}


/**
 * @brief Destroys the network watcher and stops all network monitoring.
 *
 * Stops network event watching and removes itself from the application's
 * handler list.
 */
NetworkWatcher::~NetworkWatcher()
{
	if (be_app->Lock()) {
		stop_watching_network(this);

		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
}


/**
 * @brief Adds a listener to be notified of network availability changes.
 *
 * When the first listener is added, an immediate availability check is
 * performed so the listener receives the current state.
 *
 * @param listener The NetworkListener to add.
 */
void
NetworkWatcher::AddListener(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.AddItem(listener);

	if (fListeners.CountItems() == 1)
		UpdateAvailability();
}


/**
 * @brief Removes a previously added network listener.
 *
 * @param listener The NetworkListener to remove.
 */
void
NetworkWatcher::RemoveListener(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.RemoveItem(listener);
}


/**
 * @brief Returns the number of currently registered network listeners.
 *
 * @return The listener count.
 */
int32
NetworkWatcher::CountListeners() const
{
	BAutolock lock(sLocker);
	return fListeners.CountItems();
}


/**
 * @brief Handles incoming network monitor messages by re-checking availability.
 *
 * @param message The BMessage received from the network monitor.
 */
void
NetworkWatcher::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NETWORK_MONITOR:
			UpdateAvailability();
			break;
	}
}


/**
 * @brief Registers a network listener, creating the singleton watcher if needed.
 *
 * @param listener The NetworkListener to register.
 */
/*static*/ void
NetworkWatcher::Register(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	if (sWatcher == NULL)
		sWatcher = new NetworkWatcher();

	sWatcher->AddListener(listener);
}


/**
 * @brief Unregisters a network listener, destroying the singleton when the last listener is removed.
 *
 * @param listener The NetworkListener to unregister.
 */
/*static*/ void
NetworkWatcher::Unregister(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	sWatcher->RemoveListener(listener);

	if (sWatcher->CountListeners() == 0)
		delete sWatcher;
}


/**
 * @brief Queries whether a usable (non-loopback, linked, up) network interface exists.
 *
 * Unless @a immediate is true, a cached result is returned if the last check
 * occurred less than kNetworkUpdateInterval ago. Otherwise, all network
 * interfaces are enumerated to find one that is up and has a link.
 *
 * @param immediate If @c true, bypass the cache and check immediately.
 * @return @c true if at least one qualifying interface is available.
 */
/*static*/ bool
NetworkWatcher::NetworkAvailable(bool immediate)
{
	if (!immediate
		&& system_time() - sLastNetworkUpdate < kNetworkUpdateInterval) {
		return sLastNetworkAvailable;
	}

	bool isAvailable = false;

	BNetworkRoster& roster = BNetworkRoster::Default();
	BNetworkInterface interface;
	uint32 cookie = 0;
	while (roster.GetNextInterface(&cookie, interface) == B_OK) {
		uint32 flags = interface.Flags();
		if ((flags & (IFF_LOOPBACK | IFF_CONFIGURING | IFF_UP | IFF_LINK))
				== (IFF_UP | IFF_LINK)) {
			isAvailable = true;
			break;
		}
	}

	sLastNetworkAvailable = isAvailable;
	sLastNetworkUpdate = system_time();
	return isAvailable;
}


/**
 * @brief Re-checks network availability and notifies listeners if the state changed.
 *
 * Calls NetworkAvailable(true) and, if the result differs from the last
 * known state, notifies all registered listeners.
 */
void
NetworkWatcher::UpdateAvailability()
{
	bool isAvailable = NetworkAvailable(true);
	if (isAvailable != fAvailable) {
		fAvailable = isAvailable;

		BAutolock lock(sLocker);
		for (int32 i = 0; i < fListeners.CountItems(); i++) {
			fListeners.ItemAt(i)->NetworkAvailabilityChanged(fAvailable);
		}
	}
}
