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
 * MIT License. Copyright 2001-2018, Haiku.
 * Original authors: DarkWyrm, Adi Oanca, Stephan Aßmus, Axel Dörfler,
 *                   Julian Harnath, Joseph Groover.
 */

/** @file DrawState.h
    @brief Server-side drawing state: colours, pen, font, clipping, transforms, and modes. */

#ifndef _DRAW_STATE_H_
#define _DRAW_STATE_H_


#include <AutoDeleter.h>
#include <AffineTransform.h>
#include <GraphicsDefs.h>
#include <InterfaceDefs.h>
#include <Point.h>
#include <Referenceable.h>
#include <View.h>

#include "AppFontManager.h"
#include "ServerFont.h"
#include "PatternHandler.h"
#include "SimpleTransform.h"

class AlphaMask;
class BRegion;
class shape_data;

namespace BPrivate {
	class LinkReceiver;
	class LinkSender;
};


/** @brief Encapsulates all drawing state for a view, including colour, pen, font,
           clipping region, coordinate transforms, and drawing mode. Supports a
           linked-list stack for PushState/PopState. */
class DrawState {
public:
							DrawState();
							DrawState(const DrawState& other);
public:
		virtual				~DrawState();

		/** @brief Pushes a copy of this state onto the stack and returns the new state.
		    @return Pointer to the newly pushed DrawState. */
		DrawState*			PushState();

		/** @brief Pops this state from the stack and returns the previous state.
		    @return Pointer to the previous DrawState. */
		DrawState*			PopState();

		/** @brief Returns the state below this one in the stack without modifying it.
		    @return Pointer to the previous DrawState, or NULL if at the bottom. */
		DrawState*			PreviousState() const
								{ return fPreviousState.Get(); }

		/** @brief Reads font state from a protocol link.
		    @param link The LinkReceiver to read from.
		    @param fontManager Optional AppFontManager for resolving font families.
		    @return Bitmask of font attributes that were read. */
		uint16				ReadFontFromLink(BPrivate::LinkReceiver& link,
								AppFontManager* fontManager = NULL);
								// NOTE: ReadFromLink() does not read Font state!!
								// It was separate in ServerWindow, and I didn't
								// want to change it without knowing implications.

		/** @brief Reads all non-font drawing state from a protocol link.
		    @param link The LinkReceiver to read from. */
		void				ReadFromLink(BPrivate::LinkReceiver& link);

		/** @brief Writes the complete drawing state to a protocol link.
		    @param link The LinkSender to write to. */
		void				WriteToLink(BPrivate::LinkSender& link) const;

							// coordinate transformation
		/** @brief Sets the local drawing origin in view coordinates.
		    @param origin New origin point. */
		void				SetOrigin(BPoint origin);

		/** @brief Returns the local origin set by SetOrigin().
		    @return Local origin point. */
		BPoint				Origin() const
								{ return fOrigin; }

		/** @brief Returns the combined (accumulated) origin including parent states.
		    @return Combined origin point. */
		BPoint				CombinedOrigin() const
								{ return fCombinedOrigin; }

		/** @brief Sets the local drawing scale factor.
		    @param scale New scale value. */
		void				SetScale(float scale);

		/** @brief Returns the local scale factor.
		    @return Current local scale. */
		float				Scale() const
								{ return fScale; }

		/** @brief Returns the combined (accumulated) scale including parent states.
		    @return Combined scale factor. */
		float				CombinedScale() const
								{ return fCombinedScale; }

		/** @brief Sets the local affine transform.
		    @param transform New BAffineTransform. */
		void				SetTransform(BAffineTransform transform);

		/** @brief Returns the local affine transform.
		    @return Current BAffineTransform. */
		BAffineTransform	Transform() const
								{ return fTransform; }

		/** @brief Returns the combined (accumulated) affine transform.
		    @return Combined BAffineTransform. */
		BAffineTransform	CombinedTransform() const
								{ return fCombinedTransform; }

		/** @brief Enables or disables application of the affine transform.
		    @param enabled true to apply the transform during rendering. */
		void				SetTransformEnabled(bool enabled);

		/** @brief Returns a new DrawState that flattens all parent-state contributions
		           into a single level (used for picture recording).
		    @return Pointer to a newly allocated squashed DrawState. */
		DrawState*			Squash() const;

							// additional clipping as requested by client
		/** @brief Replaces the client-specified clipping region.
		    @param region New clipping region in view coordinates, or NULL to clear. */
		void				SetClippingRegion(const BRegion* region);

		/** @brief Returns true if any clipping (client or shape) is active.
		    @return true if clipping is applied. */
		bool				HasClipping() const;

