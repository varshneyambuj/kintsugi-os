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

/** @file VolumeWatcher.cpp
 *  @brief Implements volume mount/unmount monitoring and listener notification for launch events. */


#include "VolumeWatcher.h"

#include <Application.h>
#include <Autolock.h>
#include <NodeMonitor.h>


static BLocker sLocker("volume watcher");
static VolumeWatcher* sWatcher;


VolumeListener::~VolumeListener()
{
}


// #pragma mark -


VolumeWatcher::VolumeWatcher()
	:
	BHandler("volume watcher")
{
	if (be_app->Lock()) {
		be_app->AddHandler(this);

		watch_node(NULL, B_WATCH_MOUNT, this);
		be_app->Unlock();
	}
}


VolumeWatcher::~VolumeWatcher()
{
	if (be_app->Lock()) {
		stop_watching(this);

		be_app->RemoveHandler(this);
		be_app->Unlock();
	}
}


void
VolumeWatcher::AddListener(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.AddItem(listener);
}


void
VolumeWatcher::RemoveListener(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	fListeners.RemoveItem(listener);
}


int32
VolumeWatcher::CountListeners() const
{
	BAutolock lock(sLocker);
	return fListeners.CountItems();
}


void
VolumeWatcher::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			int32 opcode = message->GetInt32("opcode", -1);
			if (opcode == B_DEVICE_MOUNTED) {
				dev_t device;
				if (message->FindInt32("new device", &device) == B_OK) {
					BAutolock lock(sLocker);
					for (int32 i = 0; i < fListeners.CountItems(); i++) {
						fListeners.ItemAt(i)->VolumeMounted(device);
					}
				}
			} else if (opcode == B_DEVICE_UNMOUNTED) {
				dev_t device;
				if (message->FindInt32("device", &device) == B_OK) {
					BAutolock lock(sLocker);
					for (int32 i = 0; i < fListeners.CountItems(); i++) {
						fListeners.ItemAt(i)->VolumeUnmounted(device);
					}
				}
			}
			break;
		}
	}
}


/*static*/ void
VolumeWatcher::Register(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	if (sWatcher == NULL)
		sWatcher = new VolumeWatcher();

	sWatcher->AddListener(listener);
}


/*static*/ void
VolumeWatcher::Unregister(VolumeListener* listener)
{
	BAutolock lock(sLocker);
	sWatcher->RemoveListener(listener);

	if (sWatcher->CountListeners() == 0)
		delete sWatcher;
}
