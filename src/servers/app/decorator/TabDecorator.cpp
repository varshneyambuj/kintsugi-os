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
 *   Copyright 2001-2020 Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Ryan Leavengood, leavengood@gmail.com
 *       Philippe Saint-Pierre, stpere@gmail.com
 *       John Scipione, jscipione@gmail.com
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *       Clemens Zeidler, haiku@clemens-zeidler.de
 *       Joseph Groover, looncraz@looncraz.net
 *       Jacob Secunda, secundaja@gmail.com
 */


/**
 * @file TabDecorator.cpp
 * @brief Decorator base class that arranges a tabbed title bar and a
 *        resizable border frame.
 *
 * TabDecorator provides the geometry pass: it computes border widths, tab
 * rectangles, resize-knob placement, and multi-tab stacking layout based on
 * the window look and font metrics. Concrete painting (gradients, button
 * artwork, title text) is left to subclasses such as DefaultDecorator.
 *
 * @see DefaultDecorator, Decorator
 */


#include "TabDecorator.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdio.h>

#include <Autolock.h>
#include <Debug.h>
#include <GradientLinear.h>
#include <Rect.h>
#include <View.h>

#include <WindowPrivate.h>

#include "BitmapDrawingEngine.h"
#include "DesktopSettings.h"
#include "DrawingEngine.h"
#include "DrawState.h"
#include "FontManager.h"
#include "PatternHandler.h"


//#define DEBUG_DECORATOR
#ifdef DEBUG_DECORATOR
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


/**
 * @brief Returns true when two float widths differ by at most one pixel.
 *
 * Used by the multi-tab size distribution to coalesce tabs whose widths are
 * effectively equal under rounding.
 *
 * @param x First width.
 * @param y Second width.
 * @return true if the values are within one pixel of each other.
 */
static bool
int_equal(float x, float y)
{
	return abs(x - y) <= 1;
}


/** @brief Length, in pixels (at 1x font scale), of the lines that mark the
           resize-active region along the bottom and right borders. */
static const float kBorderResizeLength = 22.0;
/** @brief Side length, in pixels (at 1x font scale), of the document-look
           resize knob in the bottom-right corner. */
static const float kResizeKnobSize = 18.0;


//	#pragma mark -


/**
 * @brief Constructs a TabDecorator for @a frame on @a desktop.
 *
 * The base Decorator initializer is invoked first; this constructor only
 * resets the moving-tab cache used during multi-tab drag.
 *
 * @param settings Current desktop settings.
 * @param frame    Initial client-area frame.
 * @param desktop  Desktop the window belongs to.
 *
 * @todo Drop the DesktopSettings parameter and use a private accessor on
 *       the Decorator base instead.
 */
// TODO: get rid of DesktopSettings here, and introduce private accessor
//	methods to the Decorator base class
TabDecorator::TabDecorator(DesktopSettings& settings, BRect frame,
							Desktop* desktop)
	:
	Decorator(settings, frame, desktop),
	fOldMovingTab(0, 0, -1, -1)
{
	STRACE(("TabDecorator:\n"));
	STRACE(("\tFrame (%.1f,%.1f,%.1f,%.1f)\n",
		frame.left, frame.top, frame.right, frame.bottom));

	// TODO: If the decorator was created with a frame too small, it should
	// resize itself!
}


/**
 * @brief Destroys the TabDecorator. Tab list members are deleted by the
 *        Decorator base class.
 */
TabDecorator::~TabDecorator()
{
	STRACE(("TabDecorator: ~TabDecorator()\n"));
}


// #pragma mark - Public methods


/**
 * @brief Repaints the parts of the decorator that intersect @a updateRect.
 *
 * The frame, optional outline frame (during interactive resize), and the
 * tab bar are each clipped against @a updateRect before drawing.
 *
 * @param updateRect Rectangle to repaint, in screen coordinates.
 */
void
TabDecorator::Draw(BRect updateRect)
{
	STRACE(("TabDecorator::Draw(BRect "
		"updateRect(l:%.1f, t:%.1f, r:%.1f, b:%.1f))\n",
		updateRect.left, updateRect.top, updateRect.right, updateRect.bottom));

	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(updateRect & fBorderRect);

	if (IsOutlineResizing())
		_DrawOutlineFrame(updateRect & fOutlineBorderRect);

	_DrawTabs(updateRect & fTitleBarRect);
}


/**
 * @brief Repaints the entire decorator, ignoring the dirty region.
 *
 * Used for full refreshes after a font, color, or look change.
 */
void
TabDecorator::Draw()
{
	STRACE(("TabDecorator: Draw()"));

	fDrawingEngine->SetDrawState(&fDrawState);

	_DrawFrame(fBorderRect);

	if (IsOutlineResizing())
		_DrawOutlineFrame(fOutlineBorderRect);

	_DrawTabs(fTitleBarRect);
}


/**
 * @brief Hit-tests @a where against tabs, borders, and the resize corner.
 *
 * Extends the base class behaviour by recognising hits on the four borders
 * and on the resize knob/corner, including the diagonal resize area at the
 * bottom-right of titled, floating, and modal windows.
 *
 * @param where Point to test, in screen coordinates.
 * @param tab   Out-parameter receiving the index of the hit tab, or -1.
 * @return The matching Region constant, or REGION_NONE.
 */