		/** @brief Returns true if client-specified clipping (beyond the view bounds) is active.
		    @return true if additional clipping is set. */
		bool				HasAdditionalClipping() const;

		/** @brief Fills in the combined clipping region from all active clip sources.
		    @param region Output BRegion to populate.
		    @return true if the region is non-empty. */
		bool				GetCombinedClippingRegion(BRegion* region) const;

		/** @brief Intersects (or subtracts) a rectangle from the clipping region.
		    @param rect The clipping rectangle.
		    @param inverse true to subtract the rectangle (clip to the outside).
		    @return true if the clip region changed. */
		bool				ClipToRect(BRect rect, bool inverse);

		/** @brief Intersects (or subtracts) a shape from the clipping region.
		    @param shape The shape data.
		    @param inverse true to subtract the shape. */
		void				ClipToShape(shape_data* shape, bool inverse);

			/** @brief Sets the alpha mask applied during rendering.
			    @param mask The AlphaMask to use, or NULL to clear. */
			void			SetAlphaMask(AlphaMask* mask);

			/** @brief Returns the currently active alpha mask.
			    @return Pointer to the AlphaMask, or NULL if none. */
			AlphaMask*		GetAlphaMask() const;

							// coordinate transformations
				/** @brief Applies the forward coordinate transform to a SimpleTransform.
				    @param transform In/out SimpleTransform to modify. */
				void		Transform(SimpleTransform& transform) const;

				/** @brief Applies the inverse coordinate transform to a SimpleTransform.
				    @param transform In/out SimpleTransform to modify. */
				void		InverseTransform(SimpleTransform& transform) const;

							// color
		/** @brief Sets the high (foreground) colour.
		    @param color New rgb_color value. */
		void				SetHighColor(rgb_color color);

		/** @brief Returns the current high (foreground) colour.
		    @return Current high rgb_color. */
		rgb_color			HighColor() const
								{ return fHighColor; }

		/** @brief Sets the low (background) colour.
		    @param color New rgb_color value. */
		void				SetLowColor(rgb_color color);

		/** @brief Returns the current low (background) colour.
		    @return Current low rgb_color. */
		rgb_color			LowColor() const
								{ return fLowColor; }

		/** @brief Sets the high colour to a dynamic UI colour role with a tint.
		    @param which The UI colour role.
		    @param tint Tint factor to apply. */
		void				SetHighUIColor(color_which which, float tint);

		/** @brief Returns the UI colour role and tint of the high colour.
		    @param tint Output tint factor.
		    @return The color_which role, or B_NO_COLOR if not a UI colour. */
		color_which			HighUIColor(float* tint) const;

		/** @brief Sets the low colour to a dynamic UI colour role with a tint.
		    @param which The UI colour role.
		    @param tint Tint factor to apply. */
		void				SetLowUIColor(color_which which, float tint);

		/** @brief Returns the UI colour role and tint of the low colour.
		    @param tint Output tint factor.
		    @return The color_which role, or B_NO_COLOR if not a UI colour. */
		color_which			LowUIColor(float* tint) const;

		/** @brief Sets the drawing pattern.
		    @param pattern New Pattern value. */
		void				SetPattern(const Pattern& pattern);

		/** @brief Returns the current drawing pattern.
		    @return Const reference to the current Pattern. */
		const Pattern&		GetPattern() const
								{ return fPattern; }

							// drawing/blending mode
		/** @brief Sets the drawing compositing mode.
		    @param mode New drawing_mode value.
		    @return true if the mode was changed, false if it was already locked. */
		bool				SetDrawingMode(drawing_mode mode);

		/** @brief Returns the current drawing mode.
		    @return Current drawing_mode. */
		drawing_mode		GetDrawingMode() const
								{ return fDrawingMode; }

		/** @brief Sets the alpha blending source and function modes.
		    @param srcMode Alpha source mode.
		    @param fncMode Alpha blending function.
		    @return true if either mode changed. */
		bool				SetBlendingMode(source_alpha srcMode,
								alpha_function fncMode);

		/** @brief Returns the alpha source mode.
		    @return Current source_alpha mode. */
		source_alpha		AlphaSrcMode() const
								{ return fAlphaSrcMode; }

		/** @brief Returns the alpha blending function mode.
		    @return Current alpha_function mode. */
		alpha_function		AlphaFncMode() const
								{ return fAlphaFncMode; }

		/** @brief Prevents further changes to the drawing mode.
		    @param locked true to lock the drawing mode. */
		void				SetDrawingModeLocked(bool locked);

							// pen
		/** @brief Sets the current pen position.
		    @param location New pen position in view coordinates. */
		void				SetPenLocation(BPoint location);

		/** @brief Returns the current pen position.
		    @return Current pen location. */
		BPoint				PenLocation() const;

