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
 *   Copyright 2014-2015, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Adrien Destugues <pulkomandy@pulkomandy.tk>
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Julian Harnath <julian.harnath@rwth-aachen.de>
 */


/**
 * @file AlphaMask.cpp
 * @brief Implementation of the alpha clipping masks consumed by Painter.
 *
 * This translation unit implements the AlphaMask base class plus the
 * concrete UniformAlphaMask, VectorAlphaMask, PictureAlphaMask, and
 * ShapeAlphaMask subclasses. The shared logic deals with stacking masks,
 * regenerating them when the canvas grows, and combining the current mask
 * with the previous mask in the stack so the rasteriser sees a single
 * 8-bit clipping bitmap.
 */


#include "AlphaMask.h"

#include "AlphaMaskCache.h"
#include "BitmapHWInterface.h"
#include "BitmapManager.h"
#include "Canvas.h"
#include "DrawingEngine.h"
#include "PictureBoundingBoxPlayer.h"
#include "ServerBitmap.h"
#include "ServerPicture.h"
#include "Shape.h"
#include "ShapePrivate.h"

#include <AutoLocker.h>


// #pragma mark - AlphaMask


/**
 * @brief Constructs an empty mask placed on top of @a previousMask.
 *
 * Initialises every owned member to a defined state and bumps the next-mask
 * reference count of @a previousMask so the cache eviction logic does not
 * remove a mask that is still being used as a base.
 *
 * @param previousMask Lower mask in the stack (may be NULL).
 * @param inverse      Whether to interpret the mask in inverted form.
 */
AlphaMask::AlphaMask(AlphaMask* previousMask, bool inverse)
	:
	fPreviousMask(previousMask),
	fBounds(),
	fClippedToCanvas(true),
	fCanvasOrigin(),
	fCanvasBounds(),
	fInverse(inverse),
	fBackgroundOpacity(0),
	fNextMaskCount(0),
	fInCache(false),
	fIndirectCacheReferences(0),
	fBits(NULL),
	fBuffer(),
	fMask(),
	fScanline(fMask)
{
	recursive_lock_init(&fLock, "AlphaMask");

	if (previousMask != NULL)
		atomic_add(&previousMask->fNextMaskCount, 1);

	_SetOutsideOpacity();
}


/**
 * @brief Constructs a mask that shares the rendered bitmap of @a other.
 *
 * Used by AlphaMaskCache to hand out a fresh wrapper that reuses an
 * already-rendered bitmap. The new mask stacks on top of @a previousMask.
 *
 * @param previousMask Lower mask in the stack (may be NULL).
 * @param other        Donor mask whose rendered bitmap is shared.
 */
AlphaMask::AlphaMask(AlphaMask* previousMask, AlphaMask* other)
	:
	fPreviousMask(previousMask),
	fBounds(other->fBounds),
	fClippedToCanvas(other->fClippedToCanvas),
	fCanvasOrigin(other->fCanvasOrigin),
	fCanvasBounds(other->fCanvasBounds),
	fInverse(other->fInverse),
	fBackgroundOpacity(other->fBackgroundOpacity),
	fNextMaskCount(0),
	fInCache(false),
	fIndirectCacheReferences(0),
	fBits(other->fBits),
	fBuffer(other->fBuffer),
	fMask(other->fMask),
	fScanline(fMask)
{
	recursive_lock_init(&fLock, "AlphaMask");

	fMask.attach(fBuffer);

	if (previousMask != NULL)
		atomic_add(&previousMask->fNextMaskCount, 1);

	_SetOutsideOpacity();
}


/**
 * @brief Constructs a uniform mask with the given background opacity.
 *
 * The resulting mask has no shape: every pixel evaluates to
 * @a backgroundOpacity. Used as the leaf mask for B_OP_ALPHA blending where
 * a constant opacity is needed.
 *
 * @param backgroundOpacity Constant alpha applied to every pixel (0..255).
 */