Decorator::Region
TabDecorator::RegionAt(BPoint where, int32& tab) const
{
	// Let the base class version identify hits of the buttons and the tab.
	Region region = Decorator::RegionAt(where, tab);
	if (region != REGION_NONE)
		return region;

	// check the resize corner
	if (fTopTab->look == B_DOCUMENT_WINDOW_LOOK && fResizeRect.Contains(where))
		return REGION_RIGHT_BOTTOM_CORNER;

	// hit-test the borders
	if (fLeftBorder.Contains(where))
		return REGION_LEFT_BORDER;
	if (fTopBorder.Contains(where))
		return REGION_TOP_BORDER;

	// Part of the bottom and right borders may be a resize-region, so we have
	// to check explicitly, if it has been it.
	if (fRightBorder.Contains(where))
		region = REGION_RIGHT_BORDER;
	else if (fBottomBorder.Contains(where))
		region = REGION_BOTTOM_BORDER;
	else
		return REGION_NONE;

	// check resize area
	if ((fTopTab->flags & B_NOT_RESIZABLE) == 0
		&& (fTopTab->look == B_TITLED_WINDOW_LOOK
			|| fTopTab->look == B_FLOATING_WINDOW_LOOK
			|| fTopTab->look == B_MODAL_WINDOW_LOOK
			|| fTopTab->look == kLeftTitledWindowLook)) {
		BRect resizeRect(BPoint(fBottomBorder.right - fBorderResizeLength,
			fBottomBorder.bottom - fBorderResizeLength),
			fBottomBorder.RightBottom());
		if (resizeRect.Contains(where))
			return REGION_RIGHT_BOTTOM_CORNER;
	}

	return region;
}


/**
 * @brief Updates a region highlight and invalidates affected button bitmap
 *        caches.
 *
 * When the close or zoom button highlight changes, the cached pre-rendered
 * bitmap arrays for that button are zeroed so that the next paint pass
 * regenerates them with the new highlight color.
 *
 * @param region    Region whose highlight is being updated.
 * @param highlight New highlight value.
 * @param dirty     Region extended with affected paint area; may be NULL.
 * @param tabIndex  Tab index whose button caches should be invalidated.
 * @return true on success; false if the base class rejects the change.
 */
bool
TabDecorator::SetRegionHighlight(Region region, uint8 highlight,
	BRegion* dirty, int32 tabIndex)
{
	Decorator::Tab* tab
		= static_cast<Decorator::Tab*>(_TabAt(tabIndex));
	if (tab != NULL) {
		tab->isHighlighted = highlight != 0;
		// Invalidate the bitmap caches for the close/zoom button, when the
		// highlight changes.
		switch (region) {
			case REGION_CLOSE_BUTTON:
				if (highlight != RegionHighlight(region))
					memset(&tab->closeBitmaps, 0, sizeof(tab->closeBitmaps));
				break;
			case REGION_ZOOM_BUTTON:
				if (highlight != RegionHighlight(region))
					memset(&tab->zoomBitmaps, 0, sizeof(tab->zoomBitmaps));
				break;
			default:
				break;
		}
	}

	return Decorator::SetRegionHighlight(region, highlight, dirty, tabIndex);
}


/**
 * @brief Recomputes focused and unfocused frame, tab, bevel, shadow, and
 *        text colors from the current desktop settings.
 *
 * Called both at construction and whenever the system color set changes.
 * The desktop is held write-locked during the call so the work must be brief.
 *
 * @param settings Current desktop settings.
 */
void
TabDecorator::UpdateColors(DesktopSettings& settings)
{
	// Desktop is write locked, so be quick about it.
	fFocusFrameColor		= settings.UIColor(B_WINDOW_BORDER_COLOR);
	fFocusTabColor			= settings.UIColor(B_WINDOW_TAB_COLOR);
	fFocusTabColorLight		= tint_color(fFocusTabColor,
								(B_LIGHTEN_MAX_TINT + B_LIGHTEN_2_TINT) / 2);
	fFocusTabColorBevel		= tint_color(fFocusTabColor, B_LIGHTEN_2_TINT);
	fFocusTabColorShadow	= tint_color(fFocusTabColor,
								(B_DARKEN_1_TINT + B_NO_TINT) / 2);
	fFocusTextColor			= settings.UIColor(B_WINDOW_TEXT_COLOR);

	fNonFocusFrameColor		= settings.UIColor(B_WINDOW_INACTIVE_BORDER_COLOR);
	fNonFocusTabColor		= settings.UIColor(B_WINDOW_INACTIVE_TAB_COLOR);
	fNonFocusTabColorLight	= tint_color(fNonFocusTabColor,
								(B_LIGHTEN_MAX_TINT + B_LIGHTEN_2_TINT) / 2);
	fNonFocusTabColorBevel	= tint_color(fNonFocusTabColor, B_LIGHTEN_2_TINT);
	fNonFocusTabColorShadow	= tint_color(fNonFocusTabColor,
								(B_DARKEN_1_TINT + B_NO_TINT) / 2);
	fNonFocusTextColor = settings.UIColor(B_WINDOW_INACTIVE_TEXT_COLOR);
}


/**
 * @brief Recomputes border, frame, resize, and tab geometry.
 *
 * Selects a border width based on the window look, scales it to the current
 * font size, lays out the four border rectangles around the client frame,
 * positions the resize knob, and finally delegates to _DoTabLayout when the
 * window has a tab.
 */
