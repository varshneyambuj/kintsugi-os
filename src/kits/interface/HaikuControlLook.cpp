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
 *   Copyright 2009-2020 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file HaikuControlLook.cpp
 * @brief Implementation of HaikuControlLook, the default visual style for controls
 *
 * HaikuControlLook is the concrete implementation of BControlLook that renders
 * the default Haiku-style appearance for all standard Interface Kit controls.
 * It is installed as be_control_look at application startup.
 *
 * @see BControlLook, BView
 */


#include <HaikuControlLook.h>

#include <algorithm>

#include <Bitmap.h>
#include <Control.h>
#include <GradientLinear.h>
#include <LayoutUtils.h>
#include <Region.h>
#include <Shape.h>
#include <String.h>
#include <TabView.h>
#include <View.h>
#include <Window.h>
#include <WindowPrivate.h>


namespace BPrivate {

/** @brief Tint multiplier applied to gradient tints when the mouse hovers over a control. */
static const float kHoverTintFactor = 0.85;

/** @brief Width of the pop-up indicator region appended to split-button controls. */
static const int32 kButtonPopUpIndicatorWidth = B_USE_ITEM_SPACING;

/** @brief Fully-opaque black used for desktop label outlines. */
static const rgb_color kBlack = { 0, 0, 0, 255 };

/** @brief Fully-opaque white used for desktop label glows. */
static const rgb_color kWhite = { 255, 255, 255, 255 };


/**
 * @brief Constructs the HaikuControlLook renderer.
 *
 * Initialises the cached desktop-label outline flag to @c false; the flag is
 * lazily refreshed on the first DrawLabel() call against a desktop window.
 */
HaikuControlLook::HaikuControlLook()
	:
	fCachedOutline(false)
{
}


/**
 * @brief Destroys the HaikuControlLook renderer.
 */
HaikuControlLook::~HaikuControlLook()
{
}


/**
 * @brief Returns the default alignment for control labels.
 *
 * @return A BAlignment with horizontal left and vertical centre alignment.
 */
BAlignment
HaikuControlLook::DefaultLabelAlignment() const
{
	return BAlignment(B_ALIGN_LEFT, B_ALIGN_VERTICAL_CENTER);
}


/**
 * @brief Returns the default spacing between a label and an adjacent icon or control.
 *
 * The value is half the current plain-font point size, rounded up to the
 * nearest pixel.
 *
 * @return Spacing in pixels.
 */
float
HaikuControlLook::DefaultLabelSpacing() const
{
	return ceilf(be_plain_font->Size() / 2.0);
}


/**
 * @brief Returns the default spacing between adjacent items in a list or menu.
 *
 * The value is 85% of the current plain-font point size, rounded up.
 *
 * @return Spacing in pixels.
 */
float
HaikuControlLook::DefaultItemSpacing() const
{
	return ceilf(be_plain_font->Size() * 0.85);
}


/**
 * @brief Derives the control-look flags for a BControl from its current state.
 *
 * Inspects the control's enabled state, focus, value, and parent flags, then
 * assembles the corresponding @c B_IS_CONTROL, @c B_DISABLED, @c B_FOCUSED,
 * @c B_ACTIVATED, @c B_PARTIALLY_ACTIVATED, and @c B_BLEND_FRAME bits.
 *
 * @param control The control whose state is queried; must not be @c NULL.
 * @return A bitmask of BControlLook flag constants appropriate for rendering.
 * @see BControlLook::Flags()
 */
uint32
HaikuControlLook::Flags(BControl* control) const
{
	uint32 flags = B_IS_CONTROL;

	if (!control->IsEnabled())
		flags |= B_DISABLED;

	if (control->IsFocus() && control->Window() != NULL
		&& control->Window()->IsActive()) {
		flags |= B_FOCUSED;
	}

	switch (control->Value()) {
		case B_CONTROL_ON:
			flags |= B_ACTIVATED;
			break;
		case B_CONTROL_PARTIALLY_ON:
			flags |= B_PARTIALLY_ACTIVATED;
			break;
	}

	if (control->Parent() != NULL
		&& (control->Parent()->Flags() & B_DRAW_ON_CHILDREN) != 0) {
		// In this constellation, assume we want to render the control
		// against the already existing view contents of the parent view.
		flags |= B_BLEND_FRAME;
	}

	return flags;
}


// #pragma mark -


/**
 * @brief Draws the outer frame of a button with square corners.
 *
 * Delegates to _DrawButtonFrame() using a corner radius of 0 on all corners.
 *
 * @param view        The view to draw into.
 * @param rect        The button bounding rectangle; inset by each drawn border.
 * @param updateRect  The dirty region; drawing is skipped when outside this rect.
 * @param base        The button's base colour used to derive frame tints.
 * @param background  The parent background colour used for the outer edge.
 * @param flags       Control-look flags (e.g. @c B_DISABLED, @c B_FOCUSED).
 * @param borders     Which sides to draw (combination of @c B_*_BORDER constants).
 */
void
HaikuControlLook::DrawButtonFrame(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, const rgb_color& background, uint32 flags,
	uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, 0.0f, 0.0f, 0.0f, 0.0f, base,
		background, flags, borders);
}


/**
 * @brief Draws the outer frame of a button with uniform rounded corners.
 *
 * Applies the same @a radius to all four corners.
 *
 * @param view        The view to draw into.
 * @param rect        The button bounding rectangle; inset by each drawn border.
 * @param updateRect  The dirty region.
 * @param radius      Corner radius in pixels, applied uniformly.
 * @param base        The button's base colour.
 * @param background  The parent background colour.
 * @param flags       Control-look flags.
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::DrawButtonFrame(BView* view, BRect& rect, const BRect& updateRect,
	float radius, const rgb_color& base, const rgb_color& background, uint32 flags,
	uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, radius, radius, radius, radius,
		base, background, flags, borders);
}


/**
 * @brief Draws the outer frame of a button with individually specified corner radii.
 *
 * Each corner radius may differ, allowing pill, tab, and other asymmetric shapes.
 *
 * @param view               The view to draw into.
 * @param rect               The button bounding rectangle; inset by each drawn border.
 * @param updateRect         The dirty region.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The button's base colour.
 * @param background         The parent background colour.
 * @param flags              Control-look flags.
 * @param borders            Which sides to draw.
 */
void
HaikuControlLook::DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	const rgb_color& background, uint32 flags,
	uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, leftTopRadius, rightTopRadius,
		leftBottomRadius, rightBottomRadius, base, background,
		flags, borders);
}


/**
 * @brief Draws the filled background surface of a button with square corners.
 *
 * Paints the bevel and gradient fill inside the frame already drawn by
 * DrawButtonFrame().  The pop-up indicator region is not included.
 *
 * @param view        The view to draw into.
 * @param rect        The area to fill; inset by bevel borders after drawing.
 * @param updateRect  The dirty region.
 * @param base        The button's base colour used to derive the gradient.
 * @param flags       Control-look flags.
 * @param borders     Which sides receive a bevel line.
 * @param orientation Gradient direction: @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	_DrawButtonBackground(view, rect, updateRect, 0.0f, 0.0f, 0.0f, 0.0f,
		base, false, flags, borders, orientation);
}


/**
 * @brief Draws the filled background of a button with uniform rounded corners.
 *
 * @param view        The view to draw into.
 * @param rect        The area to fill; inset after drawing.
 * @param updateRect  The dirty region.
 * @param radius      Corner radius applied uniformly to all four corners.
 * @param base        The button's base colour.
 * @param flags       Control-look flags.
 * @param borders     Which sides receive a bevel line.
 * @param orientation Gradient direction.
 */
void
HaikuControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	_DrawButtonBackground(view, rect, updateRect, radius, radius, radius,
		radius, base, false, flags, borders, orientation);
}


/**
 * @brief Draws the filled background of a button with individually specified corner radii.
 *
 * @param view               The view to draw into.
 * @param rect               The area to fill; inset after drawing.
 * @param updateRect         The dirty region.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The button's base colour.
 * @param flags              Control-look flags.
 * @param borders            Which sides receive a bevel line.
 * @param orientation        Gradient direction.
 */
void
HaikuControlLook::DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	uint32 flags, uint32 borders, orientation orientation)
{
	_DrawButtonBackground(view, rect, updateRect, leftTopRadius,
		rightTopRadius, leftBottomRadius, rightBottomRadius, base, false, flags,
		borders, orientation);
}


/**
 * @brief Draws the background surface of a menu bar.
 *
 * Draws a frame bevel and fills the bar with a top-to-bottom gradient.
 * When the @c B_ACTIVATED flag is set (menu open), darker tints are used to
 * give a pressed appearance.
 *
 * @param view        The view to draw into.
 * @param rect        The menu-bar bounding rectangle; inset by drawn borders.
 * @param updateRect  The dirty region.
 * @param base        The menu-bar base colour.
 * @param flags       Control-look flags (e.g. @c B_ACTIVATED).
 * @param borders     Which sides receive a frame line.
 */
void
HaikuControlLook::DrawMenuBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// the surface edges

	// colors
	float topTint;
	float bottomTint;

	if ((flags & B_ACTIVATED) != 0) {
		rgb_color bevelColor1 = tint_color(base, 1.40);
		rgb_color bevelColor2 = tint_color(base, 1.25);

		topTint = 1.25;
		bottomTint = 1.20;

		_DrawFrame(view, rect,
			bevelColor1, bevelColor1,
			bevelColor2, bevelColor2,
			borders & B_TOP_BORDER);
	} else {
		rgb_color cornerColor = tint_color(base, 0.9);
		rgb_color bevelColorTop = tint_color(base, 0.5);
		rgb_color bevelColorLeft = tint_color(base, 0.7);
		rgb_color bevelColorRightBottom = tint_color(base, 1.08);

		topTint = 0.69;
		bottomTint = 1.03;

		_DrawFrame(view, rect,
			bevelColorLeft, bevelColorTop,
			bevelColorRightBottom, bevelColorRightBottom,
			cornerColor, cornerColor,
			borders);
	}

	// draw surface top
	_FillGradient(view, rect, base, topTint, bottomTint);
}


/**
 * @brief Draws the outer frame of a menu field with square corners.
 *
 * Delegates to _DrawButtonFrame() using zero corner radii, producing the same
 * frame appearance as a plain button.
 *
 * @param view        The view to draw into.
 * @param rect        The menu-field bounding rectangle; inset by drawn borders.
 * @param updateRect  The dirty region.
 * @param base        The menu field's base colour.
 * @param background  The parent background colour.
 * @param flags       Control-look flags.
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, 0.0f, 0.0f, 0.0f, 0.0f, base,
		background, flags, borders);
}


/**
 * @brief Draws the outer frame of a menu field with uniform rounded corners.
 *
 * @param view        The view to draw into.
 * @param rect        The menu-field bounding rectangle; inset by drawn borders.
 * @param updateRect  The dirty region.
 * @param radius      Corner radius applied uniformly to all four corners.
 * @param base        The menu field's base colour.
 * @param background  The parent background colour.
 * @param flags       Control-look flags.
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, radius, radius, radius, radius,
		base, background, flags, borders);
}


/**
 * @brief Draws the outer frame of a menu field with individually specified corner radii.
 *
 * @param view               The view to draw into.
 * @param rect               The menu-field bounding rectangle; inset by drawn borders.
 * @param updateRect         The dirty region.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The menu field's base colour.
 * @param background         The parent background colour.
 * @param flags              Control-look flags.
 * @param borders            Which sides to draw.
 */
void
HaikuControlLook::DrawMenuFieldFrame(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius,
	float rightTopRadius, float leftBottomRadius,
	float rightBottomRadius, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	_DrawButtonFrame(view, rect, updateRect, leftTopRadius, rightTopRadius,
		leftBottomRadius, rightBottomRadius, base, background,
		flags, borders);
}


/**
 * @brief Draws the menu-field background with square corners, optionally including a pop-up indicator.
 *
 * When @a popupIndicator is @c true the field is split into a label area and
 * an indicator area that contains a down-arrow marker.
 *
 * @param view            The view to draw into.
 * @param rect            The menu-field bounding rectangle; updated on return.
 * @param updateRect      The dirty region.
 * @param base            The menu field's base colour.
 * @param popupIndicator  If @c true, render the pop-up indicator region.
 * @param flags           Control-look flags.
 */
void
HaikuControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, bool popupIndicator,
	uint32 flags)
{
	_DrawMenuFieldBackgroundOutside(view, rect, updateRect,
		0.0f, 0.0f, 0.0f, 0.0f, base, popupIndicator, flags);
}


/**
 * @brief Draws the interior background of a menu field (label side only) with square corners.
 *
 * Used for the label portion of a split menu field when the pop-up indicator
 * is rendered separately.
 *
 * @param view        The view to draw into.
 * @param rect        The area to fill; inset by drawn borders.
 * @param updateRect  The dirty region.
 * @param base        The menu field's base colour.
 * @param flags       Control-look flags.
 * @param borders     Which sides receive a bevel line.
 */
void
HaikuControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	_DrawMenuFieldBackgroundInside(view, rect, updateRect,
		0.0f, 0.0f, 0.0f, 0.0f, base, flags, borders);
}


/**
 * @brief Draws the menu-field background with uniform rounded corners.
 *
 * @param view            The view to draw into.
 * @param rect            The menu-field bounding rectangle; updated on return.
 * @param updateRect      The dirty region.
 * @param radius          Corner radius applied uniformly to all four corners.
 * @param base            The menu field's base colour.
 * @param popupIndicator  If @c true, render the pop-up indicator region.
 * @param flags           Control-look flags.
 */
void
HaikuControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base,
	bool popupIndicator, uint32 flags)
{
	_DrawMenuFieldBackgroundOutside(view, rect, updateRect, radius, radius,
		radius, radius, base, popupIndicator, flags);
}


/**
 * @brief Draws the menu-field background with individually specified corner radii.
 *
 * @param view               The view to draw into.
 * @param rect               The menu-field bounding rectangle; updated on return.
 * @param updateRect         The dirty region.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The menu field's base colour.
 * @param popupIndicator     If @c true, render the pop-up indicator region.
 * @param flags              Control-look flags.
 */
void
HaikuControlLook::DrawMenuFieldBackground(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	bool popupIndicator, uint32 flags)
{
	_DrawMenuFieldBackgroundOutside(view, rect, updateRect, leftTopRadius,
		rightTopRadius, leftBottomRadius, rightBottomRadius, base,
		popupIndicator, flags);
}


/**
 * @brief Draws the background of an open menu panel.
 *
 * Paints a one-pixel bevel frame and then fills the interior with the solid
 * @a base colour.  Disabled menus use lighter bevel colours.
 *
 * @param view        The view to draw into.
 * @param rect        The menu panel bounding rectangle; inset by drawn borders.
 * @param updateRect  The dirty region.
 * @param base        The menu's background colour.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param borders     Which sides receive a bevel line.
 */
void
HaikuControlLook::DrawMenuBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// inner bevel colors
	rgb_color bevelLightColor;
	rgb_color bevelShadowColor;

	if ((flags & B_DISABLED) != 0) {
		bevelLightColor = tint_color(base, 0.80);
		bevelShadowColor = tint_color(base, 1.07);
	} else {
		bevelLightColor = tint_color(base, 0.6);
		bevelShadowColor = tint_color(base, 1.12);
	}

	// draw inner bevel
	_DrawFrame(view, rect,
		bevelLightColor, bevelLightColor,
		bevelShadowColor, bevelShadowColor,
		borders);

	// draw surface top
	view->SetHighColor(base);
	view->FillRect(rect);
}


/**
 * @brief Draws the background of a single menu item.
 *
 * Renders a bevel frame and gradient fill appropriate for the item's state.
 * Activated items use a subtle tint, disabled items appear lighter, and
 * normal items receive the full bevel contrast.
 *
 * @param view        The view to draw into.
 * @param rect        The item bounding rectangle; inset by drawn borders.
 * @param updateRect  The dirty region.
 * @param base        The item's background colour (typically the selection colour).
 * @param flags       Control-look flags (e.g. @c B_ACTIVATED, @c B_DISABLED).
 * @param borders     Which sides receive a bevel line.
 */
void
HaikuControlLook::DrawMenuItemBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// surface edges
	float topTint;
	float bottomTint;
	rgb_color selectedColor = base;

	if ((flags & B_ACTIVATED) != 0) {
		topTint = 0.9;
		bottomTint = 1.05;
	} else if ((flags & B_DISABLED) != 0) {
		topTint = 0.80;
		bottomTint = 1.07;
	} else {
		topTint = 0.6;
		bottomTint = 1.12;
	}

	rgb_color bevelLightColor = tint_color(selectedColor, topTint);
	rgb_color bevelShadowColor = tint_color(selectedColor, bottomTint);

	// draw surface edges
	_DrawFrame(view, rect,
		bevelLightColor, bevelLightColor,
		bevelShadowColor, bevelShadowColor,
		borders);

	// draw surface top
	view->SetLowColor(selectedColor);
//	_FillGradient(view, rect, selectedColor, topTint, bottomTint);
	_FillGradient(view, rect, selectedColor, bottomTint, topTint);
}


/**
 * @brief Draws a progress/status bar with a filled and an unfilled region.
 *
 * Paints a recessed outer frame, then two interior segments: the filled
 * (progress) side uses a glossy gradient in @a barColor, and the unfilled
 * side uses a lighter gradient derived from @a base.  A shadow line is drawn
 * between the two regions.
 *
 * @param view              The view to draw into.
 * @param rect              The status-bar bounding rectangle; inset during drawing.
 * @param updateRect        The dirty region.
 * @param base              The unfilled background colour.
 * @param barColor          The colour of the filled progress region.
 * @param progressPosition  Pixel X-coordinate of the fill boundary within @a rect.
 */
void
HaikuControlLook::DrawStatusBar(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, const rgb_color& barColor, float progressPosition)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	_DrawOuterResessedFrame(view, rect, base, 0.6);

	// colors
	rgb_color dark1BorderColor = tint_color(base, 1.3);
	rgb_color dark2BorderColor = tint_color(base, 1.2);
	rgb_color dark1FilledBorderColor = tint_color(barColor, 1.20);
	rgb_color dark2FilledBorderColor = tint_color(barColor, 1.45);

	BRect filledRect(rect);
	filledRect.right = progressPosition - 1;

	BRect nonfilledRect(rect);
	nonfilledRect.left = progressPosition;

	bool filledSurface = filledRect.Width() > 0;
	bool nonfilledSurface = nonfilledRect.Width() > 0;

	if (filledSurface) {
		_DrawFrame(view, filledRect,
			dark1FilledBorderColor, dark1FilledBorderColor,
			dark2FilledBorderColor, dark2FilledBorderColor);

		_FillGlossyGradient(view, filledRect, barColor, 0.55, 0.68, 0.76, 0.90);
	}

	if (nonfilledSurface) {
		_DrawFrame(view, nonfilledRect, dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor,
			B_TOP_BORDER | B_BOTTOM_BORDER | B_RIGHT_BORDER);

		if (nonfilledRect.left < nonfilledRect.right) {
			// shadow from fill bar, or left border
			rgb_color leftBorder = dark1BorderColor;
			if (filledSurface)
				leftBorder = tint_color(base, 0.50);
			view->SetHighColor(leftBorder);
			view->StrokeLine(nonfilledRect.LeftTop(),
				nonfilledRect.LeftBottom());
			nonfilledRect.left++;
		}

		_FillGradient(view, nonfilledRect, base, 0.25, 0.06);
	}
}


/**
 * @brief Draws a check box control, including its frame, fill, and optional check mark.
 *
 * Paints a recessed bevel frame, a gradient fill, and—when the control is
 * activated or partially activated—an X-shaped mark or a horizontal bar
 * respectively, scaled to the current font size.
 *
 * @param view        The view to draw into.
 * @param rect        The check-box bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The control's base colour.
 * @param flags       Control-look flags (e.g. @c B_ACTIVATED, @c B_PARTIALLY_ACTIVATED,
 *                    @c B_FOCUSED, @c B_DISABLED, @c B_CLICKED).
 */
void
HaikuControlLook::DrawCheckBox(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color dark1BorderColor;
	rgb_color dark2BorderColor;
	rgb_color navigationColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if ((flags & B_DISABLED) != 0) {
		_DrawOuterResessedFrame(view, rect, base, flags);

		dark1BorderColor = tint_color(base, 1.15);
		dark2BorderColor = tint_color(base, 1.15);
	} else if ((flags & B_CLICKED) != 0) {
		dark1BorderColor = tint_color(base, 1.50);
		dark2BorderColor = tint_color(base, 1.48);

		_DrawFrame(view, rect,
			dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor);

		dark2BorderColor = dark1BorderColor;
	} else {
		_DrawOuterResessedFrame(view, rect, base, flags);

		dark1BorderColor = tint_color(base, 1.40);
		dark2BorderColor = tint_color(base, 1.38);
	}

	if ((flags & B_FOCUSED) != 0) {
		dark1BorderColor = navigationColor;
		dark2BorderColor = navigationColor;
	}

	_DrawFrame(view, rect,
		dark1BorderColor, dark1BorderColor,
		dark2BorderColor, dark2BorderColor);

	if ((flags & B_DISABLED) != 0)
		_FillGradient(view, rect, base, 0.4, 0.2);
	else
		_FillGradient(view, rect, base, 0.15, 0.0);

	rgb_color markColor;
	if (_RadioButtonAndCheckBoxMarkColor(base, markColor, flags)) {
		view->PushState();
		view->SetHighColor(markColor);

		BFont font;
		view->GetFont(&font);
		float inset = std::max(2.0f, roundf(font.Size() / 6));
		rect.InsetBy(inset, inset);

		float penSize = std::max(1.0f, ceilf(rect.Width() / 3.5f));
		if (penSize > 1.0f && fmodf(penSize, 2.0f) == 0.0f) {
			// Tweak ends to "include" the pixel at the index,
			// we need to do this in order to produce results like R5,
			// where coordinates were inclusive
			rect.right++;
			rect.bottom++;
		}

		view->SetDrawingMode(B_OP_OVER);
		view->SetPenSize(penSize);
		if (flags & B_PARTIALLY_ACTIVATED) {
			float x1 = rect.left;
			float x2 = rect.right;
			float y = (rect.top + rect.bottom) / 2;
			view->StrokeLine(BPoint(x1, y), BPoint(x2,y));
		} else {
			view->StrokeLine(rect.LeftTop(), rect.RightBottom());
			view->StrokeLine(rect.LeftBottom(), rect.RightTop());
		}
		view->PopState();
	}
}