AlphaMask::AlphaMask(uint8 backgroundOpacity)
	:
	fPreviousMask(),
	fBounds(),
	fClippedToCanvas(true),
	fCanvasOrigin(),
	fCanvasBounds(),
	fInverse(false),
	fBackgroundOpacity(backgroundOpacity),
	fNextMaskCount(0),
	fInCache(false),
	fIndirectCacheReferences(0),
	fBits(NULL),
	fBuffer(),
	fMask(),
	fScanline(fMask)
{
	recursive_lock_init(&fLock, "AlphaMask");

	_SetOutsideOpacity();
}


/**
 * @brief Destructor; releases the recursive lock and decrements parent counters.
 */
AlphaMask::~AlphaMask()
{
	if (fPreviousMask.IsSet())
		atomic_add(&fPreviousMask->fNextMaskCount, -1);

	recursive_lock_destroy(&fLock);
}


/**
 * @brief Records the canvas geometry, regenerating the mask if the canvas grew.
 *
 * If the canvas is now larger than when the mask was last drawn and the mask
 * was clipped to the (smaller) old canvas, the mask is rebuilt at the new
 * size. The previous canvas origin is returned so the caller can restore it.
 *
 * @param origin New canvas origin.
 * @param bounds New canvas bounds.
 * @return The canvas origin in effect before this call.
 */
IntPoint
AlphaMask::SetCanvasGeometry(IntPoint origin, IntRect bounds)
{
	RecursiveLocker locker(fLock);

	if (origin == fCanvasOrigin && bounds.Width() == fCanvasBounds.Width()
		&& bounds.Height() == fCanvasBounds.Height())
		return fCanvasOrigin;

	IntPoint oldOrigin = fCanvasOrigin;
	fCanvasOrigin = origin;
	IntRect oldBounds = fCanvasBounds;
	fCanvasBounds = IntRect(0, 0, bounds.Width(), bounds.Height());

	if (fPreviousMask != NULL)
		fPreviousMask->SetCanvasGeometry(origin, bounds);

	if (fClippedToCanvas && (fCanvasBounds.Width() > oldBounds.Width()
		|| fCanvasBounds.Height() > oldBounds.Height())) {
		// The canvas is now larger than before and we previously
		// drew the alpha mask clipped to the (old) bounds of the
		// canvas. So we now have to redraw the alpha mask with the
		// new size.
		_Generate();
	}

	_AttachMaskToBuffer();

	return oldOrigin;
}


/**
 * @brief Returns the size of the rendered mask bitmap in bytes.
 */
size_t
AlphaMask::BitmapSize() const
{
	return fBits->BitsLength();
}


/**
 * @brief Allocates a temporary B_RGBA32 bitmap pre-filled with the background opacity.
 *
 * @param bounds Bitmap dimensions.
 * @return New bitmap (caller adopts the reference) or NULL on allocation failure.
 */
ServerBitmap*
AlphaMask::_CreateTemporaryBitmap(BRect bounds) const
{
	BReference<UtilityBitmap> bitmap(new(std::nothrow) UtilityBitmap(bounds,
		B_RGBA32, 0), true);
	if (bitmap == NULL)
		return NULL;

	if (!bitmap->IsValid())
		return NULL;

	memset(bitmap->Bits(), fBackgroundOpacity, bitmap->BitsLength());

	return bitmap.Detach();
}


/**
 * @brief Rasterises the mask and combines it with the previous mask in the stack.
 *
 * Renders the source bitmap (provided by the subclass via _RenderSource()),
 * extracts its alpha channel, and multiplies it with the previous mask's
 * alpha (if any) into a fresh 8-bit UtilityBitmap. The result is then
 * attached to the AGG rendering buffer and registered with the cache.
 *
 * @note Holds the mask's recursive lock plus the previous mask's lock for
 *       the duration of the call.
 */
