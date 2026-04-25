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
 * MIT License.
 */

/** @file BitmapDrawingEngine.h
    @brief DrawingEngine subclass that renders into an in-memory UtilityBitmap. */

#ifndef BITMAP_DRAWING_ENGINE_H
#define BITMAP_DRAWING_ENGINE_H

#include "DrawingEngine.h"

#include <AutoDeleter.h>
#include <Referenceable.h>
#include <Region.h>

class BitmapHWInterface;
class UtilityBitmap;


/** @brief A DrawingEngine that targets an off-screen UtilityBitmap (B_RGB32 by default).
 *
 * BitmapDrawingEngine is used for drawing operations that should not hit the
 * screen, for example BBitmap rendering and rasterising shapes into AlphaMask
 * bitmaps. It owns its own BitmapHWInterface so it never contends for the real
 * frame buffer's lock.
 */
class BitmapDrawingEngine : public DrawingEngine {
public:
	/** @brief Constructs an engine that will allocate bitmaps in @a colorSpace. */
								BitmapDrawingEngine(
									color_space colorSpace = B_RGB32);
virtual							~BitmapDrawingEngine();

#if DEBUG
	/** @brief Always returns true; the engine has its own non-shared HWInterface. */
	virtual	bool				IsParallelAccessLocked() const;
#endif
	/** @brief Always returns true; the engine has its own non-shared HWInterface. */
	virtual	bool				IsExclusiveAccessLocked() const;

	/** @brief Resizes (or grows) the backing bitmap to at least @a newWidth x @a newHeight.
	 *  @return B_OK on success, B_NO_MEMORY when allocation fails. */
			status_t			SetSize(int32 newWidth, int32 newHeight);
	/** @brief Returns a freshly allocated UtilityBitmap with the engine's contents copied in.
	 *  @return New bitmap (caller owns) or NULL on failure. */
			UtilityBitmap*		ExportToBitmap(int32 width, int32 height,
									color_space space);

private:
			color_space			fColorSpace;
			ObjectDeleter<BitmapHWInterface>
								fHWInterface;
			BReference<UtilityBitmap>
								fBitmap;
			BRegion				fClipping;
};

#endif // BITMAP_DRAWING_ENGINE_H
