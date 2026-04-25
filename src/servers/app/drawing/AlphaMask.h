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
 * MIT License. Copyright 2014-2015, Haiku.
 */

/** @file AlphaMask.h
    @brief 8-bit alpha clipping mask used by Painter for shape-based clipping (BPicture, BShape). */

#ifndef ALPHA_MASK_H
#define ALPHA_MASK_H

#include <Referenceable.h>
#include <locks.h>

#include "agg_clipped_alpha_mask.h"
#include "ServerPicture.h"

#include "DrawState.h"
#include "drawing/Painter/defines.h"
#include "IntRect.h"


class BShape;
class ServerBitmap;
class ServerPicture;
class shape_data;
class UtilityBitmap;


// #pragma mark - AlphaMask


/** @brief Reference-counted 8-bit alpha clipping mask, optionally chained to a parent mask.
 *
 * AlphaMask is the abstract base for the various alpha clip sources used by
 * Painter (uniform opacity, BPicture content, BShape outlines). Each instance
 * caches a rasterised UtilityBitmap and the AGG buffer / scanline objects
 * needed to feed the mask to the rasteriser. Masks may be stacked: when
 * @c fPreviousMask is non-NULL the effective opacity at every pixel is the
 * product of this mask and the mask below it.
 */
class AlphaMask : public BReferenceable {
public:
	/** @brief Constructs an empty mask chained on top of @a previousMask. */
								AlphaMask(AlphaMask* previousMask,
									bool inverse);
	/** @brief Constructs a copy that shares the rendered bitmap of @a other. */
								AlphaMask(AlphaMask* previousMask,
									AlphaMask* other);
	/** @brief Constructs a uniform mask with the given background opacity (no clipping shape). */
								AlphaMask(uint8 backgroundOpacity);
	virtual						~AlphaMask();

	/** @brief Records the canvas origin / bounds, regenerating if the canvas grew.
	 *  @return The previous canvas origin (so the caller can restore it). */
			IntPoint			SetCanvasGeometry(IntPoint origin,
									IntRect bounds);

	/** @brief Returns the AGG scanline used by the rasteriser. */
			scanline_unpacked_masked_type* Scanline()
								{ return &fScanline; }

	/** @brief Returns the AGG clipped alpha mask used by the rasteriser. */
			agg::clipped_alpha_mask* Mask()
								{ return &fMask; }

	/** @brief Returns the size in bytes of the rendered mask bitmap. */
			size_t				BitmapSize() const;

	/** @brief Returns true when the mask interpretation is inverted. */
			bool				IsInverted() const
								{ return fInverse; }

	/** @brief Returns true when the mask had to be clipped to the canvas bounds. */
			bool				IsClipped() const
								{ return fClippedToCanvas; }

	/** @brief Returns the alpha value used for pixels outside the mask bitmap. */
			uint8				OutsideOpacity() const
								{ return fOutsideOpacity; }

protected:
			ServerBitmap*		_CreateTemporaryBitmap(BRect bounds) const;
			void				_Generate();
			void				_SetNoClipping();
			const IntRect&		_PreviousMaskBounds() const;
	virtual	void				_AddToCache() = 0;
			void				_SetOutsideOpacity();

private:
	virtual	ServerBitmap*		_RenderSource(const IntRect& canvasBounds) = 0;
 	virtual	IntPoint			_Offset() = 0;

			void				_AttachMaskToBuffer();

protected:
			BReference<AlphaMask> fPreviousMask;
			IntRect				fBounds;
			bool				fClippedToCanvas;
			recursive_lock		fLock;

private:
	friend class AlphaMaskCache;

			IntPoint			fCanvasOrigin;
			IntRect				fCanvasBounds;
			const bool			fInverse;
			uint8				fBackgroundOpacity;
			uint8				fOutsideOpacity;

			int32				fNextMaskCount;
			bool				fInCache;
			uint32				fIndirectCacheReferences;
									// number of times this mask has been
									// seen as "previous mask" of another
									// one in the cache, without being
									// in the cache itself