void
TabDecorator::_DoLayout()
{
	STRACE(("TabDecorator: Do Layout\n"));
	// Here we determine the size of every rectangle that we use
	// internally when we are given the size of the client rectangle.

	bool hasTab = false;

	// TODO: Put this computation somewhere more central!
	const float scaleFactor = max_c(fDrawState.Font().Size() / 12.0f, 1.0f);

	switch ((int)fTopTab->look) {
		case B_MODAL_WINDOW_LOOK:
			fBorderWidth = 5;
			break;

		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
			hasTab = true;
			fBorderWidth = 5;
			break;
		case B_FLOATING_WINDOW_LOOK:
		case kLeftTitledWindowLook:
			hasTab = true;
			fBorderWidth = 3;
			break;

		case B_BORDERED_WINDOW_LOOK:
			fBorderWidth = 1;
			break;

		default:
			fBorderWidth = 0;
	}

	fBorderWidth = int32(fBorderWidth * scaleFactor);
	fResizeKnobSize = kResizeKnobSize * scaleFactor;
	fBorderResizeLength = kBorderResizeLength * scaleFactor;

	// calculate left/top/right/bottom borders
	if (fBorderWidth > 0) {
		// NOTE: no overlapping, the left and right border rects
		// don't include the corners!
		fLeftBorder.Set(fFrame.left - fBorderWidth, fFrame.top,
			fFrame.left - 1, fFrame.bottom);

		fRightBorder.Set(fFrame.right + 1, fFrame.top ,
			fFrame.right + fBorderWidth, fFrame.bottom);

		fTopBorder.Set(fFrame.left - fBorderWidth, fFrame.top - fBorderWidth,
			fFrame.right + fBorderWidth, fFrame.top - 1);

		fBottomBorder.Set(fFrame.left - fBorderWidth, fFrame.bottom + 1,
			fFrame.right + fBorderWidth, fFrame.bottom + fBorderWidth);
	} else {
		// no border
		fLeftBorder.Set(0.0, 0.0, -1.0, -1.0);
		fRightBorder.Set(0.0, 0.0, -1.0, -1.0);
		fTopBorder.Set(0.0, 0.0, -1.0, -1.0);
		fBottomBorder.Set(0.0, 0.0, -1.0, -1.0);
	}

	fBorderRect = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());

	// calculate resize rect
	if (fBorderWidth > 1) {
		fResizeRect.Set(fBottomBorder.right - fResizeKnobSize,
			fBottomBorder.bottom - fResizeKnobSize, fBottomBorder.right,
			fBottomBorder.bottom);
	} else {
		// no border or one pixel border (menus and such)
		fResizeRect.Set(0, 0, -1, -1);
	}

	if (hasTab) {
		_DoTabLayout();
		return;
	} else {
		// no tab
		for (int32 i = 0; i < fTabList.CountItems(); i++) {
			Decorator::Tab* tab = fTabList.ItemAt(i);
			tab->tabRect.Set(0.0, 0.0, -1.0, -1.0);
		}
		fTabsRegion.MakeEmpty();
		fTitleBarRect.Set(0.0, 0.0, -1.0, -1.0);
	}
}


/**
 * @brief Computes the one-pixel-wide outline rectangles used to render the
 *        ghost frame during interactive outline-resize.
 */
void
TabDecorator::_DoOutlineLayout()
{
	fOutlineBorderWidth = 1;

	// calculate left/top/right/bottom outline borders
	// NOTE: no overlapping, the left and right border rects
	// don't include the corners!
	fLeftOutlineBorder.Set(fFrame.left - fOutlineBorderWidth, fFrame.top,
		fFrame.left - 1, fFrame.bottom);

	fRightOutlineBorder.Set(fFrame.right + 1, fFrame.top ,
		fFrame.right + fOutlineBorderWidth, fFrame.bottom);

	fTopOutlineBorder.Set(fFrame.left - fOutlineBorderWidth,
		fFrame.top - fOutlineBorderWidth,
		fFrame.right + fOutlineBorderWidth, fFrame.top - 1);

	fBottomOutlineBorder.Set(fFrame.left - fOutlineBorderWidth,
		fFrame.bottom + 1,
		fFrame.right + fOutlineBorderWidth,
		fFrame.bottom + fOutlineBorderWidth);

	fOutlineBorderRect = BRect(fTopOutlineBorder.LeftTop(),
		fBottomOutlineBorder.RightBottom());
}


/**
 * @brief Lays out every tab in the title bar.
 *
 * Computes minimum and maximum tab widths from the title's measured text,
 * places each tab side by side, applies floating-window inset adjustments,
 * shrinks tabs proportionally if the combined width exceeds the window
 * width, and finally lays out the buttons inside each tab.
 */
void
TabDecorator::_DoTabLayout()
{
	float tabOffset = 0;
	if (fTabList.CountItems() == 1) {
		float tabSize;
		tabOffset = _SingleTabOffsetAndSize(tabSize);
	}

	float sumTabWidth = 0;
	// calculate our tab rect
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = _TabAt(i);

		BRect& tabRect = tab->tabRect;
		// distance from one item of the tab bar to another.
		// In this case the text and close/zoom rects
		tab->textOffset = _DefaultTextOffset();

		font_height fontHeight;
		fDrawState.Font().GetHeight(fontHeight);

		if (tab->look != kLeftTitledWindowLook) {
			const float spacing = fBorderWidth * 1.4f;
			tabRect.Set(fFrame.left - fBorderWidth,
				fFrame.top - fBorderWidth
					- ceilf(fontHeight.ascent + fontHeight.descent + spacing),
				((fFrame.right - fFrame.left) < (spacing * 5) ?
					fFrame.left + (spacing * 5) : fFrame.right) + fBorderWidth,
				fFrame.top - fBorderWidth);
		} else {
			tabRect.Set(fFrame.left - fBorderWidth
				- ceilf(fontHeight.ascent + fontHeight.descent + fBorderWidth),
					fFrame.top - fBorderWidth, fFrame.left - fBorderWidth,
				fFrame.bottom + fBorderWidth);
		}

		// format tab rect for a floating window - make the rect smaller
		if (tab->look == B_FLOATING_WINDOW_LOOK) {
			tabRect.InsetBy(0, 2);
			tabRect.OffsetBy(0, 2);
		}

		float offset;
		float size;
		float inset;
		_GetButtonSizeAndOffset(tabRect, &offset, &size, &inset);

		// tab->minTabSize contains just the room for the buttons
		tab->minTabSize = inset * 2 + tab->textOffset;
		if ((tab->flags & B_NOT_CLOSABLE) == 0)
			tab->minTabSize += offset + size;
		if ((tab->flags & B_NOT_ZOOMABLE) == 0)
			tab->minTabSize += offset + size;

		// tab->maxTabSize contains tab->minTabSize + the width required for the
		// title
		tab->maxTabSize = fDrawingEngine
			? ceilf(fDrawingEngine->StringWidth(Title(tab), strlen(Title(tab)),
				fDrawState.Font())) : 0.0;
		if (tab->maxTabSize > 0.0)
			tab->maxTabSize += tab->textOffset;
		tab->maxTabSize += tab->minTabSize;

		float tabSize = (tab->look != kLeftTitledWindowLook
			? fFrame.Width() : fFrame.Height()) + fBorderWidth * 2;
		if (tabSize < tab->minTabSize)
			tabSize = tab->minTabSize;
		if (tabSize > tab->maxTabSize)
			tabSize = tab->maxTabSize;

		// layout buttons and truncate text
		if (tab->look != kLeftTitledWindowLook)
			tabRect.right = tabRect.left + tabSize;
		else
			tabRect.bottom = tabRect.top + tabSize;

		// make sure fTabOffset is within limits and apply it to
		// the tabRect
		tab->tabOffset = (uint32)tabOffset;
		if (tab->tabLocation != 0.0 && fTabList.CountItems() == 1
			&& tab->tabOffset > (fRightBorder.right - fLeftBorder.left
				- tabRect.Width())) {
			tab->tabOffset = uint32(fRightBorder.right - fLeftBorder.left
				- tabRect.Width());
		}
		tabRect.OffsetBy(tab->tabOffset, 0);
		tabOffset += tabRect.Width();

		sumTabWidth += tabRect.Width();
	}

	float windowWidth = fFrame.Width() + 2 * fBorderWidth;
	if (CountTabs() > 1 && sumTabWidth > windowWidth)
		_DistributeTabSize(sumTabWidth - windowWidth);

	// finally, layout the buttons and text within the tab rect
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);

		if (i == 0)
			fTitleBarRect = tab->tabRect;
		else
			fTitleBarRect = fTitleBarRect | tab->tabRect;

		_LayoutTabItems(tab, tab->tabRect);
	}

	fTabsRegion = fTitleBarRect;
}