/**
 * @brief Draws a radio button control, including its elliptical frame, fill, and dot mark.
 *
 * Renders three concentric ellipses for the bevel/border/fill layers, then
 * paints a filled dot when the control is activated.
 *
 * @param view        The view to draw into.
 * @param rect        The radio-button bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The control's base colour.
 * @param flags       Control-look flags (e.g. @c B_ACTIVATED, @c B_FOCUSED,
 *                    @c B_DISABLED, @c B_CLICKED).
 */
void
HaikuControlLook::DrawRadioButton(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color borderColor;
	rgb_color bevelLight;
	rgb_color bevelShadow;
	rgb_color navigationColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if ((flags & B_DISABLED) != 0) {
		borderColor = tint_color(base, 1.15);
		bevelLight = base;
		bevelShadow = base;
	} else if ((flags & B_CLICKED) != 0) {
		borderColor = tint_color(base, 1.50);
		bevelLight = borderColor;
		bevelShadow = borderColor;
	} else {
		borderColor = tint_color(base, 1.45);
		bevelLight = tint_color(base, 0.55);
		bevelShadow = tint_color(base, 1.11);
	}

	if ((flags & B_FOCUSED) != 0) {
		borderColor = navigationColor;
	}

	BGradientLinear bevelGradient;
	bevelGradient.AddColor(bevelShadow, 0);
	bevelGradient.AddColor(bevelLight, 255);
	bevelGradient.SetStart(rect.LeftTop());
	bevelGradient.SetEnd(rect.RightBottom());

	view->FillEllipse(rect, bevelGradient);
	rect.InsetBy(1, 1);

	bevelGradient.MakeEmpty();
	bevelGradient.AddColor(borderColor, 0);
	bevelGradient.AddColor(tint_color(borderColor, 0.8), 255);
	view->FillEllipse(rect, bevelGradient);
	rect.InsetBy(1, 1);

	float topTint;
	float bottomTint;
	if ((flags & B_DISABLED) != 0) {
		topTint = 0.4;
		bottomTint = 0.2;
	} else {
		topTint = 0.15;
		bottomTint = 0.0;
	}

	BGradientLinear gradient;
	_MakeGradient(gradient, rect, base, topTint, bottomTint);
	view->FillEllipse(rect, gradient);

	rgb_color markColor;
	if (_RadioButtonAndCheckBoxMarkColor(base, markColor, flags)) {
		view->SetHighColor(markColor);
		BFont font;
		view->GetFont(&font);
		float inset = roundf(font.Size() / 4);
		rect.InsetBy(inset, inset);
		view->FillEllipse(rect);
	}
}


/**
 * @brief Draws the outer border stroke of a scroll bar.
 *
 * Strokes a one-pixel border around the entire scroll-bar area.  When the
 * scroll target is focused, the leading edge is drawn in the keyboard
 * navigation highlight colour.
 *
 * @param view        The view to draw into.
 * @param rect        The scroll-bar bounding rectangle.
 * @param updateRect  The dirty region.
 * @param base        The scroll-bar base colour.
 * @param flags       Control-look flags (e.g. @c B_FOCUSED, @c B_DISABLED).
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawScrollBarBorder(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();

	// set clipping constraints to rect
	view->ClipToRect(rect);

	bool isEnabled = (flags & B_DISABLED) == 0;
	bool isFocused = (flags & B_FOCUSED) != 0;

	view->SetHighColor(tint_color(base, B_DARKEN_2_TINT));

	// stroke a line around the entire scrollbar
	// take care of border highlighting, scroll target is focus view
	if (isEnabled && isFocused) {
		rgb_color borderColor = view->HighColor();
		rgb_color highlightColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

		view->BeginLineArray(4);

		view->AddLine(BPoint(rect.left + 1, rect.bottom),
			BPoint(rect.right, rect.bottom), borderColor);
		view->AddLine(BPoint(rect.right, rect.top + 1),
			BPoint(rect.right, rect.bottom - 1), borderColor);

		if (orientation == B_HORIZONTAL) {
			view->AddLine(BPoint(rect.left, rect.top + 1),
				BPoint(rect.left, rect.bottom), borderColor);
		} else {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.left, rect.bottom), highlightColor);
		}

		if (orientation == B_HORIZONTAL) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.right, rect.top), highlightColor);
		} else {
			view->AddLine(BPoint(rect.left + 1, rect.top),
				BPoint(rect.right, rect.top), borderColor);
		}

		view->EndLineArray();
	} else
		view->StrokeRect(rect);

	view->PopState();
}


/**
 * @brief Draws one of the arrow buttons at the end of a scroll bar.
 *
 * Renders the button background using DrawButtonBackground() and overlays
 * an arrow glyph via DrawArrowShape().
 *
 * @param view        The view to draw into.
 * @param rect        The button bounding rectangle.
 * @param updateRect  The dirty region.
 * @param base        The button's base colour.
 * @param text        The arrow glyph colour.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param direction   Arrow direction constant (e.g. @c B_LEFT_ARROW).
 * @param orientation Scroll bar orientation.
 * @param down        @c true when the button is being pressed.
 */
void
HaikuControlLook::DrawScrollBarButton(BView* view, BRect rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& text,
	uint32 flags, int32 direction, orientation orientation, bool down)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();

	// clip to button
	view->ClipToRect(rect);

	bool isEnabled = (flags & B_DISABLED) == 0;

	rgb_color buttonColor = isEnabled ? base : tint_color(base, B_LIGHTEN_1_TINT);
	DrawButtonBackground(view, rect, updateRect, buttonColor, flags,
		BControlLook::B_ALL_BORDERS, orientation);

	rect.InsetBy(-1, -1);
	rgb_color textColor = isEnabled ? text : tint_color(text, B_LIGHTEN_1_TINT);
	DrawArrowShape(view, rect, updateRect, textColor, direction, flags, 1);

	// revert clipping constraints
	view->PopState();
}

/**
 * @brief Draws the trough background of a scroll bar in two separate segments.
 *
 * Convenience overload that calls the single-rect overload for each of the
 * two segments (above/below or left/right of the thumb).
 *
 * @param view        The view to draw into.
 * @param rect1       First background segment; inset during drawing.
 * @param rect2       Second background segment; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The scroll-bar base colour.
 * @param flags       Control-look flags.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawScrollBarBackground(BView* view, BRect& rect1,
	BRect& rect2, const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	DrawScrollBarBackground(view, rect1, updateRect, base, flags, orientation);
	DrawScrollBarBackground(view, rect2, updateRect, base, flags, orientation);
}


/**
 * @brief Draws the trough background of a scroll bar in a single rectangle.
 *
 * Paints edge lines and a gradient fill along the scroll direction.  Enabled
 * bars use darker edge tints; disabled bars use lighter ones.
 *
 * @param view        The view to draw into.
 * @param rect        The background rectangle; inset by painted edges.
 * @param updateRect  The dirty region.
 * @param base        The scroll-bar base colour.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawScrollBarBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();

	// set clipping constraints to rect
	view->ClipToRect(rect);

	bool isEnabled = (flags & B_DISABLED) == 0;

	// fill background, we'll draw arrows and thumb on top
	view->SetDrawingMode(B_OP_COPY);

	float gradient1Tint;
	float gradient2Tint;
	float darkEdge1Tint;
	float darkEdge2Tint;
	float shadowTint;

	if (isEnabled) {
		gradient1Tint = 1.10;
		gradient2Tint = 1.05;
		darkEdge1Tint = B_DARKEN_3_TINT;
		darkEdge2Tint = B_DARKEN_2_TINT;
		shadowTint = gradient1Tint;
	} else {
		gradient1Tint = 0.9;
		gradient2Tint = 0.8;
		darkEdge1Tint = B_DARKEN_2_TINT;
		darkEdge2Tint = B_DARKEN_2_TINT;
		shadowTint = gradient1Tint;
	}

	rgb_color darkEdge1 = tint_color(base, darkEdge1Tint);
	rgb_color darkEdge2 = tint_color(base, darkEdge2Tint);
	rgb_color shadow = tint_color(base, shadowTint);

	if (orientation == B_HORIZONTAL) {
		// dark vertical line on left edge
		if (rect.Width() > 0) {
			view->SetHighColor(darkEdge1);
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
			rect.left++;
		}
		// dark vertical line on right edge
		if (rect.Width() >= 0) {
			view->SetHighColor(darkEdge2);
			view->StrokeLine(rect.RightTop(), rect.RightBottom());
			rect.right--;
		}
		// vertical shadow line after left edge
		if (rect.Width() >= 0) {
			view->SetHighColor(shadow);
			view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
			rect.left++;
		}
		// fill
		if (rect.Width() >= 0) {
			_FillGradient(view, rect, base, gradient1Tint, gradient2Tint,
				orientation);
		}
	} else {
		// dark vertical line on top edge
		if (rect.Height() > 0) {
			view->SetHighColor(darkEdge1);
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
			rect.top++;
		}
		// dark vertical line on bottom edge
		if (rect.Height() >= 0) {
			view->SetHighColor(darkEdge2);
			view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
			rect.bottom--;
		}
		// horizontal shadow line after top edge
		if (rect.Height() >= 0) {
			view->SetHighColor(shadow);
			view->StrokeLine(rect.LeftTop(), rect.RightTop());
			rect.top++;
		}
		// fill
		if (rect.Height() >= 0) {
			_FillGradient(view, rect, base, gradient1Tint, gradient2Tint,
				orientation);
		}
	}

	view->PopState();
}


/**
 * @brief Draws the draggable thumb of a scroll bar.
 *
 * Renders the thumb surface (using DrawButtonBackground() when enabled, or a
 * plain bevel when disabled) and then overlays the optional knob decoration
 * (dots or lines) controlled by @a knobStyle.
 *
 * @param view        The view to draw into.
 * @param rect        The thumb bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The scroll-bar base colour (knob colours are derived from
 *                    @c B_SCROLL_BAR_THUMB_COLOR).
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 * @param knobStyle   One of @c B_KNOB_NONE, @c B_KNOB_DOTS, or @c B_KNOB_LINES.
 */
void
HaikuControlLook::DrawScrollBarThumb(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation, uint32 knobStyle)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	view->PushState();

	// set clipping constraints to rect
	view->ClipToRect(rect);

	// flags
	bool isEnabled = (flags & B_DISABLED) == 0;

	// colors
	rgb_color thumbColor = ui_color(B_SCROLL_BAR_THUMB_COLOR);
	const float bgTint = 1.06;

	rgb_color light, dark, dark1, dark2;
	if (isEnabled) {
		light = tint_color(base, B_LIGHTEN_MAX_TINT);
		dark = tint_color(base, B_DARKEN_3_TINT);
		dark1 = tint_color(base, B_DARKEN_1_TINT);
		dark2 = tint_color(base, B_DARKEN_2_TINT);
	} else {
		light = tint_color(base, B_LIGHTEN_MAX_TINT);
		dark = tint_color(base, B_DARKEN_2_TINT);
		dark1 = tint_color(base, B_LIGHTEN_2_TINT);
		dark2 = tint_color(base, B_LIGHTEN_1_TINT);
	}

	// draw thumb over background
	view->SetDrawingMode(B_OP_OVER);
	view->SetHighColor(dark1);

	// draw scroll thumb
	if (isEnabled) {
		// fill the clickable surface of the thumb
		DrawButtonBackground(view, rect, updateRect, thumbColor, 0,
			B_ALL_BORDERS, orientation);
	} else {
		// thumb bevel
		view->BeginLineArray(4);
		view->AddLine(BPoint(rect.left, rect.bottom),
			BPoint(rect.left, rect.top), light);
		view->AddLine(BPoint(rect.left + 1, rect.top),
			BPoint(rect.right, rect.top), light);
		view->AddLine(BPoint(rect.right, rect.top + 1),
			BPoint(rect.right, rect.bottom), dark2);
		view->AddLine(BPoint(rect.right - 1, rect.bottom),
			BPoint(rect.left + 1, rect.bottom), dark2);
		view->EndLineArray();

		// thumb fill
		rect.InsetBy(1, 1);
		view->SetHighColor(dark1);
		view->FillRect(rect);
	}

	// draw knob style
	if (knobStyle != B_KNOB_NONE) {
		rgb_color knobLight = isEnabled
			? tint_color(thumbColor, B_LIGHTEN_MAX_TINT)
			: tint_color(dark1, bgTint);
		rgb_color knobDark = isEnabled
			? tint_color(thumbColor, 1.22)
			: tint_color(knobLight, B_DARKEN_1_TINT);

		if (knobStyle == B_KNOB_DOTS) {
			// draw dots on the scroll bar thumb
			float hcenter = rect.left + roundf(rect.Width() / 2);
			float vmiddle = rect.top + roundf(rect.Height() / 2);
			BRect knob(hcenter - 1, vmiddle - 1, hcenter, vmiddle);

			if (orientation == B_HORIZONTAL) {
				view->SetHighColor(knobDark);
				view->FillRect(knob);
				view->SetHighColor(knobLight);
				view->FillRect(knob.OffsetByCopy(1, 1));

				float spacer = rect.Height();

				if (rect.left + 3 < hcenter - spacer) {
					view->SetHighColor(knobDark);
					view->FillRect(knob.OffsetByCopy(-spacer, 0));
					view->SetHighColor(knobLight);
					view->FillRect(knob.OffsetByCopy(-spacer + 1, 1));
				}

				if (rect.right - 3 > hcenter + spacer) {
					view->SetHighColor(knobDark);
					view->FillRect(knob.OffsetByCopy(spacer, 0));
					view->SetHighColor(knobLight);
					view->FillRect(knob.OffsetByCopy(spacer + 1, 1));
				}
			} else {
				// B_VERTICAL
				view->SetHighColor(knobDark);
				view->FillRect(knob);
				view->SetHighColor(knobLight);
				view->FillRect(knob.OffsetByCopy(1, 1));

				float spacer = rect.Width();

				if (rect.top + 3 < vmiddle - spacer) {
					view->SetHighColor(knobDark);
					view->FillRect(knob.OffsetByCopy(0, -spacer));
					view->SetHighColor(knobLight);
					view->FillRect(knob.OffsetByCopy(1, -spacer + 1));
				}

				if (rect.bottom - 3 > vmiddle + spacer) {
					view->SetHighColor(knobDark);
					view->FillRect(knob.OffsetByCopy(0, spacer));
					view->SetHighColor(knobLight);
					view->FillRect(knob.OffsetByCopy(1, spacer + 1));
				}
			}
		} else if (knobStyle == B_KNOB_LINES) {
			// draw lines on the scroll bar thumb
			if (orientation == B_HORIZONTAL) {
				float middle = rect.Width() / 2;

				view->BeginLineArray(6);
				view->AddLine(
					BPoint(rect.left + middle - 3, rect.top + 2),
					BPoint(rect.left + middle - 3, rect.bottom - 2),
					knobDark);
				view->AddLine(
					BPoint(rect.left + middle, rect.top + 2),
					BPoint(rect.left + middle, rect.bottom - 2),
					knobDark);
				view->AddLine(
					BPoint(rect.left + middle + 3, rect.top + 2),
					BPoint(rect.left + middle + 3, rect.bottom - 2),
					knobDark);
				view->AddLine(
					BPoint(rect.left + middle - 2, rect.top + 2),
					BPoint(rect.left + middle - 2, rect.bottom - 2),
					knobLight);
				view->AddLine(
					BPoint(rect.left + middle + 1, rect.top + 2),
					BPoint(rect.left + middle + 1, rect.bottom - 2),
					knobLight);
				view->AddLine(
					BPoint(rect.left + middle + 4, rect.top + 2),
					BPoint(rect.left + middle + 4, rect.bottom - 2),
					knobLight);
				view->EndLineArray();
			} else {
				// B_VERTICAL
				float middle = rect.Height() / 2;

				view->BeginLineArray(6);
				view->AddLine(
					BPoint(rect.left + 2, rect.top + middle - 3),
					BPoint(rect.right - 2, rect.top + middle - 3),
					knobDark);
				view->AddLine(
					BPoint(rect.left + 2, rect.top + middle),
					BPoint(rect.right - 2, rect.top + middle),
					knobDark);
				view->AddLine(
					BPoint(rect.left + 2, rect.top + middle + 3),
					BPoint(rect.right - 2, rect.top + middle + 3),
					knobDark);
				view->AddLine(
					BPoint(rect.left + 2, rect.top + middle - 2),
					BPoint(rect.right - 2, rect.top + middle - 2),
					knobLight);
				view->AddLine(
					BPoint(rect.left + 2, rect.top + middle + 1),
					BPoint(rect.right - 2, rect.top + middle + 1),
					knobLight);
				view->AddLine(
					BPoint(rect.left + 2, rect.top + middle + 4),
					BPoint(rect.right - 2, rect.top + middle + 4),
					knobLight);
				view->EndLineArray();
			}
		}
	}

	view->PopState();
}


/**
 * @brief Draws the frame around a scroll view, including scroll-bar borders and corner fill.
 *
 * Handles @c B_NO_BORDER, @c B_PLAIN_BORDER, and @c B_FANCY_BORDER styles.
 * When both scroll bars are present, the corner rectangle between them is
 * filled with @a base.  Each scroll-bar border receives its own recessed-frame
 * treatment for the fancy style.
 *
 * @param view                     The view to draw into.
 * @param rect                     The scroll view bounding rectangle; inset during drawing.
 * @param updateRect               The dirty region.
 * @param verticalScrollBarFrame   The vertical scroll bar's frame (invalid = absent).
 * @param horizontalScrollBarFrame The horizontal scroll bar's frame (invalid = absent).
 * @param base                     The view background colour.
 * @param borderStyle              @c B_NO_BORDER, @c B_PLAIN_BORDER, or @c B_FANCY_BORDER.
 * @param flags                    Control-look flags (e.g. @c B_FOCUSED).
 * @param _borders                 Which sides of the outer frame to draw.
 */
void
HaikuControlLook::DrawScrollViewFrame(BView* view, BRect& rect,
	const BRect& updateRect, BRect verticalScrollBarFrame,
	BRect horizontalScrollBarFrame, const rgb_color& base,
	border_style borderStyle, uint32 flags, uint32 _borders)
{
	// calculate scroll corner rect before messing with the "rect"
	BRect scrollCornerFillRect(rect.right, rect.bottom,
		rect.right, rect.bottom);

	if (horizontalScrollBarFrame.IsValid())
		scrollCornerFillRect.left = horizontalScrollBarFrame.right + 1;

	if (verticalScrollBarFrame.IsValid())
		scrollCornerFillRect.top = verticalScrollBarFrame.bottom + 1;

	if (borderStyle == B_NO_BORDER) {
		if (scrollCornerFillRect.IsValid()) {
			view->SetHighColor(base);
			view->FillRect(scrollCornerFillRect);
		}
		return;
	}

	bool excludeScrollCorner = borderStyle == B_FANCY_BORDER
		&& horizontalScrollBarFrame.IsValid()
		&& verticalScrollBarFrame.IsValid();

	uint32 borders = _borders;
	if (excludeScrollCorner) {
		rect.bottom = horizontalScrollBarFrame.top;
		rect.right = verticalScrollBarFrame.left;
		borders &= ~(B_RIGHT_BORDER | B_BOTTOM_BORDER);
	}

	rgb_color scrollbarFrameColor = tint_color(base, B_DARKEN_2_TINT);

	if (borderStyle == B_FANCY_BORDER)
		_DrawOuterResessedFrame(view, rect, base, flags, borders);

	if ((flags & B_FOCUSED) != 0) {
		rgb_color focusColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		_DrawFrame(view, rect, focusColor, focusColor, focusColor, focusColor,
			borders);
	} else {
		_DrawFrame(view, rect, scrollbarFrameColor, scrollbarFrameColor,
			scrollbarFrameColor, scrollbarFrameColor, borders);
	}

	if (excludeScrollCorner) {
		horizontalScrollBarFrame.InsetBy(-1, -1);
		// do not overdraw the top edge
		horizontalScrollBarFrame.top += 2;
		borders = _borders;
		borders &= ~B_TOP_BORDER;
		_DrawOuterResessedFrame(view, horizontalScrollBarFrame, base,
			flags, borders);
		_DrawFrame(view, horizontalScrollBarFrame, scrollbarFrameColor,
			scrollbarFrameColor, scrollbarFrameColor, scrollbarFrameColor,
			borders);

		verticalScrollBarFrame.InsetBy(-1, -1);
		// do not overdraw the left edge
		verticalScrollBarFrame.left += 2;
		borders = _borders;
		borders &= ~B_LEFT_BORDER;
		_DrawOuterResessedFrame(view, verticalScrollBarFrame, base,
			flags, borders);
		_DrawFrame(view, verticalScrollBarFrame, scrollbarFrameColor,
			scrollbarFrameColor, scrollbarFrameColor, scrollbarFrameColor,
			borders);

		// exclude recessed frame
		scrollCornerFillRect.top++;
		scrollCornerFillRect.left++;
	}

	if (scrollCornerFillRect.IsValid()) {
		view->SetHighColor(base);
		view->FillRect(scrollCornerFillRect);
	}
}


