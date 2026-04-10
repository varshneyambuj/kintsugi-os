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


static const bigtime_t kNetworkUpdateInterval = 1000000;
	// Update network availability every second

static BLocker sLocker("network watcher");
static NetworkWatcher* sWatcher;

static bool sLastNetworkAvailable;
static bigtime_t sLastNetworkUpdate;


NetworkListener::~NetworkListener()
{
}


// #pragma mark -


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


NetworkWatcher::~NetworkWatcher()
{
	if (be_app->Lock()) {
		stop_watching_network(this);

		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
}


void
NetworkWatcher::AddListener(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.AddItem(listener);

	if (fListeners.CountItems() == 1)
		UpdateAvailability();
}


void
NetworkWatcher::RemoveListener(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.RemoveItem(listener);
}


int32
NetworkWatcher::CountListeners() const
{
	BAutolock lock(sLocker);
	return fListeners.CountItems();
}


void
NetworkWatcher::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NETWORK_MONITOR:
			UpdateAvailability();
			break;
	}
}


/*static*/ void
NetworkWatcher::Register(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	if (sWatcher == NULL)
		sWatcher = new NetworkWatcher();

	sWatcher->AddListener(listener);
}


/*static*/ void
NetworkWatcher::Unregister(NetworkListener* listener)
{
	BAutolock lock(sLocker);
	sWatcher->RemoveListener(listener);

	if (sWatcher->CountListeners() == 0)
		delete sWatcher;
}


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