/**
 * @brief Reduces tab widths by @a delta total pixels in a stacked window.
 *
 * Iteratively shrinks the widest tab(s) by the gap to the second-widest
 * width until the requested reduction has been absorbed. After distributing
 * the shrink, neighbouring tabs are pulled inward to keep the row contiguous
 * and tabOffset values are refreshed for each tab.
 *
 * @param delta Total width to remove from the tab row, in pixels.
 *
 * @note The function recurses with the remaining delta when the largest
 *       single shrink step did not absorb the full request.
 */
void
TabDecorator::_DistributeTabSize(float delta)
{
	int32 tabCount = fTabList.CountItems();
	ASSERT(tabCount > 1);

	float maxTabSize = 0;
	float secMaxTabSize = 0;
	int32 nTabsWithMaxSize = 0;
	for (int32 i = 0; i < tabCount; i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (tab == NULL)
			continue;

		float tabWidth = tab->tabRect.Width();
		if (int_equal(maxTabSize, tabWidth)) {
			nTabsWithMaxSize++;
			continue;
		}
		if (maxTabSize < tabWidth) {
			secMaxTabSize = maxTabSize;
			maxTabSize = tabWidth;
			nTabsWithMaxSize = 1;
		} else if (secMaxTabSize <= tabWidth)
			secMaxTabSize = tabWidth;
	}

	float minus = ceilf(std::min(maxTabSize - secMaxTabSize, delta));
	if (minus < 1.0)
		return;
	delta -= minus;
	minus /= nTabsWithMaxSize;

	Decorator::Tab* previousTab = NULL;
	for (int32 i = 0; i < tabCount; i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (tab == NULL)
			continue;

		if (int_equal(maxTabSize, tab->tabRect.Width()))
			tab->tabRect.right -= minus;

		if (previousTab != NULL) {
			float offsetX = previousTab->tabRect.right - tab->tabRect.left;
			tab->tabRect.OffsetBy(offsetX, 0);
		}

		previousTab = tab;
	}

	if (delta > 0) {
		_DistributeTabSize(delta);
		return;
	}

	// done
	if (previousTab != NULL)
		previousTab->tabRect.right = floorf(fFrame.right + fBorderWidth);

	for (int32 i = 0; i < tabCount; i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		if (tab == NULL)
			continue;

		tab->tabOffset = uint32(tab->tabRect.left - fLeftBorder.left);
	}
}


/**
 * @brief Strokes the dashed alpha-blended outline of a window during
 *        interactive outline-resize.
 *
 * @param rect Outline rectangle to stroke, in screen coordinates.
 */
void
TabDecorator::_DrawOutlineFrame(BRect rect)
{
	drawing_mode oldMode;

	fDrawingEngine->SetDrawingMode(B_OP_ALPHA, oldMode);
	fDrawingEngine->SetPattern(B_MIXED_COLORS);
	fDrawingEngine->StrokeRect(rect);

	fDrawingEngine->SetDrawingMode(oldMode);
}


/**
 * @brief Subclass title hook: relays out tabs and adds the affected area to
 *        @a updateRegion.
 *
 * @param tab          Tab whose title was just changed.
 * @param string       New title text (already stored on @a tab).
 * @param updateRegion Optional dirty region; may be NULL.
 */
void
TabDecorator::_SetTitle(Decorator::Tab* tab, const char* string,
	BRegion* updateRegion)
{
	// TODO: we could be much smarter about the update region

	BRect rect = TabRect((int32) 0) | TabRect(CountTabs() - 1);
		// Get a rect of all the tabs

	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion == NULL)
		return;

	rect = rect | TabRect(CountTabs() - 1);
		// Update the rect to guarantee it updates all the tabs

	rect.bottom++;
		// the border will look differently when the title is adjacent

	updateRegion->Include(rect);
}


/**
 * @brief Translates every internal rectangle (frame, borders, tabs, resize
 *        rect, title bar) by @a offset.
 *
 * @param offset Move offset, in pixels.
 */
