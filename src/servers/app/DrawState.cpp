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
 *   Copyright 2001-2018, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Adi Oanca <adioanca@mymail.ro>
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Michael Pfeiffer <laplace@users.sourceforge.net>
 *       Julian Harnath <julian.harnath@rwth-aachen.de>
 *       Joseph Groover <looncraz@looncraz.net>
 */

/** @file DrawState.cpp
    @brief Data classes for working with BView states and draw parameters. */

//!	Data classes for working with BView states and draw parameters

#include "DrawState.h"

#include <new>
#include <stdio.h>

#include <Region.h>
#include <ShapePrivate.h>

#include "AlphaMask.h"
#include "LinkReceiver.h"
#include "LinkSender.h"
#include "ServerProtocolStructs.h"


using std::nothrow;


/** @brief Default constructor. Initialises all drawing parameters to their documented defaults. */
DrawState::DrawState()
	:
	fOrigin(0.0f, 0.0f),
	fCombinedOrigin(0.0f, 0.0f),
	fScale(1.0f),
	fCombinedScale(1.0f),
	fTransform(),
	fCombinedTransform(),
	fAlphaMask(NULL),

	fHighColor((rgb_color){ 0, 0, 0, 255 }),
	fLowColor((rgb_color){ 255, 255, 255, 255 }),
	fWhichHighColor(B_NO_COLOR),
	fWhichLowColor(B_NO_COLOR),
	fWhichHighColorTint(B_NO_TINT),
	fWhichLowColorTint(B_NO_TINT),
	fPattern(kSolidHigh),

	fDrawingMode(B_OP_COPY),
	fAlphaSrcMode(B_PIXEL_ALPHA),
	fAlphaFncMode(B_ALPHA_OVERLAY),
	fDrawingModeLocked(false),

	fPenLocation(0.0f, 0.0f),
	fPenSize(1.0f),

	fFontAliasing(false),
	fSubPixelPrecise(false),
	fLineCapMode(B_BUTT_CAP),
	fLineJoinMode(B_MITER_JOIN),
	fMiterLimit(B_DEFAULT_MITER_LIMIT),
	fFillRule(B_NONZERO)
{
	fUnscaledFontSize = fFont.Size();
}


/** @brief Copy constructor. Duplicates all state parameters from another DrawState.
    @param other The source DrawState to copy from. */
DrawState::DrawState(const DrawState& other)
	:
	fOrigin(other.fOrigin),
	fCombinedOrigin(other.fCombinedOrigin),
	fScale(other.fScale),
	fCombinedScale(other.fCombinedScale),
	fTransform(other.fTransform),
	fCombinedTransform(other.fCombinedTransform),
	fClippingRegion(NULL),
	fAlphaMask(NULL),

	fHighColor(other.fHighColor),
	fLowColor(other.fLowColor),
	fWhichHighColor(other.fWhichHighColor),
	fWhichLowColor(other.fWhichLowColor),
	fWhichHighColorTint(other.fWhichHighColorTint),
	fWhichLowColorTint(other.fWhichLowColorTint),
	fPattern(other.fPattern),

	fDrawingMode(other.fDrawingMode),
	fAlphaSrcMode(other.fAlphaSrcMode),
	fAlphaFncMode(other.fAlphaFncMode),
	fDrawingModeLocked(other.fDrawingModeLocked),

	fPenLocation(other.fPenLocation),
	fPenSize(other.fPenSize),

	fFont(other.fFont),
	fFontAliasing(other.fFontAliasing),

	fSubPixelPrecise(other.fSubPixelPrecise),

	fLineCapMode(other.fLineCapMode),
	fLineJoinMode(other.fLineJoinMode),
	fMiterLimit(other.fMiterLimit),
	fFillRule(other.fFillRule),

	// Since fScale is reset to 1.0, the unscaled
	// font size is the current size of the font
	// (which is from->fUnscaledFontSize * from->fCombinedScale)
	fUnscaledFontSize(other.fUnscaledFontSize),
	fPreviousState(NULL)
{
}