/**
 * @brief Draws a triangular arrow glyph inside @a rect.
 *
 * Computes three vertices for the chosen @a direction, then strokes a filled
 * BShape with a pen width proportional to the rectangle size.  The arrow is
 * dimmed when @c B_DISABLED is set in @a flags.
 *
 * @param view        The view to draw into.
 * @param rect        The bounding rectangle for the arrow glyph; inset during drawing.
 * @param updateRect  The dirty region (unused — drawing is always performed).
 * @param base        The colour from which the arrow colour is derived.
 * @param direction   One of @c B_LEFT_ARROW, @c B_RIGHT_ARROW, @c B_UP_ARROW,
 *                    @c B_DOWN_ARROW, @c B_LEFT_UP_ARROW, @c B_RIGHT_UP_ARROW,
 *                    @c B_RIGHT_DOWN_ARROW, or @c B_LEFT_DOWN_ARROW.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param tint        Tint applied to @a base to produce the arrow colour; dimmed
 *                    further when the control is disabled.
 */
void
HaikuControlLook::DrawArrowShape(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 direction,
	uint32 flags, float tint)
{
	BPoint tri1, tri2, tri3;
	float hInset = rect.Width() / 3;
	float vInset = rect.Height() / 3;
	rect.InsetBy(hInset, vInset);

	switch (direction) {
		case B_LEFT_ARROW:
			tri1.Set(rect.right, rect.top);
			tri2.Set(rect.right - rect.Width() / 1.33,
				(rect.top + rect.bottom + 1) / 2);
			tri3.Set(rect.right, rect.bottom + 1);
			break;
		case B_RIGHT_ARROW:
			tri1.Set(rect.left + 1, rect.bottom + 1);
			tri2.Set(rect.left + 1 + rect.Width() / 1.33,
				(rect.top + rect.bottom + 1) / 2);
			tri3.Set(rect.left + 1, rect.top);
			break;
		case B_UP_ARROW:
			tri1.Set(rect.left, rect.bottom);
			tri2.Set((rect.left + rect.right + 1) / 2,
				rect.bottom - rect.Height() / 1.33);
			tri3.Set(rect.right + 1, rect.bottom);
			break;
		case B_DOWN_ARROW:
		default:
			tri1.Set(rect.left, rect.top + 1);
			tri2.Set((rect.left + rect.right + 1) / 2,
				rect.top + 1 + rect.Height() / 1.33);
			tri3.Set(rect.right + 1, rect.top + 1);
			break;
		case B_LEFT_UP_ARROW:
			tri1.Set(rect.left, rect.bottom);
			tri2.Set(rect.left, rect.top);
			tri3.Set(rect.right - 1, rect.top);
			break;
		case B_RIGHT_UP_ARROW:
			tri1.Set(rect.left + 1, rect.top);
			tri2.Set(rect.right, rect.top);
			tri3.Set(rect.right, rect.bottom);
			break;
		case B_RIGHT_DOWN_ARROW:
			tri1.Set(rect.right, rect.top);
			tri2.Set(rect.right, rect.bottom);
			tri3.Set(rect.left + 1, rect.bottom);
			break;
		case B_LEFT_DOWN_ARROW:
			tri1.Set(rect.right - 1, rect.bottom);
			tri2.Set(rect.left, rect.bottom);
			tri3.Set(rect.left, rect.top);
			break;
	}

	BShape arrowShape;
	arrowShape.MoveTo(tri1);
	arrowShape.LineTo(tri2);
	arrowShape.LineTo(tri3);

	if ((flags & B_DISABLED) != 0)
		tint = (tint + B_NO_TINT + B_NO_TINT) / 3;

	view->SetHighColor(tint_color(base, tint));

	float penSize = view->PenSize();
	drawing_mode mode = view->DrawingMode();

	view->MovePenTo(BPoint(0, 0));

	view->SetPenSize(ceilf(hInset / 2.0));
	view->SetDrawingMode(B_OP_OVER);
	view->StrokeShape(&arrowShape);

	view->SetPenSize(penSize);
	view->SetDrawingMode(mode);
}


/**
 * @brief Returns the default fill colour for a slider bar.
 *
 * @param base  The panel background colour; currently unused — the result is
 *              always derived from @c B_PANEL_BACKGROUND_COLOR.
 * @return A slightly darkened version of the panel background colour.
 */
rgb_color
HaikuControlLook::SliderBarColor(const rgb_color& base)
{
	return tint_color(ui_color(B_PANEL_BACKGROUND_COLOR), B_DARKEN_1_TINT);
}


/**
 * @brief Draws a slider bar with separately coloured left and right fill regions.
 *
 * Splits @a rect at @a sliderScale into two sub-rects and draws each half
 * using the single-colour overload, clipped to the respective region.
 *
 * @param view            The view to draw into.
 * @param rect            The full slider-bar bounding rectangle.
 * @param updateRect      The dirty region.
 * @param base            The panel background colour (used to erase corners).
 * @param leftFillColor   Fill colour for the region to the left of the thumb.
 * @param rightFillColor  Fill colour for the region to the right of the thumb.
 * @param sliderScale     Thumb position as a fraction [0, 1] of the bar width/height.
 * @param flags           Control-look flags.
 * @param orientation     @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawSliderBar(BView* view, BRect rect, const BRect& updateRect,
	const rgb_color& base, rgb_color leftFillColor, rgb_color rightFillColor,
	float sliderScale, uint32 flags, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// separate the bar in two sides
	float sliderPosition;
	BRect leftBarSide = rect;
	BRect rightBarSide = rect;

	if (orientation == B_HORIZONTAL) {
		sliderPosition = floorf(rect.left + 2 + (rect.Width() - 2)
			* sliderScale);
		leftBarSide.right = sliderPosition - 1;
		rightBarSide.left = sliderPosition;
	} else {
		// NOTE: position is reverse of coords
		sliderPosition = floorf(rect.top + 2 + (rect.Height() - 2)
			* (1.0 - sliderScale));
		leftBarSide.top = sliderPosition;
		rightBarSide.bottom = sliderPosition - 1;
	}

	view->PushState();
	view->ClipToRect(leftBarSide);
	DrawSliderBar(view, rect, updateRect, base, leftFillColor, flags,
		orientation);
	view->PopState();

	view->PushState();
	view->ClipToRect(rightBarSide);
	DrawSliderBar(view, rect, updateRect, base, rightFillColor, flags,
		orientation);
	view->PopState();
}


/**
 * @brief Draws a slider bar with a single uniform fill colour.
 *
 * Paints rounded end caps via _DrawRoundBarCorner() and a straight bar
 * segment with a four-line edge/frame bevel, then fills the interior with
 * a two-stop gradient derived from @a fillColor.
 *
 * @param view        The view to draw into.
 * @param rect        The slider-bar bounding rectangle.
 * @param updateRect  The dirty region.
 * @param base        The panel background colour (used to clear corner areas).
 * @param fillColor   The bar fill colour; blended with @a base when disabled.
 * @param flags       Control-look flags (e.g. @c B_DISABLED, @c B_BLEND_FRAME).
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawSliderBar(BView* view, BRect rect, const BRect& updateRect,
	const rgb_color& base, rgb_color fillColor, uint32 flags,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// separate the rect into corners
	BRect leftCorner(rect);
	BRect rightCorner(rect);
	BRect barRect(rect);

	if (orientation == B_HORIZONTAL) {
		leftCorner.right = leftCorner.left + leftCorner.Height();
		rightCorner.left = rightCorner.right - rightCorner.Height();
		barRect.left += ceilf(barRect.Height() / 2);
		barRect.right -= ceilf(barRect.Height() / 2);
	} else {
		leftCorner.bottom = leftCorner.top + leftCorner.Width();
		rightCorner.top = rightCorner.bottom - rightCorner.Width();
		barRect.top += ceilf(barRect.Width() / 2);
		barRect.bottom -= ceilf(barRect.Width() / 2);
	}

	// fill the background for the corners, exclude the middle bar for now
	view->PushState();
	view->ClipToRect(rect);
	view->ClipToInverseRect(barRect);

	if ((flags & B_BLEND_FRAME) == 0) {
		view->SetHighColor(base);
		view->FillRect(rect);
	}

	// figure out the tints to be used
	float edgeLightTint;
	float edgeShadowTint;
	float frameLightTint;
	float frameShadowTint;
	float fillLightTint;
	float fillShadowTint;
	uint8 edgeLightAlpha;
	uint8 edgeShadowAlpha;
	uint8 frameLightAlpha;
	uint8 frameShadowAlpha;

	if ((flags & B_DISABLED) != 0) {
		edgeLightTint = 1.0;
		edgeShadowTint = 1.0;
		frameLightTint = 1.20;
		frameShadowTint = 1.25;
		fillLightTint = 0.9;
		fillShadowTint = 1.05;
		edgeLightAlpha = 12;
		edgeShadowAlpha = 12;
		frameLightAlpha = 40;
		frameShadowAlpha = 45;

		fillColor.red = uint8(fillColor.red * 0.4 + base.red * 0.6);
		fillColor.green = uint8(fillColor.green * 0.4 + base.green * 0.6);
		fillColor.blue = uint8(fillColor.blue * 0.4 + base.blue * 0.6);
	} else {
		edgeLightTint = 0.65;
		edgeShadowTint = 1.07;
		frameLightTint = 1.40;
		frameShadowTint = 1.50;
		fillLightTint = 0.8;
		fillShadowTint = 1.1;
		edgeLightAlpha = 15;
		edgeShadowAlpha = 15;
		frameLightAlpha = 92;
		frameShadowAlpha = 107;
	}

	rgb_color edgeLightColor;
	rgb_color edgeShadowColor;
	rgb_color frameLightColor;
	rgb_color frameShadowColor;
	rgb_color fillLightColor = tint_color(fillColor, fillLightTint);
	rgb_color fillShadowColor = tint_color(fillColor, fillShadowTint);

	drawing_mode oldMode = view->DrawingMode();

	if ((flags & B_BLEND_FRAME) != 0) {
		edgeLightColor = (rgb_color){ 255, 255, 255, edgeLightAlpha };
		edgeShadowColor = (rgb_color){ 0, 0, 0, edgeShadowAlpha };
		frameLightColor = (rgb_color){ 0, 0, 0, frameLightAlpha };
		frameShadowColor = (rgb_color){ 0, 0, 0, frameShadowAlpha };

		view->SetDrawingMode(B_OP_ALPHA);
	} else {
		edgeLightColor = tint_color(base, edgeLightTint);
		edgeShadowColor = tint_color(base, edgeShadowTint);
		frameLightColor = tint_color(fillColor, frameLightTint);
		frameShadowColor = tint_color(fillColor, frameShadowTint);
	}

	if (orientation == B_HORIZONTAL) {
		_DrawRoundBarCorner(view, leftCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 1.0, 1.0, 0.0, -1.0, orientation);

		_DrawRoundBarCorner(view, rightCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 0.0, 1.0, -1.0, -1.0, orientation);
	} else {
		_DrawRoundBarCorner(view, leftCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 1.0, 1.0, -1.0, 0.0, orientation);

		_DrawRoundBarCorner(view, rightCorner, updateRect, edgeLightColor,
			edgeShadowColor, frameLightColor, frameShadowColor, fillLightColor,
			fillShadowColor, 1.0, 0.0, -1.0, -1.0, orientation);
	}

	view->PopState();
	if ((flags & B_BLEND_FRAME) != 0)
		view->SetDrawingMode(B_OP_ALPHA);

	view->BeginLineArray(4);
	if (orientation == B_HORIZONTAL) {
		view->AddLine(barRect.LeftTop(), barRect.RightTop(),
			edgeShadowColor);
		view->AddLine(barRect.LeftBottom(), barRect.RightBottom(),
			edgeLightColor);
		barRect.InsetBy(0, 1);
		view->AddLine(barRect.LeftTop(), barRect.RightTop(),
			frameShadowColor);
		view->AddLine(barRect.LeftBottom(), barRect.RightBottom(),
			frameLightColor);
		barRect.InsetBy(0, 1);
	} else {
		view->AddLine(barRect.LeftTop(), barRect.LeftBottom(),
			edgeShadowColor);
		view->AddLine(barRect.RightTop(), barRect.RightBottom(),
			edgeLightColor);
		barRect.InsetBy(1, 0);
		view->AddLine(barRect.LeftTop(), barRect.LeftBottom(),
			frameShadowColor);
		view->AddLine(barRect.RightTop(), barRect.RightBottom(),
			frameLightColor);
		barRect.InsetBy(1, 0);
	}
	view->EndLineArray();

	view->SetDrawingMode(oldMode);

	_FillGradient(view, barRect, fillColor, fillShadowTint, fillLightTint,
		orientation);
}


/**
 * @brief Draws the rectangular thumb widget of a slider control.
 *
 * Renders a framed button background with an alpha-blended drop shadow and
 * a central orientation line (vertical groove for horizontal sliders, or
 * horizontal groove for vertical sliders).
 *
 * @param view        The view to draw into.
 * @param rect        The thumb bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The thumb's base colour.
 * @param flags       Control-look flags (e.g. @c B_FOCUSED, @c B_DISABLED).
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawSliderThumb(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 flags, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// figure out frame color
	rgb_color frameLightColor;
	rgb_color frameShadowColor;
	rgb_color shadowColor = (rgb_color){ 0, 0, 0, 60 };

	if ((flags & B_FOCUSED) != 0) {
		// focused
		frameLightColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		frameShadowColor = frameLightColor;
	} else {
		// figure out the tints to be used
		float frameLightTint;
		float frameShadowTint;

		if ((flags & B_DISABLED) != 0) {
			frameLightTint = 1.30;
			frameShadowTint = 1.35;
			shadowColor.alpha = 30;
		} else {
			frameLightTint = 1.6;
			frameShadowTint = 1.65;
		}

		frameLightColor = tint_color(base, frameLightTint);
		frameShadowColor = tint_color(base, frameShadowTint);
	}

	BRect originalRect(rect);
	rect.right--;
	rect.bottom--;

	view->PushState();

	_DrawFrame(view, rect, frameLightColor, frameLightColor,
		frameShadowColor, frameShadowColor);

	flags &= ~B_ACTIVATED;
	DrawButtonBackground(view, rect, updateRect, base, flags);

	// thumb shadow
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(shadowColor);
	originalRect.left++;
	originalRect.top++;
	view->StrokeLine(originalRect.LeftBottom(), originalRect.RightBottom());
	originalRect.bottom--;
	view->StrokeLine(originalRect.RightTop(), originalRect.RightBottom());

	// thumb edge
	if (orientation == B_HORIZONTAL) {
		rect.InsetBy(0, floorf(rect.Height() / 4));
		rect.left = floorf((rect.left + rect.right) / 2);
		rect.right = rect.left + 1;
		shadowColor = tint_color(base, B_DARKEN_2_TINT);
		shadowColor.alpha = 128;
		view->SetHighColor(shadowColor);
		view->StrokeLine(rect.LeftTop(), rect.LeftBottom());
		rgb_color lightColor = tint_color(base, B_LIGHTEN_2_TINT);
		lightColor.alpha = 128;
		view->SetHighColor(lightColor);
		view->StrokeLine(rect.RightTop(), rect.RightBottom());
	} else {
		rect.InsetBy(floorf(rect.Width() / 4), 0);
		rect.top = floorf((rect.top + rect.bottom) / 2);
		rect.bottom = rect.top + 1;
		shadowColor = tint_color(base, B_DARKEN_2_TINT);
		shadowColor.alpha = 128;
		view->SetHighColor(shadowColor);
		view->StrokeLine(rect.LeftTop(), rect.RightTop());
		rgb_color lightColor = tint_color(base, B_LIGHTEN_2_TINT);
		lightColor.alpha = 128;
		view->SetHighColor(lightColor);
		view->StrokeLine(rect.LeftBottom(), rect.RightBottom());
	}

	view->PopState();
}


/**
 * @brief Draws a triangular slider thumb using the base colour for both frame and fill.
 *
 * Convenience overload that passes @a base as the fill colour.
 *
 * @param view        The view to draw into.
 * @param rect        The thumb bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The thumb's base colour (used for frame and fill).
 * @param flags       Control-look flags.
 * @param orientation @c B_HORIZONTAL (points up) or @c B_VERTICAL (points left).
 */
void
HaikuControlLook::DrawSliderTriangle(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	orientation orientation)
{
	DrawSliderTriangle(view, rect, updateRect, base, base, flags, orientation);
}


/**
 * @brief Draws a triangular slider thumb with a separate fill colour.
 *
 * Builds a BShape pointing toward the slider bar, strokes a shadow using
 * alpha blending, draws the frame outline, and fills the interior with a
 * glossy gradient or a simple gradient when disabled.
 *
 * @param view        The view to draw into.
 * @param rect        The thumb bounding rectangle; inset by 1 px for the shadow.
 * @param updateRect  The dirty region.
 * @param base        The colour used to derive the frame tints.
 * @param fill        The fill colour used for the interior gradient.
 * @param flags       Control-look flags (e.g. @c B_DISABLED, @c B_FOCUSED, @c B_HOVER).
 * @param orientation @c B_HORIZONTAL (triangle points up) or @c B_VERTICAL (points left).
 */
void
HaikuControlLook::DrawSliderTriangle(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, const rgb_color& fill,
	uint32 flags, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// figure out frame color
	rgb_color frameLightColor;
	rgb_color frameShadowColor;
	rgb_color shadowColor = (rgb_color){ 0, 0, 0, 60 };

	float topTint = 0.49;
	float middleTint1 = 0.62;
	float middleTint2 = 0.76;
	float bottomTint = 0.90;

	if ((flags & B_DISABLED) != 0) {
		topTint = (topTint + B_NO_TINT) / 2;
		middleTint1 = (middleTint1 + B_NO_TINT) / 2;
		middleTint2 = (middleTint2 + B_NO_TINT) / 2;
		bottomTint = (bottomTint + B_NO_TINT) / 2;
	} else if ((flags & B_HOVER) != 0) {
		topTint *= kHoverTintFactor;
		middleTint1 *= kHoverTintFactor;
		middleTint2 *= kHoverTintFactor;
		bottomTint *= kHoverTintFactor;
	}

	if ((flags & B_FOCUSED) != 0) {
		// focused
		frameLightColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
		frameShadowColor = frameLightColor;
	} else {
		// figure out the tints to be used
		float frameLightTint;
		float frameShadowTint;

		if ((flags & B_DISABLED) != 0) {
			frameLightTint = 1.30;
			frameShadowTint = 1.35;
			shadowColor.alpha = 30;
		} else {
			frameLightTint = 1.6;
			frameShadowTint = 1.65;
		}

		frameLightColor = tint_color(base, frameLightTint);
		frameShadowColor = tint_color(base, frameShadowTint);
	}

	// make room for the shadow
	rect.right--;
	rect.bottom--;

	view->PushState();

	uint32 viewFlags = view->Flags();
	view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);
	view->SetLineMode(B_ROUND_CAP, B_ROUND_JOIN);

	float centerh = (rect.left + rect.right) / 2;
	float centerv = (rect.top + rect.bottom) / 2;

	BShape shape;
	if (orientation == B_HORIZONTAL) {
		shape.MoveTo(BPoint(rect.left + 0.5, rect.bottom + 0.5));
		shape.LineTo(BPoint(rect.right + 0.5, rect.bottom + 0.5));
		shape.LineTo(BPoint(rect.right + 0.5, rect.bottom - 1 + 0.5));
		shape.LineTo(BPoint(centerh + 0.5, rect.top + 0.5));
		shape.LineTo(BPoint(rect.left + 0.5, rect.bottom - 1 + 0.5));
	} else {
		shape.MoveTo(BPoint(rect.right + 0.5, rect.top + 0.5));
		shape.LineTo(BPoint(rect.right + 0.5, rect.bottom + 0.5));
		shape.LineTo(BPoint(rect.right - 1 + 0.5, rect.bottom + 0.5));
		shape.LineTo(BPoint(rect.left + 0.5, centerv + 0.5));
		shape.LineTo(BPoint(rect.right - 1 + 0.5, rect.top + 0.5));
	}
	shape.Close();

	view->MovePenTo(BPoint(1, 1));

	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(shadowColor);
	view->StrokeShape(&shape);

	view->MovePenTo(B_ORIGIN);

	view->SetDrawingMode(B_OP_COPY);
	view->SetHighColor(frameLightColor);
	view->StrokeShape(&shape);

	rect.InsetBy(1, 1);
	shape.Clear();
	if (orientation == B_HORIZONTAL) {
		shape.MoveTo(BPoint(rect.left, rect.bottom + 1));
		shape.LineTo(BPoint(rect.right + 1, rect.bottom + 1));
		shape.LineTo(BPoint(centerh + 0.5, rect.top));
	} else {
		shape.MoveTo(BPoint(rect.right + 1, rect.top));
		shape.LineTo(BPoint(rect.right + 1, rect.bottom + 1));
		shape.LineTo(BPoint(rect.left, centerv + 0.5));
	}
	shape.Close();

	BGradientLinear gradient;
	if ((flags & B_DISABLED) != 0) {
		_MakeGradient(gradient, rect, fill, topTint, bottomTint);
	} else {
		_MakeGlossyGradient(gradient, rect, fill, topTint, middleTint1,
			middleTint2, bottomTint);
	}

	view->FillShape(&shape, gradient);

	view->SetFlags(viewFlags);
	view->PopState();
}


