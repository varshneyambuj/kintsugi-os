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
 *   Copyright 2001-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file BitmapManager.cpp
    @brief Singleton manager responsible for creating, tracking, and destroying server-side bitmaps. */


/*!	Whenever a ServerBitmap associated with a client-side BBitmap needs to be
	created or destroyed, the BitmapManager needs to handle it. It takes care of
	all memory management related to them.
*/


#include "BitmapManager.h"

#include "ClientMemoryAllocator.h"
#include "HWInterface.h"
#include "Overlay.h"
#include "ServerApp.h"
#include "ServerBitmap.h"
#include "ServerProtocol.h"
#include "ServerTokenSpace.h"

#include <BitmapPrivate.h>
#include <ObjectList.h>
#include <video_overlay.h>

#include <AppDefs.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Message.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

using std::nothrow;


/** @brief The global BitmapManager instance created by the AppServer. */
BitmapManager *gBitmapManager = NULL;


/** @brief Comparison function used to sort ServerApp pointers in the overlay app list.
    @param a First ServerApp pointer.
    @param b Second ServerApp pointer.
    @return Negative, zero, or positive depending on pointer ordering. */
int
compare_app_pointer(const ServerApp* a, const ServerApp* b)
{
	return (addr_t)a - (addr_t)b;
}


//	#pragma mark -


/** @brief Constructor. Initialises internal data structures ready to allocate bitmaps. */
BitmapManager::BitmapManager()
	:
	fBitmapList(1024),
	fLock("BitmapManager Lock")
{
}


/** @brief Destructor. Deallocates all ServerBitmaps still tracked by the manager. */
BitmapManager::~BitmapManager()
{
	int32 count = fBitmapList.CountItems();
	for (int32 i = 0; i < count; i++)
		delete (ServerBitmap*)fBitmapList.ItemAt(i);
}


/** @brief Allocates a new ServerBitmap and registers it with the token space.
    @param allocator        Client memory allocator to use, or NULL for server-heap allocation.
    @param hwInterface      Hardware interface used for overlay queries.
    @param bounds           Pixel dimensions of the new bitmap.
    @param space            Color space of the new bitmap.
    @param flags            Bitmap flags as defined in Bitmap.h.
    @param bytesPerRow      Bytes per row, or -1 for the default.
    @param screen           Screen id (currently unused).
    @param _allocationFlags Optional pointer that receives allocation type flags on success.
    @return A new ServerBitmap on success, or NULL if allocation failed. */
ServerBitmap*
BitmapManager::CreateBitmap(ClientMemoryAllocator* allocator,
	HWInterface& hwInterface, BRect bounds, color_space space, uint32 flags,
	int32 bytesPerRow, int32 screen, uint8* _allocationFlags)
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return NULL;

	overlay_token overlayToken = NULL;

	if (flags & B_BITMAP_WILL_OVERLAY) {
		if (!hwInterface.CheckOverlayRestrictions(bounds.IntegerWidth() + 1,
				bounds.IntegerHeight() + 1, space))
			return NULL;

		if (flags & B_BITMAP_RESERVE_OVERLAY_CHANNEL) {
			overlayToken = hwInterface.AcquireOverlayChannel();
			if (overlayToken == NULL)
				return NULL;
		}
	}

	ServerBitmap* bitmap = new(std::nothrow) ServerBitmap(bounds, space, flags,
		bytesPerRow);
	if (bitmap == NULL) {
		if (overlayToken != NULL)
			hwInterface.ReleaseOverlayChannel(overlayToken);

		return NULL;
	}

	uint8* buffer = NULL;

	if (flags & B_BITMAP_WILL_OVERLAY) {
		Overlay* overlay = new(std::nothrow) Overlay(hwInterface, bitmap,
			overlayToken);

		overlay_client_data* clientData = NULL;

		if (overlay != NULL && overlay->InitCheck() == B_OK) {
			// allocate client memory to communicate the overlay semaphore
			// and buffer location to the BBitmap
			clientData = (overlay_client_data*)bitmap->fClientMemory.Allocate(
				allocator, sizeof(overlay_client_data));
		}

		if (clientData != NULL) {
			overlay->SetClientData(clientData);

			bitmap->fMemory = &bitmap->fClientMemory;
			bitmap->SetOverlay(overlay);
			bitmap->fBytesPerRow = overlay->OverlayBuffer()->bytes_per_row;

			buffer = (uint8*)overlay->OverlayBuffer()->buffer;
			if (_allocationFlags)
				*_allocationFlags = kFramebuffer;
		} else
			delete overlay;
	} else if (allocator != NULL) {
		// standard bitmaps
		buffer = (uint8*)bitmap->fClientMemory.Allocate(allocator,
			bitmap->BitsLength());
		if (buffer != NULL) {
			bitmap->fMemory = &bitmap->fClientMemory;

			if (_allocationFlags)
				*_allocationFlags = kAllocator;
		}
	} else {
		// server side only bitmaps
		buffer = (uint8*)malloc(bitmap->BitsLength());
		if (buffer != NULL) {
			bitmap->fMemory = NULL;

			if (_allocationFlags)
				*_allocationFlags = kHeap;
		}
	}

	bool success = false;
	if (buffer != NULL) {
		success = fBitmapList.AddItem(bitmap);
		if (success && bitmap->Overlay() != NULL) {
			success = fOverlays.AddItem(bitmap);
			if (!success)
				fBitmapList.RemoveItem(bitmap);
		}
	}

	if (success) {
		bitmap->fBuffer = buffer;
		bitmap->fToken = gTokenSpace.NewToken(kBitmapToken, bitmap);
		// NOTE: the client handles clearing to white in case the flags
		// indicate this is needed
	} else {
		// Allocation failed for buffer or bitmap list
		free(buffer);
		delete bitmap;
		bitmap = NULL;
	}

	return bitmap;
}