void
AlphaMask::_Generate()
{
	RecursiveLocker locker(fLock);
	RecursiveLocker previousLocker;
	if (fPreviousMask != NULL)
		previousLocker.SetTo(fPreviousMask->fLock, false);

	ServerBitmap* const bitmap = _RenderSource(fCanvasBounds);
	BReference<ServerBitmap> bitmapRef(bitmap, true);
	if (bitmap == NULL) {
		_SetNoClipping();
		return;
	}

	fBits.SetTo(new(std::nothrow) UtilityBitmap(fBounds, B_GRAY8, 0), true);
	if (fBits == NULL)
		return;

	const int32 width = fBits->Width();
	const int32 height = fBits->Height();
	uint8* source = bitmap->Bits();
	uint8* destination = fBits->Bits();
	uint32 numPixels = width * height;

	if (fPreviousMask != NULL) {
		uint8 previousOutsideOpacity = fPreviousMask->OutsideOpacity();

		if (fPreviousMask->fBounds.Intersects(fBounds)) {
			IntRect previousBounds(fBounds.OffsetByCopy(
				-fPreviousMask->fBounds.left, -fPreviousMask->fBounds.top));
			if (previousBounds.right > fPreviousMask->fBounds.Width())
				previousBounds.right = fPreviousMask->fBounds.Width();
			if (previousBounds.bottom > fPreviousMask->fBounds.Height())
				previousBounds.bottom = fPreviousMask->fBounds.Height();

			int32 y = previousBounds.top;

			for (; y < 0; y++) {
				for (int32 x = 0; x < width; x++) {
					*destination = (fInverse ? 255 - source[3] : source[3])
						* previousOutsideOpacity / 255;
					destination++;
					source += 4;
				}
			}

			for (; y <= previousBounds.bottom; y++) {
				int32 x = previousBounds.left;
				for (; x < 0; x++) {
					*destination = (fInverse ? 255 - source[3] : source[3])
						* previousOutsideOpacity / 255;
					destination++;
					source += 4;
				}
				uint8* previousRow = fPreviousMask->fBuffer.row_ptr(y);
				for (; x <= previousBounds.right; x++) {
					uint8 sourceAlpha = fInverse ? 255 - source[3] : source[3];
					*destination = sourceAlpha * previousRow[x] / 255;
					destination++;
					source += 4;
				}
				for (; x < previousBounds.left + width; x++) {
					*destination = (fInverse ? 255 - source[3] : source[3])
						* previousOutsideOpacity / 255;
					destination++;
					source += 4;
				}
			}

			for (; y < previousBounds.top + height; y++) {
				for (int32 x = 0; x < width; x++) {
					*destination = (fInverse ? 255 - source[3] : source[3])
						* previousOutsideOpacity / 255;
					destination++;
					source += 4;
				}
			}

		} else {
			while (numPixels--) {
				*destination = (fInverse ? 255 - source[3] : source[3])
					* previousOutsideOpacity / 255;
				destination++;
				source += 4;
			}
		}
	} else {
		while (numPixels--) {
			*destination = fInverse ? 255 - source[3] : source[3];
			destination++;
			source += 4;
		}
	}

	fBuffer.attach(fBits->Bits(), width, height, width);
	_AttachMaskToBuffer();

	_AddToCache();
}


/**
 * @brief Detaches any backing buffer so the mask becomes a no-op clip.
 */
void
AlphaMask::_SetNoClipping()
{
	fBuffer.attach(NULL, 0, 0, 0);
	_AttachMaskToBuffer();
}


/**
 * @brief Returns the bounds of the previous mask in the stack.
 *
 * @note The caller must ensure @c fPreviousMask is non-NULL.
 */
const IntRect&
AlphaMask::_PreviousMaskBounds() const
{
	return fPreviousMask->fBounds;
}


/**
 * @brief Attaches the AGG clipped-alpha-mask to the rendering buffer at the right offset.
 *
 * Computes the global pixel offset of the mask (taking into account the
 * mask's own offset, the canvas origin, and the mask bounds) and pushes it
 * into AGG along with the outside opacity.
 */
void
AlphaMask::_AttachMaskToBuffer()
{
	const IntPoint maskOffset = _Offset();
	const int32 offsetX = fBounds.left + maskOffset.x + fCanvasOrigin.x;
	const int32 offsetY = fBounds.top + maskOffset.y + fCanvasOrigin.y;

	fMask.attach(fBuffer, offsetX, offsetY, fOutsideOpacity);
}


/**
 * @brief Computes the alpha applied to pixels outside the mask bitmap.
 *
 * Combines the background opacity with the previous mask's outside opacity,
 * inverting the value when @c fInverse is set.
 */