/**
 * @brief Draws tick marks along the edge(s) of a slider bar.
 *
 * Distributes @a count evenly-spaced hash marks above/left and/or
 * below/right of the slider track.  Each mark is two pixels wide:
 * a dark line and a light offset line for a bevelled appearance.
 *
 * @param view        The view to draw into.
 * @param rect        The area in which to draw the hash marks.
 * @param updateRect  The dirty region.
 * @param base        The control's base colour used to derive light/dark tints.
 * @param count       The total number of marks to draw (minimum 2).
 * @param location    @c B_HASH_MARKS_TOP, @c B_HASH_MARKS_BOTTOM, or both OR'd together.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::DrawSliderHashMarks(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, int32 count,
	hash_mark_location location, uint32 flags, orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color lightColor;
	rgb_color darkColor;

	if ((flags & B_DISABLED) != 0) {
		lightColor = tint_color(base, 0.9);
		darkColor = tint_color(base, 1.07);
	} else {
		lightColor = tint_color(base, 0.8);
		darkColor = tint_color(base, 1.14);
	}

	int32 hashMarkCount = std::max(count, (int32)2);
		// draw at least two hashmarks at min/max if
		// fHashMarks != B_HASH_MARKS_NONE
	float factor;
	float startPos;
	if (orientation == B_HORIZONTAL) {
		factor = (rect.Width() - 2) / (hashMarkCount - 1);
		startPos = rect.left + 1;
	} else {
		factor = (rect.Height() - 2) / (hashMarkCount - 1);
		startPos = rect.top + 1;
	}

	if (location & B_HASH_MARKS_TOP) {
		view->BeginLineArray(hashMarkCount * 2);

		if (orientation == B_HORIZONTAL) {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(pos, rect.top),
							  BPoint(pos, rect.top + 4), darkColor);
				view->AddLine(BPoint(pos + 1, rect.top),
							  BPoint(pos + 1, rect.top + 4), lightColor);

				pos += factor;
			}
		} else {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(rect.left, pos),
							  BPoint(rect.left + 4, pos), darkColor);
				view->AddLine(BPoint(rect.left, pos + 1),
							  BPoint(rect.left + 4, pos + 1), lightColor);

				pos += factor;
			}
		}

		view->EndLineArray();
	}

	if ((location & B_HASH_MARKS_BOTTOM) != 0) {
		view->BeginLineArray(hashMarkCount * 2);

		if (orientation == B_HORIZONTAL) {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(pos, rect.bottom - 4),
							  BPoint(pos, rect.bottom), darkColor);
				view->AddLine(BPoint(pos + 1, rect.bottom - 4),
							  BPoint(pos + 1, rect.bottom), lightColor);

				pos += factor;
			}
		} else {
			float pos = startPos;
			for (int32 i = 0; i < hashMarkCount; i++) {
				view->AddLine(BPoint(rect.right - 4, pos),
							  BPoint(rect.right, pos), darkColor);
				view->AddLine(BPoint(rect.right - 4, pos + 1),
							  BPoint(rect.right, pos + 1), lightColor);

				pos += factor;
			}
		}

		view->EndLineArray();
	}
}


/**
 * @brief Draws the frame strip behind all tabs of a tab view.
 *
 * Selects the appropriate border sides based on @a side and @a borderStyle,
 * then calls DrawInactiveTab() to paint the entire frame rectangle in the
 * inactive tab style.
 *
 * @param view        The view to draw into.
 * @param rect        The tab-frame bounding rectangle; may be widened by 1 px
 *                    when @a borderStyle is @c B_PLAIN_BORDER.
 * @param updateRect  The dirty region.
 * @param base        The tab background colour.
 * @param flags       Control-look flags.
 * @param borders     Which outer sides of the tab frame are drawn.
 * @param borderStyle @c B_NO_BORDER, @c B_PLAIN_BORDER, or @c B_FANCY_BORDER.
 * @param side        Which edge of the content area the tabs are attached to
 *                    (e.g. @c BTabView::kTopSide).
 */
void
HaikuControlLook::DrawTabFrame(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, border_style borderStyle, uint32 side)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	if (side == BTabView::kTopSide || side == BTabView::kBottomSide) {
		// draw an inactive tab frame behind all tabs
		borders = B_TOP_BORDER | B_BOTTOM_BORDER;
		if (borderStyle != B_NO_BORDER)
			borders |= B_LEFT_BORDER | B_RIGHT_BORDER;

		// DrawInactiveTab draws 2px border
		// draw tab frame wider to align B_PLAIN_BORDER with it
		if (borderStyle == B_PLAIN_BORDER)
			rect.InsetBy(-1, 0);
	} else if (side == BTabView::kLeftSide || side == BTabView::kRightSide) {
		// draw an inactive tab frame behind all tabs
		borders = B_LEFT_BORDER | B_RIGHT_BORDER;
		if (borderStyle != B_NO_BORDER)
			borders |= B_TOP_BORDER | B_BOTTOM_BORDER;

		// DrawInactiveTab draws 2px border
		// draw tab frame wider to align B_PLAIN_BORDER with it
		if (borderStyle == B_PLAIN_BORDER)
			rect.InsetBy(0, -1);
	}

	DrawInactiveTab(view, rect, rect, base, 0, borders, side);
}


/**
 * @brief Draws the currently selected (active) tab of a tab view.
 *
 * Paints rounded corners on the two leading edges of the tab appropriate for
 * @a side, then draws the edge, frame, and bevel layers before filling with a
 * vertical gradient.  The active tab appears lighter than inactive tabs and
 * stands proud of the tab frame.
 *
 * @param view        The view to draw into.
 * @param rect        The active tab bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The tab background colour.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param borders     Which sides receive a border line.
 * @param side        Tab attachment edge (e.g. @c B_TOP_BORDER, @c B_LEFT_BORDER).
 */
void
HaikuControlLook::DrawActiveTab(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, uint32 side, int32, int32, int32, int32)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// Snap the rectangle to pixels to avoid rounding errors.
	rect.left = floorf(rect.left);
	rect.right = floorf(rect.right);
	rect.top = floorf(rect.top);
	rect.bottom = floorf(rect.bottom);

	// save the clipping constraints of the view
	view->PushState();

	// set clipping constraints to rect
	view->ClipToRect(rect);

	rgb_color edgeShadowColor;
	rgb_color edgeLightColor;
	rgb_color frameShadowColor;
	rgb_color frameLightColor;
	rgb_color bevelShadowColor;
	rgb_color bevelLightColor;
	BGradientLinear fillGradient;
	fillGradient.SetStart(rect.LeftTop() + BPoint(3, 3));
	fillGradient.SetEnd(rect.LeftBottom() + BPoint(3, -3));

	if ((flags & B_DISABLED) != 0) {
		edgeLightColor = base;
		edgeShadowColor = base;
		frameLightColor = tint_color(base, 1.25);
		frameShadowColor = tint_color(base, 1.30);
		bevelLightColor = tint_color(base, 0.8);
		bevelShadowColor = tint_color(base, 1.07);
		fillGradient.AddColor(tint_color(base, 0.85), 0);
		fillGradient.AddColor(base, 255);
	} else {
		edgeLightColor = tint_color(base, 0.80);
		edgeShadowColor = tint_color(base, 1.03);
		frameLightColor = tint_color(base, 1.30);
		frameShadowColor = tint_color(base, 1.30);
		bevelLightColor = tint_color(base, 0.6);
		bevelShadowColor = tint_color(base, 1.07);
		fillGradient.AddColor(tint_color(base, 0.75), 0);
		fillGradient.AddColor(tint_color(base, 1.03), 255);
	}

	static const float kRoundCornerRadius = 4.0f;

	// left top corner dimensions
	BRect leftTopCorner(rect);
	leftTopCorner.right = floorf(leftTopCorner.left + kRoundCornerRadius);
	leftTopCorner.bottom = floorf(rect.top + kRoundCornerRadius);

	// right top corner dimensions
	BRect rightTopCorner(rect);
	rightTopCorner.left = floorf(rightTopCorner.right - kRoundCornerRadius);
	rightTopCorner.bottom = floorf(rect.top + kRoundCornerRadius);

	// left bottom corner dimensions
	BRect leftBottomCorner(rect);
	leftBottomCorner.right = floorf(leftBottomCorner.left + kRoundCornerRadius);
	leftBottomCorner.top = floorf(rect.bottom - kRoundCornerRadius);

	// right bottom corner dimensions
	BRect rightBottomCorner(rect);
	rightBottomCorner.left = floorf(rightBottomCorner.right
		- kRoundCornerRadius);
	rightBottomCorner.top = floorf(rect.bottom - kRoundCornerRadius);

	BRect roundCorner[2];

	switch (side) {
		case B_TOP_BORDER:
			roundCorner[0] = leftTopCorner;
			roundCorner[1] = rightTopCorner;

			// draw the left top corner
			_DrawRoundCornerLeftTop(view, leftTopCorner, updateRect, base,
				edgeShadowColor, frameLightColor, bevelLightColor,
				fillGradient);
			// draw the right top corner
			_DrawRoundCornerRightTop(view, rightTopCorner, updateRect, base,
				edgeShadowColor, edgeLightColor, frameLightColor,
				frameShadowColor, bevelLightColor, bevelShadowColor,
				fillGradient);
			break;
		case B_BOTTOM_BORDER:
			roundCorner[0] = leftBottomCorner;
			roundCorner[1] = rightBottomCorner;

			// draw the left bottom corner
			_DrawRoundCornerLeftBottom(view, leftBottomCorner, updateRect, base,
				edgeShadowColor, edgeLightColor, frameLightColor,
				frameShadowColor, bevelLightColor, bevelShadowColor,
				fillGradient);
			// draw the right bottom corner
			_DrawRoundCornerRightBottom(view, rightBottomCorner, updateRect,
				base, edgeLightColor, frameShadowColor, bevelShadowColor,
				fillGradient);
			break;
		case B_LEFT_BORDER:
			roundCorner[0] = leftTopCorner;
			roundCorner[1] = leftBottomCorner;

			// draw the left top corner
			_DrawRoundCornerLeftTop(view, leftTopCorner, updateRect, base,
				edgeShadowColor, frameLightColor, bevelLightColor,
				fillGradient);
			// draw the left bottom corner
			_DrawRoundCornerLeftBottom(view, leftBottomCorner, updateRect, base,
				edgeShadowColor, edgeLightColor, frameLightColor,
				frameShadowColor, bevelLightColor, bevelShadowColor,
				fillGradient);
			break;
		case B_RIGHT_BORDER:
			roundCorner[0] = rightTopCorner;
			roundCorner[1] = rightBottomCorner;

			// draw the right top corner
			_DrawRoundCornerRightTop(view, rightTopCorner, updateRect, base,
				edgeShadowColor, edgeLightColor, frameLightColor,
				frameShadowColor, bevelLightColor, bevelShadowColor,
				fillGradient);
			// draw the right bottom corner
			_DrawRoundCornerRightBottom(view, rightBottomCorner, updateRect,
				base, edgeLightColor, frameShadowColor, bevelShadowColor,
				fillGradient);
			break;
	}

	// clip out the corners
	view->ClipToInverseRect(roundCorner[0]);
	view->ClipToInverseRect(roundCorner[1]);

	uint32 bordersToDraw = 0;
	switch (side) {
		case B_TOP_BORDER:
			bordersToDraw = (B_LEFT_BORDER | B_TOP_BORDER | B_RIGHT_BORDER);
			break;
		case B_BOTTOM_BORDER:
			bordersToDraw = (B_LEFT_BORDER | B_BOTTOM_BORDER | B_RIGHT_BORDER);
			break;
		case B_LEFT_BORDER:
			bordersToDraw = (B_LEFT_BORDER | B_BOTTOM_BORDER | B_TOP_BORDER);
			break;
		case B_RIGHT_BORDER:
			bordersToDraw = (B_RIGHT_BORDER | B_BOTTOM_BORDER | B_TOP_BORDER);
			break;
	}

	// draw the rest of frame and fill
	_DrawFrame(view, rect, edgeShadowColor, edgeShadowColor, edgeLightColor,
		edgeLightColor, borders);
	if (side == B_TOP_BORDER || side == B_BOTTOM_BORDER) {
		if ((borders & B_LEFT_BORDER) == 0)
			rect.left++;
		if ((borders & B_RIGHT_BORDER) == 0)
			rect.right--;
	} else if (side == B_LEFT_BORDER || side == B_RIGHT_BORDER) {
		if ((borders & B_TOP_BORDER) == 0)
			rect.top++;
		if ((borders & B_BOTTOM_BORDER) == 0)
			rect.bottom--;
	}

	_DrawFrame(view, rect, frameLightColor, frameLightColor, frameShadowColor,
		frameShadowColor, bordersToDraw);

	_DrawFrame(view, rect, bevelLightColor, bevelLightColor, bevelShadowColor,
		bevelShadowColor);

	view->FillRect(rect, fillGradient);

	// restore the clipping constraints of the view
	view->PopState();
}


/**
 * @brief Draws an inactive (unselected) tab of a tab view.
 *
 * The inactive tab is inset from the leading edge by 4 pixels to appear
 * recessed relative to the active tab, and its background strip is filled
 * with the @a base colour.  Bevel and frame lines are drawn in darker tints
 * than the active tab.
 *
 * @param view        The view to draw into.
 * @param rect        The inactive-tab bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The tab background colour.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param borders     Which sides receive a border line.
 * @param side        Tab attachment edge (e.g. @c BTabView::kTopSide).
 */
void
HaikuControlLook::DrawInactiveTab(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, uint32 side, int32, int32, int32, int32)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color edgeShadowColor;
	rgb_color edgeLightColor;
	rgb_color frameShadowColor;
	rgb_color frameLightColor;
	rgb_color bevelShadowColor;
	rgb_color bevelLightColor;
	BGradientLinear fillGradient;
	fillGradient.SetStart(rect.LeftTop() + BPoint(3, 3));
	fillGradient.SetEnd(rect.LeftBottom() + BPoint(3, -3));

	if ((flags & B_DISABLED) != 0) {
		edgeLightColor = base;
		edgeShadowColor = base;
		frameLightColor = tint_color(base, 1.25);
		frameShadowColor = tint_color(base, 1.30);
		bevelLightColor = tint_color(base, 0.8);
		bevelShadowColor = tint_color(base, 1.07);
		fillGradient.AddColor(tint_color(base, 0.85), 0);
		fillGradient.AddColor(base, 255);
	} else {
		edgeLightColor = tint_color(base, 0.80);
		edgeShadowColor = tint_color(base, 1.03);
		frameLightColor = tint_color(base, 1.30);
		frameShadowColor = tint_color(base, 1.30);
		bevelLightColor = tint_color(base, 1.10);
		bevelShadowColor = tint_color(base, 1.17);
		fillGradient.AddColor(tint_color(base, 1.12), 0);
		fillGradient.AddColor(tint_color(base, 1.08), 255);
	}

	BRect background = rect;
	bool isVertical;
	switch (side) {
		default:
		case BTabView::kTopSide:
			rect.top += 4;
			background.bottom = rect.top;
			isVertical = false;
			break;

		case BTabView::kBottomSide:
			rect.bottom -= 4;
			background.top = rect.bottom;
			isVertical = false;
			break;

		case BTabView::kLeftSide:
			rect.left += 4;
			background.right = rect.left;
			isVertical = true;
			break;

		case BTabView::kRightSide:
			rect.right -= 4;
			background.left = rect.right;
			isVertical = true;
			break;
	}

	// active tabs stand out at the top, but this is an inactive tab
	view->SetHighColor(base);
	view->FillRect(background);

	// frame and fill
	// Note that _DrawFrame also insets the rect, so each of the calls here
	// operate on a smaller rect than the previous ones
	_DrawFrame(view, rect, edgeShadowColor, edgeShadowColor, edgeLightColor,
		edgeLightColor, borders);

	_DrawFrame(view, rect, frameLightColor, frameLightColor, frameShadowColor,
		frameShadowColor, borders);

	if (rect.IsValid()) {
		if (isVertical) {
			_DrawFrame(view, rect, bevelShadowColor, bevelShadowColor,
				bevelLightColor, bevelLightColor, B_TOP_BORDER & ~borders);
		} else {
			_DrawFrame(view, rect, bevelShadowColor, bevelShadowColor,
				bevelLightColor, bevelLightColor, B_LEFT_BORDER & ~borders);
		}
	} else {
		if (isVertical) {
			if ((B_LEFT_BORDER & ~borders) != 0)
				rect.left++;
		} else {
			if ((B_TOP_BORDER & ~borders) != 0)
				rect.top++;
		}
	}

	view->FillRect(rect, fillGradient);
}


/**
 * @brief Draws a splitter divider bar between two panes.
 *
 * Optionally draws a raised border around the splitter, then fills the
 * background on both sides of the centre line and paints a repeating
 * three-pixel pattern of background/shadow/light dots along the divider.
 *
 * @param view        The view to draw into.
 * @param rect        The splitter bounding rectangle.
 * @param updateRect  The dirty region.
 * @param base        The splitter's base colour; darkened by 1 tint when clicked.
 * @param orientation @c B_HORIZONTAL (the divider runs left-right) or @c B_VERTICAL.
 * @param flags       Control-look flags (e.g. @c B_CLICKED, @c B_ACTIVATED).
 * @param borders     Which sides receive a raised border; pass 0 to suppress borders.
 */
void
HaikuControlLook::DrawSplitter(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, orientation orientation, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color background;
	if ((flags & (B_CLICKED | B_ACTIVATED)) != 0)
		background = tint_color(base, B_DARKEN_1_TINT);
	else
		background = base;

	rgb_color light = tint_color(background, 0.6);
	rgb_color shadow = tint_color(background, 1.21);

	// frame
	if (borders != 0 && rect.Width() > 3 && rect.Height() > 3)
		DrawRaisedBorder(view, rect, updateRect, background, flags, borders);

	// dots and rest of background
	if (orientation == B_HORIZONTAL) {
		if (rect.Width() > 2) {
			// background on left/right
			BRegion region(rect);
			rect.left = floorf((rect.left + rect.right) / 2.0 - 0.5);
			rect.right = rect.left + 1;
			region.Exclude(rect);
			view->SetHighColor(background);
			view->FillRegion(&region);
		}

		BPoint dot = rect.LeftTop();
		BPoint stop = rect.LeftBottom();
		int32 num = 1;
		while (dot.y <= stop.y) {
			rgb_color col1;
			rgb_color col2;
			switch (num) {
				case 1:
					col1 = background;
					col2 = background;
					break;
				case 2:
					col1 = shadow;
					col2 = background;
					break;
				case 3:
				default:
					col1 = background;
					col2 = light;
					num = 0;
					break;
			}
			view->SetHighColor(col1);
			view->StrokeLine(dot, dot, B_SOLID_HIGH);
			view->SetHighColor(col2);
			dot.x++;
			view->StrokeLine(dot, dot, B_SOLID_HIGH);
			dot.x -= 1.0;
			// next pixel
			num++;
			dot.y++;
		}
	} else {
		if (rect.Height() > 2) {
			// background on left/right
			BRegion region(rect);
			rect.top = floorf((rect.top + rect.bottom) / 2.0 - 0.5);
			rect.bottom = rect.top + 1;
			region.Exclude(rect);
			view->SetHighColor(background);
			view->FillRegion(&region);
		}

		BPoint dot = rect.LeftTop();
		BPoint stop = rect.RightTop();
		int32 num = 1;
		while (dot.x <= stop.x) {
			rgb_color col1;
			rgb_color col2;
			switch (num) {
				case 1:
					col1 = background;
					col2 = background;
					break;
				case 2:
					col1 = shadow;
					col2 = background;
					break;
				case 3:
				default:
					col1 = background;
					col2 = light;
					num = 0;
					break;
			}
			view->SetHighColor(col1);
			view->StrokeLine(dot, dot, B_SOLID_HIGH);
			view->SetHighColor(col2);
			dot.y++;
			view->StrokeLine(dot, dot, B_SOLID_HIGH);
			dot.y -= 1.0;
			// next pixel
			num++;
			dot.x++;
		}
	}
}


// #pragma mark -