/** @brief Destructor. */
DrawState::~DrawState()
{
}


/** @brief Creates a new DrawState derived from this one and links them via fPreviousState.
    @return A pointer to the new DrawState, or NULL on allocation failure. */
DrawState*
DrawState::PushState()
{
	DrawState* next = new (nothrow) DrawState(*this);

	if (next != NULL) {
		// Prepare state as derived from this state
		next->fOrigin = BPoint(0.0, 0.0);
		next->fScale = 1.0;
		next->fTransform.Reset();
		next->fPreviousState.SetTo(this);
		next->SetAlphaMask(fAlphaMask);
	}

	return next;
}


/** @brief Detaches and returns the previous state, effectively popping this state off the stack.
    @return Pointer to the previous DrawState, transferring ownership to the caller. */
DrawState*
DrawState::PopState()
{
	return fPreviousState.Detach();
}


/** @brief Reads font attributes from a link message, applying only those indicated by the mask.
    @param link        The link receiver to read from.
    @param fontManager Optional AppFontManager to look up application-specific fonts.
    @return A bitmask indicating which font attributes were received. */
uint16
DrawState::ReadFontFromLink(BPrivate::LinkReceiver& link,
	AppFontManager* fontManager)
{
	uint16 mask;
	link.Read<uint16>(&mask);

	if ((mask & B_FONT_FAMILY_AND_STYLE) != 0) {
		uint32 fontID;
		link.Read<uint32>(&fontID);
		fFont.SetFamilyAndStyle(fontID, fontManager);
	}

	if ((mask & B_FONT_SIZE) != 0) {
		float size;
		link.Read<float>(&size);
		fUnscaledFontSize = size;
		fFont.SetSize(fUnscaledFontSize * fCombinedScale);
	}

	if ((mask & B_FONT_SHEAR) != 0) {
		float shear;
		link.Read<float>(&shear);
		fFont.SetShear(shear);
	}

	if ((mask & B_FONT_ROTATION) != 0) {
		float rotation;
		link.Read<float>(&rotation);
		fFont.SetRotation(rotation);
	}

	if ((mask & B_FONT_FALSE_BOLD_WIDTH) != 0) {
		float falseBoldWidth;
		link.Read<float>(&falseBoldWidth);
		fFont.SetFalseBoldWidth(falseBoldWidth);
	}

	if ((mask & B_FONT_SPACING) != 0) {
		uint8 spacing;
		link.Read<uint8>(&spacing);
		fFont.SetSpacing(spacing);
	}

	if ((mask & B_FONT_ENCODING) != 0) {
		uint8 encoding;
		link.Read<uint8>(&encoding);
		fFont.SetEncoding(encoding);
	}

	if ((mask & B_FONT_FACE) != 0) {
		uint16 face;
		link.Read<uint16>(&face);
		fFont.SetFace(face);
	}

	if ((mask & B_FONT_FLAGS) != 0) {
		uint32 flags;
		link.Read<uint32>(&flags);
		fFont.SetFlags(flags);
	}

	return mask;
}


/** @brief Reads the complete view state from a link message, updating combined transforms and optional clipping.
    @param link The link receiver containing a serialised ViewSetStateInfo and optional clipping region. */