void
AlphaMask::_SetOutsideOpacity()
{
	fOutsideOpacity = fInverse ? 255 - fBackgroundOpacity
		: fBackgroundOpacity;

	if (fPreviousMask != NULL) {
		fOutsideOpacity = fOutsideOpacity * fPreviousMask->OutsideOpacity()
			/ 255;
	}
}


// #pragma mark - UniformAlphaMask


/**
 * @brief Constructs a mask of constant @a opacity covering the whole canvas.
 *
 * @param opacity Constant alpha (0..255).
 */
UniformAlphaMask::UniformAlphaMask(uint8 opacity)
	:
	AlphaMask(opacity)
{
	fBounds.Set(0, 0, 0, 0);
	_SetNoClipping();
}


/**
 * @brief A uniform mask has no source bitmap.
 *
 * @return Always NULL.
 */
ServerBitmap*
UniformAlphaMask::_RenderSource(const IntRect&)
{
	return NULL;
}


/**
 * @brief A uniform mask has no positional offset.
 *
 * @return Always (0, 0).
 */
IntPoint
UniformAlphaMask::_Offset()
{
	return IntPoint(0, 0);
}


/**
 * @brief Uniform masks are not cached; this is a no-op.
 */
void
UniformAlphaMask::_AddToCache()
{
}


// #pragma mark - VectorAlphaMask


/**
 * @brief Constructs a vector mask placed at @a where.
 *
 * @param previousMask Lower mask in the stack (may be NULL).
 * @param where        Position offset applied to the rasterised vectors.
 * @param inverse      Whether to invert the mask alpha after rasterising.
 */
template<class VectorMaskType>
VectorAlphaMask<VectorMaskType>::VectorAlphaMask(AlphaMask* previousMask,
	BPoint where, bool inverse)
	:
	AlphaMask(previousMask, inverse),
	fWhere(where)
{
}


/**
 * @brief Constructs a vector mask sharing the rendered bitmap of @a other.
 */
template<class VectorMaskType>
VectorAlphaMask<VectorMaskType>::VectorAlphaMask(AlphaMask* previousMask,
	VectorAlphaMask* other)
	:
	AlphaMask(previousMask, other),
	fWhere(other->fWhere)
{
}


/**
 * @brief Rasterises the vector content into a temporary B_RGBA32 bitmap.
 *
 * Computes the bounding box (clipping to the canvas if larger), allocates a
 * scratch UtilityBitmap, builds a BitmapHWInterface / DrawingEngine pair,
 * pushes a B_OP_ALPHA / B_PIXEL_ALPHA + B_ALPHA_COMPOSITE draw state onto
 * the canvas, and asks the subclass to draw its vectors.
 *
 * @param canvasBounds Bounds of the parent canvas; the mask is clipped here when needed.
 * @return Newly allocated bitmap with the rendered shapes (caller owns), or NULL.
 */
template<class VectorMaskType>
ServerBitmap*
VectorAlphaMask<VectorMaskType>::_RenderSource(const IntRect& canvasBounds)
{
	fBounds = static_cast<VectorMaskType*>(this)->DetermineBoundingBox();

	if (fBounds.Width() > canvasBounds.Width()
		|| fBounds.Height() > canvasBounds.Height()) {
		fBounds = fBounds & canvasBounds;
		fClippedToCanvas = true;
	} else
		fClippedToCanvas = false;

	if (fPreviousMask != NULL) {
		if (IsInverted()) {
			if (fPreviousMask->OutsideOpacity() != 0) {
				IntRect previousBounds = _PreviousMaskBounds();
				if (previousBounds.IsValid())
					fBounds = fBounds | previousBounds;
			} else
				fBounds = _PreviousMaskBounds();
			fClippedToCanvas = fClippedToCanvas || fPreviousMask->IsClipped();
		} else if (fPreviousMask->OutsideOpacity() == 0)
			fBounds = fBounds & _PreviousMaskBounds();
	}
	if (!fBounds.IsValid())
		return NULL;

	BReference<ServerBitmap> bitmap(_CreateTemporaryBitmap(fBounds), true);
	if (bitmap == NULL)
		return NULL;

	// Render the picture to the bitmap
	BitmapHWInterface interface(bitmap);
	ObjectDeleter<DrawingEngine> engine(interface.CreateDrawingEngine());
	if (!engine.IsSet())
		return NULL;

	engine->SetRendererOffset(fBounds.left, fBounds.top);

	OffscreenCanvas canvas(engine.Get(),
		static_cast<VectorMaskType*>(this)->GetDrawState(), fBounds);

	DrawState* const drawState = canvas.CurrentState();
	drawState->SetDrawingMode(B_OP_ALPHA);
	drawState->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	drawState->SetDrawingModeLocked(true);
	canvas.PushState();

	canvas.ResyncDrawState();

	if (engine->LockParallelAccess()) {
		BRegion clipping;
		clipping.Set((clipping_rect)fBounds);
		engine->ConstrainClippingRegion(&clipping);
		static_cast<VectorMaskType*>(this)->DrawVectors(&canvas);
		engine->UnlockParallelAccess();
	}

	canvas.PopState();

	return bitmap.Detach();
}