void
TabDecorator::_MoveBy(BPoint offset)
{
	STRACE(("TabDecorator: Move By (%.1f, %.1f)\n", offset.x, offset.y));

	// Move all internal rectangles the appropriate amount
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		Decorator::Tab* tab = fTabList.ItemAt(i);
		tab->zoomRect.OffsetBy(offset);
		tab->closeRect.OffsetBy(offset);
		tab->tabRect.OffsetBy(offset);
	}

	fFrame.OffsetBy(offset);
	fTitleBarRect.OffsetBy(offset);
	fTabsRegion.OffsetBy(offset);
	fResizeRect.OffsetBy(offset);
	fBorderRect.OffsetBy(offset);

	fLeftBorder.OffsetBy(offset);
	fRightBorder.OffsetBy(offset);
	fTopBorder.OffsetBy(offset);
	fBottomBorder.OffsetBy(offset);
}


/**
 * @brief Resizes the decorator's frame and borders, and emits the dirty
 *        rectangles needed to repaint the changed area.
 *
 * Handles both the document-look resize knob and the line-style resize
 * markers used by titled, floating, and modal looks. For single-tab
 * windows the tab is resized in place; for multi-tab windows the full
 * tab layout is rerun.
 *
 * @param offset Size delta in pixels.
 * @param dirty  Region extended with areas requiring repaint; may be NULL.
 */
void
TabDecorator::_ResizeBy(BPoint offset, BRegion* dirty)
{
	STRACE(("TabDecorator: Resize By (%.1f, %.1f)\n", offset.x, offset.y));

	// Move all internal rectangles the appropriate amount
	fFrame.right += offset.x;
	fFrame.bottom += offset.y;

	// Handle invalidation of resize rect
	if (dirty != NULL && !(fTopTab->flags & B_NOT_RESIZABLE)) {
		BRect realResizeRect;
		switch ((int)fTopTab->look) {
			case B_DOCUMENT_WINDOW_LOOK:
				realResizeRect = fResizeRect;
				// Resize rect at old location
				dirty->Include(realResizeRect);
				realResizeRect.OffsetBy(offset);
				// Resize rect at new location
				dirty->Include(realResizeRect);
				break;

			case B_TITLED_WINDOW_LOOK:
			case B_FLOATING_WINDOW_LOOK:
			case B_MODAL_WINDOW_LOOK:
			case kLeftTitledWindowLook:
				// The bottom border resize line
				realResizeRect.Set(fRightBorder.right - fBorderResizeLength,
					fBottomBorder.top,
					fRightBorder.right - fBorderResizeLength,
					fBottomBorder.bottom - 1);
				// Old location
				dirty->Include(realResizeRect);
				realResizeRect.OffsetBy(offset);
				// New location
				dirty->Include(realResizeRect);

				// The right border resize line
				realResizeRect.Set(fRightBorder.left,
					fBottomBorder.bottom - fBorderResizeLength,
					fRightBorder.right - 1,
					fBottomBorder.bottom - fBorderResizeLength);
				// Old location
				dirty->Include(realResizeRect);
				realResizeRect.OffsetBy(offset);
				// New location
				dirty->Include(realResizeRect);
				break;

			default:
				break;
		}
	}

	fResizeRect.OffsetBy(offset);

	fBorderRect.right += offset.x;
	fBorderRect.bottom += offset.y;

	fLeftBorder.bottom += offset.y;
	fTopBorder.right += offset.x;

	fRightBorder.OffsetBy(offset.x, 0.0);
	fRightBorder.bottom	+= offset.y;

	fBottomBorder.OffsetBy(0.0, offset.y);
	fBottomBorder.right	+= offset.x;

	if (dirty) {
		if (offset.x > 0.0) {
			BRect t(fRightBorder.left - offset.x, fTopBorder.top,
				fRightBorder.right, fTopBorder.bottom);
			dirty->Include(t);
			t.Set(fRightBorder.left - offset.x, fBottomBorder.top,
				fRightBorder.right, fBottomBorder.bottom);
			dirty->Include(t);
			dirty->Include(fRightBorder);
		} else if (offset.x < 0.0) {
			dirty->Include(BRect(fRightBorder.left, fTopBorder.top,
				fRightBorder.right, fBottomBorder.bottom));
		}
		if (offset.y > 0.0) {
			BRect t(fLeftBorder.left, fLeftBorder.bottom - offset.y,
				fLeftBorder.right, fLeftBorder.bottom);
			dirty->Include(t);
			t.Set(fRightBorder.left, fRightBorder.bottom - offset.y,
				fRightBorder.right, fRightBorder.bottom);
			dirty->Include(t);
			dirty->Include(fBottomBorder);
		} else if (offset.y < 0.0) {
			dirty->Include(fBottomBorder);
		}
	}

	// resize tab and layout tab items
	if (fTitleBarRect.IsValid()) {
		if (fTabList.CountItems() > 1) {
			_DoTabLayout();
			if (dirty != NULL)
				dirty->Include(fTitleBarRect);
			return;
		}

		Decorator::Tab* tab = _TabAt(0);
		BRect& tabRect = tab->tabRect;
		BRect oldTabRect(tabRect);

		float tabSize;
		float tabOffset = _SingleTabOffsetAndSize(tabSize);

		float delta = tabOffset - tab->tabOffset;
		tab->tabOffset = (uint32)tabOffset;
		if (fTopTab->look != kLeftTitledWindowLook)
			tabRect.OffsetBy(delta, 0.0);
		else
			tabRect.OffsetBy(0.0, delta);

		if (tabSize < tab->minTabSize)
			tabSize = tab->minTabSize;
		if (tabSize > tab->maxTabSize)
			tabSize = tab->maxTabSize;

		if (fTopTab->look != kLeftTitledWindowLook
			&& tabSize != tabRect.Width()) {
			tabRect.right = tabRect.left + tabSize;
		} else if (fTopTab->look == kLeftTitledWindowLook
			&& tabSize != tabRect.Height()) {
			tabRect.bottom = tabRect.top + tabSize;
		}

		if (oldTabRect != tabRect) {
			_LayoutTabItems(tab, tabRect);

			if (dirty) {
				// NOTE: the tab rect becoming smaller only would
				// handled be the Desktop anyways, so it is sufficient
				// to include it into the dirty region in it's
				// final state
				BRect redraw(tabRect);
				if (delta != 0.0) {
					redraw = redraw | oldTabRect;
					if (fTopTab->look != kLeftTitledWindowLook)
						redraw.bottom++;
					else
						redraw.right++;
				}
				dirty->Include(redraw);
			}
		}
		fTitleBarRect = tabRect;
		fTabsRegion = fTitleBarRect;
	}
}


