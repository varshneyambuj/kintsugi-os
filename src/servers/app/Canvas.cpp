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
 *   Copyright (c) 2001-2015, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Adi Oanca <adioanca@gmail.com>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Marcus Overhagen <marcus@overhagen.de>
 *       Adrien Destugues <pulkomandy@pulkomandy.tk
 *       Julian Harnath <julian.harnath@rwth-aachen.de>
 */

/** @file Canvas.cpp
    @brief Drawing canvas base class and offscreen canvas implementation. */


#include "Canvas.h"

#include <new>

#include <Region.h>

#include "AlphaMask.h"
#include "DrawingEngine.h"
#include "DrawState.h"
#include "Layer.h"


#if __GNUC__ >= 3
#	define GCC_2_NRV(x)
	// GCC >= 3.1 doesn't need it anymore
#else
#	define GCC_2_NRV(x) return x;
	// GCC 2 named return value syntax
	// see http://gcc.gnu.org/onlinedocs/gcc-2.95.2/gcc_5.html#SEC106
#endif


/** @brief Default constructor; creates a Canvas with a freshly allocated DrawState. */
Canvas::Canvas()
	:
	fDrawState(new(std::nothrow) DrawState())
{
}


/** @brief Constructor that initialises the Canvas from an existing draw state.
    @param state The DrawState to copy into this canvas. */
Canvas::Canvas(const DrawState& state)
	:
	fDrawState(new(std::nothrow) DrawState(state))
{
}


/** @brief Destructor. */
Canvas::~Canvas()
{
}


/** @brief Returns whether the canvas was initialised successfully.
    @return B_OK on success, B_NO_MEMORY if the DrawState could not be allocated. */
status_t
Canvas::InitCheck() const
{
	if (!fDrawState.IsSet())
		return B_NO_MEMORY;

	return B_OK;
}


/** @brief Pushes the current draw state onto an internal stack, creating a new derived state. */
void
Canvas::PushState()
{
	DrawState* previous = fDrawState.Detach();
	DrawState* newState = previous->PushState();
	if (newState == NULL)
		newState = previous;

	fDrawState.SetTo(newState);
}


/** @brief Pops the most recently pushed draw state, restoring the previous state. */
void
Canvas::PopState()
{
	if (fDrawState->PreviousState() == NULL)
		return;

	bool rebuildClipping = fDrawState->HasAdditionalClipping();

	fDrawState.SetTo(fDrawState->PopState());

	// rebuild clipping
	// (the clipping from the popped state is not effective anymore)
	if (rebuildClipping)
		RebuildClipping(false);
}


/** @brief Replaces the current draw state with a new one.
    @param newState Pointer to the DrawState to adopt. */
void
Canvas::SetDrawState(DrawState* newState)
{
	fDrawState.SetTo(newState);
}


/** @brief Sets the drawing origin for coordinate translation.
    @param origin The new origin point in canvas-local coordinates. */
void
Canvas::SetDrawingOrigin(BPoint origin)
{
	fDrawState->SetOrigin(origin);

	// rebuild clipping
	if (fDrawState->HasClipping())
		RebuildClipping(false);
}


/** @brief Returns the current drawing origin.
    @return The drawing origin as a BPoint in canvas-local coordinates. */
BPoint
Canvas::DrawingOrigin() const
{
	return fDrawState->Origin();
}


/** @brief Sets the scale factor applied to subsequent drawing operations.
    @param scale The scale factor to apply. */
void
Canvas::SetScale(float scale)
{
	fDrawState->SetScale(scale);

	// rebuild clipping
	if (fDrawState->HasClipping())
		RebuildClipping(false);
}


/** @brief Returns the current drawing scale factor.
    @return The current scale as a float. */
float
Canvas::Scale() const
{
	return fDrawState->Scale();
}


/** @brief Sets the user-defined clipping region for this canvas.
    @param region Pointer to the clipping BRegion, or NULL to remove clipping. */
void
Canvas::SetUserClipping(const BRegion* region)
{
	fDrawState->SetClippingRegion(region);

	// rebuild clipping (for just this canvas)
	RebuildClipping(false);
}


/** @brief Clips the canvas to the given rectangle, optionally inverting the clip.
    @param rect The rectangle to clip to.
    @param inverse If true, clip to the complement of rect.
    @return true if the draw state needs to be updated after this call. */
bool
Canvas::ClipToRect(BRect rect, bool inverse)
{
	bool needDrawStateUpdate = fDrawState->ClipToRect(rect, inverse);
	RebuildClipping(false);
	return needDrawStateUpdate;
}


/** @brief Clips the canvas using an arbitrary shape, optionally inverting the clip.
    @param shape Pointer to the shape_data structure describing the clip shape.
    @param inverse If true, clip to the complement of the shape. */
void
Canvas::ClipToShape(shape_data* shape, bool inverse)
{
	fDrawState->ClipToShape(shape, inverse);
}


/** @brief Sets the alpha mask applied during compositing.
    @param mask Pointer to the AlphaMask to use, or NULL to remove the mask. */
void
Canvas::SetAlphaMask(AlphaMask* mask)
{
	fDrawState->SetAlphaMask(mask);
}