void
DrawState::ReadFromLink(BPrivate::LinkReceiver& link)
{
	ViewSetStateInfo info;

	link.Read<ViewSetStateInfo>(&info);

	// BAffineTransform is transmitted as a double array
	double transform[6];
	link.Read<double[6]>(&transform);
	if (fTransform.Unflatten(B_AFFINE_TRANSFORM_TYPE, transform,
		sizeof(transform)) != B_OK) {
		return;
	}

	fPenLocation = info.penLocation;
	fPenSize = info.penSize;
	fHighColor = info.highColor;
	fLowColor = info.lowColor;
	fWhichHighColor = info.whichHighColor;
	fWhichLowColor = info.whichLowColor;
	fWhichHighColorTint = info.whichHighColorTint;
	fWhichLowColorTint = info.whichLowColorTint;
	fPattern = info.pattern;
	fDrawingMode = info.drawingMode;
	fOrigin = info.origin;
	fScale = info.scale;
	fLineJoinMode = info.lineJoin;
	fLineCapMode = info.lineCap;
	fMiterLimit = info.miterLimit;
	fFillRule = info.fillRule;
	fAlphaSrcMode = info.alphaSourceMode;
	fAlphaFncMode = info.alphaFunctionMode;
	fFontAliasing = info.fontAntialiasing;

	if (fPreviousState.IsSet()) {
		fCombinedOrigin = fPreviousState->fCombinedOrigin + fOrigin;
		fCombinedScale = fPreviousState->fCombinedScale * fScale;
		fCombinedTransform = fPreviousState->fCombinedTransform * fTransform;
	} else {
		fCombinedOrigin = fOrigin;
		fCombinedScale = fScale;
		fCombinedTransform = fTransform;
	}


	// read clipping
	bool hasClippingRegion;
	link.Read<bool>(&hasClippingRegion);

	if (hasClippingRegion) {
		BRegion region;
		link.ReadRegion(&region);
		SetClippingRegion(&region);
	} else {
		// No user clipping used
		SetClippingRegion(NULL);
	}
}


/** @brief Serialises the complete view state (font, pen, colors, transform, clipping) to a link message.
    @param link The link sender to write into. */
void
DrawState::WriteToLink(BPrivate::LinkSender& link) const
{
	// Attach font state
	ViewGetStateInfo info;
	info.fontID = fFont.GetFamilyAndStyle();
	info.fontSize = fFont.Size();
	info.fontShear = fFont.Shear();
	info.fontRotation = fFont.Rotation();
	info.fontFalseBoldWidth = fFont.FalseBoldWidth();
	info.fontSpacing = fFont.Spacing();
	info.fontEncoding = fFont.Encoding();
	info.fontFace = fFont.Face();
	info.fontFlags = fFont.Flags();

	// Attach view state
	info.viewStateInfo.penLocation = fPenLocation;
	info.viewStateInfo.penSize = fPenSize;
	info.viewStateInfo.highColor = fHighColor;
	info.viewStateInfo.lowColor = fLowColor;
	info.viewStateInfo.whichHighColor = fWhichHighColor;
	info.viewStateInfo.whichLowColor = fWhichLowColor;
	info.viewStateInfo.whichHighColorTint = fWhichHighColorTint;
	info.viewStateInfo.whichLowColorTint = fWhichLowColorTint;
	info.viewStateInfo.pattern = (::pattern)fPattern.GetPattern();
	info.viewStateInfo.drawingMode = fDrawingMode;
	info.viewStateInfo.origin = fOrigin;
	info.viewStateInfo.scale = fScale;
	info.viewStateInfo.lineJoin = fLineJoinMode;
	info.viewStateInfo.lineCap = fLineCapMode;
	info.viewStateInfo.miterLimit = fMiterLimit;
	info.viewStateInfo.fillRule = fFillRule;
	info.viewStateInfo.alphaSourceMode = fAlphaSrcMode;
	info.viewStateInfo.alphaFunctionMode = fAlphaFncMode;
	info.viewStateInfo.fontAntialiasing = fFontAliasing;


	link.Attach<ViewGetStateInfo>(info);

	// BAffineTransform is transmitted as a double array
	double transform[6];
	if (fTransform.Flatten(transform, sizeof(transform)) != B_OK)
		return;
	link.Attach<double[6]>(transform);

	link.Attach<bool>(fClippingRegion.IsSet());
	if (fClippingRegion.IsSet())
		link.AttachRegion(*fClippingRegion.Get());
}