			BReference<UtilityBitmap> fBits;
			agg::rendering_buffer fBuffer;
			agg::clipped_alpha_mask fMask;
			scanline_unpacked_masked_type fScanline;
};


/** @brief Alpha mask of a single uniform opacity (no shape, just a constant alpha). */
class UniformAlphaMask : public AlphaMask {
public:
	/** @brief Constructs a uniform mask whose every pixel has the given @a opacity. */
								UniformAlphaMask(uint8 opacity);

private:
	virtual	ServerBitmap*		_RenderSource(const IntRect& canvasBounds);
	virtual	IntPoint			_Offset();
	virtual void				_AddToCache();
};


// #pragma mark - VectorAlphaMask


/** @brief CRTP base for alpha masks generated from vector primitives (BPicture, BShape).
 *
 * The derived class provides @c DetermineBoundingBox(), @c GetDrawState(), and
 * @c DrawVectors() which together describe how to rasterise the mask into an
 * 8-bit bitmap on demand.
 */
template<class VectorMaskType>
class VectorAlphaMask : public AlphaMask {
public:
	/** @brief Constructs a vector mask placed at @a where, optionally inverted. */
								VectorAlphaMask(AlphaMask* previousMask,
									BPoint where, bool inverse);
	/** @brief Constructs a vector mask sharing the rendered bitmap of @a other. */
								VectorAlphaMask(AlphaMask* previousMask,
									VectorAlphaMask* other);

private:
	virtual	ServerBitmap*		_RenderSource(const IntRect& canvasBounds);
	virtual	IntPoint			_Offset();

protected:
			BPoint				fWhere;
};


// #pragma mark - PictureAlphaMask


/** @brief Alpha mask whose contents are produced by playing back a ServerPicture. */
class PictureAlphaMask : public VectorAlphaMask<PictureAlphaMask> {
public:
	/** @brief Builds a mask from @a picture using the provided @a drawState. */
								PictureAlphaMask(AlphaMask* previousMask,
									ServerPicture* picture,
									const DrawState& drawState, BPoint where,
									bool inverse);
	virtual						~PictureAlphaMask();

	/** @brief Replays the picture into the supplied canvas during mask rendering. */
			void				DrawVectors(Canvas* canvas);
	/** @brief Computes the bounding box of the picture by walking it once. */
			BRect				DetermineBoundingBox() const;
	/** @brief Returns the saved DrawState used when rendering the mask. */
			const DrawState&	GetDrawState() const;

private:
	virtual void				_AddToCache();

private:
			BReference<ServerPicture> fPicture;
			ObjectDeleter<DrawState> fDrawState;
};


// #pragma mark - ShapeAlphaMask


/** @brief Alpha mask produced from a BShape outline; cacheable through AlphaMaskCache. */
class ShapeAlphaMask : public VectorAlphaMask<ShapeAlphaMask> {
private:
								ShapeAlphaMask(AlphaMask* previousMask,
									const shape_data& shape,
									BPoint where, bool inverse);
								ShapeAlphaMask(AlphaMask* previousMask,
									ShapeAlphaMask* other);

public:
	virtual						~ShapeAlphaMask();

	/** @brief Returns a reference to a (possibly cached) ShapeAlphaMask matching the parameters. */
	static	ShapeAlphaMask*		Create(AlphaMask* previousMask,
									const shape_data& shape,
									BPoint where, bool inverse);

	/** @brief Strokes / fills the shape into the supplied canvas during mask rendering. */
			void				DrawVectors(Canvas* canvas);
	/** @brief Returns the cached bounding box of the shape. */
			BRect				DetermineBoundingBox() const;
	/** @brief Returns the static fall-back DrawState shared by all ShapeAlphaMasks. */
			const DrawState&	GetDrawState() const;

private:
	virtual void				_AddToCache();

private:
	friend class AlphaMaskCache;

			BReference<shape_data> fShape;
			BRect				fShapeBounds;
	/** @brief Process-global default DrawState used to render shape masks. */
	static	DrawState*			fDrawState;
};


#endif // ALPHA_MASK_H