/**
 * @brief Subclass focus hook: tracks button focus state and reflows the
 *        clicked tab's items if the window is a stack.
 *
 * Floating and left-titled windows that opt out of focus still receive
 * button focus highlighting, allowing the user to interact with the close
 * button without stealing focus from the active window.
 *
 * @param tab Tab whose focus state changed.
 */
void
TabDecorator::_SetFocus(Decorator::Tab* tab)
{
	Decorator::Tab* decoratorTab = static_cast<Decorator::Tab*>(tab);

	decoratorTab->buttonFocus = IsFocus(tab)
		|| ((decoratorTab->look == B_FLOATING_WINDOW_LOOK
			|| decoratorTab->look == kLeftTitledWindowLook)
			&& (decoratorTab->flags & B_AVOID_FOCUS) != 0);
	if (CountTabs() > 1)
		_LayoutTabItems(decoratorTab, decoratorTab->tabRect);
}


/**
 * @brief Implements interactive tab sliding for both single- and multi-tab
 *        windows.
 *
 * For single-tab windows the tab is moved horizontally within the available
 * range. For multi-tab windows the row layout is rerun on commit; while the
 * user is still dragging, a snapshot of the original tab position is kept so
 * the row can be redrawn cleanly when the drag ends.
 *
 * @param _tab         Tab being slid.
 * @param location     Requested new horizontal offset.
 * @param isShifting   true while the user is mid-drag, false on commit.
 * @param updateRegion Optional dirty region to be extended; may be NULL.
 * @return true if the tab location actually changed.
 */
bool
TabDecorator::_SetTabLocation(Decorator::Tab* _tab, float location,
	bool isShifting, BRegion* updateRegion)
{
	STRACE(("TabDecorator: Set Tab Location(%.1f)\n", location));

	if (CountTabs() > 1) {
		if (isShifting == false) {
			_DoTabLayout();
			if (updateRegion != NULL)
				updateRegion->Include(fTitleBarRect);

			fOldMovingTab = BRect(0, 0, -1, -1);
			return true;
		} else {
			if (fOldMovingTab.IsValid() == false)
				fOldMovingTab = _tab->tabRect;
		}
	}

	Decorator::Tab* tab = static_cast<Decorator::Tab*>(_tab);
	BRect& tabRect = tab->tabRect;
	if (tabRect.IsValid() == false)
		return false;

	if (location < 0)
		location = 0;

	float maxLocation
		= fRightBorder.right - fLeftBorder.left - tabRect.Width();
	if (CountTabs() > 1)
		maxLocation = fTitleBarRect.right - fLeftBorder.left - tabRect.Width();

	if (location > maxLocation)
		location = maxLocation;

	float delta = floor(location - tab->tabOffset);
	if (delta == 0.0)
		return false;

	// redraw old rect (1 pixel on the border must also be updated)
	BRect rect(tabRect);
	rect.bottom++;
	if (updateRegion != NULL)
		updateRegion->Include(rect);

	tabRect.OffsetBy(delta, 0);
	tab->tabOffset = (int32)location;
	_LayoutTabItems(_tab, tabRect);
	tab->tabLocation = maxLocation > 0.0 ? tab->tabOffset / maxLocation : 0.0;

	if (fTabList.CountItems() == 1)
		fTitleBarRect = tabRect;

	_CalculateTabsRegion();

	// redraw new rect as well
	rect = tabRect;
	rect.bottom++;
	if (updateRegion != NULL)
		updateRegion->Include(rect);

	return true;
}


/**
 * @brief Restores per-tab horizontal offsets from a flattened settings
 *        message.
 *
 * @param settings     Flattened settings (per-tab "tab location" floats).
 * @param updateRegion Optional dirty region; may be NULL.
 * @return true if any tab location changed; false on read failure or no-op.
 */
bool
TabDecorator::_SetSettings(const BMessage& settings, BRegion* updateRegion)
{
	float tabLocation;
	bool modified = false;
	for (int32 i = 0; i < fTabList.CountItems(); i++) {
		if (settings.FindFloat("tab location", i, &tabLocation) != B_OK)
			return false;
		modified |= SetTabLocation(i, tabLocation, updateRegion);
	}
	return modified;
}


/**
 * @brief Subclass hook for tab addition: refreshes the font and reruns
 *        layout, then marks the title bar dirty.
 *
 * @param settings     Current desktop settings.
 * @param index        Insertion index (currently unused beyond layout).
 * @param updateRegion Optional dirty region; may be NULL.
 * @return Always true.
 */
bool
TabDecorator::_AddTab(DesktopSettings& settings, int32 index,
	BRegion* updateRegion)
{
	_UpdateFont(settings);

	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);
	return true;
}


/**
 * @brief Subclass hook for tab removal: marks both the old contiguous tab
 *        block and the new title bar dirty so the row can shift left.
 *
 * @param index        Index of the tab being removed.
 * @param updateRegion Optional dirty region; may be NULL.
 * @return Always true.
 */