/** @brief Sets the local drawing origin and recomputes the combined origin relative to any parent state.
    @param origin The new origin in parent-state coordinate space. */
void
DrawState::SetOrigin(BPoint origin)
{
	fOrigin = origin;

	// NOTE: the origins of earlier states are never expected to
	// change, only the topmost state ever changes
	if (fPreviousState.IsSet()) {
		fCombinedOrigin.x = fPreviousState->fCombinedOrigin.x
			+ fOrigin.x * fPreviousState->fCombinedScale;
		fCombinedOrigin.y = fPreviousState->fCombinedOrigin.y
			+ fOrigin.y * fPreviousState->fCombinedScale;
	} else {
		fCombinedOrigin = fOrigin;
	}
}


/** @brief Sets the local scale factor and recomputes the combined scale; also updates the scaled font size.
    @param scale The new scale factor. No-op if scale equals the current value. */
void
DrawState::SetScale(float scale)
{
	if (fScale == scale)
		return;

	fScale = scale;

	// NOTE: the scales of earlier states are never expected to
	// change, only the topmost state ever changes
	if (fPreviousState.IsSet())
		fCombinedScale = fPreviousState->fCombinedScale * fScale;
	else
		fCombinedScale = fScale;

	// update font size
	// NOTE: This is what makes the call potentially expensive,
	// hence the introductory check
	fFont.SetSize(fUnscaledFontSize * fCombinedScale);
}


/** @brief Sets the local affine transform and recomputes the combined transform.
    @param transform The new BAffineTransform. No-op if equal to the current value. */
void
DrawState::SetTransform(BAffineTransform transform)
{
	if (fTransform == transform)
		return;

	fTransform = transform;

	// NOTE: the transforms of earlier states are never expected to
	// change, only the topmost state ever changes
	if (fPreviousState.IsSet())
		fCombinedTransform = fPreviousState->fCombinedTransform * fTransform;
	else
		fCombinedTransform = fTransform;
}


/** @brief Temporarily enables or disables all BAffineTransforms in the state stack.

    Can be used to temporarily disable all BAffineTransforms in the state
    stack, and later reenable them.
    @param enabled If true, re-applies the current transform; if false, resets the combined transform to identity. */
void
DrawState::SetTransformEnabled(bool enabled)
{
	if (enabled) {
		BAffineTransform temp = fTransform;
		SetTransform(BAffineTransform());
		SetTransform(temp);
	}
	else
		fCombinedTransform = BAffineTransform();
}


/** @brief Creates a flattened (squashed) copy of this state, collapsing the state stack into a single level.
    @return A new DrawState that is a pushed child of a copy of this state, or NULL on failure. */
DrawState*
DrawState::Squash() const
{
	DrawState* const squashedState = new(nothrow) DrawState(*this);
	return squashedState->PushState();
}


/** @brief Sets the user-defined clipping region for this state.
    @param region Pointer to the BRegion to use as clipping, or NULL to remove clipping. */
void
DrawState::SetClippingRegion(const BRegion* region)
{
	if (region) {
		if (fClippingRegion.IsSet())
			*fClippingRegion.Get() = *region;
		else
			fClippingRegion.SetTo(new(nothrow) BRegion(*region));
	} else {
		fClippingRegion.Unset();
	}
}


/** @brief Returns whether any clipping region exists in this state or any parent state.
    @return true if a clipping region is set anywhere in the state stack. */
bool
DrawState::HasClipping() const
{
	if (fClippingRegion.IsSet())
		return true;
	if (fPreviousState.IsSet())
		return fPreviousState->HasClipping();
	return false;
}


/** @brief Returns whether this specific state level has its own clipping region.
    @return true if this state (not a parent) has a clipping region. */
bool
DrawState::HasAdditionalClipping() const
{
	return fClippingRegion.IsSet();
}