/** @brief Returns the currently active alpha mask.
    @return Pointer to the current AlphaMask, or NULL if none is set. */
AlphaMask*
Canvas::GetAlphaMask() const
{
	return fDrawState->GetAlphaMask();
}


/** @brief Constructs and returns the transform that maps local canvas coordinates to screen coordinates.
    @return A SimpleTransform representing the local-to-screen mapping. */
SimpleTransform
Canvas::LocalToScreenTransform() const GCC_2_NRV(transform)
{
#if __GNUC__ >= 3
	SimpleTransform transform;
#endif
	_LocalToScreenTransform(transform);
	return transform;
}


/** @brief Constructs and returns the transform that maps screen coordinates to local canvas coordinates.
    @return A SimpleTransform representing the screen-to-local mapping. */
SimpleTransform
Canvas::ScreenToLocalTransform() const GCC_2_NRV(transform)
{
#if __GNUC__ >= 3
	SimpleTransform transform;
#endif
	_ScreenToLocalTransform(transform);
	return transform;
}


/** @brief Constructs and returns the transform from pen coordinates to screen coordinates.
    @return A SimpleTransform representing the pen-to-screen mapping. */
SimpleTransform
Canvas::PenToScreenTransform() const GCC_2_NRV(transform)
{
#if __GNUC__ >= 3
	SimpleTransform transform;
#endif
	fDrawState->Transform(transform);
	_LocalToScreenTransform(transform);
	return transform;
}


/** @brief Constructs and returns the transform from pen coordinates to local canvas coordinates.
    @return A SimpleTransform representing the pen-to-local mapping. */
SimpleTransform
Canvas::PenToLocalTransform() const GCC_2_NRV(transform)
{
#if __GNUC__ >= 3
	SimpleTransform transform;
#endif
	fDrawState->Transform(transform);
	return transform;
}


/** @brief Constructs and returns the transform from screen coordinates to pen coordinates.
    @return A SimpleTransform representing the screen-to-pen mapping. */
SimpleTransform
Canvas::ScreenToPenTransform() const GCC_2_NRV(transform)
{
#if __GNUC__ >= 3
	SimpleTransform transform;
#endif
	_ScreenToLocalTransform(transform);
	fDrawState->InverseTransform(transform);
	return transform;
}


/** @brief Renders a compositing layer onto this canvas, applying alpha and opacity.
    @param layerPtr Pointer to the Layer to blend; the method acquires a reference
                    and releases it on return. */
void
Canvas::BlendLayer(Layer* layerPtr)
{
	BReference<Layer> layer(layerPtr, true);

	BReference <UtilityBitmap> layerBitmap(layer->RenderToBitmap(this), true);
	if (layerBitmap == NULL)
		return;

	BRect destination = layerBitmap->Bounds();
	destination.OffsetBy(layer->LeftTopOffset());
	LocalToScreenTransform().Apply(&destination);

	PushState();

	fDrawState->SetDrawingMode(B_OP_ALPHA);
	fDrawState->SetBlendingMode(B_PIXEL_ALPHA, B_ALPHA_COMPOSITE);
	fDrawState->SetTransformEnabled(false);

	if (layer->Opacity() < 255) {
		BReference<AlphaMask> mask(new(std::nothrow) UniformAlphaMask(layer->Opacity()), true);
		if (mask == NULL)
			return;

		SetAlphaMask(mask);
	}
	ResyncDrawState();

	GetDrawingEngine()->DrawBitmap(layerBitmap, layerBitmap->Bounds(),
		destination, 0);

	fDrawState->SetTransformEnabled(true);

	PopState();
	ResyncDrawState();
}


// #pragma mark - OffscreenCanvas


/** @brief Constructs an OffscreenCanvas backed by the given drawing engine.
    @param engine Pointer to the DrawingEngine to use for rendering.
    @param state  Initial draw state for the canvas.
    @param bounds Pixel bounds of the offscreen surface. */
OffscreenCanvas::OffscreenCanvas(DrawingEngine* engine,
		const DrawState& state, const IntRect& bounds)
	:
	Canvas(state),
	fDrawingEngine(engine),
	fBounds(bounds)
{
	ResyncDrawState();
}


/** @brief Destructor. */
OffscreenCanvas::~OffscreenCanvas()
{
}


/** @brief Pushes the current draw state into the backing drawing engine. */
void
OffscreenCanvas::ResyncDrawState()
{
	fDrawingEngine->SetDrawState(fDrawState.Get());
}


/** @brief Recomputes and applies the clipping region to the drawing engine. */
void
OffscreenCanvas::UpdateCurrentDrawingRegion()
{
	if (fDrawState->HasClipping()) {
		fDrawState->GetCombinedClippingRegion(&fCurrentDrawingRegion);
		fDrawingEngine->ConstrainClippingRegion(&fCurrentDrawingRegion);
	}
}


/** @brief Returns the pixel bounds of this offscreen canvas.
    @return An IntRect describing the canvas dimensions. */
IntRect
OffscreenCanvas::Bounds() const
{
	return fBounds;
}
