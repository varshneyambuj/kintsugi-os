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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BitmapDrawingEngine.cpp
 * @brief DrawingEngine that renders into an off-screen UtilityBitmap.
 *
 * BitmapDrawingEngine layers a DrawingEngine on top of an internal
 * BitmapHWInterface so that BBitmap drawing, alpha-mask rasterisation, and
 * other off-screen work can use the regular Painter pipeline without
 * touching the screen's framebuffer.
 */


#include "BitmapDrawingEngine.h"
#include "BitmapHWInterface.h"
#include "ServerBitmap.h"
#include <new>


/**
 * @brief Constructs an off-screen engine that will allocate bitmaps in @a colorSpace.
 *
 * No bitmap is allocated up front; the first SetSize() call lazily creates
 * the backing UtilityBitmap and BitmapHWInterface.
 *
 * @param colorSpace Color space for any bitmap allocated by SetSize().
 */
BitmapDrawingEngine::BitmapDrawingEngine(color_space colorSpace)
	:	DrawingEngine(),
		fColorSpace(colorSpace),
		fHWInterface(NULL),
		fBitmap(NULL)
{
}


/**
 * @brief Destructor; releases the backing bitmap and HWInterface via SetSize(0, 0).
 */
BitmapDrawingEngine::~BitmapDrawingEngine()
{
	SetSize(0, 0);
}


#if DEBUG
/**
 * @brief Always returns true; the engine owns its own non-shared HWInterface.
 *
 * @return Always true.
 * @note   Debug-only override: parallel locking is irrelevant because no
 *         other engine shares this Painter / HWInterface pair.
 */
bool
BitmapDrawingEngine::IsParallelAccessLocked() const
{
	// We don't share the HWInterface instance that the Painter is
	// attached to, so we never need to be locked.
	return true;
}
#endif


/**
 * @brief Always returns true; the engine owns its own non-shared HWInterface.
 *
 * @return Always true.
 * @see    IsParallelAccessLocked()
 */
bool
BitmapDrawingEngine::IsExclusiveAccessLocked() const
{
	// See IsParallelAccessLocked().
	return true;
}


/**
 * @brief Resizes (or grows) the backing bitmap to at least the requested dimensions.
 *
 * Reuses the existing bitmap when it is already large enough. Otherwise
 * tears down the old HWInterface and bitmap, allocates a fresh
 * UtilityBitmap of the requested size, builds a new BitmapHWInterface,
 * and rebinds the Painter to it.
 *
 * @param newWidth  Desired width in pixels; pass 0 to release resources.
 * @param newHeight Desired height in pixels; pass 0 to release resources.
 * @retval B_OK         Resize succeeded (or no resize was needed).
 * @retval B_NO_MEMORY  Bitmap or HWInterface allocation failed.
 * @return Other        Errors propagated from the new HWInterface's Initialize().
 */
status_t
BitmapDrawingEngine::SetSize(int32 newWidth, int32 newHeight)
{
	if (fBitmap != NULL && newWidth > 0 && newHeight > 0
		&& fBitmap->Bounds().IntegerWidth() >= newWidth
		&& fBitmap->Bounds().IntegerHeight() >= newHeight) {
		return B_OK;
	}

	SetHWInterface(NULL);
	if (fHWInterface.IsSet()) {
		fHWInterface->LockExclusiveAccess();
		fHWInterface->Shutdown();
		fHWInterface->UnlockExclusiveAccess();
		fHWInterface.Unset();
	}

	if (newWidth <= 0 || newHeight <= 0)
		return B_OK;

	fBitmap.SetTo(new(std::nothrow) UtilityBitmap(BRect(0, 0, newWidth - 1,
		newHeight - 1), fColorSpace, 0));
	if (!fBitmap.IsSet())
		return B_NO_MEMORY;

	fHWInterface.SetTo(new(std::nothrow) BitmapHWInterface(fBitmap));
	if (!fHWInterface.IsSet())
		return B_NO_MEMORY;

	status_t result = fHWInterface->Initialize();
	if (result != B_OK)
		return result;

	// we have to set a valid clipping first
	fClipping.Set(fBitmap->Bounds());
	ConstrainClippingRegion(&fClipping);
	SetHWInterface(fHWInterface.Get());
	return B_OK;
}


/**
 * @brief Returns a fresh UtilityBitmap with the engine's contents copied in.
 *
 * Allocates a new UtilityBitmap of size (@a width, @a height) in @a space
 * and imports the current backing bitmap's pixels into it. Color-space
 * conversion is handled by ImportBits().
 *
 * @param width  Width of the exported bitmap in pixels.
 * @param height Height of the exported bitmap in pixels.
 * @param space  Color space for the exported bitmap.
 * @return New bitmap (caller owns), or NULL on bad arguments / allocation failure.
 */
UtilityBitmap*
BitmapDrawingEngine::ExportToBitmap(int32 width, int32 height,
	color_space space)
{
	if (width <= 0 || height <= 0)
		return NULL;

	UtilityBitmap *result = new(std::nothrow) UtilityBitmap(BRect(0, 0,
		width - 1, height - 1), space, 0);
	if (result == NULL)
		return NULL;

	if (result->ImportBits(fBitmap->Bits(), fBitmap->BitsLength(),
		fBitmap->BytesPerRow(), fBitmap->ColorSpace()) != B_OK) {
		delete result;
		return NULL;
	}

	return result;
}
