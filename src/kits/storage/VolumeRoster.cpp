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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file VolumeRoster.cpp
 * @brief Implementation of BVolumeRoster for enumerating mounted volumes.
 *
 * BVolumeRoster provides an iterator over the set of currently mounted
 * filesystem volumes, a direct accessor for the boot volume, and an optional
 * volume-change watching service. Watching is implemented via the node monitor
 * B_WATCH_MOUNT flag and delivers mount/unmount notifications to a BMessenger.
 *
 * @see BVolumeRoster
 */


#include <errno.h>
#include <new>

#include <Bitmap.h>
#include <Directory.h>
#include <fs_info.h>
#include <Node.h>
#include <NodeMonitor.h>
#include <VolumeRoster.h>


static const char kBootVolumePath[] = "/boot";

using namespace std;


#ifdef USE_OPENBEOS_NAMESPACE
namespace OpenBeOS {
#endif


/**
 * @brief Default constructor; initializes the roster with iteration cookie at 0.
 */
BVolumeRoster::BVolumeRoster()
	: fCookie(0),
	  fTarget(NULL)
{
}


/**
 * @brief Destructor; stops any active volume watching and frees resources.
 */
BVolumeRoster::~BVolumeRoster()
{
	StopWatching();
}


/**
 * @brief Fills out volume with the next available mounted volume.
 *
 * Iterates through all mounted volumes in device-ID order. Returns
 * B_BAD_VALUE if volume is NULL, or a negative error code when the list
 * is exhausted.
 *
 * @param volume Pointer to a BVolume to be initialized with the next volume.
 * @return B_OK on success, B_BAD_VALUE if volume is NULL, or an error code
 *         when iteration is exhausted.
 */
status_t
BVolumeRoster::GetNextVolume(BVolume *volume)
{
	// check parameter
	status_t error = (volume ? B_OK : B_BAD_VALUE);
	// get next device
	dev_t device;
	if (error == B_OK) {
		device = next_dev(&fCookie);
		if (device < 0)
			error = device;
	}
	// init volume
	if (error == B_OK)
		error = volume->SetTo(device);
	return error;
}


/**
 * @brief Rewinds the volume iterator back to the first mounted volume.
 */
void
BVolumeRoster::Rewind()
{
	fCookie = 0;
}


/**
 * @brief Fills out volume with the boot volume.
 *
 * @param volume Pointer to a BVolume to be initialized to the boot volume.
 * @return B_OK on success, B_BAD_VALUE if volume is NULL, or an error code on failure.
 */
status_t
BVolumeRoster::GetBootVolume(BVolume *volume)
{
	// check parameter
	status_t error = (volume ? B_OK : B_BAD_VALUE);
	// get device
	dev_t device;
	if (error == B_OK) {
		device = dev_for_path(kBootVolumePath);
		if (device < 0)
			error = device;
	}
	// init volume
	if (error == B_OK)
		error = volume->SetTo(device);
	return error;
}


/**
 * @brief Starts watching for volume mount and unmount events.
 *
 * Notifications are delivered as B_NODE_MONITOR messages to messenger.
 * Any previous watch is stopped first.
 *
 * @param messenger A valid BMessenger to receive mount/unmount notifications.
 * @return B_OK on success, B_ERROR if messenger is invalid, or B_NO_MEMORY on allocation failure.
 */
status_t
BVolumeRoster::StartWatching(BMessenger messenger)
{
	StopWatching();
	status_t error = (messenger.IsValid() ? B_OK : B_ERROR);
	// clone messenger
	if (error == B_OK) {
		fTarget = new(nothrow) BMessenger(messenger);
		if (!fTarget)
			error = B_NO_MEMORY;
	}
	// start watching
	if (error == B_OK)
		error = watch_node(NULL, B_WATCH_MOUNT, messenger);
	// cleanup on failure
	if (error != B_OK && fTarget) {
		delete fTarget;
		fTarget = NULL;
	}
	return error;
}


/**
 * @brief Stops watching for volume changes started by StartWatching().
 *
 * Has no effect if watching is not currently active.
 */
void
BVolumeRoster::StopWatching()
{
	if (fTarget) {
		stop_watching(*fTarget);
		delete fTarget;
		fTarget = NULL;
	}
}


/**
 * @brief Returns the messenger currently receiving volume-change notifications.
 *
 * @return The active BMessenger, or a default-constructed (invalid) BMessenger if
 *         not currently watching.
 */
BMessenger
BVolumeRoster::Messenger() const
{
	return (fTarget ? *fTarget : BMessenger());
}


// FBC
void BVolumeRoster::_SeveredVRoster1() {}
void BVolumeRoster::_SeveredVRoster2() {}


#ifdef USE_OPENBEOS_NAMESPACE
}
#endif
