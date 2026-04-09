/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2001-2009, Haiku.
 * Original author: DarkWyrm <bpmagic@columbus.rr.com>.
 */

/** @file BitmapManager.h
    @brief Manages allocation and lifetime of all server-side bitmaps. */

#ifndef BITMAP_MANAGER_H
#define BITMAP_MANAGER_H


#include <GraphicsDefs.h>
#include <List.h>
#include <Locker.h>
#include <OS.h>
#include <Rect.h>


class ClientMemoryAllocator;
class HWInterface;
class ServerBitmap;


/** @brief Central registry that creates, tracks, and destroys ServerBitmap objects,
           including overlay bitmaps backed by hardware. */
class BitmapManager {
public:
								BitmapManager();
	virtual						~BitmapManager();

			/** @brief Allocates a new ServerBitmap in client-accessible memory.
			    @param allocator The client memory allocator to use.
			    @param hwInterface Hardware interface for overlay support.
			    @param bounds Dimensions of the bitmap.
			    @param space Pixel color space.
			    @param flags Bitmap creation flags.
			    @param bytesPerRow Row stride in bytes, or -1 to compute automatically.
			    @param screen Screen ID, defaults to B_MAIN_SCREEN_ID.
			    @param _allocationFlags Optional output flags describing the allocation.
			    @return Pointer to the new ServerBitmap, or NULL on failure. */
			ServerBitmap*		CreateBitmap(ClientMemoryAllocator* allocator,
									HWInterface& hwInterface, BRect bounds,
									color_space space, uint32 flags,
									int32 bytesPerRow = -1,
									int32 screen = B_MAIN_SCREEN_ID.id,
									uint8* _allocationFlags = NULL);

			/** @brief Creates a ServerBitmap by cloning an area shared by the client.
			    @param clientArea The area_id of the client-owned memory.
			    @param areaOffset Byte offset into the client area.
			    @param bounds Dimensions of the bitmap.
			    @param space Pixel color space.
			    @param flags Bitmap creation flags.
			    @param bytesPerRow Row stride in bytes, or -1 to compute automatically.
			    @return Pointer to the cloned ServerBitmap, or NULL on failure. */
			ServerBitmap*		CloneFromClient(area_id clientArea,
									int32 areaOffset, BRect bounds,
									color_space space, uint32 flags,
									int32 bytesPerRow = -1);

			/** @brief Unregisters a bitmap from the manager and frees its resources.
			    @param bitmap The ServerBitmap to remove. */
			void				BitmapRemoved(ServerBitmap* bitmap);

			/** @brief Suspends all hardware overlay bitmaps (e.g. during a mode change). */
			void				SuspendOverlays();

			/** @brief Resumes all hardware overlay bitmaps after suspension. */
			void				ResumeOverlays();

protected:
			BList				fBitmapList;
			BList				fOverlays;
			BLocker				fLock;
};

/** @brief Global BitmapManager instance. */
extern BitmapManager* gBitmapManager;

#endif	/* BITMAP_MANAGER_H */
