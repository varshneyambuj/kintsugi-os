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
 * MIT License. Copyright (c) 2001-2015, Haiku, Inc.
 * Original authors: DarkWyrm, Adi Oanca, Axel Dörfler, Stephan Aßmus,
 *                   Marcus Overhagen, Adrien Destugues, Julian Harnath.
 */

/** @file Canvas.h
    @brief Abstract drawing surface with a draw-state stack and coordinate transform support. */

#ifndef CANVAS_H
#define CANVAS_H


#include <AutoDeleter.h>
#include <Point.h>

#include "SimpleTransform.h"


class AlphaMask;
class BGradient;
class BRegion;
class DrawingEngine;
class DrawState;
class IntPoint;
class IntRect;
class Layer;
class ServerPicture;
class shape_data;


/** @brief Abstract base class representing a drawing surface with a state stack,
           coordinate transforms, clipping, and alpha mask support. */
class Canvas {
public:
							Canvas();
							Canvas(const DrawState& state);
	virtual					~Canvas();

			/** @brief Returns B_OK if the canvas was successfully initialised.
			    @return B_OK on success, or an error code. */
			status_t		InitCheck() const;

			/** @brief Pushes a copy of the current draw state onto the state stack. */
	virtual	void			PushState();

			/** @brief Pops the topmost draw state from the stack, restoring the previous one. */
	virtual	void			PopState();

			/** @brief Returns the current draw state without transferring ownership.
			    @return Pointer to the active DrawState. */
			DrawState*		CurrentState() const { return fDrawState.Get(); }

			/** @brief Replaces the current draw state with the given one.
			    @param newState The new DrawState to adopt. */
			void			SetDrawState(DrawState* newState);

			/** @brief Detaches and returns the current draw state, transferring ownership.
			    @return Pointer to the detached DrawState. */
			DrawState*		DetachDrawState() { return fDrawState.Detach(); }

			/** @brief Sets the drawing origin in view coordinates.
			    @param origin New drawing origin point. */
			void			SetDrawingOrigin(BPoint origin);

			/** @brief Returns the current drawing origin in view coordinates.
			    @return Current drawing origin. */
			BPoint			DrawingOrigin() const;

			/** @brief Sets the uniform drawing scale factor.
			    @param scale New scale value. */
			void			SetScale(float scale);

			/** @brief Returns the current drawing scale factor.
			    @return Current scale. */
			float			Scale() const;

			/** @brief Replaces the user-defined clipping region.
			    @param region New clipping region in view coordinates, or NULL to clear. */
			void			SetUserClipping(const BRegion* region);
				// region is expected in view coordinates

			/** @brief Clips the canvas to a rectangle, optionally inverting the clip.
			    @param rect The clipping rectangle.
			    @param inverse true to clip to the area outside rect.
			    @return true if the clip region changed. */
			bool			ClipToRect(BRect rect, bool inverse);

			/** @brief Clips the canvas to a shape, optionally inverting the clip.
			    @param shape The shape data defining the clip boundary.
			    @param inverse true to clip to the area outside the shape. */
			void			ClipToShape(shape_data* shape, bool inverse);

			/** @brief Sets the alpha mask applied during drawing.
			    @param mask The AlphaMask to use, or NULL to disable. */
			void			SetAlphaMask(AlphaMask* mask);

			/** @brief Returns the currently active alpha mask.
			    @return Pointer to the AlphaMask, or NULL if none is set. */
			AlphaMask*		GetAlphaMask() const;

			/** @brief Returns the bounding rectangle of this canvas in local coordinates.
			    @return Bounding IntRect. */
	virtual	IntRect			Bounds() const = 0;

			/** @brief Returns a transform that maps local coordinates to screen coordinates.
			    @return SimpleTransform for local-to-screen conversion. */
			SimpleTransform LocalToScreenTransform() const;

			/** @brief Returns a transform that maps screen coordinates to local coordinates.
			    @return SimpleTransform for screen-to-local conversion. */
			SimpleTransform ScreenToLocalTransform() const;

			/** @brief Returns a transform that maps pen (state) coordinates to screen coordinates.
			    @return SimpleTransform for pen-to-screen conversion. */
			SimpleTransform PenToScreenTransform() const;

			/** @brief Returns a transform that maps pen coordinates to local coordinates.
			    @return SimpleTransform for pen-to-local conversion. */
			SimpleTransform PenToLocalTransform() const;

			/** @brief Returns a transform that maps screen coordinates to pen coordinates.
			    @return SimpleTransform for screen-to-pen conversion. */
			SimpleTransform ScreenToPenTransform() const;

			/** @brief Composites a Layer onto this canvas using its opacity.
			    @param layer The Layer to blend. */
			void			BlendLayer(Layer* layer);

	/** @brief Returns the DrawingEngine used by this canvas.
	    @return Pointer to the DrawingEngine. */
	virtual	DrawingEngine*	GetDrawingEngine() const = 0;

	/** @brief Rebuilds the clipping region for this canvas.
	    @param deep true to recurse into children. */
	virtual	void			RebuildClipping(bool deep) = 0;

	/** @brief Synchronises the drawing engine state with the current DrawState. */
	virtual void			ResyncDrawState() {};

	/** @brief Updates the region used by the drawing engine for the current draw cycle. */
	virtual void			UpdateCurrentDrawingRegion() {};

protected:
	/** @brief Fills in a transform that converts local to screen coordinates.
	    @param transform Transform to populate. */
	virtual	void			_LocalToScreenTransform(
								SimpleTransform& transform) const = 0;

	/** @brief Fills in a transform that converts screen to local coordinates.
	    @param transform Transform to populate. */
	virtual	void			_ScreenToLocalTransform(
								SimpleTransform& transform) const = 0;

protected:
			ObjectDeleter<DrawState>
							fDrawState;
};


/** @brief Concrete Canvas backed by a specific DrawingEngine and a fixed bounds rectangle,
           used for off-screen rendering. */
class OffscreenCanvas : public Canvas {
public:
							OffscreenCanvas(DrawingEngine* engine,
								const DrawState& state, const IntRect& bounds);
	virtual					~OffscreenCanvas();

	/** @brief Returns the DrawingEngine that renders into the off-screen surface.
	    @return Pointer to the DrawingEngine. */
	virtual DrawingEngine*	GetDrawingEngine() const { return fDrawingEngine; }

	/** @brief No-op clipping rebuild for the off-screen canvas. */
	virtual void			RebuildClipping(bool deep) { /* TODO */ }

	/** @brief Synchronises the DrawingEngine state with the current DrawState. */
	virtual void			ResyncDrawState();

	/** @brief Updates the current drawing region from the off-screen bounds. */
	virtual void			UpdateCurrentDrawingRegion();

	/** @brief Returns the fixed bounding rectangle of this off-screen canvas.
	    @return Bounding IntRect. */
	virtual	IntRect			Bounds() const;

protected:
	virtual	void			_LocalToScreenTransform(SimpleTransform&) const {}
	virtual	void			_ScreenToLocalTransform(SimpleTransform&) const {}

private:
			DrawingEngine*	fDrawingEngine;
			BRegion			fCurrentDrawingRegion;
			IntRect			fBounds;
};


#endif // CANVAS_H