/**
 * @brief Draws a plain or fancy border around an arbitrary rectangular area.
 *
 * For @c B_FANCY_BORDER a recessed outer edge is drawn first; then a single
 * dark frame stroke is applied.  Focused controls use the keyboard navigation
 * highlight colour.  @c B_NO_BORDER is a no-op.
 *
 * @param view        The view to draw into.
 * @param rect        The bordered rectangle; inset by drawn layers.
 * @param updateRect  The dirty region.
 * @param base        The background colour used to derive the frame tint.
 * @param borderStyle @c B_NO_BORDER, @c B_PLAIN_BORDER, or @c B_FANCY_BORDER.
 * @param flags       Control-look flags (e.g. @c B_FOCUSED).
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::DrawBorder(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, border_style borderStyle, uint32 flags,
	uint32 borders)
{
	if (borderStyle == B_NO_BORDER)
		return;

	rgb_color scrollbarFrameColor = tint_color(base, B_DARKEN_2_TINT);
	if (base.red + base.green + base.blue <= 128 * 3) {
		scrollbarFrameColor = tint_color(base, B_LIGHTEN_1_TINT);
	}

	if ((flags & B_FOCUSED) != 0)
		scrollbarFrameColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if (borderStyle == B_FANCY_BORDER)
		_DrawOuterResessedFrame(view, rect, base, flags, borders);

	_DrawFrame(view, rect, scrollbarFrameColor, scrollbarFrameColor,
		scrollbarFrameColor, scrollbarFrameColor, borders);
}


/**
 * @brief Draws a single-pixel raised bevel border.
 *
 * Uses a light tint on the top and left edges and a shadow tint on the
 * bottom and right edges.  Disabled controls use the base colour on all sides
 * (flat appearance).
 *
 * @param view        The view to draw into.
 * @param rect        The bordered rectangle; inset by drawn edges.
 * @param updateRect  The dirty region.
 * @param base        The background colour used to derive the bevel tints.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::DrawRaisedBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	rgb_color lightColor;
	rgb_color shadowColor;

	if ((flags & B_DISABLED) != 0) {
		lightColor = base;
		shadowColor = base;
	} else {
		lightColor = tint_color(base, 0.85);
		shadowColor = tint_color(base, 1.07);
	}

	_DrawFrame(view, rect, lightColor, lightColor, shadowColor, shadowColor,
		borders);
}


/**
 * @brief Draws the border around a text input control.
 *
 * Renders a recessed outer frame followed by a dark inner frame.  The frame
 * colour is overridden by the keyboard-navigation highlight when focused, and
 * by @c B_FAILURE_COLOR when the @c B_INVALID flag is set.  Alpha-blended
 * rendering is used when @c B_BLEND_FRAME is set.
 *
 * @param view        The view to draw into.
 * @param rect        The text-control bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The control's background colour.
 * @param flags       Control-look flags (e.g. @c B_FOCUSED, @c B_INVALID,
 *                    @c B_DISABLED, @c B_BLEND_FRAME).
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::DrawTextControlBorder(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	rgb_color dark1BorderColor;
	rgb_color dark2BorderColor;
	rgb_color navigationColor = ui_color(B_KEYBOARD_NAVIGATION_COLOR);
	rgb_color invalidColor = ui_color(B_FAILURE_COLOR);

	if ((flags & B_DISABLED) != 0) {
		_DrawOuterResessedFrame(view, rect, base, flags, borders);

		if ((flags & B_BLEND_FRAME) != 0)
			dark1BorderColor = (rgb_color){ 0, 0, 0, 40 };
		else
			dark1BorderColor = tint_color(base, 1.15);
		dark2BorderColor = dark1BorderColor;
	} else if ((flags & B_CLICKED) != 0) {
		dark1BorderColor = tint_color(base, 1.50);
		dark2BorderColor = tint_color(base, 1.49);

		// BCheckBox uses this to indicate the clicked state...
		_DrawFrame(view, rect,
			dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor);

		dark2BorderColor = dark1BorderColor;
	} else {
		_DrawOuterResessedFrame(view, rect, base, flags, borders);

		if ((flags & B_BLEND_FRAME) != 0) {
			dark1BorderColor = (rgb_color){ 0, 0, 0, 102 };
			dark2BorderColor = (rgb_color){ 0, 0, 0, 97 };
		} else {
			dark1BorderColor = tint_color(base, 1.40);
			dark2BorderColor = tint_color(base, 1.38);
		}
	}

	if ((flags & B_DISABLED) == 0 && (flags & B_FOCUSED) != 0) {
		dark1BorderColor = navigationColor;
		dark2BorderColor = navigationColor;
	}

	if ((flags & B_DISABLED) == 0 && (flags & B_INVALID) != 0) {
		dark1BorderColor = invalidColor;
		dark2BorderColor = invalidColor;
	}

	if ((flags & B_BLEND_FRAME) != 0) {
		drawing_mode oldMode = view->DrawingMode();
		view->SetDrawingMode(B_OP_ALPHA);

		_DrawFrame(view, rect,
			dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor, borders);

		view->SetDrawingMode(oldMode);
	} else {
		_DrawFrame(view, rect,
			dark1BorderColor, dark1BorderColor,
			dark2BorderColor, dark2BorderColor, borders);
	}
}


/**
 * @brief Draws a three-layer group-box frame.
 *
 * Paints an outer shadow, a darker inner frame, and an inner highlight in
 * three consecutive _DrawFrame() calls, giving the appearance of a recessed
 * panel border suitable for grouping controls.
 *
 * @param view        The view to draw into.
 * @param rect        The group-frame bounding rectangle; inset by each drawn layer.
 * @param updateRect  The dirty region.
 * @param base        The panel background colour used to derive the bevel tints.
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::DrawGroupFrame(BView* view, BRect& rect, const BRect& updateRect,
	const rgb_color& base, uint32 borders)
{
	rgb_color frameColor = tint_color(base, 1.30);
	rgb_color bevelLight = tint_color(base, 0.8);
	rgb_color bevelShadow = tint_color(base, 1.03);

	_DrawFrame(view, rect, bevelShadow, bevelShadow, bevelLight, bevelLight,
		borders);

	_DrawFrame(view, rect, frameColor, frameColor, frameColor, frameColor,
		borders);

	_DrawFrame(view, rect, bevelLight, bevelLight, bevelShadow, bevelShadow,
		borders);
}


/**
 * @brief Draws a text label using the default alignment.
 *
 * Convenience overload that uses DefaultLabelAlignment() and passes @c NULL
 * for the icon.
 *
 * @param view        The view to draw into.
 * @param label       The UTF-8 string to draw; @c NULL is silently ignored.
 * @param rect        The bounding rectangle available for the label.
 * @param updateRect  The dirty region.
 * @param base        The background colour used for disabled-colour blending.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param textColor   Optional override for the text colour; pass @c NULL to use
 *                    the default derived from the parent view or UI colour.
 */
void
HaikuControlLook::DrawLabel(BView* view, const char* label, BRect rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	const rgb_color* textColor)
{
	DrawLabel(view, label, NULL, rect, updateRect, base, flags,
		DefaultLabelAlignment(), textColor);
}


/**
 * @brief Draws a text label with explicit alignment.
 *
 * Convenience overload that passes @c NULL for the icon.
 *
 * @param view        The view to draw into.
 * @param label       The UTF-8 string to draw.
 * @param rect        The bounding rectangle available for the label.
 * @param updateRect  The dirty region.
 * @param base        The background colour used for disabled-colour blending.
 * @param flags       Control-look flags.
 * @param alignment   How to align the label within @a rect.
 * @param textColor   Optional text colour override; @c NULL uses the default.
 */
void
HaikuControlLook::DrawLabel(BView* view, const char* label, BRect rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	const BAlignment& alignment, const rgb_color* textColor)
{
	DrawLabel(view, label, NULL, rect, updateRect, base, flags, alignment,
		textColor);
}


/**
 * @brief Draws a text label at an explicit baseline position.
 *
 * Handles desktop-window label rendering including glow/outline effects
 * (reading the workspace background configuration) and disabled-state colour
 * blending.  For desktop labels the glow colour is chosen to contrast with
 * the desktop colour.
 *
 * @param view       The view to draw into.
 * @param label      The UTF-8 string to draw.
 * @param base       The background colour used for disabled-colour blending and
 *                   desktop brightness comparisons.
 * @param flags      Control-look flags (e.g. @c B_DISABLED, @c B_IGNORE_OUTLINE).
 * @param where      The baseline origin for the string.
 * @param textColor  Optional text colour override; @c NULL uses the default.
 * @note Desktop-label outline settings are cached per-workspace and refreshed
 *       lazily on each workspace change.
 */
void
HaikuControlLook::DrawLabel(BView* view, const char* label, const rgb_color& base,
	uint32 flags, const BPoint& where, const rgb_color* textColor)
{
	// setup the text color

	BWindow* window = view->Window();
	bool isDesktop = window != NULL && window->Feel() == kDesktopWindowFeel
		&& window->Look() == kDesktopWindowLook && view->Parent() != NULL
		&& view->Parent()->Parent() == NULL && (flags & B_IGNORE_OUTLINE) == 0;

	rgb_color low;
	rgb_color color;
	rgb_color glowColor;

	if (textColor != NULL)
		glowColor = *textColor;
	else if (view->Parent() != NULL)
		glowColor = view->Parent()->HighColor();
	else if ((flags & B_IS_CONTROL) != 0)
		glowColor = ui_color(B_CONTROL_TEXT_COLOR);
	else
		glowColor = ui_color(B_PANEL_TEXT_COLOR);

	color = glowColor;

	if (isDesktop)
		low = view->Parent()->ViewColor();
	else
		low = base;

	view->PushState();

	if (isDesktop) {
		// enforce proper use of desktop label colors
		if (low.Brightness() <= ui_color(B_DESKTOP_COLOR).Brightness()) {
			color = kWhite;
			glowColor = kBlack;
		} else {
			color = kBlack;
			glowColor = kWhite;
		}

		// drawing occurs on the desktop
		if (fCachedWorkspace != current_workspace()) {
			int8 indice = 0;
			int32 mask;
			bool tmpOutline;
			while (fBackgroundInfo.FindInt32("be:bgndimginfoworkspaces",
					indice, &mask) == B_OK
				&& fBackgroundInfo.FindBool("be:bgndimginfoerasetext",
					indice, &tmpOutline) == B_OK) {

				if (((1 << current_workspace()) & mask) != 0) {
					fCachedOutline = tmpOutline;
					fCachedWorkspace = current_workspace();
					break;
				}
				indice++;
			}
		}

		if (fCachedOutline) {
			BFont font;
			view->GetFont(&font);

			view->SetDrawingMode(B_OP_ALPHA);
			view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_OVERLAY);

			// Draw glow or outline
			if (glowColor.IsLight()) {
				font.SetFalseBoldWidth(2.0);
				view->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);

				glowColor.alpha = 30;
				view->SetHighColor(glowColor);
				view->DrawString(label, where);

				font.SetFalseBoldWidth(1.0);
				view->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);

				glowColor.alpha = 65;
				view->SetHighColor(glowColor);
				view->DrawString(label, where);

				font.SetFalseBoldWidth(0.0);
				view->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);
			} else {
				font.SetFalseBoldWidth(1.0);
				view->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);

				glowColor.alpha = 30;
				view->SetHighColor(glowColor);
				view->DrawString(label, where);

				font.SetFalseBoldWidth(0.0);
				view->SetFont(&font, B_FONT_FALSE_BOLD_WIDTH);

				glowColor.alpha = 200;
				view->SetHighColor(glowColor);
				view->DrawString(label, BPoint(where.x + 1, where.y + 1));
			}
		}
	}

	if ((flags & B_DISABLED) != 0)
		color = disable_color(color, low);

	view->SetHighColor(color);
	view->SetDrawingMode(B_OP_OVER);
	view->DrawString(label, where);
	view->PopState();
}


/**
 * @brief Draws a label with an optional icon bitmap, respecting alignment.
 *
 * If both @a label and @a icon are provided the icon is placed to the left
 * of the text with DefaultLabelSpacing() between them.  The combined width is
 * aligned within @a rect according to @a alignment.  The label string is
 * truncated with an ellipsis if it exceeds the available width.
 *
 * @param view        The view to draw into.
 * @param label       The UTF-8 label string; @c NULL renders the icon only.
 * @param icon        Optional icon bitmap; @c NULL renders the label only.
 * @param rect        The bounding rectangle for the label + icon.
 * @param updateRect  The dirty region.
 * @param base        The background colour used for disabled-colour blending.
 * @param flags       Control-look flags (e.g. @c B_DISABLED).
 * @param alignment   Alignment of the combined content within @a rect.
 * @param textColor   Optional text colour override; @c NULL uses the default.
 */
void
HaikuControlLook::DrawLabel(BView* view, const char* label, const BBitmap* icon,
	BRect rect, const BRect& updateRect, const rgb_color& base, uint32 flags,
	const BAlignment& alignment, const rgb_color* textColor)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	if (label == NULL && icon == NULL)
		return;

	if (label == NULL) {
		// icon only
		BRect alignedRect = BLayoutUtils::AlignInFrame(rect,
			icon->Bounds().Size(), alignment);
		drawing_mode oldMode = view->DrawingMode();
		view->SetDrawingMode(B_OP_OVER);
		view->DrawBitmap(icon, alignedRect.LeftTop());
		view->SetDrawingMode(oldMode);
		return;
	}

	// label, possibly with icon
	float availableWidth = rect.Width() + 1;
	float width = 0;
	float textOffset = 0;
	float height = 0;

	if (icon != NULL) {
		width = icon->Bounds().Width() + DefaultLabelSpacing() + 1;
		height = icon->Bounds().Height() + 1;
		textOffset = width;
		availableWidth -= textOffset;
	}

	// truncate the label if necessary and get the width and height
	BString truncatedLabel(label);

	BFont font;
	view->GetFont(&font);

	font.TruncateString(&truncatedLabel, B_TRUNCATE_END, availableWidth);
	width += ceilf(font.StringWidth(truncatedLabel.String()));

	font_height fontHeight;
	font.GetHeight(&fontHeight);
	float textHeight = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
	height = std::max(height, textHeight);

	// handle alignment
	BRect alignedRect(BLayoutUtils::AlignOnRect(rect,
		BSize(width - 1, height - 1), alignment));

	if (icon != NULL) {
		BPoint location(alignedRect.LeftTop());
		if (icon->Bounds().Height() + 1 < height)
			location.y += ceilf((height - icon->Bounds().Height() - 1) / 2);

		drawing_mode oldMode = view->DrawingMode();
		view->SetDrawingMode(B_OP_OVER);
		view->DrawBitmap(icon, location);
		view->SetDrawingMode(oldMode);
	}

	BPoint location(alignedRect.left + textOffset,
		alignedRect.top + ceilf(fontHeight.ascent));
	if (textHeight < height)
		location.y += ceilf((height - textHeight) / 2);

	DrawLabel(view, truncatedLabel.String(), base, flags, location, textColor);
}


/**
 * @brief Returns the pixel insets consumed by a control frame on each side.
 *
 * The inset values are scaled proportionally to the current plain-font size
 * so that controls scale correctly with user font-size preferences.
 *
 * @param frameType  The type of frame (e.g. @c B_BUTTON_FRAME, @c B_TEXT_CONTROL_FRAME).
 * @param flags      Control-look flags; @c B_DEFAULT_BUTTON increases the button inset.
 * @param[out] _left    Left inset in pixels.
 * @param[out] _top     Top inset in pixels.
 * @param[out] _right   Right inset in pixels.
 * @param[out] _bottom  Bottom inset in pixels.
 */
void
HaikuControlLook::GetFrameInsets(frame_type frameType, uint32 flags, float& _left,
	float& _top, float& _right, float& _bottom)
{
	// All frames have the same inset on each side.
	float inset = 0;

	switch (frameType) {
		case B_BUTTON_FRAME:
			inset = (flags & B_DEFAULT_BUTTON) != 0 ? 5 : 2;
			break;
		case B_GROUP_FRAME:
		case B_MENU_FIELD_FRAME:
			inset = 3;
			break;
		case B_SCROLL_VIEW_FRAME:
		case B_TEXT_CONTROL_FRAME:
			inset = 2;
			break;
	}

	inset = ceilf(inset * (be_plain_font->Size() / 12.0f));

	_left = inset;
	_top = inset;
	_right = inset;
	_bottom = inset;
}


/**
 * @brief Returns the pixel insets consumed by a control background on each side.
 *
 * Most backgrounds use a uniform 1-pixel inset.  Scroll-bar backgrounds and
 * the button-with-pop-up background have asymmetric insets that are returned
 * directly without font scaling.
 *
 * @param backgroundType  The type of background (e.g. @c B_BUTTON_BACKGROUND,
 *                        @c B_HORIZONTAL_SCROLL_BAR_BACKGROUND).
 * @param flags           Control-look flags (currently unused).
 * @param[out] _left    Left inset in pixels.
 * @param[out] _top     Top inset in pixels.
 * @param[out] _right   Right inset in pixels.
 * @param[out] _bottom  Bottom inset in pixels.
 */
void
HaikuControlLook::GetBackgroundInsets(background_type backgroundType,
	uint32 flags, float& _left, float& _top, float& _right, float& _bottom)
{
	// Most backgrounds have the same inset on each side.
	float inset = 0;

	switch (backgroundType) {
		case B_BUTTON_BACKGROUND:
		case B_MENU_BACKGROUND:
		case B_MENU_BAR_BACKGROUND:
		case B_MENU_FIELD_BACKGROUND:
		case B_MENU_ITEM_BACKGROUND:
			inset = 1;
			break;
		case B_BUTTON_WITH_POP_UP_BACKGROUND:
			_left = 1;
			_top = 1;
			_right = 1 + ComposeSpacing(kButtonPopUpIndicatorWidth);
			_bottom = 1;
			return;
		case B_HORIZONTAL_SCROLL_BAR_BACKGROUND:
			_left = 2;
			_top = 0;
			_right = 1;
			_bottom = 0;
			return;
		case B_VERTICAL_SCROLL_BAR_BACKGROUND:
			_left = 0;
			_top = 2;
			_right = 0;
			_bottom = 1;
			return;
	}

	_left = inset;
	_top = inset;
	_right = inset;
	_bottom = inset;
}


/**
 * @brief Draws the background of a button that includes a pop-up indicator, with square corners.
 *
 * Delegates to _DrawButtonBackground() with @c popupIndicator = @c true, causing
 * a right-side separator and down-arrow marker to be rendered.
 *
 * @param view        The view to draw into.
 * @param rect        The full button+indicator bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param base        The button's base colour.
 * @param flags       Control-look flags.
 * @param borders     Which sides receive a bevel line.
 * @param orientation Gradient direction.
 */
void
HaikuControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	_DrawButtonBackground(view, rect, updateRect, 0.0f, 0.0f, 0.0f, 0.0f,
		base, true, flags, borders, orientation);
}


/**
 * @brief Draws the background of a button with a pop-up indicator and uniform rounded corners.
 *
 * @param view        The view to draw into.
 * @param rect        The full button+indicator bounding rectangle; inset during drawing.
 * @param updateRect  The dirty region.
 * @param radius      Corner radius applied uniformly.
 * @param base        The button's base colour.
 * @param flags       Control-look flags.
 * @param borders     Which sides receive a bevel line.
 * @param orientation Gradient direction.
 */
void
HaikuControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float radius, const rgb_color& base, uint32 flags,
	uint32 borders, orientation orientation)
{
	_DrawButtonBackground(view, rect, updateRect, radius, radius, radius,
		radius, base, true, flags, borders, orientation);
}


/**
 * @brief Draws the background of a button with a pop-up indicator and individual corner radii.
 *
 * @param view               The view to draw into.
 * @param rect               The full button+indicator bounding rectangle; inset during drawing.
 * @param updateRect         The dirty region.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The button's base colour.
 * @param flags              Control-look flags.
 * @param borders            Which sides receive a bevel line.
 * @param orientation        Gradient direction.
 */
void
HaikuControlLook::DrawButtonWithPopUpBackground(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	uint32 flags, uint32 borders, orientation orientation)
{
	_DrawButtonBackground(view, rect, updateRect, leftTopRadius,
		rightTopRadius, leftBottomRadius, rightBottomRadius, base, true, flags,
		borders, orientation);
}


// #pragma mark -


/**
 * @brief Internal implementation for all DrawButtonFrame() overloads.
 *
 * Draws the default-button indicator ring (if @c B_DEFAULT_BUTTON is set),
 * the outer recessed edge, optional rounded corners, and the dark frame border.
 * Flat buttons that are neither hovered nor focused skip all drawing.
 *
 * @param view               The view to draw into.
 * @param rect               The button bounding rectangle; inset by each drawn layer.
 * @param updateRect         The dirty region; drawing is skipped when rect is invalid.
 * @param leftTopRadius      Radius of the top-left rounded corner (0 = square).
 * @param rightTopRadius     Radius of the top-right rounded corner.
 * @param leftBottomRadius   Radius of the bottom-left rounded corner.
 * @param rightBottomRadius  Radius of the bottom-right rounded corner.
 * @param base               The button's base colour.
 * @param background         The parent background colour for the outer edge.
 * @param flags              Control-look flags.
 * @param borders            Which sides to draw.
 */
