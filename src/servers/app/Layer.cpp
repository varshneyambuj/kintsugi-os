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
 *   Copyright 2015 Julian Harnath <julian.harnath@rwth-aachen.de>
 *   All rights reserved. Distributed under the terms of the MIT license.
 */

/** @file Layer.cpp
 *  @brief Off-screen compositing layer that records drawing commands and blends them with opacity.
 */

#include "Layer.h"

#include "AlphaMask.h"
#include "BitmapHWInterface.h"
#include "DrawingEngine.h"
#include "DrawState.h"
#include "IntRect.h"
#include "PictureBoundingBoxPlayer.h"
#include "ServerBitmap.h"
#include "View.h"


/** @brief A Canvas implementation that directs drawing into an off-screen layer bitmap.
 *
 *  LayerCanvas bridges the generic Canvas interface with a specific DrawingEngine
 *  and bitmap bounds so that the recorded picture commands stored in a Layer can
 *  be replayed into an isolated bitmap without affecting the main screen surface.
 */
class LayerCanvas : public Canvas {
public:
	/** @brief Constructs a LayerCanvas.
	 *  @param drawingEngine The DrawingEngine pointed at the layer bitmap.
	 *  @param drawState     The draw state to inherit from the parent canvas.
	 *  @param bitmapBounds  The bounds of the backing bitmap in canvas coordinates.
	 */
	LayerCanvas(DrawingEngine* drawingEngine, DrawState* drawState,
		BRect bitmapBounds)
		:
		Canvas(),
		fDrawingEngine(drawingEngine),
		fBitmapBounds(bitmapBounds)
	{
		fDrawState.SetTo(drawState);
	}

	/** @brief Returns the DrawingEngine used to render into the layer bitmap.
	 *  @return Pointer to the layer's DrawingEngine.
	 */
	virtual DrawingEngine* GetDrawingEngine() const
	{
		return fDrawingEngine;
	}

	/** @brief Returns a ServerPicture by token (always NULL for a layer canvas).
	 *  @param token The picture token (unused).
	 *  @return Always NULL.
	 */
	virtual ServerPicture* GetPicture(int32 token) const
	{
		return NULL;
	}

	/** @brief No-op: layer canvases do not maintain a local clipping hierarchy. */
	virtual void RebuildClipping(bool)
	{
	}

	/** @brief Pushes the current draw state to the underlying DrawingEngine. */
	virtual void ResyncDrawState()
	{
		fDrawingEngine->SetDrawState(fDrawState.Get());
	}

	/** @brief Recomputes the current drawing region and constrains the engine to it.
	 *
	 *  Combines any draw-state clipping with the bitmap bounds and applies the
	 *  result to the DrawingEngine via ConstrainClippingRegion().
	 */
	virtual void UpdateCurrentDrawingRegion()
	{
		bool hasDrawStateClipping = fDrawState->GetCombinedClippingRegion(
			&fCurrentDrawingRegion);

		BRegion bitmapRegion(fBitmapBounds);
		if (hasDrawStateClipping)
			fCurrentDrawingRegion.IntersectWith(&bitmapRegion);
		else
			fCurrentDrawingRegion = bitmapRegion;

		fDrawingEngine->ConstrainClippingRegion(&fCurrentDrawingRegion);
	}

	/** @brief Returns the bounds of the backing bitmap.
	 *  @return The bitmap bounds as an IntRect.
	 */
	virtual	IntRect Bounds() const
	{
		return fBitmapBounds;
	}

protected:
	/** @brief No-op: LayerCanvas coordinates are already in screen space.
	 *  @param transform The transform to populate (left unchanged).
	 */
	virtual void _LocalToScreenTransform(SimpleTransform&) const
	{
	}

	/** @brief No-op: LayerCanvas coordinates are already in screen space.
	 *  @param transform The transform to populate (left unchanged).
	 */
	virtual void _ScreenToLocalTransform(SimpleTransform&) const
	{
	}

private:
	DrawingEngine*	fDrawingEngine;
	BRegion			fCurrentDrawingRegion;
	BRect			fBitmapBounds;
};


/** @brief Constructs a Layer with the given opacity.
 *  @param opacity The alpha opacity value (0 = fully transparent, 255 = fully opaque).
 */
Layer::Layer(uint8 opacity)
	:
	fOpacity(opacity),
	fLeftTopOffset(0, 0)
{
}


/** @brief Destructor. */
Layer::~Layer()
{
}


/** @brief Pushes a nested layer onto the picture stack.
 *
 *  Delegates to the underlying ServerPicture::PushPicture() mechanism so that
 *  nested layer commands are recorded within the parent layer's picture stream.
 *
 *  @param layer The child Layer to push onto the stack.
 */
void
Layer::PushLayer(Layer* layer)
{
	PushPicture(layer);
}


/** @brief Pops the topmost layer from the picture stack and returns it.
 *  @return The Layer that was on top of the stack, cast from ServerPicture*.
 */
Layer*
Layer::PopLayer()
{
	return static_cast<Layer*>(PopPicture());
}


/** @brief Renders the layer's recorded picture commands into a new bitmap.
 *
 *  Determines the bounding box of all drawing commands via
 *  PictureBoundingBoxPlayer, allocates an RGBA bitmap of that size, creates
 *  an isolated LayerCanvas, replays the picture into it, and returns the
 *  resulting bitmap.  The caller takes ownership of the returned bitmap.
 *
 *  Alpha mask geometry is temporarily adjusted to the bitmap origin during
 *  rendering and restored afterwards.  The parent canvas's draw state is
 *  updated to reflect any push/pop state changes that occurred during playback.
 *
 *  @param canvas The parent Canvas providing the current draw state and transforms.
 *  @return A newly allocated UtilityBitmap containing the rendered layer, or NULL
 *          on failure (invalid bounding box or allocation error).
 */