bool
TabDecorator::_RemoveTab(int32 index, BRegion* updateRegion)
{
	BRect oldRect = TabRect(index) | TabRect(CountTabs() - 1);
		// Get a rect of all the tabs to the right - they will all be moved

	_DoLayout();
	_DoOutlineLayout();

	if (updateRegion != NULL) {
		updateRegion->Include(oldRect);
		updateRegion->Include(fTitleBarRect);
	}
	return true;
}


/**
 * @brief Subclass hook for tab reordering: visually swaps the affected pair
 *        by adjusting their tab rectangles and recomputing the tabs region.
 *
 * @param from         Source index.
 * @param to           Destination index.
 * @param isMoving     true while the user is mid-drag.
 * @param updateRegion Optional dirty region; may be NULL.
 * @return true on success, false if @a to is out of range.
 */
bool
TabDecorator::_MoveTab(int32 from, int32 to, bool isMoving,
	BRegion* updateRegion)
{
	Decorator::Tab* toTab = _TabAt(to);
	if (toTab == NULL)
		return false;

	if (from < to) {
		fOldMovingTab.OffsetBy(toTab->tabRect.Width(), 0);
		toTab->tabRect.OffsetBy(-fOldMovingTab.Width(), 0);
	} else {
		fOldMovingTab.OffsetBy(-toTab->tabRect.Width(), 0);
		toTab->tabRect.OffsetBy(fOldMovingTab.Width(), 0);
	}

	toTab->tabOffset = uint32(toTab->tabRect.left - fLeftBorder.left);
	_LayoutTabItems(toTab, toTab->tabRect);

	_CalculateTabsRegion();

	if (updateRegion != NULL)
		updateRegion->Include(fTitleBarRect);
	return true;
}


/**
 * @brief Populates @a region with the screen-space footprint of the
 *        decorator borders, tabs, and (for document-look) the resize knob.
 *
 * Returns immediately for borderless looks. Bordered-only looks include
 * just the four borders; otherwise tabs and resize knobs are added.
 *
 * @param region Region to populate; ignored if NULL.
 */
void
TabDecorator::_GetFootprint(BRegion *region)
{
	STRACE(("TabDecorator: GetFootprint\n"));

	// This function calculates the decorator's footprint in coordinates
	// relative to the view. This is most often used to set a Window
	// object's visible region.

	if (region == NULL)
		return;

	if (fTopTab->look == B_NO_BORDER_WINDOW_LOOK)
		return;

	region->Include(fTopBorder);
	region->Include(fLeftBorder);
	region->Include(fRightBorder);
	region->Include(fBottomBorder);

	if (fTopTab->look == B_BORDERED_WINDOW_LOOK)
		return;

	region->Include(&fTabsRegion);

	if (fTopTab->look == B_DOCUMENT_WINDOW_LOOK) {
		// include the rectangular resize knob on the bottom right
		float knobSize = fResizeKnobSize - fBorderWidth;
		region->Include(BRect(fFrame.right - knobSize, fFrame.bottom - knobSize,
			fFrame.right, fFrame.bottom));
	}
}


/**
 * @brief Draws the close and zoom buttons of a tab when their rectangles
 *        intersect @a invalid and the corresponding flags allow them.
 *
 * @param tab     Tab whose buttons are being drawn.
 * @param invalid Update rectangle, in screen coordinates.
 */
void
TabDecorator::_DrawButtons(Decorator::Tab* tab, const BRect& invalid)
{
	STRACE(("TabDecorator: _DrawButtons\n"));

	// Draw the buttons if we're supposed to
	if (!(tab->flags & B_NOT_CLOSABLE) && invalid.Intersects(tab->closeRect))
		_DrawClose(tab, false, tab->closeRect);
	if (!(tab->flags & B_NOT_ZOOMABLE) && invalid.Intersects(tab->zoomRect))
		_DrawZoom(tab, false, tab->zoomRect);
}


/**
 * @brief Selects the title-bar font from the current desktop settings.
 *
 * Floating and left-titled looks use the plain font (rotated 90 degrees for
 * left-titled), while titled and modal looks use the bold font. Forced
 * antialiasing and string spacing are always enabled.
 *
 * @param settings Current desktop settings.
 */
void
TabDecorator::_UpdateFont(DesktopSettings& settings)
{
	ServerFont font;
	if (fTopTab->look == B_FLOATING_WINDOW_LOOK
		|| fTopTab->look == kLeftTitledWindowLook) {
		settings.GetDefaultPlainFont(font);
		if (fTopTab->look == kLeftTitledWindowLook)
			font.SetRotation(90.0f);
	} else
		settings.GetDefaultBoldFont(font);

	font.SetFlags(B_FORCE_ANTIALIASING);
	font.SetSpacing(B_STRING_SPACING);
	fDrawState.SetFont(font);
}


/**
 * @brief Derives the button offset, size, and inset from the current font
 *        size.
 *
 * The numerical ratios come from the historical app_server tuning and
 * differ between regular and small (floating, left-titled) tabs.
 *
 * @param tabRect Tab rectangle whose major dimension drives the button size.
 * @param _offset Out-parameter receiving the inset of the button from the
 *                tab edge.
 * @param _size   Out-parameter receiving the button side length.
 * @param _inset  Out-parameter receiving an extra inset added to centre the
 *                button artwork.
 */
void
TabDecorator::_GetButtonSizeAndOffset(const BRect& tabRect, float* _offset,
	float* _size, float* _inset) const
{
	float tabSize = fTopTab->look == kLeftTitledWindowLook ?
		tabRect.Width() : tabRect.Height();

	bool smallTab = fTopTab->look == B_FLOATING_WINDOW_LOOK
		|| fTopTab->look == kLeftTitledWindowLook;

	*_offset = smallTab ? floorf(fDrawState.Font().Size() / 2.6)
		: floorf(fDrawState.Font().Size() / 2.3);
	*_inset = smallTab ? floorf(fDrawState.Font().Size() / 5.0)
		: floorf(fDrawState.Font().Size() / 6.0);

	// "+ 2" so that the rects are centered within the solid area
	// (without the 2 pixels for the top border)
	*_size = tabSize - 2 * *_offset + *_inset;
}