/** @brief Computes the effective clipping region by intersecting all regions in the state stack.
    @param region Output parameter receiving the combined clipping region.
    @return true if a combined region was computed, false if there is no clipping. */
bool
DrawState::GetCombinedClippingRegion(BRegion* region) const
{
	if (fClippingRegion.IsSet()) {
		BRegion localTransformedClipping(*fClippingRegion.Get());
		SimpleTransform penTransform;
		Transform(penTransform);
		penTransform.Apply(&localTransformedClipping);
		if (fPreviousState.IsSet()
			&& fPreviousState->GetCombinedClippingRegion(region)) {
			localTransformedClipping.IntersectWith(region);
		}
		*region = localTransformedClipping;
		return true;
	} else {
		if (fPreviousState.IsSet())
			return fPreviousState->GetCombinedClippingRegion(region);
	}
	return false;
}


/** @brief Clips the drawing area to the given rectangle, handling transforms and inverse clipping.
    @param rect    The clipping rectangle in current coordinate space.
    @param inverse If true, excludes the rectangle rather than restricting to it.
    @return true if the alpha mask geometry needs to be updated after this call. */
bool
DrawState::ClipToRect(BRect rect, bool inverse)
{
	if (!rect.IsValid()) {
		if (!inverse) {
			if (!fClippingRegion.IsSet())
				fClippingRegion.SetTo(new(nothrow) BRegion());
			else
				fClippingRegion->MakeEmpty();
		}
		return false;
	}

	if (!fCombinedTransform.IsIdentity()) {
		if (fCombinedTransform.IsDilation()) {
			BPoint points[2] = { rect.LeftTop(), rect.RightBottom() };
			fCombinedTransform.Apply(&points[0], 2);
			rect.Set(points[0].x, points[0].y, points[1].x, points[1].y);
		} else {
			uint32 ops[] = {
				OP_MOVETO | OP_LINETO | 3,
				OP_CLOSE
			};
			BPoint points[4] = {
				BPoint(rect.left,  rect.top),
				BPoint(rect.right, rect.top),
				BPoint(rect.right, rect.bottom),
				BPoint(rect.left,  rect.bottom)
			};
			shape_data rectShape;
			rectShape.opList = &ops[0];
			rectShape.opCount = 2;
			rectShape.opSize = sizeof(uint32) * 2;
			rectShape.ptList = &points[0];
			rectShape.ptCount = 4;
			rectShape.ptSize = sizeof(BPoint) * 4;

			ClipToShape(&rectShape, inverse);
			return true;
		}
	}

	if (inverse) {
		if (!fClippingRegion.IsSet()) {
			fClippingRegion.SetTo(new(nothrow) BRegion(BRect(
				-(1 << 16), -(1 << 16), (1 << 16), (1 << 16))));
				// TODO: we should have a definition for a rect (or region)
				// with "infinite" area. For now, this region size should do...
		}
		fClippingRegion->Exclude(rect);
	} else {
		if (!fClippingRegion.IsSet())
			fClippingRegion.SetTo(new(nothrow) BRegion(rect));
		else {
			BRegion rectRegion(rect);
			fClippingRegion->IntersectWith(&rectRegion);
		}
	}

	return false;
}


/** @brief Clips the drawing area to an arbitrary shape, creating or updating an AlphaMask.
    @param shape   Pointer to the shape_data describing the clip shape.
    @param inverse If true, clips to the complement of the shape. */
void
DrawState::ClipToShape(shape_data* shape, bool inverse)
{
	if (shape->ptCount == 0)
		return;

	if (!fCombinedTransform.IsIdentity())
		fCombinedTransform.Apply(shape->ptList, shape->ptCount);

	BReference<AlphaMask> const mask(ShapeAlphaMask::Create(GetAlphaMask(), *shape,
		BPoint(0, 0), inverse), true);

	SetAlphaMask(mask);
}