void
HaikuControlLook::_DrawButtonFrame(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	const rgb_color& background, uint32 flags, uint32 borders)
{
	if (!rect.IsValid())
		return;

	// save the clipping constraints of the view
	view->PushState();

	// set clipping constraints to rect
	view->ClipToRect(rect);

	// If the button is flat and neither activated nor otherwise highlighted
	// (mouse hovering or focussed), draw it flat.
	if ((flags & B_FLAT) != 0
		&& (flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED)) == 0
		&& ((flags & (B_HOVER | B_FOCUSED)) == 0
			|| (flags & B_DISABLED) != 0)) {
		_DrawFrame(view, rect, background, background, background,
			background, borders);
		_DrawFrame(view, rect, background, background, background,
			background, borders);
		view->PopState();
		return;
	}

	// outer edge colors
	rgb_color edgeLightColor;
	rgb_color edgeShadowColor;

	// default button frame color
	rgb_color defaultIndicatorColor = ui_color(B_CONTROL_BORDER_COLOR);
	rgb_color cornerBgColor;

	if ((flags & B_DISABLED) != 0) {
		defaultIndicatorColor = disable_color(defaultIndicatorColor,
			background);
	}

	drawing_mode oldMode = view->DrawingMode();

	if ((flags & B_DEFAULT_BUTTON) != 0) {
		cornerBgColor = defaultIndicatorColor;
		edgeLightColor = _EdgeColor(defaultIndicatorColor, false, flags);
		edgeShadowColor = _EdgeColor(defaultIndicatorColor, true, flags);

		// draw default button indicator
		// Allow a 1-pixel border of the background to come through.
		rect.InsetBy(1, 1);

		view->SetHighColor(defaultIndicatorColor);
		view->StrokeRoundRect(rect, leftTopRadius, leftTopRadius);
		rect.InsetBy(1, 1);

		view->StrokeRoundRect(rect, leftTopRadius, leftTopRadius);
		rect.InsetBy(1, 1);
	} else {
		cornerBgColor = background;
		if ((flags & B_BLEND_FRAME) != 0) {
			// set the background color to transparent for the case
			// that we are on the desktop
			cornerBgColor.alpha = 0;
			view->SetDrawingMode(B_OP_ALPHA);
		}

		edgeLightColor = _EdgeColor(background, false, flags);
		edgeShadowColor = _EdgeColor(background, true, flags);
	}

	// frame colors
	rgb_color frameLightColor  = _FrameLightColor(base, flags);
	rgb_color frameShadowColor = _FrameShadowColor(base, flags);

	// rounded corners

	if ((borders & B_LEFT_BORDER) != 0 && (borders & B_TOP_BORDER) != 0
		&& leftTopRadius > 0) {
		// draw left top rounded corner
		BRect leftTopCorner(floorf(rect.left), floorf(rect.top),
			floorf(rect.left + leftTopRadius),
			floorf(rect.top + leftTopRadius));
		BRect cornerRect(leftTopCorner);
		_DrawRoundCornerFrameLeftTop(view, leftTopCorner, updateRect,
			cornerBgColor, edgeShadowColor, frameLightColor);
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_TOP_BORDER) != 0 && (borders & B_RIGHT_BORDER) != 0
		&& rightTopRadius > 0) {
		// draw right top rounded corner
		BRect rightTopCorner(floorf(rect.right - rightTopRadius),
			floorf(rect.top), floorf(rect.right),
			floorf(rect.top + rightTopRadius));
		BRect cornerRect(rightTopCorner);
		_DrawRoundCornerFrameRightTop(view, rightTopCorner, updateRect,
			cornerBgColor, edgeShadowColor, edgeLightColor,
			frameLightColor, frameShadowColor);
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_LEFT_BORDER) != 0 && (borders & B_BOTTOM_BORDER) != 0
		&& leftBottomRadius > 0) {
		// draw left bottom rounded corner
		BRect leftBottomCorner(floorf(rect.left),
			floorf(rect.bottom - leftBottomRadius),
			floorf(rect.left + leftBottomRadius), floorf(rect.bottom));
		BRect cornerRect(leftBottomCorner);
		_DrawRoundCornerFrameLeftBottom(view, leftBottomCorner, updateRect,
			cornerBgColor, edgeShadowColor, edgeLightColor,
			frameLightColor, frameShadowColor);
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_RIGHT_BORDER) != 0 && (borders & B_BOTTOM_BORDER) != 0
		&& rightBottomRadius > 0) {
		// draw right bottom rounded corner
		BRect rightBottomCorner(floorf(rect.right - rightBottomRadius),
			floorf(rect.bottom - rightBottomRadius), floorf(rect.right),
			floorf(rect.bottom));
		BRect cornerRect(rightBottomCorner);
		_DrawRoundCornerFrameRightBottom(view, rightBottomCorner,
			updateRect, cornerBgColor, edgeLightColor, frameShadowColor);
		view->ClipToInverseRect(cornerRect);
	}

	// draw outer edge
	if ((flags & B_DEFAULT_BUTTON) != 0) {
		_DrawOuterResessedFrame(view, rect, defaultIndicatorColor,
			flags, borders);
	} else {
		_DrawOuterResessedFrame(view, rect, background,
			flags, borders);
	}

	view->SetDrawingMode(oldMode);

	// draw frame
	if ((flags & B_BLEND_FRAME) != 0) {
		drawing_mode oldDrawingMode = view->DrawingMode();
		view->SetDrawingMode(B_OP_ALPHA);

		_DrawFrame(view, rect, frameLightColor, frameLightColor,
			frameShadowColor, frameShadowColor, borders);

		view->SetDrawingMode(oldDrawingMode);
	} else {
		_DrawFrame(view, rect, frameLightColor, frameLightColor,
			frameShadowColor, frameShadowColor, borders);
	}

	// restore the clipping constraints of the view
	view->PopState();
}


/**
 * @brief Draws the outer recessed (shadow/light) edge shared by many control types.
 *
 * Computes the edge shadow and light colours via _EdgeColor() and calls
 * _DrawFrame().  When @c B_BLEND_FRAME is set the drawing mode is temporarily
 * switched to @c B_OP_ALPHA.
 *
 * @param view     The view to draw into.
 * @param rect     The bounding rectangle; inset by the drawn edges.
 * @param base     The colour from which edge tints are derived.
 * @param flags    Control-look flags (e.g. @c B_DISABLED, @c B_BLEND_FRAME,
 *                 @c B_DEFAULT_BUTTON).
 * @param borders  Which sides to draw.
 */
void
HaikuControlLook::_DrawOuterResessedFrame(BView* view, BRect& rect,
	const rgb_color& base, uint32 flags, uint32 borders)
{
	rgb_color edgeLightColor = _EdgeColor(base, false, flags);
	rgb_color edgeShadowColor = _EdgeColor(base, true, flags);

	if ((flags & B_BLEND_FRAME) != 0) {
		// assumes the background has already been painted
		drawing_mode oldDrawingMode = view->DrawingMode();
		view->SetDrawingMode(B_OP_ALPHA);

		_DrawFrame(view, rect, edgeShadowColor, edgeShadowColor,
			edgeLightColor, edgeLightColor, borders);

		view->SetDrawingMode(oldDrawingMode);
	} else {
		_DrawFrame(view, rect, edgeShadowColor, edgeShadowColor,
			edgeLightColor, edgeLightColor, borders);
	}
}


/**
 * @brief Strokes one border line per selected side and insets @a rect accordingly.
 *
 * Each enabled side is drawn with its own colour, then the corresponding edge
 * of @a rect is moved inward by one pixel, so the caller's rect reflects the
 * remaining drawable interior after the call.
 *
 * @param view     The view to draw into.
 * @param rect     The bounding rectangle; modified in-place as borders are drawn.
 * @param left     Colour for the left border.
 * @param top      Colour for the top border.
 * @param right    Colour for the right border.
 * @param bottom   Colour for the bottom border.
 * @param borders  Bitmask of @c B_LEFT_BORDER, @c B_TOP_BORDER,
 *                 @c B_RIGHT_BORDER, @c B_BOTTOM_BORDER.
 */
void
HaikuControlLook::_DrawFrame(BView* view, BRect& rect, const rgb_color& left,
	const rgb_color& top, const rgb_color& right, const rgb_color& bottom,
	uint32 borders)
{
	view->BeginLineArray(4);

	if (borders & B_LEFT_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.left, rect.top), left);
		rect.left++;
	}
	if (borders & B_TOP_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.top),
			BPoint(rect.right, rect.top), top);
		rect.top++;
	}
	if (borders & B_RIGHT_BORDER) {
		view->AddLine(
			BPoint(rect.right, rect.top),
			BPoint(rect.right, rect.bottom), right);
		rect.right--;
	}
	if (borders & B_BOTTOM_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.right, rect.bottom), bottom);
		rect.bottom--;
	}

	view->EndLineArray();
}


/**
 * @brief Strokes border lines with separate colours for the top-right and bottom-left corners.
 *
 * Overload used when the corner pixels need a colour different from the
 * adjacent edge — for example, to produce the "corner bead" seen in some
 * control frames.  The top edge terminates one pixel before the right corner,
 * which is drawn in @a rightTop; similarly the left edge terminates before the
 * bottom-left corner drawn in @a leftBottom.
 *
 * @param view        The view to draw into.
 * @param rect        The bounding rectangle; modified in-place as borders are drawn.
 * @param left        Colour for the left border (exclusive of the bottom-left corner).
 * @param top         Colour for the top border (exclusive of the top-right corner).
 * @param right       Colour for the right border.
 * @param bottom      Colour for the bottom border.
 * @param rightTop    Colour for the single top-right corner pixel.
 * @param leftBottom  Colour for the single bottom-left corner pixel.
 * @param borders     Which sides to draw.
 */
void
HaikuControlLook::_DrawFrame(BView* view, BRect& rect, const rgb_color& left,
	const rgb_color& top, const rgb_color& right, const rgb_color& bottom,
	const rgb_color& rightTop, const rgb_color& leftBottom, uint32 borders)
{
	view->BeginLineArray(6);

	if (borders & B_TOP_BORDER) {
		if (borders & B_RIGHT_BORDER) {
			view->AddLine(
				BPoint(rect.left, rect.top),
				BPoint(rect.right - 1, rect.top), top);
			view->AddLine(
				BPoint(rect.right, rect.top),
				BPoint(rect.right, rect.top), rightTop);
		} else {
			view->AddLine(
				BPoint(rect.left, rect.top),
				BPoint(rect.right, rect.top), top);
		}
		rect.top++;
	}

	if (borders & B_LEFT_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.top),
			BPoint(rect.left, rect.bottom - 1), left);
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.left, rect.bottom), leftBottom);
		rect.left++;
	}

	if (borders & B_BOTTOM_BORDER) {
		view->AddLine(
			BPoint(rect.left, rect.bottom),
			BPoint(rect.right, rect.bottom), bottom);
		rect.bottom--;
	}

	if (borders & B_RIGHT_BORDER) {
		view->AddLine(
			BPoint(rect.right, rect.bottom),
			BPoint(rect.right, rect.top), right);
		rect.right--;
	}

	view->EndLineArray();
}


/**
 * @brief Internal implementation for all DrawButtonBackground() and
 *        DrawButtonWithPopUpBackground() overloads.
 *
 * Determines whether the button should be painted flat or with the full bevel
 * and gradient treatment, then dispatches to _DrawFlatButtonBackground() or
 * _DrawNonFlatButtonBackground() respectively.
 *
 * @param view               The view to draw into.
 * @param rect               The button bounding rectangle; inset during drawing.
 * @param updateRect         The dirty region; drawing is skipped when rect is invalid.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The button's base colour.
 * @param popupIndicator     @c true to render the pop-up arrow indicator region.
 * @param flags              Control-look flags.
 * @param borders            Which sides receive a bevel line.
 * @param orientation        Gradient direction.
 */
void
HaikuControlLook::_DrawButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	bool popupIndicator, uint32 flags, uint32 borders, orientation orientation)
{
	if (!rect.IsValid())
		return;

	// save the clipping constraints of the view
	view->PushState();

	// set clipping constraints to rect
	view->ClipToRect(rect);

	// If the button is flat and neither activated nor otherwise highlighted
	// (mouse hovering or focussed), draw it flat.
	if ((flags & B_FLAT) != 0
		&& (flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED)) == 0
		&& ((flags & (B_HOVER | B_FOCUSED)) == 0
			|| (flags & B_DISABLED) != 0)) {
		rgb_color flatBase = base;
		if (view->Parent() != NULL)
			flatBase = view->Parent()->LowColor();
		_DrawFlatButtonBackground(view, rect, updateRect, flatBase, popupIndicator,
			flags, borders, orientation);
	} else {
		BRegion clipping(rect);
		_DrawNonFlatButtonBackground(view, rect, updateRect, clipping,
			leftTopRadius, rightTopRadius, leftBottomRadius, rightBottomRadius,
			base, popupIndicator, flags, borders, orientation);
	}

	// restore the clipping constraints of the view
	view->PopState();
}


/**
 * @brief Draws the background for a flat (undecorated) button state.
 *
 * Fills the rectangle with the solid @a base colour after calling _DrawFrame()
 * for inset accounting.  When @a popupIndicator is @c true the indicator
 * sub-region is filled and a pop-up arrow is drawn.
 *
 * @param view            The view to draw into.
 * @param rect            The button bounding rectangle; inset during drawing.
 * @param updateRect      The dirty region.
 * @param base            The flat fill colour (typically the parent background).
 * @param popupIndicator  @c true to render the pop-up arrow.
 * @param flags           Control-look flags.
 * @param borders         Which sides to account for in the inset.
 * @param orientation     Orientation (passed through to the indicator region).
 */
void
HaikuControlLook::_DrawFlatButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, bool popupIndicator,
	uint32 flags, uint32 borders, orientation orientation)
{
	_DrawFrame(view, rect, base, base, base, base, borders);
		// Not an actual frame, but the method insets our rect as needed.

	view->SetHighColor(base);
	view->FillRect(rect);

	if (popupIndicator) {
		BRect indicatorRect(rect);
		rect.right -= ComposeSpacing(kButtonPopUpIndicatorWidth);
		indicatorRect.left = rect.right + 3;
			// 2 pixels for the separator

		view->SetHighColor(base);
		view->FillRect(indicatorRect);

		_DrawPopUpMarker(view, indicatorRect, base, flags);
	}
}


/**
 * @brief Draws the fully bevelled, gradient-filled button background.
 *
 * Handles optional rounded corners, the inner bevel (active state uses shadow
 * lines; normal state uses _DrawFrame()), the pop-up indicator separator and
 * arrow, and the final gradient fill.
 *
 * @param view               The view to draw into.
 * @param rect               The button bounding rectangle; inset during drawing.
 * @param updateRect         The dirty region.
 * @param clipping           Initial clipping region; corners are excluded from it.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The button's base colour.
 * @param popupIndicator     @c true to render the pop-up indicator region.
 * @param flags              Control-look flags.
 * @param borders            Which sides receive a bevel line.
 * @param orientation        Gradient direction.
 */
void
HaikuControlLook::_DrawNonFlatButtonBackground(BView* view, BRect& rect,
	const BRect& updateRect, BRegion& clipping, float leftTopRadius,
	float rightTopRadius, float leftBottomRadius, float rightBottomRadius,
	const rgb_color& base, bool popupIndicator, uint32 flags, uint32 borders,
	orientation orientation)
{
	// inner bevel colors
	rgb_color bevelLightColor  = _BevelLightColor(base, flags);
	rgb_color bevelShadowColor = _BevelShadowColor(base, flags);

	// button corners color
	rgb_color buttonCornerColor;
	if ((flags & B_DISABLED) != 0)
		buttonCornerColor = tint_color(base, 0.84 /* lighten "< 1" */);
	else
		buttonCornerColor = tint_color(base, 0.7 /* lighten "< 1" */);

	// surface top gradient
	BGradientLinear fillGradient;
	_MakeButtonGradient(fillGradient, rect, base, flags, orientation);

	// rounded corners

	if ((borders & B_LEFT_BORDER) != 0 && (borders & B_TOP_BORDER) != 0
		&& leftTopRadius > 0) {
		// draw left top rounded corner
		BRect leftTopCorner(floorf(rect.left), floorf(rect.top),
			floorf(rect.left + leftTopRadius - 2.0),
			floorf(rect.top + leftTopRadius - 2.0));
		clipping.Exclude(leftTopCorner);
		BRect cornerRect(leftTopCorner);
		_DrawRoundCornerBackgroundLeftTop(view, leftTopCorner, updateRect,
			bevelLightColor, fillGradient);
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_TOP_BORDER) != 0 && (borders & B_RIGHT_BORDER) != 0
		&& rightTopRadius > 0) {
		// draw right top rounded corner
		BRect rightTopCorner(floorf(rect.right - rightTopRadius + 2.0),
			floorf(rect.top), floorf(rect.right),
			floorf(rect.top + rightTopRadius - 2.0));
		clipping.Exclude(rightTopCorner);
		BRect cornerRect(rightTopCorner);
		_DrawRoundCornerBackgroundRightTop(view, rightTopCorner,
			updateRect, bevelLightColor, bevelShadowColor, fillGradient);
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_LEFT_BORDER) != 0 && (borders & B_BOTTOM_BORDER) != 0
		&& leftBottomRadius > 0) {
		// draw left bottom rounded corner
		BRect leftBottomCorner(floorf(rect.left),
			floorf(rect.bottom - leftBottomRadius + 2.0),
			floorf(rect.left + leftBottomRadius - 2.0),
			floorf(rect.bottom));
		clipping.Exclude(leftBottomCorner);
		BRect cornerRect(leftBottomCorner);
		_DrawRoundCornerBackgroundLeftBottom(view, leftBottomCorner,
			updateRect, bevelLightColor, bevelShadowColor, fillGradient);
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_RIGHT_BORDER) != 0 && (borders & B_BOTTOM_BORDER) != 0
		&& rightBottomRadius > 0) {
		// draw right bottom rounded corner
		BRect rightBottomCorner(floorf(rect.right - rightBottomRadius + 2.0),
			floorf(rect.bottom - rightBottomRadius + 2.0), floorf(rect.right),
			floorf(rect.bottom));
		clipping.Exclude(rightBottomCorner);
		BRect cornerRect(rightBottomCorner);
		_DrawRoundCornerBackgroundRightBottom(view, rightBottomCorner,
			updateRect, bevelShadowColor, fillGradient);
		view->ClipToInverseRect(cornerRect);
	}

	// draw inner bevel

	if ((flags & B_ACTIVATED) != 0) {
		view->BeginLineArray(4);

		// shadow along left/top borders
		if (borders & B_LEFT_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.left, rect.bottom), bevelLightColor);
			rect.left++;
		}
		if (borders & B_TOP_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.right, rect.top), bevelLightColor);
			rect.top++;
		}

		// softer shadow along left/top borders
		if (borders & B_LEFT_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.left, rect.bottom), bevelShadowColor);
			rect.left++;
		}
		if (borders & B_TOP_BORDER) {
			view->AddLine(BPoint(rect.left, rect.top),
				BPoint(rect.right, rect.top), bevelShadowColor);
			rect.top++;
		}

		view->EndLineArray();
	} else {
		_DrawFrame(view, rect,
			bevelLightColor, bevelLightColor,
			bevelShadowColor, bevelShadowColor,
			buttonCornerColor, buttonCornerColor, borders);
	}

	if (popupIndicator) {
		BRect indicatorRect(rect);
		rect.right -= ComposeSpacing(kButtonPopUpIndicatorWidth);
		indicatorRect.left = rect.right + 3;
			// 2 pixels for the separator

		// Even when depressed we want the pop-up indicator background and
		// separator to cover the area up to the top.
		if ((flags & B_ACTIVATED) != 0)
			indicatorRect.top--;

		// draw the separator
		rgb_color separatorBaseColor = base;
		if ((flags & B_ACTIVATED) != 0)
			separatorBaseColor = tint_color(base, B_DARKEN_1_TINT);

		rgb_color separatorLightColor = _EdgeColor(separatorBaseColor,
			true, flags);
		rgb_color separatorShadowColor = _EdgeColor(separatorBaseColor,
			false, flags);

		view->BeginLineArray(2);

		view->AddLine(BPoint(indicatorRect.left - 2, indicatorRect.top),
			BPoint(indicatorRect.left - 2, indicatorRect.bottom),
			separatorShadowColor);
		view->AddLine(BPoint(indicatorRect.left - 1, indicatorRect.top),
			BPoint(indicatorRect.left - 1, indicatorRect.bottom),
			separatorLightColor);

		view->EndLineArray();

		// draw background and pop-up marker
		_DrawMenuFieldBackgroundInside(view, indicatorRect, updateRect,
			0.0f, rightTopRadius, 0.0f, rightBottomRadius, base, flags, 0);

		if ((flags & B_ACTIVATED) != 0)
			indicatorRect.top++;

		_DrawPopUpMarker(view, indicatorRect, base, flags);
	}

	// fill in the background
	view->FillRect(rect, fillGradient);
}


/**
 * @brief Draws the small downward-pointing triangle used as a pop-up indicator.
 *
 * Centres a filled triangle within @a rect.  The marker colour is a dark tint
 * of @a base; disabled controls receive a lighter tint.
 *
 * @param view   The view to draw into.
 * @param rect   The rectangle in which the triangle is centred.
 * @param base   The button's base colour used to derive the marker colour.
 * @param flags  Control-look flags (e.g. @c B_DISABLED).
 */
void
HaikuControlLook::_DrawPopUpMarker(BView* view, const BRect& rect,
	const rgb_color& base, uint32 flags)
{
	BPoint center(roundf((rect.left + rect.right) / 2.0),
		roundf((rect.top + rect.bottom) / 2.0));
	const float metric = roundf(rect.Width() * 3.125f) / 10.0f,
		offset = ceilf((metric * 0.2f) * 10.0f) / 10.0f;
	BPoint triangle[3];
	triangle[0] = center + BPoint(-metric, -offset);
	triangle[1] = center + BPoint(metric, -offset);
	triangle[2] = center + BPoint(0.0, metric * 0.8f);

	const uint32 viewFlags = view->Flags();
	view->SetFlags(viewFlags | B_SUBPIXEL_PRECISE);

	rgb_color markColor;
	if ((flags & B_DISABLED) != 0)
		markColor = tint_color(base, 1.35);
	else
		markColor = tint_color(base, 1.65);

	view->SetHighColor(markColor);
	view->FillTriangle(triangle[0], triangle[1], triangle[2]);

	view->SetFlags(viewFlags);
}


/**
 * @brief Draws the outer (split) view of a menu-field background.
 *
 * When @a popupIndicator is @c true, @a rect is split into a label portion
 * and an indicator portion separated by a spacing gap.  Each portion is drawn
 * independently using _DrawMenuFieldBackgroundInside(), and the pop-up marker
 * is overlaid on the indicator portion.  On return @a rect holds the label
 * portion only.  When @a popupIndicator is @c false the entire rect is filled
 * as a single segment.
 *
 * @param view               The view to draw into.
 * @param rect               The full menu-field rectangle; updated to the label area.
 * @param updateRect         The dirty region.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The menu field's base colour.
 * @param popupIndicator     @c true to draw the indicator arrow region.
 * @param flags              Control-look flags.
 */