		/** @brief Sets the pen (stroke) width.
		    @param size New pen width in pixels. */
		void				SetPenSize(float size);

		/** @brief Returns the current scaled pen width.
		    @return Scaled pen size. */
		float				PenSize() const;

		/** @brief Returns the pen width before scale is applied.
		    @return Unscaled pen size. */
		float				UnscaledPenSize() const;

							// font
		/** @brief Sets the drawing font, optionally limiting which attributes are changed.
		    @param font The new ServerFont.
		    @param flags Bitmask of B_FONT_* attributes to update (default: all). */
		void				SetFont(const ServerFont& font,
								uint32 flags = B_FONT_ALL);

		/** @brief Returns the current drawing font.
		    @return Const reference to the current ServerFont. */
		const ServerFont&	Font() const
								{ return fFont; }

		// overrides aliasing flag contained in SeverFont::Flags())
		/** @brief Forces or clears font anti-aliasing, overriding the font's own flag.
		    @param aliasing true to force aliased (non-anti-aliased) rendering. */
		void				SetForceFontAliasing(bool aliasing);

		/** @brief Returns whether forced font aliasing is active.
		    @return true if aliasing is forced. */
		bool				ForceFontAliasing() const
								{ return fFontAliasing; }

							// postscript style settings
		/** @brief Sets the line cap style.
		    @param mode New cap_mode value. */
		void				SetLineCapMode(cap_mode mode);

		/** @brief Returns the current line cap style.
		    @return Current cap_mode. */
		cap_mode			LineCapMode() const
								{ return fLineCapMode; }

		/** @brief Sets the line join style.
		    @param mode New join_mode value. */
		void				SetLineJoinMode(join_mode mode);

		/** @brief Returns the current line join style.
		    @return Current join_mode. */
		join_mode			LineJoinMode() const
								{ return fLineJoinMode; }

		/** @brief Sets the miter limit for mitered line joins.
		    @param limit New miter limit. */
		void				SetMiterLimit(float limit);

		/** @brief Returns the current miter limit.
		    @return Current miter limit value. */
		float				MiterLimit() const
								{ return fMiterLimit; }

		/** @brief Sets the fill rule for complex polygon fills.
		    @param fillRule New fill rule (e.g. B_EVEN_ODD, B_NONZERO). */
		void				SetFillRule(int32 fillRule);

		/** @brief Returns the current fill rule.
		    @return Current fill rule. */
		int32				FillRule() const
								{ return fFillRule; }

							// convenience functions
		/** @brief Prints the current draw state to stdout for debugging. */
		void				PrintToStream() const;

		/** @brief Enables sub-pixel coordinate precision for this state.
		    @param precise true to enable sub-pixel precision. */
		void				SetSubPixelPrecise(bool precise);

		/** @brief Returns whether sub-pixel coordinate precision is active.
		    @return true if sub-pixel precision is enabled. */
		bool				SubPixelPrecise() const
								{ return fSubPixelPrecise; }

protected:
		BPoint				fOrigin;
		BPoint				fCombinedOrigin;
		float				fScale;
		float				fCombinedScale;
		BAffineTransform	fTransform;
		BAffineTransform	fCombinedTransform;

		ObjectDeleter<BRegion>
							fClippingRegion;

		BReference<AlphaMask> fAlphaMask;

		rgb_color			fHighColor;
		rgb_color			fLowColor;

		color_which			fWhichHighColor;
		color_which			fWhichLowColor;
		float				fWhichHighColorTint;
		float				fWhichLowColorTint;
		Pattern				fPattern;

		drawing_mode		fDrawingMode;
		source_alpha		fAlphaSrcMode;
		alpha_function		fAlphaFncMode;
		bool				fDrawingModeLocked;

		BPoint				fPenLocation;
		float				fPenSize;

		ServerFont			fFont;
		// overrides font aliasing flag
		bool				fFontAliasing;

		// This is not part of the normal state stack.
		// The view will update it in PushState/PopState.
		// A BView can have a flag "B_SUBPIXEL_PRECISE",
		// I never knew what it does on R5, but I can use
		// it in Painter to actually draw stuff with
		// sub-pixel coordinates. It means
		// StrokeLine(BPoint(10, 5), BPoint(20, 9));
		// will look different from
		// StrokeLine(BPoint(10.3, 5.8), BPoint(20.6, 9.5));
		bool				fSubPixelPrecise;

		cap_mode			fLineCapMode;
		join_mode			fLineJoinMode;
		float				fMiterLimit;
		int32				fFillRule;
		// "internal", used to calculate the size
		// of the font (again) when the scale changes
		float				fUnscaledFontSize;

		ObjectDeleter<DrawState>
							fPreviousState;
};

#endif	// _DRAW_STATE_H_