/** @brief Sets the alpha mask for this draw state.
    @param mask Pointer to the AlphaMask to use. Replaces any existing mask. */
void
DrawState::SetAlphaMask(AlphaMask* mask)
{
	// NOTE: In BeOS, it wasn't possible to clip to a BPicture and keep
	// regular custom clipping to a BRegion at the same time.
	fAlphaMask.SetTo(mask);
}


/** @brief Returns the current alpha mask.
    @return Pointer to the AlphaMask, or NULL if none is set. */
AlphaMask*
DrawState::GetAlphaMask() const
{
	return fAlphaMask.Get();
}


// #pragma mark -


/** @brief Applies this state's combined origin and scale to a SimpleTransform.
    @param transform The transform object to modify in place. */
void
DrawState::Transform(SimpleTransform& transform) const
{
	transform.AddOffset(fCombinedOrigin.x, fCombinedOrigin.y);
	transform.SetScale(fCombinedScale);
}


/** @brief Applies the inverse of this state's combined origin and scale to a SimpleTransform.
    @param transform The transform object to modify in place. */
void
DrawState::InverseTransform(SimpleTransform& transform) const
{
	transform.AddOffset(-fCombinedOrigin.x, -fCombinedOrigin.y);
	if (fCombinedScale != 0.0)
		transform.SetScale(1.0 / fCombinedScale);
}


// #pragma mark -


/** @brief Sets the high (foreground) color.
    @param color The new high color as an rgb_color. */
void
DrawState::SetHighColor(rgb_color color)
{
	fHighColor = color;
}


/** @brief Sets the low (background) color.
    @param color The new low color as an rgb_color. */
void
DrawState::SetLowColor(rgb_color color)
{
	fLowColor = color;
}


/** @brief Sets the high color using a UI color constant and an optional tint.
    @param which The color_which constant identifying the UI color role.
    @param tint  Tint factor to apply to the UI color (B_NO_TINT for none). */
void
DrawState::SetHighUIColor(color_which which, float tint)
{
	fWhichHighColor = which;
	fWhichHighColorTint = tint;
}


/** @brief Returns the UI color constant and tint for the high color.
    @param tint Optional output pointer that receives the tint value.
    @return The color_which constant for the high UI color. */
color_which
DrawState::HighUIColor(float* tint) const
{
	if (tint != NULL)
		*tint = fWhichHighColorTint;

	return fWhichHighColor;
}


/** @brief Sets the low color using a UI color constant and an optional tint.
    @param which The color_which constant identifying the UI color role.
    @param tint  Tint factor to apply to the UI color (B_NO_TINT for none). */
void
DrawState::SetLowUIColor(color_which which, float tint)
{
	fWhichLowColor = which;
	fWhichLowColorTint = tint;
}


/** @brief Returns the UI color constant and tint for the low color.
    @param tint Optional output pointer that receives the tint value.
    @return The color_which constant for the low UI color. */
color_which
DrawState::LowUIColor(float* tint) const
{
	if (tint != NULL)
		*tint = fWhichLowColorTint;

	return fWhichLowColor;
}


/** @brief Sets the drawing pattern.
    @param pattern The Pattern object to use for subsequent drawing operations. */
void
DrawState::SetPattern(const Pattern& pattern)
{
	fPattern = pattern;
}


/** @brief Sets the drawing mode if the mode is not locked.
    @param mode The drawing_mode to set.
    @return true if the mode was applied, false if the drawing mode is locked. */
bool
DrawState::SetDrawingMode(drawing_mode mode)
{
	if (!fDrawingModeLocked) {
		fDrawingMode = mode;
		return true;
	}
	return false;
}


/** @brief Sets the alpha blending mode if the drawing mode is not locked.
    @param srcMode The source alpha mode.
    @param fncMode The alpha compositing function.
    @return true if the modes were applied, false if the drawing mode is locked. */