/**
 * @brief Returns the per-instance positional offset of a vector mask.
 *
 * @return The @c fWhere point supplied at construction.
 */
template<class VectorMaskType>
IntPoint
VectorAlphaMask<VectorMaskType>::_Offset()
{
	return fWhere;
}



// #pragma mark - PictureAlphaMask


/**
 * @brief Constructs a mask that rasterises the contents of a ServerPicture.
 *
 * @param previousMask Lower mask in the stack (may be NULL).
 * @param picture      ServerPicture whose drawing operations form the mask.
 * @param drawState    DrawState used while replaying @a picture.
 * @param where        Position offset applied to the rasterised picture.
 * @param inverse      Whether to invert the resulting mask alpha.
 */
PictureAlphaMask::PictureAlphaMask(AlphaMask* previousMask,
	ServerPicture* picture, const DrawState& drawState, BPoint where,
	bool inverse)
	:
	VectorAlphaMask<PictureAlphaMask>(previousMask, where, inverse),
	fPicture(picture),
	fDrawState(new(std::nothrow) DrawState(drawState))
{
}


/**
 * @brief Destructor; releases references to the picture and draw state.
 */
PictureAlphaMask::~PictureAlphaMask()
{
}


/**
 * @brief Replays the picture into @a canvas to produce the mask.
 */
void
PictureAlphaMask::DrawVectors(Canvas* canvas)
{
	fPicture->Play(canvas);
}


/**
 * @brief Computes the picture's bounding box by playing it without rendering.
 *
 * The returned box is rounded outwards (and padded by two pixels on the
 * bottom/right) to compensate for Painter's various rounding modes.
 *
 * @return The bounding box of the picture, or an invalid rect when empty.
 */
BRect
PictureAlphaMask::DetermineBoundingBox() const
{
	BRect boundingBox;
	PictureBoundingBoxPlayer::Play(fPicture, fDrawState.Get(), &boundingBox);

	if (!boundingBox.IsValid())
		return boundingBox;

	// Round up and add an additional 2 pixels on the bottom/right to
	// compensate for the various types of rounding used in Painter.
	boundingBox.left = floorf(boundingBox.left);
	boundingBox.right = ceilf(boundingBox.right) + 2;
	boundingBox.top = floorf(boundingBox.top);
	boundingBox.bottom = ceilf(boundingBox.bottom) + 2;

	return boundingBox;
}


/**
 * @brief Returns the saved DrawState used to replay the picture.
 */
const DrawState&
PictureAlphaMask::GetDrawState() const
{
	return *fDrawState.Get();
}


/**
 * @brief Picture masks are not cached at the moment; this is a no-op.
 *
 * @todo Implement picture-mask caching once a stable hash key is defined.
 */
void
PictureAlphaMask::_AddToCache()
{
	// currently not implemented
}


// #pragma mark - ShapeAlphaMask


/** @brief Process-global default DrawState used to render shape-based masks. */
DrawState* ShapeAlphaMask::fDrawState = NULL;


