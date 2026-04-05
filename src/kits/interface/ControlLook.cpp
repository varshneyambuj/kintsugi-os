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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2012-2020 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ControlLook.cpp
 * @brief Implementation of BControlLook, the abstract rendering interface for controls
 *
 * BControlLook defines the interface through which all standard Interface Kit
 * controls delegate their drawing. The global be_control_look pointer is set to
 * a concrete subclass (e.g., HaikuControlLook) at startup, allowing the visual
 * style to be replaced by add-ons.
 *
 * @see HaikuControlLook, BView
 */


#include <ControlLook.h>

#include <algorithm>
#include <binary_compatibility/Interface.h>


namespace BPrivate {


/**
 * @brief Construct a BControlLook, initialising the workspace cache to an
 *        invalid sentinel so that it is refreshed on first use.
 */
BControlLook::BControlLook()
	:
	fCachedWorkspace(-1)
{
}


/**
 * @brief Destroy the BControlLook.
 *
 * The destructor is virtual so that concrete subclasses are correctly destroyed
 * when deleted through a BControlLook pointer.
 */
BControlLook::~BControlLook()
{
}


/**
 * @brief Convert a symbolic spacing constant to a concrete pixel value.
 *
 * Maps B_USE_* spacing tokens (e.g. B_USE_DEFAULT_SPACING,
 * B_USE_HALF_ITEM_SPACING, B_USE_BIG_SPACING) to pixel values derived from
 * DefaultItemSpacing(). Arbitrary floating-point values are returned unchanged.
 *
 * @param spacing A B_USE_* spacing constant or a literal pixel value.
 * @return The resolved spacing in pixels.
 * @see DefaultItemSpacing()
 */
float
BControlLook::ComposeSpacing(float spacing)
{
	switch ((int)spacing) {
		case B_USE_DEFAULT_SPACING:
		case B_USE_ITEM_SPACING:
			return be_control_look->DefaultItemSpacing();
		case B_USE_HALF_ITEM_SPACING:
			return ceilf(be_control_look->DefaultItemSpacing() * 0.5f);
		case B_USE_WINDOW_SPACING:
			return be_control_look->DefaultItemSpacing();
		case B_USE_SMALL_SPACING:
			return ceilf(be_control_look->DefaultItemSpacing() * 0.7f);
		case B_USE_CORNER_SPACING:
			return ceilf(be_control_look->DefaultItemSpacing() * 1.272f);
		case B_USE_BIG_SPACING:
			return ceilf(be_control_look->DefaultItemSpacing() * 1.8f);

		case B_USE_BORDER_SPACING:
			return std::max(1.0f, floorf(be_control_look->DefaultItemSpacing() / 11.0f));
	}

	return spacing;
}


/**
 * @brief Compute a DPI-scaled icon size from a nominal pixel count.
 *
 * Scales \a size by the ratio of the current plain-font size to 12 pt, then
 * returns a BSize with both dimensions reduced by one to account for the
 * inclusive pixel convention used by BRect.
 *
 * @param size The nominal icon dimension in pixels at 12 pt font size.
 * @return A BSize whose width and height equal the scaled dimension minus one.
 */
BSize
BControlLook::ComposeIconSize(int32 size)
{
	float scale = be_plain_font->Size() / 12.0f;
	if (scale < 1.0f)
		scale = 1.0f;

	const int32 scaled = (int32)(size * scale);
	return BSize(scaled - 1, scaled - 1);
}


/**
 * @brief Determine whether a rectangle needs to be drawn in the current update.
 *
 * Transforms \a rect from the view's coordinate system to screen coordinates,
 * computes its axis-aligned bounding box, and checks whether that box
 * intersects \a updateRect. This is an optimisation that lets drawing code
 * skip invisible regions without manually performing the transform.
 *
 * @param view       The view whose current transform is used.
 * @param rect       The candidate rectangle in view-local coordinates.
 * @param updateRect The dirty rectangle in view-local coordinates.
 * @return True if \a rect intersects the update region, false otherwise.
 */
bool
BControlLook::ShouldDraw(BView* view, const BRect& rect, const BRect& updateRect)
{
	if (!rect.IsValid())
		return false;

	BPoint points[4];
	points[0] = rect.LeftTop();
	points[1] = rect.RightBottom();
	points[2] = rect.LeftBottom();
	points[3] = rect.RightTop();

	view->TransformTo(B_VIEW_COORDINATES).Apply(points, 4);

	BRect dest;
	dest.left = dest.right = points[0].x;
	dest.top = dest.bottom = points[0].y;
	for (int i = 1; i < 4; i++) {
		dest.left = std::min(dest.left, points[i].x);
		dest.right = std::max(dest.right, points[i].x);
		dest.top = std::min(dest.top, points[i].y);
		dest.bottom = std::max(dest.bottom, points[i].y);
	}
	dest.left = floorf(dest.left);
	dest.right = ceilf(dest.right);
	dest.top = floorf(dest.top);
	dest.bottom = ceilf(dest.bottom);

	return dest.Intersects(updateRect);
}


/**
 * @brief Draw a text label with an optional icon, using the default alignment.
 *
 * Convenience overload that calls the full DrawLabel() with
 * DefaultLabelAlignment() so callers do not need to specify alignment when
 * they want the standard look.
 *
 * @param view       The view to draw into.
 * @param label      The label string, or NULL to omit the text.
 * @param icon       An optional icon bitmap placed beside the label, or NULL.
 * @param rect       The bounding rectangle available for the label.
 * @param updateRect The dirty rectangle; drawing outside it can be skipped.
 * @param base       The background colour used to derive text and shadow tones.
 * @param flags      Drawing flags (e.g. BControlLook::B_DISABLED).
 * @param textColor  Override text colour, or NULL to derive from \a base.
 * @see DefaultLabelAlignment()
 */
void
BControlLook::DrawLabel(BView* view, const char* label, const BBitmap* icon,
	BRect rect, const BRect& updateRect, const rgb_color& base, uint32 flags,
	const rgb_color* textColor)
{
	DrawLabel(view, label, icon, rect, updateRect, base, flags,
		DefaultLabelAlignment(), textColor);
}


/**
 * @brief Query the combined frame and background insets for a given control region.
 *
 * Adds the frame insets (from GetFrameInsets()) and background insets (from
 * GetBackgroundInsets()) together so that callers can determine the total
 * inset of the content area from the control border in a single call.
 *
 * @param frameType      The frame style (e.g. B_BUTTON_FRAME).
 * @param backgroundType The background fill style (e.g. B_BUTTON_BACKGROUND).
 * @param flags          Drawing flags that may affect inset sizes.
 * @param _left   Set to the total left inset on return.
 * @param _top    Set to the total top inset on return.
 * @param _right  Set to the total right inset on return.
 * @param _bottom Set to the total bottom inset on return.
 * @see GetFrameInsets(), GetBackgroundInsets()
 */
void
BControlLook::GetInsets(frame_type frameType, background_type backgroundType,
	uint32 flags, float& _left, float& _top, float& _right, float& _bottom)
{
	GetFrameInsets(frameType, flags, _left, _top, _right, _bottom);

	float left, top, right, bottom;
	GetBackgroundInsets(backgroundType, flags, left, top, right, bottom);

	_left += left;
	_top += top;
	_right += right;
	_bottom += bottom;
}


/**
 * @brief Return the preferred scroll bar width for the given orientation.
 *
 * Returns the larger of the corner spacing metric and a 14-pixel minimum so
 * that scroll bars remain usable at all DPI scales.
 *
 * @param orientation B_HORIZONTAL or B_VERTICAL (both use the same width in
 *                    this implementation).
 * @return Scroll bar width in pixels.
 * @see ComposeSpacing()
 */
float
BControlLook::GetScrollBarWidth(orientation orientation)
{
	return std::max(ComposeSpacing(B_USE_CORNER_SPACING), 14.0f);
}


/**
 * @brief Store desktop background information used when drawing transparent
 *        or blended control backgrounds.
 *
 * Caches the supplied message and resets fCachedWorkspace to -1 so that the
 * next draw call will re-derive workspace-specific colours from the new data.
 *
 * @param backgroundInfo A BMessage describing the current desktop background
 *                       (colour, image path, placement, etc.).
 */
void
BControlLook::SetBackgroundInfo(const BMessage& backgroundInfo)
{
	fBackgroundInfo = backgroundInfo;
	fCachedWorkspace = -1;
}


/**
 * @brief GCC 2 binary-compatibility thunk for DrawTabFrame().
 *
 * Routes the call to the virtual DrawTabFrame() method so that binaries
 * compiled against the old ABI can invoke the correct overridden implementation.
 *
 * @param controlLook The BControlLook instance to dispatch on.
 * @param view        The view to draw into.
 * @param rect        The tab frame rectangle (may be modified by the call).
 * @param updateRect  The dirty region.
 * @param base        Background base colour.
 * @param flags       Drawing flags.
 * @param borders     Which borders to draw.
 * @param borderStyle The border style constant.
 * @param side        Which side of the tab bar this tab occupies.
 */
extern "C" void
B_IF_GCC_2(_ReservedControlLook1__Q28BPrivate12BControlLook,
		_ZN8BPrivate12BControlLook21_ReservedControlLook1Ev)(
	BControlLook* controlLook, BView* view, BRect& rect,
	const BRect& updateRect, const rgb_color& base, uint32 flags,
	uint32 borders, border_style borderStyle, uint32 side)
{
	controlLook->DrawTabFrame(view, rect, updateRect, base, flags, borders,
		borderStyle, side);
}


/**
 * @brief GCC 2 binary-compatibility thunk for DrawScrollBarButton().
 *
 * @param controlLook  The BControlLook instance to dispatch on.
 * @param view         The view to draw into.
 * @param rect         The button rectangle.
 * @param updateRect   The dirty region.
 * @param base         Background base colour.
 * @param text         Arrow/glyph colour.
 * @param flags        Drawing flags.
 * @param direction    Arrow direction constant.
 * @param orientation  B_HORIZONTAL or B_VERTICAL.
 * @param down         True if the button is in its pressed state.
 */
extern "C" void
B_IF_GCC_2(_ReservedControlLook2__Q28BPrivate12BControlLook,
		_ZN8BPrivate12BControlLook21_ReservedControlLook2Ev)(
	BControlLook* controlLook, BView* view, BRect rect,
		const BRect& updateRect, const rgb_color& base, const rgb_color& text,
		uint32 flags, int32 direction, orientation orientation, bool down)
{
	controlLook->DrawScrollBarButton(view, rect, updateRect, base, text,
		flags, direction, orientation, down);
}


/**
 * @brief GCC 2 binary-compatibility thunk for DrawScrollBarThumb().
 *
 * @param controlLook The BControlLook instance to dispatch on.
 * @param view        The view to draw into.
 * @param rect        The thumb rectangle.
 * @param updateRect  The dirty region.
 * @param base        Background base colour.
 * @param flags       Drawing flags.
 * @param direction   Unused direction parameter (ignored by implementations).
 * @param orientation B_HORIZONTAL or B_VERTICAL.
 * @param knobStyle   Style constant for the thumb knob decoration.
 */
extern "C" void
B_IF_GCC_2(_ReservedControlLook3__Q28BPrivate12BControlLook,
		_ZN8BPrivate12BControlLook21_ReservedControlLook3Ev)(
	BControlLook* controlLook, BView* view, BRect rect,
		const BRect& updateRect, const rgb_color& base, uint32 flags,
		int32 direction, orientation orientation, uint32 knobStyle)
{
	controlLook->DrawScrollBarThumb(view, rect, updateRect, base, flags,
		orientation, knobStyle);
}


/**
 * @brief GCC 2 binary-compatibility thunk for DrawScrollBarBorder().
 *
 * @param controlLook The BControlLook instance to dispatch on.
 * @param view        The view to draw into.
 * @param rect        The border rectangle.
 * @param updateRect  The dirty region.
 * @param base        Background base colour.
 * @param flags       Drawing flags.
 * @param orientation B_HORIZONTAL or B_VERTICAL.
 */
extern "C" void
B_IF_GCC_2(_ReservedControlLook4__Q28BPrivate12BControlLook,
		_ZN8BPrivate12BControlLook21_ReservedControlLook4Ev)(
	BControlLook* controlLook, BView* view, BRect rect,
		const BRect& updateRect, const rgb_color& base, uint32 flags,
		orientation orientation)
{
	controlLook->DrawScrollBarBorder(view, rect, updateRect, base, flags,
		orientation);
}


/**
 * @brief GCC 2 binary-compatibility thunk for GetScrollBarWidth().
 *
 * @param controlLook The BControlLook instance to dispatch on.
 * @param orientation B_HORIZONTAL or B_VERTICAL.
 * @return The scroll bar width in pixels.
 */
extern "C" float
B_IF_GCC_2(_ReservedControlLook5__Q28BPrivate12BControlLook,
		_ZN8BPrivate12BControlLook21_ReservedControlLook5Ev)(
	BControlLook* controlLook, orientation orientation)
{
	return controlLook->GetScrollBarWidth(orientation);
}


void BControlLook::_ReservedControlLook6() {}
void BControlLook::_ReservedControlLook7() {}
void BControlLook::_ReservedControlLook8() {}
void BControlLook::_ReservedControlLook9() {}
void BControlLook::_ReservedControlLook10() {}


/** @brief Global pointer to the active BControlLook instance.
 *
 *  Set to a concrete implementation (e.g. HaikuControlLook) during
 *  Interface Kit initialisation in InterfaceDefs.cpp. All controls use
 *  this pointer to delegate their rendering.
 */
// Initialized in InterfaceDefs.cpp
BControlLook* be_control_look = NULL;

} // namespace BPrivate