void
HaikuControlLook::_DrawMenuFieldBackgroundOutside(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	bool popupIndicator, uint32 flags)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	if (popupIndicator) {
		const float indicatorWidth = ComposeSpacing(kButtonPopUpIndicatorWidth);
		const float spacing = (indicatorWidth <= 11.0f) ? 1.0f : roundf(indicatorWidth / 11.0f);

		BRect leftRect(rect);
		leftRect.right -= indicatorWidth - spacing;

		BRect rightRect(rect);
		rightRect.left = rightRect.right - (indicatorWidth - spacing * 2);

		_DrawMenuFieldBackgroundInside(view, leftRect, updateRect,
			leftTopRadius, 0.0f, leftBottomRadius, 0.0f, base, flags,
			B_LEFT_BORDER | B_TOP_BORDER | B_BOTTOM_BORDER);

		_DrawMenuFieldBackgroundInside(view, rightRect, updateRect,
			0.0f, rightTopRadius, 0.0f, rightBottomRadius, base, flags,
			B_TOP_BORDER | B_RIGHT_BORDER | B_BOTTOM_BORDER);

		_DrawPopUpMarker(view, rightRect, base, flags);

		// draw a line on the left of the popup frame
		rgb_color bevelShadowColor = _BevelShadowColor(base, flags);
		view->SetHighColor(bevelShadowColor);
		BPoint leftTopCorner(floorf(rightRect.left - spacing),
			floorf(rightRect.top - spacing));
		BPoint leftBottomCorner(floorf(rightRect.left - spacing),
			floorf(rightRect.bottom + spacing));
		for (float i = 0; i < spacing; i++) {
			view->StrokeLine(leftTopCorner + BPoint(i, 0),
				leftBottomCorner + BPoint(i, 0));
		}

		rect = leftRect;
	} else {
		_DrawMenuFieldBackgroundInside(view, rect, updateRect, leftTopRadius,
			rightTopRadius, leftBottomRadius, rightBottomRadius, base, flags);
	}
}


/**
 * @brief Draws the interior gradient fill for one segment of a menu-field background.
 *
 * Handles rounded corners using ellipse clipping, then draws the bevel frame
 * and gradient fill.  The right-hand segment (indicator side) uses a slightly
 * darker base colour derived from @a base.
 *
 * @param view               The view to draw into.
 * @param rect               The segment bounding rectangle; inset during drawing.
 * @param updateRect         The dirty region.
 * @param leftTopRadius      Radius of the top-left corner.
 * @param rightTopRadius     Radius of the top-right corner.
 * @param leftBottomRadius   Radius of the bottom-left corner.
 * @param rightBottomRadius  Radius of the bottom-right corner.
 * @param base               The segment base colour.
 * @param flags              Control-look flags.
 * @param borders            Which sides receive a bevel line.
 */
void
HaikuControlLook::_DrawMenuFieldBackgroundInside(BView* view, BRect& rect,
	const BRect& updateRect, float leftTopRadius, float rightTopRadius,
	float leftBottomRadius, float rightBottomRadius, const rgb_color& base,
	uint32 flags, uint32 borders)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	// save the clipping constraints of the view
	view->PushState();

	// set clipping constraints to rect
	view->ClipToRect(rect);

	// frame colors
	rgb_color frameLightColor  = _FrameLightColor(base, flags);
	rgb_color frameShadowColor = _FrameShadowColor(base, flags);

	// indicator background color
	rgb_color indicatorBase;
	if ((borders & B_LEFT_BORDER) != 0)
		indicatorBase = base;
	else {
		if ((flags & B_DISABLED) != 0)
			indicatorBase = tint_color(base, 1.05);
		else
			indicatorBase = tint_color(base, 1.12);
	}

	// bevel colors
	rgb_color cornerColor = tint_color(indicatorBase, 0.85);
	rgb_color bevelColor1 = tint_color(indicatorBase, 0.3);
	rgb_color bevelColor2 = tint_color(indicatorBase, 0.5);
	rgb_color bevelColor3 = tint_color(indicatorBase, 1.03);

	if ((flags & B_DISABLED) != 0) {
		cornerColor = tint_color(indicatorBase, 0.8);
		bevelColor1 = tint_color(indicatorBase, 0.7);
		bevelColor2 = tint_color(indicatorBase, 0.8);
		bevelColor3 = tint_color(indicatorBase, 1.01);
	} else {
		cornerColor = tint_color(indicatorBase, 0.85);
		bevelColor1 = tint_color(indicatorBase, 0.3);
		bevelColor2 = tint_color(indicatorBase, 0.5);
		bevelColor3 = tint_color(indicatorBase, 1.03);
	}

	// surface top gradient
	BGradientLinear fillGradient;
	_MakeButtonGradient(fillGradient, rect, indicatorBase, flags);

	// rounded corners

	if ((borders & B_LEFT_BORDER) != 0 && (borders & B_TOP_BORDER) != 0
		&& leftTopRadius > 0) {
		// draw left top rounded corner
		BRect leftTopCorner(floorf(rect.left), floorf(rect.top),
			floorf(rect.left + leftTopRadius - 2.0),
			floorf(rect.top + leftTopRadius - 2.0));
		BRect cornerRect(leftTopCorner);

		view->PushState();
		view->ClipToRect(cornerRect);

		BRect ellipseRect(leftTopCorner);
		ellipseRect.InsetBy(-1.0, -1.0);
		ellipseRect.right = ellipseRect.left + ellipseRect.Width() * 2;
		ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

		// draw the frame (again)
		view->SetHighColor(frameLightColor);
		view->FillEllipse(ellipseRect);

		// draw the bevel and background
		_DrawRoundCornerBackgroundLeftTop(view, leftTopCorner, updateRect,
			bevelColor1, fillGradient);

		view->PopState();
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_TOP_BORDER) != 0 && (borders & B_RIGHT_BORDER) != 0
		&& rightTopRadius > 0) {
		// draw right top rounded corner
		BRect rightTopCorner(floorf(rect.right - rightTopRadius + 2.0),
			floorf(rect.top), floorf(rect.right),
			floorf(rect.top + rightTopRadius - 2.0));
		BRect cornerRect(rightTopCorner);

		view->PushState();
		view->ClipToRect(cornerRect);

		BRect ellipseRect(rightTopCorner);
		ellipseRect.InsetBy(-1.0, -1.0);
		ellipseRect.left = ellipseRect.right - ellipseRect.Width() * 2;
		ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

		// draw the frame (again)
		if (frameLightColor == frameShadowColor) {
			view->SetHighColor(frameLightColor);
			view->FillEllipse(ellipseRect);
		} else {
			BGradientLinear gradient;
			gradient.AddColor(frameLightColor, 0);
			gradient.AddColor(frameShadowColor, 255);
			gradient.SetStart(rightTopCorner.LeftTop());
			gradient.SetEnd(rightTopCorner.RightBottom());
			view->FillEllipse(ellipseRect, gradient);
		}

		// draw the bevel and background
		_DrawRoundCornerBackgroundRightTop(view, rightTopCorner, updateRect,
			bevelColor1, bevelColor3, fillGradient);

		view->PopState();
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_LEFT_BORDER) != 0 && (borders & B_BOTTOM_BORDER) != 0
		&& leftBottomRadius > 0) {
		// draw left bottom rounded corner
		BRect leftBottomCorner(floorf(rect.left),
			floorf(rect.bottom - leftBottomRadius + 2.0),
			floorf(rect.left + leftBottomRadius - 2.0),
			floorf(rect.bottom));
		BRect cornerRect(leftBottomCorner);

		view->PushState();
		view->ClipToRect(cornerRect);

		BRect ellipseRect(leftBottomCorner);
		ellipseRect.InsetBy(-1.0, -1.0);
		ellipseRect.right = ellipseRect.left + ellipseRect.Width() * 2;
		ellipseRect.top = ellipseRect.bottom - ellipseRect.Height() * 2;

		// draw the frame (again)
		if (frameLightColor == frameShadowColor) {
			view->SetHighColor(frameLightColor);
			view->FillEllipse(ellipseRect);
		} else {
			BGradientLinear gradient;
			gradient.AddColor(frameLightColor, 0);
			gradient.AddColor(frameShadowColor, 255);
			gradient.SetStart(leftBottomCorner.LeftTop());
			gradient.SetEnd(leftBottomCorner.RightBottom());
			view->FillEllipse(ellipseRect, gradient);
		}

		// draw the bevel and background
		_DrawRoundCornerBackgroundLeftBottom(view, leftBottomCorner,
			updateRect, bevelColor2, bevelColor3, fillGradient);

		view->PopState();
		view->ClipToInverseRect(cornerRect);
	}

	if ((borders & B_RIGHT_BORDER) != 0 && (borders & B_BOTTOM_BORDER) != 0
		&& rightBottomRadius > 0) {
		// draw right bottom rounded corner
		BRect rightBottomCorner(floorf(rect.right - rightBottomRadius + 2.0),
			floorf(rect.bottom - rightBottomRadius + 2.0), floorf(rect.right),
			floorf(rect.bottom));
		BRect cornerRect(rightBottomCorner);

		view->PushState();
		view->ClipToRect(cornerRect);

		BRect ellipseRect(rightBottomCorner);
		ellipseRect.InsetBy(-1.0, -1.0);
		ellipseRect.left = ellipseRect.right - ellipseRect.Width() * 2;
		ellipseRect.top = ellipseRect.bottom - ellipseRect.Height() * 2;

		// draw the frame (again)
		view->SetHighColor(frameShadowColor);
		view->FillEllipse(ellipseRect);

		// draw the bevel and background
		_DrawRoundCornerBackgroundRightBottom(view, rightBottomCorner,
			updateRect, bevelColor3, fillGradient);

		view->PopState();
		view->ClipToInverseRect(cornerRect);
	}

	// draw the bevel
	_DrawFrame(view, rect,
		bevelColor2, bevelColor1,
		bevelColor3, bevelColor3,
		cornerColor, cornerColor,
		borders);

	// fill in the background
	view->FillRect(rect, fillGradient);

	// restore the clipping constraints of the view
	view->PopState();
}


/**
 * @brief Draws both the frame and background of a top-left rounded tab corner.
 *
 * Convenience wrapper that calls _DrawRoundCornerFrameLeftTop() followed by
 * _DrawRoundCornerBackgroundLeftTop().
 *
 * @param view          The view to draw into.
 * @param cornerRect    The corner bounding rectangle; inset by each drawn layer.
 * @param updateRect    The dirty region.
 * @param background    The background colour to paint before the ellipse.
 * @param edgeColor     The outer edge tint colour.
 * @param frameColor    The frame colour applied inside the edge.
 * @param bevelColor    The bevel colour applied inside the frame.
 * @param fillGradient  The gradient used to fill the interior.
 */
void
HaikuControlLook::_DrawRoundCornerLeftTop(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeColor, const rgb_color& frameColor,
	const rgb_color& bevelColor, const BGradientLinear& fillGradient)
{
	_DrawRoundCornerFrameLeftTop(view, cornerRect, updateRect,
		background, edgeColor, frameColor);
	_DrawRoundCornerBackgroundLeftTop(view, cornerRect, updateRect,
		bevelColor, fillGradient);
}


/**
 * @brief Draws the edge and frame layers of a top-left rounded corner.
 *
 * Clips to the corner rect, fills the background, then paints two concentric
 * quarter-ellipses (edge and frame) and advances @a cornerRect inward by two
 * pixels so the caller can continue with the bevel/fill.
 *
 * @param view        The view to draw into.
 * @param cornerRect  The corner bounding rectangle; inset by 2 px after drawing.
 * @param updateRect  The dirty region.
 * @param background  The background colour.
 * @param edgeColor   The outer edge colour.
 * @param frameColor  The frame colour.
 */
void
HaikuControlLook::_DrawRoundCornerFrameLeftTop(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeColor, const rgb_color& frameColor)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	// background
	view->SetHighColor(background);
	view->FillRect(cornerRect);

	// outer edge
	BRect ellipseRect(cornerRect);
	ellipseRect.right = ellipseRect.left + ellipseRect.Width() * 2;
	ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

	view->SetHighColor(edgeColor);
	view->FillEllipse(ellipseRect);

	// frame
	ellipseRect.InsetBy(1, 1);
	cornerRect.left++;
	cornerRect.top++;
	view->SetHighColor(frameColor);
	view->FillEllipse(ellipseRect);

	// prepare for bevel
	cornerRect.left++;
	cornerRect.top++;

	view->PopState();
}


/**
 * @brief Draws the bevel and gradient fill of a top-left rounded corner.
 *
 * Clips to the corner rect and paints a bevel ellipse followed by a slightly
 * smaller gradient-filled ellipse.
 *
 * @param view          The view to draw into.
 * @param cornerRect    The corner bounding rectangle (already advanced past the frame).
 * @param updateRect    The dirty region.
 * @param bevelColor    The solid bevel colour.
 * @param fillGradient  The linear gradient for the interior fill.
 */
void
HaikuControlLook::_DrawRoundCornerBackgroundLeftTop(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& bevelColor,
	const BGradientLinear& fillGradient)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	BRect ellipseRect(cornerRect);
	ellipseRect.right = ellipseRect.left + ellipseRect.Width() * 2;
	ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

	// bevel
	view->SetHighColor(bevelColor);
	view->FillEllipse(ellipseRect);

	// gradient
	ellipseRect.InsetBy(1, 1);
	view->FillEllipse(ellipseRect, fillGradient);

	view->PopState();
}


/**
 * @brief Draws both the frame and background of a top-right rounded tab corner.
 *
 * Convenience wrapper that calls _DrawRoundCornerFrameRightTop() followed by
 * _DrawRoundCornerBackgroundRightTop().  Each corner side may carry a distinct
 * edge and frame colour to blend the top and right edges of the tab.
 *
 * @param view            The view to draw into.
 * @param cornerRect      The corner bounding rectangle; inset by each drawn layer.
 * @param updateRect      The dirty region.
 * @param background      The background colour.
 * @param edgeTopColor    The outer edge colour along the top of the corner.
 * @param edgeRightColor  The outer edge colour along the right of the corner.
 * @param frameTopColor   The frame colour along the top.
 * @param frameRightColor The frame colour along the right.
 * @param bevelTopColor   The bevel colour along the top.
 * @param bevelRightColor The bevel colour along the right.
 * @param fillGradient    The gradient for the interior fill.
 */
void
HaikuControlLook::_DrawRoundCornerRightTop(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeTopColor, const rgb_color& edgeRightColor,
	const rgb_color& frameTopColor, const rgb_color& frameRightColor,
	const rgb_color& bevelTopColor, const rgb_color& bevelRightColor,
	const BGradientLinear& fillGradient)
{
	_DrawRoundCornerFrameRightTop(view, cornerRect, updateRect,
		background, edgeTopColor, edgeRightColor, frameTopColor,
		frameRightColor);
	_DrawRoundCornerBackgroundRightTop(view, cornerRect, updateRect,
		bevelTopColor, bevelRightColor, fillGradient);
}


/**
 * @brief Draws the edge and frame layers of a top-right rounded corner.
 *
 * Clips to the corner rect, fills the background, then paints two concentric
 * quarter-ellipses with gradient colours (top-to-right) for both the edge and
 * frame layers, advancing @a cornerRect inward by two pixels.
 *
 * @param view            The view to draw into.
 * @param cornerRect      The corner bounding rectangle; inset by 2 px after drawing.
 * @param updateRect      The dirty region.
 * @param background      The background colour.
 * @param edgeTopColor    Edge colour at the top of the corner.
 * @param edgeRightColor  Edge colour at the right of the corner.
 * @param frameTopColor   Frame colour at the top.
 * @param frameRightColor Frame colour at the right.
 */
void
HaikuControlLook::_DrawRoundCornerFrameRightTop(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeTopColor, const rgb_color& edgeRightColor,
	const rgb_color& frameTopColor, const rgb_color& frameRightColor)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	// background
	view->SetHighColor(background);
	view->FillRect(cornerRect);

	// outer edge
	BRect ellipseRect(cornerRect);
	ellipseRect.left = ellipseRect.right - ellipseRect.Width() * 2;
	ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

	BGradientLinear gradient;
	gradient.AddColor(edgeTopColor, 0);
	gradient.AddColor(edgeRightColor, 255);
	gradient.SetStart(cornerRect.LeftTop());
	gradient.SetEnd(cornerRect.RightBottom());
	view->FillEllipse(ellipseRect, gradient);

	// frame
	ellipseRect.InsetBy(1, 1);
	cornerRect.right--;
	cornerRect.top++;
	if (frameTopColor == frameRightColor) {
		view->SetHighColor(frameTopColor);
		view->FillEllipse(ellipseRect);
	} else {
		gradient.SetColor(0, frameTopColor);
		gradient.SetColor(1, frameRightColor);
		gradient.SetStart(cornerRect.LeftTop());
		gradient.SetEnd(cornerRect.RightBottom());
		view->FillEllipse(ellipseRect, gradient);
	}

	// prepare for bevel
	cornerRect.right--;
	cornerRect.top++;

	view->PopState();
}


/**
 * @brief Draws the bevel and gradient fill of a top-right rounded corner.
 *
 * Paints a gradient bevel ellipse followed by a smaller gradient-filled ellipse,
 * both anchored to the top-right of the corner rectangle.
 *
 * @param view            The view to draw into.
 * @param cornerRect      The corner bounding rectangle.
 * @param updateRect      The dirty region.
 * @param bevelTopColor   Bevel colour at the top of the gradient.
 * @param bevelRightColor Bevel colour at the right of the gradient.
 * @param fillGradient    The gradient for the interior fill.
 */
void
HaikuControlLook::_DrawRoundCornerBackgroundRightTop(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& bevelTopColor,
	const rgb_color& bevelRightColor, const BGradientLinear& fillGradient)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	BRect ellipseRect(cornerRect);
	ellipseRect.left = ellipseRect.right - ellipseRect.Width() * 2;
	ellipseRect.bottom = ellipseRect.top + ellipseRect.Height() * 2;

	// bevel
	BGradientLinear gradient;
	gradient.AddColor(bevelTopColor, 0);
	gradient.AddColor(bevelRightColor, 255);
	gradient.SetStart(cornerRect.LeftTop());
	gradient.SetEnd(cornerRect.RightBottom());
	view->FillEllipse(ellipseRect, gradient);

	// gradient
	ellipseRect.InsetBy(1, 1);
	view->FillEllipse(ellipseRect, fillGradient);

	view->PopState();
}


/**
 * @brief Draws both the frame and background of a bottom-left rounded tab corner.
 *
 * Convenience wrapper that calls _DrawRoundCornerFrameLeftBottom() then
 * _DrawRoundCornerBackgroundLeftBottom().
 *
 * @param view              The view to draw into.
 * @param cornerRect        The corner bounding rectangle; inset by each drawn layer.
 * @param updateRect        The dirty region.
 * @param background        The background colour.
 * @param edgeLeftColor     Outer edge colour along the left side.
 * @param edgeBottomColor   Outer edge colour along the bottom.
 * @param frameLeftColor    Frame colour along the left side.
 * @param frameBottomColor  Frame colour along the bottom.
 * @param bevelLeftColor    Bevel colour along the left side.
 * @param bevelBottomColor  Bevel colour along the bottom.
 * @param fillGradient      The gradient for the interior fill.
 */
void
HaikuControlLook::_DrawRoundCornerLeftBottom(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeLeftColor, const rgb_color& edgeBottomColor,
	const rgb_color& frameLeftColor, const rgb_color& frameBottomColor,
	const rgb_color& bevelLeftColor, const rgb_color& bevelBottomColor,
	const BGradientLinear& fillGradient)
{
	_DrawRoundCornerFrameLeftBottom(view, cornerRect, updateRect,
		background, edgeLeftColor, edgeBottomColor, frameLeftColor,
		frameBottomColor);
	_DrawRoundCornerBackgroundLeftBottom(view, cornerRect, updateRect,
		bevelLeftColor, bevelBottomColor, fillGradient);
}


/**
 * @brief Draws the edge and frame layers of a bottom-left rounded corner.
 *
 * Clips, fills the background, then paints two concentric quarter-ellipses
 * with gradient colours (left-to-bottom), advancing @a cornerRect inward.
 *
 * @param view              The view to draw into.
 * @param cornerRect        The corner bounding rectangle; inset by 2 px after drawing.
 * @param updateRect        The dirty region.
 * @param background        The background colour.
 * @param edgeLeftColor     Edge colour along the left.
 * @param edgeBottomColor   Edge colour along the bottom.
 * @param frameLeftColor    Frame colour along the left.
 * @param frameBottomColor  Frame colour along the bottom.
 */
void
HaikuControlLook::_DrawRoundCornerFrameLeftBottom(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeLeftColor, const rgb_color& edgeBottomColor,
	const rgb_color& frameLeftColor, const rgb_color& frameBottomColor)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	// background
	view->SetHighColor(background);
	view->FillRect(cornerRect);

	// outer edge
	BRect ellipseRect(cornerRect);
	ellipseRect.right = ellipseRect.left + ellipseRect.Width() * 2;
	ellipseRect.top = ellipseRect.bottom - ellipseRect.Height() * 2;

	BGradientLinear gradient;
	gradient.AddColor(edgeLeftColor, 0);
	gradient.AddColor(edgeBottomColor, 255);
	gradient.SetStart(cornerRect.LeftTop());
	gradient.SetEnd(cornerRect.RightBottom());
	view->FillEllipse(ellipseRect, gradient);

	// frame
	ellipseRect.InsetBy(1, 1);
	cornerRect.left++;
	cornerRect.bottom--;
	if (frameLeftColor == frameBottomColor) {
		view->SetHighColor(frameLeftColor);
		view->FillEllipse(ellipseRect);
	} else {
		gradient.SetColor(0, frameLeftColor);
		gradient.SetColor(1, frameBottomColor);
		gradient.SetStart(cornerRect.LeftTop());
		gradient.SetEnd(cornerRect.RightBottom());
		view->FillEllipse(ellipseRect, gradient);
	}

	// prepare for bevel
	cornerRect.left++;
	cornerRect.bottom--;

	view->PopState();
}