/**
 * @brief Private constructor used by ShapeAlphaMask::Create() on a cache miss.
 *
 * @param previousMask Lower mask in the stack (may be NULL).
 * @param shape        Shape to rasterise.
 * @param where        Position offset applied to the rasterised shape.
 * @param inverse      Whether to invert the resulting mask alpha.
 */
ShapeAlphaMask::ShapeAlphaMask(AlphaMask* previousMask,
	const shape_data& shape, BPoint where, bool inverse)
	:
	VectorAlphaMask<ShapeAlphaMask>(previousMask, where, inverse),
	fShape(new(std::nothrow) shape_data(shape), true)
{
	if (fDrawState == NULL)
		fDrawState = new(std::nothrow) DrawState();

	fShapeBounds = fShape->DetermineBoundingBox();
}


/**
 * @brief Private constructor used by ShapeAlphaMask::Create() on a cache hit.
 *
 * Wraps the supplied @a other so the new instance shares its rendered bitmap
 * but starts with a clean parent reference.
 *
 * @param previousMask Lower mask in the stack (may be NULL).
 * @param other        Donor mask whose bitmap and shape are reused.
 */
ShapeAlphaMask::ShapeAlphaMask(AlphaMask* previousMask,
	ShapeAlphaMask* other)
	:
	VectorAlphaMask<ShapeAlphaMask>(previousMask, other),
	fShape(other->fShape),
	fShapeBounds(other->fShapeBounds)
{
}


/**
 * @brief Destructor; the shape reference and rendered bitmap are released by smart pointers.
 */
ShapeAlphaMask::~ShapeAlphaMask()
{
}


/**
 * @brief Returns a (possibly cached) ShapeAlphaMask for the supplied parameters.
 *
 * Looks up the AlphaMaskCache for an existing match. On a hit, a wrapper
 * is created that reuses the cached bitmap. On a miss, a fresh mask is
 * created which will rasterise on demand and register itself with the cache.
 *
 * @param previousMask Lower mask in the stack (may be NULL).
 * @param shape        Shape that defines the mask outline.
 * @param where        Position offset applied to the rasterised shape.
 * @param inverse      Whether the resulting mask should be inverted.
 * @return New reference-counted mask (caller owns), or NULL on allocation failure.
 */
/* static */ ShapeAlphaMask*
ShapeAlphaMask::Create(AlphaMask* previousMask, const shape_data& shape,
	BPoint where, bool inverse)
{
	// Look if we have a suitable cached mask
	BReference<ShapeAlphaMask> mask(AlphaMaskCache::Default()->Get(shape,
		previousMask, inverse), true);

	if (mask == NULL) {
		// No cached mask, create new one
		mask.SetTo(new(std::nothrow) ShapeAlphaMask(previousMask, shape,
			BPoint(0, 0), inverse), true);
	} else {
		// Create new mask which reuses the parameters and the mask bitmap
		// of the cache entry
		// TODO: don't make a new mask if the cache entry has no drawstate
		// using it anymore, because then we ca just immediately reuse it
		RecursiveLocker locker(mask->fLock);
		mask.SetTo(new(std::nothrow) ShapeAlphaMask(previousMask, mask), true);
	}

	return mask.Detach();
}


/**
 * @brief Strokes / fills the shape into @a canvas to produce the mask alpha.
 */
void
ShapeAlphaMask::DrawVectors(Canvas* canvas)
{
	canvas->GetDrawingEngine()->DrawShape(fBounds,
		fShape->opCount, fShape->opList,
		fShape->ptCount, fShape->ptList,
		true, BPoint(0, 0), 1.0);
}


/**
 * @brief Returns the cached shape bounding box.
 */
BRect
ShapeAlphaMask::DetermineBoundingBox() const
{
	return fShapeBounds;
}


/**
 * @brief Returns the static fall-back DrawState shared by every ShapeAlphaMask.
 */
const DrawState&
ShapeAlphaMask::GetDrawState() const
{
	return *fDrawState;
}


/**
 * @brief Registers the freshly rendered mask with the global cache.
 */
void
ShapeAlphaMask::_AddToCache()
{
	AlphaMaskCache::Default()->Put(this);
}