bool
DrawState::SetBlendingMode(source_alpha srcMode, alpha_function fncMode)
{
	if (!fDrawingModeLocked) {
		fAlphaSrcMode = srcMode;
		fAlphaFncMode = fncMode;
		return true;
	}
	return false;
}


/** @brief Locks or unlocks the drawing mode to prevent changes via SetDrawingMode() or SetBlendingMode().
    @param locked If true, the drawing mode is locked against further changes. */
void
DrawState::SetDrawingModeLocked(bool locked)
{
	fDrawingModeLocked = locked;
}



/** @brief Sets the current pen position in unscaled view coordinates.
    @param location The new pen location as a BPoint. */
void
DrawState::SetPenLocation(BPoint location)
{
	fPenLocation = location;
}


/** @brief Returns the current pen position in unscaled view coordinates.
    @return The pen location as a BPoint. */
BPoint
DrawState::PenLocation() const
{
	return fPenLocation;
}


/** @brief Sets the unscaled pen size.
    @param size The new pen size in view units. */
void
DrawState::SetPenSize(float size)
{
	fPenSize = size;
}


/** @brief Returns the scaled pen size, enforcing the minimum size of 1.0.
    @return The pen size scaled by fCombinedScale, clamped to a minimum of 1.0. */
float
DrawState::PenSize() const
{
	float penSize = fPenSize * fCombinedScale;
	// NOTE: As documented in the BeBook,
	// pen size is never smaller than 1.0.
	// This is supposed to be the smallest
	// possible device size.
	if (penSize < 1.0)
		penSize = 1.0;
	return penSize;
}


/** @brief Returns the unscaled pen size, enforcing the minimum size of 1.0.
    @return The raw pen size clamped to a minimum of 1.0. */
float
DrawState::UnscaledPenSize() const
{
	// NOTE: As documented in the BeBook,
	// pen size is never smaller than 1.0.
	// This is supposed to be the smallest
	// possible device size.
	return max_c(fPenSize, 1.0);
}


/** @brief Sets the font, scaling the font size by fCombinedScale.

    The font is assumed to carry an already unscaled size; this method
    applies the current combined scale when storing it.
    @param font  The source ServerFont.
    @param flags Bitmask of B_FONT_* constants indicating which attributes to copy. */
void
DrawState::SetFont(const ServerFont& font, uint32 flags)
{
	if (flags == B_FONT_ALL) {
		fFont = font;
		fUnscaledFontSize = font.Size();
		fFont.SetSize(fUnscaledFontSize * fCombinedScale);
	} else {
		// family & style
		if ((flags & B_FONT_FAMILY_AND_STYLE) != 0)
			fFont.SetFamilyAndStyle(font.GetFamilyAndStyle());
		// size
		if ((flags & B_FONT_SIZE) != 0) {
			fUnscaledFontSize = font.Size();
			fFont.SetSize(fUnscaledFontSize * fCombinedScale);
		}
		// shear
		if ((flags & B_FONT_SHEAR) != 0)
			fFont.SetShear(font.Shear());
		// rotation
		if ((flags & B_FONT_ROTATION) != 0)
			fFont.SetRotation(font.Rotation());
		// spacing
		if ((flags & B_FONT_SPACING) != 0)
			fFont.SetSpacing(font.Spacing());
		// encoding
		if ((flags & B_FONT_ENCODING) != 0)
			fFont.SetEncoding(font.Encoding());
		// face
		if ((flags & B_FONT_FACE) != 0)
			fFont.SetFace(font.Face());
		// flags
		if ((flags & B_FONT_FLAGS) != 0)
			fFont.SetFlags(font.Flags());
	}
}


/** @brief Forces font anti-aliasing on or off regardless of the font's own settings.
    @param aliasing If true, force aliased (non-antialiased) rendering. */
void
DrawState::SetForceFontAliasing(bool aliasing)
{
	fFontAliasing = aliasing;
}