/**
 * @brief Draws the bevel and gradient fill of a bottom-left rounded corner.
 *
 * Paints a gradient bevel ellipse then a smaller gradient-filled ellipse,
 * both anchored to the bottom-left of the corner rectangle.
 *
 * @param view              The view to draw into.
 * @param cornerRect        The corner bounding rectangle.
 * @param updateRect        The dirty region.
 * @param bevelLeftColor    Bevel colour at the top of the gradient (left side).
 * @param bevelBottomColor  Bevel colour at the bottom of the gradient.
 * @param fillGradient      The gradient for the interior fill.
 */
void
HaikuControlLook::_DrawRoundCornerBackgroundLeftBottom(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& bevelLeftColor,
	const rgb_color& bevelBottomColor, const BGradientLinear& fillGradient)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	BRect ellipseRect(cornerRect);
	ellipseRect.right = ellipseRect.left + ellipseRect.Width() * 2;
	ellipseRect.top = ellipseRect.bottom - ellipseRect.Height() * 2;

	// bevel
	BGradientLinear gradient;
	gradient.AddColor(bevelLeftColor, 0);
	gradient.AddColor(bevelBottomColor, 255);
	gradient.SetStart(cornerRect.LeftTop());
	gradient.SetEnd(cornerRect.RightBottom());
	view->FillEllipse(ellipseRect, gradient);

	// gradient
	ellipseRect.InsetBy(1, 1);
	view->FillEllipse(ellipseRect, fillGradient);

	view->PopState();
}


/**
 * @brief Draws both the frame and background of a bottom-right rounded tab corner.
 *
 * Convenience wrapper that calls _DrawRoundCornerFrameRightBottom() then
 * _DrawRoundCornerBackgroundRightBottom().
 *
 * @param view          The view to draw into.
 * @param cornerRect    The corner bounding rectangle; inset by each drawn layer.
 * @param updateRect    The dirty region.
 * @param background    The background colour.
 * @param edgeColor     The outer edge colour.
 * @param frameColor    The frame colour.
 * @param bevelColor    The bevel colour.
 * @param fillGradient  The gradient for the interior fill.
 */
void
HaikuControlLook::_DrawRoundCornerRightBottom(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeColor, const rgb_color& frameColor,
	const rgb_color& bevelColor, const BGradientLinear& fillGradient)
{
	_DrawRoundCornerFrameRightBottom(view, cornerRect, updateRect,
		background, edgeColor, frameColor);
	_DrawRoundCornerBackgroundRightBottom(view, cornerRect, updateRect,
		bevelColor, fillGradient);
}


/**
 * @brief Draws the edge and frame layers of a bottom-right rounded corner.
 *
 * Clips, fills the background, then paints two concentric quarter-ellipses
 * anchored to the bottom-right, advancing @a cornerRect inward by two pixels.
 *
 * @param view        The view to draw into.
 * @param cornerRect  The corner bounding rectangle; inset by 2 px after drawing.
 * @param updateRect  The dirty region.
 * @param background  The background colour.
 * @param edgeColor   The outer edge colour.
 * @param frameColor  The frame colour.
 */
void
HaikuControlLook::_DrawRoundCornerFrameRightBottom(BView* view, BRect& cornerRect,
	const BRect& updateRect, const rgb_color& background,
	const rgb_color& edgeColor, const rgb_color& frameColor)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	// background
	view->SetHighColor(background);
	view->FillRect(cornerRect);

	// outer edge
	BRect ellipseRect(cornerRect);
	ellipseRect.left = ellipseRect.right - ellipseRect.Width() * 2;
	ellipseRect.top = ellipseRect.bottom - ellipseRect.Height() * 2;

	view->SetHighColor(edgeColor);
	view->FillEllipse(ellipseRect);

	// frame
	ellipseRect.InsetBy(1, 1);
	cornerRect.right--;
	cornerRect.bottom--;
	view->SetHighColor(frameColor);
	view->FillEllipse(ellipseRect);

	// prepare for bevel
	cornerRect.right--;
	cornerRect.bottom--;

	view->PopState();
}


/**
 * @brief Draws the bevel and gradient fill of a bottom-right rounded corner.
 *
 * Paints a solid bevel ellipse then a smaller gradient-filled ellipse, both
 * anchored to the bottom-right of the corner rectangle.
 *
 * @param view          The view to draw into.
 * @param cornerRect    The corner bounding rectangle.
 * @param updateRect    The dirty region.
 * @param bevelColor    The solid bevel colour.
 * @param fillGradient  The gradient for the interior fill.
 */
void
HaikuControlLook::_DrawRoundCornerBackgroundRightBottom(BView* view,
	BRect& cornerRect, const BRect& updateRect, const rgb_color& bevelColor,
	const BGradientLinear& fillGradient)
{
	view->PushState();

	// constrain clipping region to corner
	view->ClipToRect(cornerRect);

	BRect ellipseRect(cornerRect);
	ellipseRect.left = ellipseRect.right - ellipseRect.Width() * 2;
	ellipseRect.top = ellipseRect.bottom - ellipseRect.Height() * 2;

	// bevel
	view->SetHighColor(bevelColor);
	view->FillEllipse(ellipseRect);

	// gradient
	ellipseRect.InsetBy(1, 1);
	view->FillEllipse(ellipseRect, fillGradient);

	view->PopState();
}


/**
 * @brief Draws one rounded end cap of a slider bar.
 *
 * Paints three concentric ellipses (edge, frame, fill) using gradient colours
 * oriented along the slider axis.  After each ellipse the rectangle is inset
 * by the supplied amounts to produce the layered bevel appearance.
 *
 * @param view             The view to draw into.
 * @param rect             The end-cap bounding rectangle; inset between ellipses.
 * @param updateRect       The dirty region; drawing is skipped outside this area.
 * @param edgeLightColor   Light edge colour at the gradient start.
 * @param edgeShadowColor  Shadow edge colour at the gradient end.
 * @param frameLightColor  Light frame colour.
 * @param frameShadowColor Shadow frame colour.
 * @param fillLightColor   Light fill colour.
 * @param fillShadowColor  Shadow fill colour.
 * @param leftInset        Amount to inset @a rect on the left between layers.
 * @param topInset         Amount to inset @a rect on the top between layers.
 * @param rightInset       Amount to inset @a rect on the right between layers (negative = shrink).
 * @param bottomInset      Amount to inset @a rect on the bottom between layers (negative = shrink).
 * @param orientation      @c B_HORIZONTAL or @c B_VERTICAL, controls gradient direction.
 */
void
HaikuControlLook::_DrawRoundBarCorner(BView* view, BRect& rect,
	const BRect& updateRect,
	const rgb_color& edgeLightColor, const rgb_color& edgeShadowColor,
	const rgb_color& frameLightColor, const rgb_color& frameShadowColor,
	const rgb_color& fillLightColor, const rgb_color& fillShadowColor,
	float leftInset, float topInset, float rightInset, float bottomInset,
	orientation orientation)
{
	if (!ShouldDraw(view, rect, updateRect))
		return;

	BGradientLinear gradient;
	gradient.AddColor(edgeShadowColor, 0);
	gradient.AddColor(edgeLightColor, 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());

	view->FillEllipse(rect, gradient);

	rect.left += leftInset;
	rect.top += topInset;
	rect.right += rightInset;
	rect.bottom += bottomInset;

	gradient.MakeEmpty();
	gradient.AddColor(frameShadowColor, 0);
	gradient.AddColor(frameLightColor, 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());

	view->FillEllipse(rect, gradient);

	rect.left += leftInset;
	rect.top += topInset;
	rect.right += rightInset;
	rect.bottom += bottomInset;

	gradient.MakeEmpty();
	gradient.AddColor(fillShadowColor, 0);
	gradient.AddColor(fillLightColor, 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());

	view->FillEllipse(rect, gradient);
}


/**
 * @brief Computes the outer edge colour for buttons and borders.
 *
 * Returns either a shadow (dark/transparent) or light colour depending on
 * @a shadow.  When @c B_BLEND_FRAME is set an alpha-only colour is returned;
 * otherwise a tint of @a base is used.  Both @c B_DEFAULT_BUTTON and
 * @c B_DISABLED flags further adjust the result.
 *
 * @param base    The base colour from which the edge tint is derived.
 * @param shadow  @c true for the shadow edge, @c false for the light edge.
 * @param flags   Control-look flags.
 * @return The computed edge colour.
 */
rgb_color
HaikuControlLook::_EdgeColor(const rgb_color& base, bool shadow, uint32 flags)
{
	rgb_color edgeColor;

	if ((flags & B_BLEND_FRAME) != 0) {
		uint8 alpha = 20;
		uint8 value = shadow ? 0 : 255;
		if ((flags & B_DEFAULT_BUTTON) != 0) {
			if ((flags & B_DISABLED) != 0) {
				alpha = (uint8)(alpha * 0.3);
				value = (uint8)(value * 0.9);
			} else
				alpha = (uint8)(alpha * 0.8);
		} else {
			if ((flags & B_DISABLED) != 0)
				alpha = 0;
		}

		edgeColor = (rgb_color){ value, value, value, alpha };
	} else {
		float tint = shadow ? 1.0735 : 0.59;
		if ((flags & B_DEFAULT_BUTTON) != 0) {
			if ((flags & B_DISABLED) != 0)
				tint = B_NO_TINT + (tint - B_NO_TINT) * 0.3;
			else
				tint = (tint + 1.245f /* darken "< 2" */) / 2;
		} else {
			if ((flags & B_DISABLED) != 0)
				tint = B_NO_TINT;
		}

		edgeColor = tint_color(base, tint);
	}

	return edgeColor;
}


/**
 * @brief Computes the light (top/left) frame border colour.
 *
 * Returns the keyboard-navigation colour when @c B_FOCUSED is set.  When
 * @c B_ACTIVATED is set, the roles of light and shadow are swapped by
 * delegating to _FrameShadowColor().
 *
 * @param base   The control's base colour.
 * @param flags  Control-look flags.
 * @return The light frame colour.
 */
rgb_color
HaikuControlLook::_FrameLightColor(const rgb_color& base, uint32 flags)
{
	if ((flags & B_FOCUSED) != 0)
		return ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if ((flags & B_ACTIVATED) != 0)
		return _FrameShadowColor(base, flags & ~B_ACTIVATED);

	rgb_color frameLightColor;

	if ((flags & B_DISABLED) != 0) {
		// TODO: B_BLEND_FRAME
		frameLightColor = tint_color(base, 1.145);

		if ((flags & B_DEFAULT_BUTTON) != 0)
			frameLightColor = tint_color(frameLightColor, 1.14);
	} else {
		if ((flags & B_BLEND_FRAME) != 0)
			frameLightColor = (rgb_color){ 0, 0, 0, 75 };
		else
			frameLightColor = tint_color(base, 1.35);

		if ((flags & B_DEFAULT_BUTTON) != 0)
			frameLightColor = tint_color(frameLightColor, 1.35);
	}

	return frameLightColor;
}


/**
 * @brief Computes the shadow (bottom/right) frame border colour.
 *
 * Returns the keyboard-navigation colour when @c B_FOCUSED is set.  When
 * @c B_ACTIVATED is set, the roles of light and shadow are swapped.
 *
 * @param base   The control's base colour.
 * @param flags  Control-look flags.
 * @return The shadow frame colour.
 */
rgb_color
HaikuControlLook::_FrameShadowColor(const rgb_color& base, uint32 flags)
{
	if ((flags & B_FOCUSED) != 0)
		return ui_color(B_KEYBOARD_NAVIGATION_COLOR);

	if ((flags & B_ACTIVATED) != 0)
		return _FrameLightColor(base, flags & ~B_ACTIVATED);

	rgb_color frameShadowColor;

	if ((flags & B_DISABLED) != 0) {
		// TODO: B_BLEND_FRAME
		frameShadowColor = tint_color(base, 1.26);

		if ((flags & B_DEFAULT_BUTTON) != 0) {
			frameShadowColor = tint_color(base, 1.145);
			frameShadowColor = tint_color(frameShadowColor, 1.12);
		}
	} else {
		if ((flags & B_DEFAULT_BUTTON) != 0) {
			if ((flags & B_BLEND_FRAME) != 0)
				frameShadowColor = (rgb_color){ 0, 0, 0, 75 };
			else
				frameShadowColor = tint_color(base, 1.33);

			frameShadowColor = tint_color(frameShadowColor, 1.5);
		} else {
			if ((flags & B_BLEND_FRAME) != 0)
				frameShadowColor = (rgb_color){ 0, 0, 0, 95 };
			else
				frameShadowColor = tint_color(base, 1.485);
		}
	}

	return frameShadowColor;
}


/**
 * @brief Computes the inner bevel light colour for buttons.
 *
 * Activated buttons use a dark tint; disabled buttons use a slightly
 * lightened tint; normal buttons use a very light (near-white) tint.
 *
 * @param base   The control's base colour.
 * @param flags  Control-look flags.
 * @return The bevel light colour.
 */
rgb_color
HaikuControlLook::_BevelLightColor(const rgb_color& base, uint32 flags)
{
	rgb_color bevelLightColor;

	if ((flags & B_ACTIVATED) != 0)
		bevelLightColor = tint_color(base, 1.17);
	else if ((flags & B_DISABLED) != 0)
		bevelLightColor = tint_color(base, B_LIGHTEN_1_TINT);
	else
		bevelLightColor = tint_color(base, 0.2);

	return bevelLightColor;
}


/**
 * @brief Computes the inner bevel shadow colour for buttons.
 *
 * Activated buttons use the same tint as the bevel light (no bevel); disabled
 * buttons return the base colour; normal buttons use a slight dark tint.
 *
 * @param base   The control's base colour.
 * @param flags  Control-look flags.
 * @return The bevel shadow colour.
 */
rgb_color
HaikuControlLook::_BevelShadowColor(const rgb_color& base, uint32 flags)
{
	rgb_color bevelShadowColor;

	if ((flags & B_ACTIVATED) != 0)
		bevelShadowColor = tint_color(base, 1.17);
	else if ((flags & B_DISABLED) != 0)
		bevelShadowColor = base;
	else
		bevelShadowColor = tint_color(base, 1.105);

	return bevelShadowColor;
}


/**
 * @brief Fills a rectangle with a two-stop linear gradient derived from @a base.
 *
 * Constructs the gradient via _MakeGradient() and calls view->FillRect().
 *
 * @param view        The view to draw into.
 * @param rect        The rectangle to fill.
 * @param base        The colour from which gradient stops are tinted.
 * @param topTint     Tint factor for the leading stop (top or left).
 * @param bottomTint  Tint factor for the trailing stop (bottom or right).
 * @param orientation @c B_HORIZONTAL (top-to-bottom) or @c B_VERTICAL (left-to-right).
 */
void
HaikuControlLook::_FillGradient(BView* view, const BRect& rect,
	const rgb_color& base, float topTint, float bottomTint,
	orientation orientation)
{
	BGradientLinear gradient;
	_MakeGradient(gradient, rect, base, topTint, bottomTint, orientation);
	view->FillRect(rect, gradient);
}


/**
 * @brief Fills a rectangle with a four-stop glossy gradient derived from @a base.
 *
 * Constructs the gradient via _MakeGlossyGradient() and calls view->FillRect().
 * The two middle stops are placed at gradient offsets 132 and 136 to create a
 * sharp gloss sheen near the centre.
 *
 * @param view         The view to draw into.
 * @param rect         The rectangle to fill.
 * @param base         The colour from which gradient stops are tinted.
 * @param topTint      Tint for the leading stop.
 * @param middle1Tint  Tint for the first middle stop (just before the sheen).
 * @param middle2Tint  Tint for the second middle stop (just after the sheen).
 * @param bottomTint   Tint for the trailing stop.
 * @param orientation  @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::_FillGlossyGradient(BView* view, const BRect& rect,
	const rgb_color& base, float topTint, float middle1Tint,
	float middle2Tint, float bottomTint, orientation orientation)
{
	BGradientLinear gradient;
	_MakeGlossyGradient(gradient, rect, base, topTint, middle1Tint,
		middle2Tint, bottomTint, orientation);
	view->FillRect(rect, gradient);
}


/**
 * @brief Populates a BGradientLinear with a two-stop gradient derived from @a base.
 *
 * The gradient runs from top to bottom for @c B_HORIZONTAL orientation, and
 * from left to right for @c B_VERTICAL.  Existing colour stops are not cleared;
 * callers should pass a freshly constructed or emptied gradient.
 *
 * @param gradient    The gradient object to populate.
 * @param rect        Used to determine the gradient start and end points.
 * @param base        The colour from which stops are tinted.
 * @param topTint     Tint factor for the leading stop.
 * @param bottomTint  Tint factor for the trailing stop.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::_MakeGradient(BGradientLinear& gradient, const BRect& rect,
	const rgb_color& base, float topTint, float bottomTint,
	orientation orientation) const
{
	gradient.AddColor(tint_color(base, topTint), 0);
	gradient.AddColor(tint_color(base, bottomTint), 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());
}


/**
 * @brief Populates a BGradientLinear with a four-stop glossy gradient.
 *
 * Places stops at offsets 0, 132, 136, and 255 to produce a sharp gloss
 * transition at approximately 52% of the gradient length.
 *
 * @param gradient    The gradient object to populate.
 * @param rect        Used to determine the gradient start and end points.
 * @param base        The colour from which stops are tinted.
 * @param topTint     Tint for the leading stop.
 * @param middle1Tint Tint for the first middle stop (offset 132).
 * @param middle2Tint Tint for the second middle stop (offset 136).
 * @param bottomTint  Tint for the trailing stop.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::_MakeGlossyGradient(BGradientLinear& gradient, const BRect& rect,
	const rgb_color& base, float topTint, float middle1Tint,
	float middle2Tint, float bottomTint,
	orientation orientation) const
{
	gradient.AddColor(tint_color(base, topTint), 0);
	gradient.AddColor(tint_color(base, middle1Tint), 132);
	gradient.AddColor(tint_color(base, middle2Tint), 136);
	gradient.AddColor(tint_color(base, bottomTint), 255);
	gradient.SetStart(rect.LeftTop());
	if (orientation == B_HORIZONTAL)
		gradient.SetEnd(rect.LeftBottom());
	else
		gradient.SetEnd(rect.RightTop());
}


/**
 * @brief Populates a BGradientLinear with the standard button surface gradient.
 *
 * Activated buttons use a simple two-stop gradient; normal buttons use the
 * four-stop glossy gradient.  Disabled buttons blend the tints toward
 * @c B_NO_TINT; hovered buttons multiply tints by @c kHoverTintFactor.
 *
 * @param gradient    The gradient object to populate.
 * @param rect        Used to determine the gradient endpoints.
 * @param base        The button's base colour.
 * @param flags       Control-look flags (e.g. @c B_ACTIVATED, @c B_DISABLED, @c B_HOVER).
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 */
void
HaikuControlLook::_MakeButtonGradient(BGradientLinear& gradient, BRect& rect,
	const rgb_color& base, uint32 flags, orientation orientation) const
{
	float topTint = 0.6;
	float middleTint1 = 0.75;
	float middleTint2 = 0.9;
	float bottomTint = 1.01;

	if ((flags & B_ACTIVATED) != 0) {
		topTint = 1.135;
		bottomTint = 1.105;
	}

	if ((flags & B_DISABLED) != 0) {
		topTint = (topTint + B_NO_TINT) / 2;
		middleTint1 = (middleTint1 + B_NO_TINT) / 2;
		middleTint2 = (middleTint2 + B_NO_TINT) / 2;
		bottomTint = (bottomTint + B_NO_TINT) / 2;
	} else if ((flags & B_HOVER) != 0) {
		topTint *= kHoverTintFactor;
		middleTint1 *= kHoverTintFactor;
		middleTint2 *= kHoverTintFactor;
		bottomTint *= kHoverTintFactor;
	}

	if ((flags & B_ACTIVATED) != 0) {
		_MakeGradient(gradient, rect, base, topTint, bottomTint, orientation);
	} else {
		_MakeGlossyGradient(gradient, rect, base, topTint, middleTint1,
			middleTint2, bottomTint, orientation);
	}
}


/**
 * @brief Determines the mark colour for radio buttons and check boxes.
 *
 * Returns @c false (no mark) when neither @c B_ACTIVATED, @c B_PARTIALLY_ACTIVATED,
 * nor @c B_CLICKED is set.  Otherwise blends @c B_CONTROL_MARK_COLOR with
 * @a base according to the activation and click state.
 *
 * @param base   The control's base colour used for blending.
 * @param color  On return, contains the mark colour to use.
 * @param flags  Control-look flags.
 * @return @c true if a mark should be drawn; @c false otherwise.
 */
bool
HaikuControlLook::_RadioButtonAndCheckBoxMarkColor(const rgb_color& base,
	rgb_color& color, uint32 flags) const
{
	if ((flags & (B_ACTIVATED | B_PARTIALLY_ACTIVATED | B_CLICKED)) == 0) {
		// no mark to be drawn at all
		return false;
	}

	color = ui_color(B_CONTROL_MARK_COLOR);

	float mix = 1.0;

	if ((flags & B_DISABLED) != 0) {
		// activated, but disabled
		mix = 0.4;
	} else if ((flags & B_CLICKED) != 0) {
		if ((flags & B_ACTIVATED) != 0) {
			// losing activation
			mix = 0.7;
		} else {
			// becoming activated (or losing partial activation)
			mix = 0.3;
		}
	} else {
		// simply activated or partially activated
	}

	color.red = uint8(color.red * mix + base.red * (1.0 - mix));
	color.green = uint8(color.green * mix + base.green * (1.0 - mix));
	color.blue = uint8(color.blue * mix + base.blue * (1.0 - mix));

	return true;
}

} // namespace BPrivate