UtilityBitmap*
Layer::RenderToBitmap(Canvas* canvas)
{
	BRect boundingBox = _DetermineBoundingBox(canvas);
	if (!boundingBox.IsValid())
		return NULL;

	fLeftTopOffset = boundingBox.LeftTop();

	BReference<UtilityBitmap> layerBitmap(_AllocateBitmap(boundingBox), true);
	if (layerBitmap == NULL)
		return NULL;

	BitmapHWInterface layerInterface(layerBitmap);
	ObjectDeleter<DrawingEngine> const layerEngine(layerInterface.CreateDrawingEngine());
	if (!layerEngine.IsSet())
		return NULL;

	layerEngine->SetRendererOffset(boundingBox.left, boundingBox.top);
		// Drawing commands of the layer's picture use coordinates in the
		// coordinate space of the underlying canvas. The coordinate origin
		// of the layer bitmap is at boundingBox.LeftTop(). So all the drawing
		// from the picture needs to be offset to be moved into the bitmap.
		// We use a low-level offsetting via the AGG renderer here because the
		// offset needs to be processed independently, after all other
		// transforms, even after the BAffineTransforms (which are processed in
		// Painter), to prevent this origin from being further transformed by
		// e.g. scaling.

	LayerCanvas layerCanvas(layerEngine.Get(), canvas->DetachDrawState(), boundingBox);

	AlphaMask* const mask = layerCanvas.GetAlphaMask();
	IntPoint oldOffset;
	if (mask != NULL) {
		// Move alpha mask to bitmap origin
		oldOffset = mask->SetCanvasGeometry(IntPoint(0, 0), boundingBox);
	}

	layerCanvas.CurrentState()->SetDrawingMode(B_OP_ALPHA);
	layerCanvas.CurrentState()->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);

	layerCanvas.ResyncDrawState();
		// Apply state to the new drawing engine of the layer canvas

	if (layerEngine->LockParallelAccess()) {
		layerCanvas.UpdateCurrentDrawingRegion();

		// Draw recorded picture into bitmap
		Play(&layerCanvas);
		layerEngine->UnlockParallelAccess();
	}

	if (mask != NULL) {
		// Move alpha mask back to its old position
		// Note: this needs to be adapted if setting alpha masks is
		// implemented as BPicture command (the mask now might be a different
		// one than before).
		layerCanvas.CurrentState()->CombinedTransform().Apply(oldOffset);
		mask->SetCanvasGeometry(oldOffset, boundingBox);
		layerCanvas.ResyncDrawState();
	}

	canvas->SetDrawState(layerCanvas.DetachDrawState());
		// Update state in canvas (the top-of-stack state could be a different
		// state instance now, if the picture commands contained push/pop
		// commands)

	return layerBitmap.Detach();
}


/** @brief Returns the top-left offset of the layer bitmap within canvas coordinates.
 *
 *  This value is set during RenderToBitmap() to the top-left of the computed
 *  bounding box and can be used by the caller to composite the resulting bitmap
 *  at the correct position on the parent surface.
 *
 *  @return The top-left offset as an IntPoint.
 */
IntPoint
Layer::LeftTopOffset() const
{
	return fLeftTopOffset;
}


/** @brief Returns the layer's opacity.
 *  @return Opacity in the range [0, 255].
 */
uint8
Layer::Opacity() const
{
	return fOpacity;
}


/** @brief Computes the axis-aligned bounding box of all drawing commands in this layer.
 *
 *  Uses PictureBoundingBoxPlayer to replay the recorded commands with bounding-box
 *  tracking enabled.  The resulting box is expanded by 2 pixels on the
 *  bottom-right edges to compensate for sub-pixel rounding differences in Painter.
 *
 *  @param canvas The parent Canvas providing the current draw state for playback.
 *  @return The bounding box in canvas coordinates, or an invalid BRect if the
 *          layer contains no drawable content.
 */
BRect
Layer::_DetermineBoundingBox(Canvas* canvas)
{
	BRect boundingBox;
	PictureBoundingBoxPlayer::Play(this, canvas->CurrentState(), &boundingBox);

	if (!boundingBox.IsValid())
		return boundingBox;

	// Round up and add an additional 2 pixels on the bottom/right to
	// compensate for the various types of rounding used in Painter.
	boundingBox.left = floorf(boundingBox.left);
	boundingBox.right = ceilf(boundingBox.right) + 2;
	boundingBox.top = floorf(boundingBox.top);
	boundingBox.bottom = ceilf(boundingBox.bottom) + 2;

	// TODO: for optimization, crop the bounding box to the underlying
	// view bounds here

	return boundingBox;
}


/** @brief Allocates a zeroed RGBA bitmap with the given bounds.
 *
 *  The bitmap is created in B_RGBA32 colour space with all pixels initialised
 *  to transparent black so that the layer starts fully transparent before
 *  drawing commands are replayed into it.
 *
 *  @param bounds The size and position of the bitmap in canvas coordinates.
 *  @return A newly allocated UtilityBitmap, or NULL on allocation failure.
 */
UtilityBitmap*
Layer::_AllocateBitmap(const BRect& bounds)
{
	BReference<UtilityBitmap> layerBitmap(new(std::nothrow) UtilityBitmap(bounds,
		B_RGBA32, 0), true);
	if (layerBitmap == NULL)
		return NULL;
	if (!layerBitmap->IsValid())
		return NULL;

	memset(layerBitmap->Bits(), 0, layerBitmap->BitsLength());

	return layerBitmap.Detach();
}