/**
 * @brief Positions the close and zoom buttons inside @a tabRect and computes
 *        the title's truncated form to fit between them.
 *
 * For stacked tabs the zoom button is hidden on non-focused tabs, the
 * truncate mode falls back to end-truncation when tabs are narrow, and the
 * text offset is shrunk to give the title more room when needed.
 *
 * @param _tab    Tab whose items are being placed.
 * @param tabRect Rectangle the tab now occupies.
 */
void
TabDecorator::_LayoutTabItems(Decorator::Tab* _tab, const BRect& tabRect)
{
	Decorator::Tab* tab = static_cast<Decorator::Tab*>(_tab);

	float offset;
	float size;
	float inset;
	_GetButtonSizeAndOffset(tabRect, &offset, &size, &inset);

	// default textOffset
	tab->textOffset = _DefaultTextOffset();

	BRect& closeRect = tab->closeRect;
	BRect& zoomRect = tab->zoomRect;

	// calulate close rect based on the tab rectangle
	if (tab->look != kLeftTitledWindowLook) {
		closeRect.Set(tabRect.left + offset, tabRect.top + offset,
			tabRect.left + offset + size, tabRect.top + offset + size);

		zoomRect.Set(tabRect.right - offset - size, tabRect.top + offset,
			tabRect.right - offset, tabRect.top + offset + size);

		// hidden buttons have no width
		if ((tab->flags & B_NOT_CLOSABLE) != 0)
			closeRect.right = closeRect.left - offset;
		if ((tab->flags & B_NOT_ZOOMABLE) != 0)
			zoomRect.left = zoomRect.right + offset;
	} else {
		closeRect.Set(tabRect.left + offset, tabRect.top + offset,
			tabRect.left + offset + size, tabRect.top + offset + size);

		zoomRect.Set(tabRect.left + offset, tabRect.bottom - offset - size,
			tabRect.left + size + offset, tabRect.bottom - offset);

		// hidden buttons have no height
		if ((tab->flags & B_NOT_CLOSABLE) != 0)
			closeRect.bottom = closeRect.top - offset;
		if ((tab->flags & B_NOT_ZOOMABLE) != 0)
			zoomRect.top = zoomRect.bottom + offset;
	}

	// calculate room for title
	// TODO: the +2 is there because the title often appeared
	//	truncated for no apparent reason - OTOH the title does
	//	also not appear perfectly in the middle
	if (tab->look != kLeftTitledWindowLook)
		size = (zoomRect.left - closeRect.right) - tab->textOffset * 2 + inset;
	else
		size = (zoomRect.top - closeRect.bottom) - tab->textOffset * 2 + inset;

	bool stackMode = fTabList.CountItems() > 1;
	if (stackMode && IsFocus(tab) == false) {
		zoomRect.Set(0, 0, 0, 0);
		size = (tab->tabRect.right - closeRect.right) - tab->textOffset * 2
			+ inset;
	}
	uint8 truncateMode = B_TRUNCATE_MIDDLE;
	if (stackMode) {
		if (tab->tabRect.Width() < 100)
			truncateMode = B_TRUNCATE_END;
		float titleWidth = fDrawState.Font().StringWidth(Title(tab),
			BString(Title(tab)).Length());
		if (size < titleWidth) {
			float oldTextOffset = tab->textOffset;
			tab->textOffset -= (titleWidth - size) / 2;
			const float kMinTextOffset = 5.;
			if (tab->textOffset < kMinTextOffset)
				tab->textOffset = kMinTextOffset;
			size += oldTextOffset * 2;
			size -= tab->textOffset * 2;
		}
	}
	tab->truncatedTitle = Title(tab);
	fDrawState.Font().TruncateString(&tab->truncatedTitle, truncateMode, size);
	tab->truncatedTitleLength = tab->truncatedTitle.Length();
}


/**
 * @brief Returns the default horizontal text offset for the title text,
 *        scaled to the current border width.
 *
 * Floating and left-titled looks use a slightly tighter ratio.
 *
 * @return Offset in pixels.
 */
float
TabDecorator::_DefaultTextOffset() const
{
	if (fTopTab->look == B_FLOATING_WINDOW_LOOK
			|| fTopTab->look == kLeftTitledWindowLook)
		return int32(fBorderWidth * 3.4f);
	return int32(fBorderWidth * 3.6f);
}


/**
 * @brief Computes the tab size and starting offset for a window with exactly
 *        one tab, honouring its persisted relative tab location.
 *
 * @param tabSize Out-parameter receiving the available size for the tab
 *                (width for top tabs, height for left-titled tabs).
 * @return Tab offset in pixels along the relevant axis.
 */
float
TabDecorator::_SingleTabOffsetAndSize(float& tabSize)
{
	float maxLocation;
	if (fTopTab->look != kLeftTitledWindowLook) {
		tabSize = fRightBorder.right - fLeftBorder.left;
	} else {
		tabSize = fBottomBorder.bottom - fTopBorder.top;
	}
	Decorator::Tab* tab = _TabAt(0);
	maxLocation = tabSize - tab->maxTabSize;
	if (maxLocation < 0)
		maxLocation = 0;

	return floorf(tab->tabLocation * maxLocation);
}


/**
 * @brief Rebuilds fTabsRegion as the union of every tab rectangle.
 *
 * Used after a tab move or size redistribution so the title-bar dirty
 * region accurately reflects the currently occupied area.
 */
void
TabDecorator::_CalculateTabsRegion()
{
	fTabsRegion.MakeEmpty();
	for (int32 i = 0; i < fTabList.CountItems(); i++)
		fTabsRegion.Include(fTabList.ItemAt(i)->tabRect);
}