/** @brief Enables or disables sub-pixel precise coordinate rendering.
    @param precise If true, coordinates are treated with sub-pixel precision. */
void
DrawState::SetSubPixelPrecise(bool precise)
{
	fSubPixelPrecise = precise;
}


/** @brief Sets the line cap mode for stroked lines.
    @param mode The cap_mode value to apply. */
void
DrawState::SetLineCapMode(cap_mode mode)
{
	fLineCapMode = mode;
}


/** @brief Sets the line join mode for stroked paths.
    @param mode The join_mode value to apply. */
void
DrawState::SetLineJoinMode(join_mode mode)
{
	fLineJoinMode = mode;
}


/** @brief Sets the miter limit for miter-style line joins.
    @param limit The maximum miter length before the join falls back to bevel. */
void
DrawState::SetMiterLimit(float limit)
{
	fMiterLimit = limit;
}


/** @brief Sets the fill rule for complex (self-intersecting) paths.
    @param fillRule An integer fill rule constant (e.g. B_NONZERO or B_EVEN_ODD). */
void
DrawState::SetFillRule(int32 fillRule)
{
	fFillRule = fillRule;
}


/** @brief Prints a human-readable summary of all draw state parameters to standard output. */
void
DrawState::PrintToStream() const
{
	printf("\t Origin: (%.1f, %.1f)\n", fOrigin.x, fOrigin.y);
	printf("\t Scale: %.2f\n", fScale);
	printf("\t Transform: %.2f, %.2f, %.2f, %.2f, %.2f, %.2f\n",
		fTransform.sx, fTransform.shy, fTransform.shx,
		fTransform.sy, fTransform.tx, fTransform.ty);

	printf("\t Pen Location and Size: (%.1f, %.1f) - %.2f (%.2f)\n",
		   fPenLocation.x, fPenLocation.y, PenSize(), fPenSize);

	printf("\t HighColor: r=%d g=%d b=%d a=%d\n",
		fHighColor.red, fHighColor.green, fHighColor.blue, fHighColor.alpha);
	printf("\t LowColor: r=%d g=%d b=%d a=%d\n",
		fLowColor.red, fLowColor.green, fLowColor.blue, fLowColor.alpha);
	printf("\t WhichHighColor: %i\n", fWhichHighColor);
	printf("\t WhichLowColor: %i\n", fWhichLowColor);
	printf("\t WhichHighColorTint: %.3f\n", fWhichHighColorTint);
	printf("\t WhichLowColorTint: %.3f\n", fWhichLowColorTint);
	printf("\t Pattern: %" B_PRIu64 "\n", fPattern.GetInt64());

	printf("\t DrawMode: %" B_PRIu32 "\n", (uint32)fDrawingMode);
	printf("\t AlphaSrcMode: %" B_PRId32 "\t AlphaFncMode: %" B_PRId32 "\n",
		   (int32)fAlphaSrcMode, (int32)fAlphaFncMode);

	printf("\t LineCap: %d\t LineJoin: %d\t MiterLimit: %.2f\n",
		   (int16)fLineCapMode, (int16)fLineJoinMode, fMiterLimit);

	if (fClippingRegion.IsSet())
		fClippingRegion->PrintToStream();

	printf("\t ===== Font Data =====\n");
	printf("\t Style: CURRENTLY NOT SET\n"); // ???
	printf("\t Size: %.1f (%.1f)\n", fFont.Size(), fUnscaledFontSize);
	printf("\t Shear: %.2f\n", fFont.Shear());
	printf("\t Rotation: %.2f\n", fFont.Rotation());
	printf("\t Spacing: %" B_PRId32 "\n", fFont.Spacing());
	printf("\t Encoding: %" B_PRId32 "\n", fFont.Encoding());
	printf("\t Face: %d\n", fFont.Face());
	printf("\t Flags: %" B_PRIu32 "\n", fFont.Flags());
}