/** @brief Creates a ServerBitmap by cloning an area from a client process.
    @param clientArea  The area_id of the client-side memory area to clone.
    @param areaOffset  Byte offset within the client area where the bitmap data begins.
    @param bounds      Pixel dimensions of the bitmap.
    @param space       Color space of the bitmap.
    @param flags       Bitmap flags as defined in Bitmap.h.
    @param bytesPerRow Bytes per row.
    @return A new ServerBitmap on success, or NULL if cloning failed. */
ServerBitmap*
BitmapManager::CloneFromClient(area_id clientArea, int32 areaOffset,
	BRect bounds, color_space space, uint32 flags, int32 bytesPerRow)
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return NULL;
	BReference<ServerBitmap> bitmap(new(std::nothrow) ServerBitmap(bounds, space, flags,
		bytesPerRow), true);
	if (bitmap == NULL)
		return NULL;

	ClonedAreaMemory* memory = new(std::nothrow) ClonedAreaMemory;
	if (memory == NULL) {
		return NULL;
	}
	int8* buffer = (int8*)memory->Clone(clientArea, areaOffset);
	if (buffer == NULL) {
		delete memory;
		return NULL;
	}

	bitmap->fMemory = memory;
	bitmap->fBuffer = memory->Address();
	bitmap->fToken = gTokenSpace.NewToken(kBitmapToken, bitmap);
	return bitmap.Detach();
}


/** @brief Called when a ServerBitmap is being deleted; removes it from the manager's tracking.
    @param bitmap Pointer to the ServerBitmap that is being removed. */
void
BitmapManager::BitmapRemoved(ServerBitmap* bitmap)
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	gTokenSpace.RemoveToken(bitmap->Token());

	if (bitmap->Overlay() != NULL)
		fOverlays.RemoveItem(bitmap);

	fBitmapList.RemoveItem(bitmap);
}


/** @brief Suspends all hardware overlays and notifies owning applications to release their locks. */
void
BitmapManager::SuspendOverlays()
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	// first, tell all applications owning an overlay to release their locks

	BObjectList<ServerApp> apps;
	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		apps.BinaryInsert(bitmap->Owner(), &compare_app_pointer);
	}
	for (int32 i = 0; i < apps.CountItems(); i++) {
		BMessage notify(B_RELEASE_OVERLAY_LOCK);
		apps.ItemAt(i)->SendMessageToClient(&notify);
	}

	// suspend overlays

	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		bitmap->Overlay()->Suspend(bitmap, false);
	}
}


/** @brief Resumes all previously suspended hardware overlays and notifies owning applications. */
void
BitmapManager::ResumeOverlays()
{
	BAutolock locker(fLock);
	if (!locker.IsLocked())
		return;

	// first, tell all applications owning an overlay that
	// they can reacquire their locks

	BObjectList<ServerApp> apps;
	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);
		apps.BinaryInsert(bitmap->Owner(), &compare_app_pointer);
	}
	for (int32 i = 0; i < apps.CountItems(); i++) {
		BMessage notify(B_RELEASE_OVERLAY_LOCK);
		apps.ItemAt(i)->SendMessageToClient(&notify);
	}

	// resume overlays

	for (int32 i = 0; i < fOverlays.CountItems(); i++) {
		ServerBitmap* bitmap = (ServerBitmap*)fOverlays.ItemAt(i);

		bitmap->Overlay()->Resume(bitmap);
	}
}
