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
 *       Tri-Edge AI
 *       Jacob Secunda, secundja@gmail.com
 */


/**
 * @file DefaultDecorator.cpp
 * @brief Default and fallback window decorator for the app_server (the
 *        familiar yellow tabs).
 *
 * Renders gradient tabs, beveled border frames, blended close/zoom buttons
 * with cached pre-rendered bitmaps, and the document-look resize knob. All
 * geometry comes from the TabDecorator base class; this subclass is purely
 * about painting.
 *
 * @see TabDecorator, Decorator
 */


#include "DefaultDecorator.h"

#include <algorithm>
#include <cmath>
#include <new>
#include <stdio.h>

#include <Autolock.h>
#include <Debug.h>
#include <GradientLinear.h>
#include <Rect.h>
#include <Region.h>
#include <View.h>

#include <WindowPrivate.h>

#include "BitmapDrawingEngine.h"
#include "DesktopSettings.h"
#include "DrawingEngine.h"
#include "DrawState.h"
#include "FontManager.h"
#include "PatternHandler.h"
#include "ServerBitmap.h"


//#define DEBUG_DECORATOR
#ifdef DEBUG_DECORATOR
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


/**
 * @brief Linearly interpolates a single color channel between @a a and @a b.
 *
 * @param a        Channel value at position 0.0.
 * @param b        Channel value at position 1.0.
 * @param position Interpolation parameter in [0.0, 1.0].
 * @return Clamped 8-bit channel value.
 */
static inline uint8
blend_color_value(uint8 a, uint8 b, float position)
{
	int16 delta = (int16)b - a;
	int32 value = a + (int32)(position * delta);
	if (value > 255)
		return 255;
	if (value < 0)
		return 0;

	return (uint8)value;
}


//	#pragma mark -


/**
 * @brief Constructs a DefaultDecorator on @a desktop with initial frame
 *        @a rect.
 *
 * @param settings Current desktop settings (forwarded to TabDecorator).
 * @param rect     Initial client-area frame.
 * @param desktop  Owning desktop.
 *
 * @todo Auto-resize the decorator if it is constructed with a too-small
 *       frame.
 */
// TODO: get rid of DesktopSettings here, and introduce private accessor
//	methods to the Decorator base class
DefaultDecorator::DefaultDecorator(DesktopSettings& settings, BRect rect,
	Desktop* desktop)
	:
	TabDecorator(settings, rect, desktop)
{
	// TODO: If the decorator was created with a frame too small, it should
	// resize itself!

	STRACE(("DefaultDecorator:\n"));
	STRACE(("\tFrame (%.1f,%.1f,%.1f,%.1f)\n",
		rect.left, rect.top, rect.right, rect.bottom));
}


/**
 * @brief Destroys the DefaultDecorator.
 */
DefaultDecorator::~DefaultDecorator()
{
	STRACE(("DefaultDecorator: ~DefaultDecorator()\n"));
}


// #pragma mark - Public methods


/**
 * @brief Fills @a _colors with the painting palette for a decorator
 *        component.
 *
 * The contents of the array depend on the component: for tab and button
 * components the slots map to the COLOR_TAB_* / COLOR_BUTTON_* indices
 * defined on TabDecorator; for borders and the resize corner six progressive
 * shades of the frame color are produced. The HIGHLIGHT_RESIZE_BORDER
 * highlight tints every output blue.
 *
 * @param component Decorator component being painted.
 * @param highlight Highlight kind for the component.
 * @param _colors   Output array; the function fills it in place.
 * @param _tab      Tab whose focus state drives the focused/non-focused
 *                  palette selection; may be NULL for non-tab components.
 */
void
DefaultDecorator::GetComponentColors(Component component, uint8 highlight,
	ComponentColors _colors, Decorator::Tab* _tab)
{
	Decorator::Tab* tab = static_cast<Decorator::Tab*>(_tab);
	switch (component) {
		case COMPONENT_TAB:
			if (tab && tab->buttonFocus) {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fFocusFrameColor, B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fFocusFrameColor, B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fFocusTextColor;
			} else {
				_colors[COLOR_TAB_FRAME_LIGHT]
					= tint_color(fNonFocusFrameColor, B_DARKEN_2_TINT);
				_colors[COLOR_TAB_FRAME_DARK]
					= tint_color(fNonFocusFrameColor, B_DARKEN_3_TINT);
				_colors[COLOR_TAB] = fNonFocusTabColor;
				_colors[COLOR_TAB_LIGHT] = fNonFocusTabColorLight;
				_colors[COLOR_TAB_BEVEL] = fNonFocusTabColorBevel;
				_colors[COLOR_TAB_SHADOW] = fNonFocusTabColorShadow;
				_colors[COLOR_TAB_TEXT] = fNonFocusTextColor;
			}
			break;

		case COMPONENT_CLOSE_BUTTON:
		case COMPONENT_ZOOM_BUTTON:
			if (tab && tab->buttonFocus) {
				_colors[COLOR_BUTTON] = fFocusTabColor;
				_colors[COLOR_BUTTON_LIGHT] = fFocusTabColorLight;
			} else {
				_colors[COLOR_BUTTON] = fNonFocusTabColor;
				_colors[COLOR_BUTTON_LIGHT] = fNonFocusTabColorLight;
			}
			break;

		case COMPONENT_LEFT_BORDER:
		case COMPONENT_RIGHT_BORDER:
		case COMPONENT_TOP_BORDER:
		case COMPONENT_BOTTOM_BORDER:
		case COMPONENT_RESIZE_CORNER:
		default:
			if (tab && tab->buttonFocus) {
				_colors[0] = tint_color(fFocusFrameColor, B_DARKEN_2_TINT);
				_colors[1] = tint_color(fFocusFrameColor, B_LIGHTEN_2_TINT);
				_colors[2] = fFocusFrameColor;
				_colors[3] = tint_color(fFocusFrameColor,
					(B_DARKEN_1_TINT + B_NO_TINT) / 2);
				_colors[4] = tint_color(fFocusFrameColor, B_DARKEN_2_TINT);
				_colors[5] = tint_color(fFocusFrameColor, B_DARKEN_3_TINT);
			} else {
				_colors[0] = tint_color(fNonFocusFrameColor, B_DARKEN_2_TINT);
				_colors[1] = tint_color(fNonFocusFrameColor, B_LIGHTEN_2_TINT);
				_colors[2] = fNonFocusFrameColor;
				_colors[3] = tint_color(fNonFocusFrameColor,
					(B_DARKEN_1_TINT + B_NO_TINT) / 2);
				_colors[4] = tint_color(fNonFocusFrameColor, B_DARKEN_2_TINT);
				_colors[5] = tint_color(fNonFocusFrameColor, B_DARKEN_3_TINT);
			}

			// for the resize-border highlight dye everything bluish.
			if (highlight == HIGHLIGHT_RESIZE_BORDER) {
				for (int32 i = 0; i < 6; i++) {
					_colors[i].red = std::max((int)_colors[i].red - 80, 0);
					_colors[i].green = std::max((int)_colors[i].green - 80, 0);
					_colors[i].blue = 255;
				}
			}
			break;
	}
}


/**
 * @brief Forwards color refresh to the TabDecorator base implementation.
 *
 * @param settings Current desktop settings.
 */
void
DefaultDecorator::UpdateColors(DesktopSettings& settings)
{
	TabDecorator::UpdateColors(settings);
}


// #pragma mark - Protected methods


/**
 * @brief Renders the four border strips of the window frame and any
 *        document-look resize knob within @a rect.
 *
 * Different looks use different border depths and shading: titled, document
 * and modal looks paint five-step gradients; floating and left-titled looks
 * use a tighter three-step palette; bordered windows draw a single line.
 *
 * @param rect Update rectangle, in screen coordinates. Each border is only
 *             repainted if it intersects @a rect.
 *
 * @note The DrawingEngine must remain locked across this entire call so
 *       that the border clipping stays valid.
 */
void
DefaultDecorator::_DrawFrame(BRect rect)
{
	STRACE(("_DrawFrame(%f,%f,%f,%f)\n", rect.left, rect.top,
		rect.right, rect.bottom));

	// NOTE: the DrawingEngine needs to be locked for the entire
	// time for the clipping to stay valid for this decorator

	if (fTopTab->look == B_NO_BORDER_WINDOW_LOOK)
		return;

	if (fBorderWidth <= 0)
		return;

	// TODO: While this works, it does not look so crisp at higher resolutions.
#define COLORS_INDEX(i, borderWidth, nominalLimit) int32((float(i) / float(borderWidth)) * nominalLimit)

	// Draw the border frame
	BRect border = BRect(fTopBorder.LeftTop(), fBottomBorder.RightBottom());
	switch ((int)fTopTab->look) {
		case B_TITLED_WINDOW_LOOK:
		case B_DOCUMENT_WINDOW_LOOK:
		case B_MODAL_WINDOW_LOOK:
		{
			// top
			if (rect.Intersects(fTopBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_TOP_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 5);
					fDrawingEngine->StrokeLine(
						BPoint(border.left + i, border.top + i),
						BPoint(border.right - i, border.top + i),
						colors[colorsIndex]);
				}
				if (fTitleBarRect.IsValid()) {
					// grey along the bottom of the tab
					// (overwrites "white" from frame)
					const int overdraw = (int)ceilf(fBorderWidth / 5.0f);
					for (int i = 1; i <= overdraw; i++) {
						fDrawingEngine->StrokeLine(
							BPoint(fTitleBarRect.left + 2, fTitleBarRect.bottom + i),
							BPoint(fTitleBarRect.right - 2, fTitleBarRect.bottom + i),
							colors[2]);
					}
				}
			}
			// left
			if (rect.Intersects(fLeftBorder.InsetByCopy(0, -fBorderWidth))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 5);
					fDrawingEngine->StrokeLine(
						BPoint(border.left + i, border.top + i),
						BPoint(border.left + i, border.bottom - i),
						colors[colorsIndex]);
				}
			}
			// bottom
			if (rect.Intersects(fBottomBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_BOTTOM_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 5);
					fDrawingEngine->StrokeLine(
						BPoint(border.left + i, border.bottom - i),
						BPoint(border.right - i, border.bottom - i),
						colors[(4 - colorsIndex) == 4 ? 5 : (4 - colorsIndex)]);
				}
			}
			// right
			if (rect.Intersects(fRightBorder.InsetByCopy(0, -fBorderWidth))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_RIGHT_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 5);
					fDrawingEngine->StrokeLine(
						BPoint(border.right - i, border.top + i),
						BPoint(border.right - i, border.bottom - i),
						colors[(4 - colorsIndex) == 4 ? 5 : (4 - colorsIndex)]);
				}
			}
			break;
		}

		case B_FLOATING_WINDOW_LOOK:
		case kLeftTitledWindowLook:
		{
			// top
			if (rect.Intersects(fTopBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_TOP_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 3);
					fDrawingEngine->StrokeLine(
						BPoint(border.left + i, border.top + i),
						BPoint(border.right - i, border.top + i),
						colors[colorsIndex * 2]);
				}
				if (fTitleBarRect.IsValid() && fTopTab->look != kLeftTitledWindowLook) {
					// grey along the bottom of the tab
					// (overwrites "white" from frame)
					const int overdraw = (int)ceilf(fBorderWidth / 5.0f);
					for (int i = 1; i <= overdraw; i++) {
						fDrawingEngine->StrokeLine(
							BPoint(fTitleBarRect.left + 2, fTitleBarRect.bottom + i),
							BPoint(fTitleBarRect.right - 2, fTitleBarRect.bottom + i),
							colors[2]);
					}
				}
			}
			// left
			if (rect.Intersects(fLeftBorder.InsetByCopy(0, -fBorderWidth))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 3);
					fDrawingEngine->StrokeLine(
						BPoint(border.left + i, border.top + i),
						BPoint(border.left + i, border.bottom - i),
						colors[colorsIndex * 2]);
				}
				if (fTopTab->look == kLeftTitledWindowLook
					&& fTitleBarRect.IsValid()) {
					// grey along the right side of the tab
					// (overwrites "white" from frame)
					fDrawingEngine->StrokeLine(
						BPoint(fTitleBarRect.right + 1,
							fTitleBarRect.top + 2),
						BPoint(fTitleBarRect.right + 1,
							fTitleBarRect.bottom - 2), colors[2]);
				}
			}
			// bottom
			if (rect.Intersects(fBottomBorder)) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_BOTTOM_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 3);
					fDrawingEngine->StrokeLine(
						BPoint(border.left + i, border.bottom - i),
						BPoint(border.right - i, border.bottom - i),
						colors[(2 - colorsIndex) == 2 ? 5 : (2 - colorsIndex) * 2]);
				}
			}
			// right
			if (rect.Intersects(fRightBorder.InsetByCopy(0, -fBorderWidth))) {
				ComponentColors colors;
				_GetComponentColors(COMPONENT_RIGHT_BORDER, colors, fTopTab);

				for (int8 i = 0; i < fBorderWidth; i++) {
					const int8 colorsIndex = COLORS_INDEX(i, fBorderWidth, 3);
					fDrawingEngine->StrokeLine(
						BPoint(border.right - i, border.top + i),
						BPoint(border.right - i, border.bottom - i),
						colors[(2 - colorsIndex) == 2 ? 5 : (2 - colorsIndex) * 2]);
				}
			}
			break;
		}

		case B_BORDERED_WINDOW_LOOK:
		{
			// TODO: Draw the borders individually!
			ComponentColors colors;
			_GetComponentColors(COMPONENT_LEFT_BORDER, colors, fTopTab);

			fDrawingEngine->StrokeRect(border, colors[5]);
			break;
		}

		default:
			// don't draw a border frame
			break;
	}

	// Draw the resize knob if we're supposed to
	if (!(fTopTab->flags & B_NOT_RESIZABLE)) {
		ComponentColors colors;
		_GetComponentColors(COMPONENT_RESIZE_CORNER, colors, fTopTab);

		switch ((int)fTopTab->look) {
			case B_DOCUMENT_WINDOW_LOOK:
			{
				if (fOutlinesDelta.x != 0 || fOutlinesDelta.y != 0) {
					border.Set(fFrame.right - 13, fFrame.bottom - 13,
						fFrame.right + 3, fFrame.bottom + 3);

					if (rect.Intersects(border))
						_DrawResizeKnob(border, false, colors);
				}

				if (rect.Intersects(fResizeRect)) {
					_DrawResizeKnob(fResizeRect, fTopTab && IsFocus(fTopTab),
						colors);
				}

				break;
			}

			case B_TITLED_WINDOW_LOOK:
			case B_FLOATING_WINDOW_LOOK:
			case B_MODAL_WINDOW_LOOK:
			case kLeftTitledWindowLook:
			{
				if (!rect.Intersects(BRect(
						fRightBorder.right - fBorderResizeLength,
						fBottomBorder.bottom - fBorderResizeLength,
						fRightBorder.right - 1,
						fBottomBorder.bottom - 1)))
					break;

				fDrawingEngine->StrokeLine(
					BPoint(fRightBorder.left,
						fBottomBorder.bottom - fBorderResizeLength),
					BPoint(fRightBorder.right - 1,
						fBottomBorder.bottom - fBorderResizeLength),
					colors[0]);
				fDrawingEngine->StrokeLine(
					BPoint(fRightBorder.right - fBorderResizeLength,
						fBottomBorder.top),
					BPoint(fRightBorder.right - fBorderResizeLength,
						fBottomBorder.bottom - 1),
					colors[0]);
				break;
			}

			default:
				// don't draw resize corner
				break;
		}
	}
}


/**
 * @brief Paints the document-look resize knob in @a rect.
 *
 * Always paints the gradient body and L-shaped highlight; when @a full is
 * true, the dotted texture filling the knob is also drawn (for the
 * focused window). Non-focused windows paint only the outline so the knob
 * looks subdued.
 *
 * @param rect   Resize knob rectangle, in screen coordinates.
 * @param full   true to paint the dotted interior, false for outline only.
 * @param colors Six-element color array supplied by _GetComponentColors().
 */
void
DefaultDecorator::_DrawResizeKnob(BRect rect, bool full,
	const ComponentColors& colors)
{
	float x = rect.right -= 3;
	float y = rect.bottom -= 3;

	BGradientLinear gradient;
	gradient.SetStart(rect.LeftTop());
	gradient.SetEnd(rect.RightBottom());
	gradient.AddColor(colors[1], 0);
	gradient.AddColor(colors[2], 255);

	fDrawingEngine->FillRect(rect, gradient);

	BPoint offset1(rect.Width(), rect.Height()),
		offset2(rect.Width() - 1, rect.Height() - 1);
	fDrawingEngine->StrokeLine(BPoint(x, y) - offset1,
		BPoint(x - offset1.x, y - 2), colors[0]);
	fDrawingEngine->StrokeLine(BPoint(x, y) - offset2,
		BPoint(x - offset2.x, y - 1), colors[1]);
	fDrawingEngine->StrokeLine(BPoint(x, y) - offset1,
		BPoint(x - 2, y - offset1.y), colors[0]);
	fDrawingEngine->StrokeLine(BPoint(x, y) - offset2,
		BPoint(x - 1, y - offset2.y), colors[1]);

	if (!full)
		return;

	static const rgb_color kWhite
		= (rgb_color){ 255, 255, 255, 255 };
	for (int8 i = 1; i <= 4; i++) {
		for (int8 j = 1; j <= i; j++) {
			BPoint pt1(x - (3 * j) + 1, y - (3 * (5 - i)) + 1);
			BPoint pt2(x - (3 * j) + 2, y - (3 * (5 - i)) + 2);
			fDrawingEngine->StrokePoint(pt1, colors[0]);
			fDrawingEngine->StrokePoint(pt2, kWhite);
		}
	}
}


/**
 * @brief Paints the tab body, including outer frame, bevel highlights, and
 *        gradient fill, then draws the title and buttons on top.
 *
 * Only repaints if @a invalid intersects the tab rectangle. The non-focused
 * tab is drawn one pixel shorter so the focused tab visually overlaps it.
 *
 * @param tab     Tab to paint.
 * @param invalid Update rectangle, in screen coordinates.
 */
void
DefaultDecorator::_DrawTab(Decorator::Tab* tab, BRect invalid)
{
	STRACE(("_DrawTab(%.1f,%.1f,%.1f,%.1f)\n",
		invalid.left, invalid.top, invalid.right, invalid.bottom));
	const BRect& tabRect = tab->tabRect;
	// If a window has a tab, this will draw it and any buttons which are
	// in it.
	if (!tabRect.IsValid() || !invalid.Intersects(tabRect))
		return;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_TAB, colors, tab);

	// outer frame
	fDrawingEngine->StrokeLine(tabRect.LeftTop(), tabRect.LeftBottom(),
		colors[COLOR_TAB_FRAME_LIGHT]);
	fDrawingEngine->StrokeLine(tabRect.LeftTop(), tabRect.RightTop(),
		colors[COLOR_TAB_FRAME_LIGHT]);
	if (tab->look != kLeftTitledWindowLook) {
		fDrawingEngine->StrokeLine(tabRect.RightTop(), tabRect.RightBottom(),
			colors[COLOR_TAB_FRAME_DARK]);
	} else {
		fDrawingEngine->StrokeLine(tabRect.LeftBottom(),
			tabRect.RightBottom(), colors[COLOR_TAB_FRAME_DARK]);
	}

	float tabBotton = tabRect.bottom;
	if (fTopTab != tab)
		tabBotton -= 1;

	// bevel
	fDrawingEngine->StrokeLine(BPoint(tabRect.left + 1, tabRect.top + 1),
		BPoint(tabRect.left + 1,
			tabBotton - (tab->look == kLeftTitledWindowLook ? 1 : 0)),
		colors[COLOR_TAB_BEVEL]);
	fDrawingEngine->StrokeLine(BPoint(tabRect.left + 1, tabRect.top + 1),
		BPoint(tabRect.right - (tab->look == kLeftTitledWindowLook ? 0 : 1),
			tabRect.top + 1),
		colors[COLOR_TAB_BEVEL]);

	if (tab->look != kLeftTitledWindowLook) {
		fDrawingEngine->StrokeLine(BPoint(tabRect.right - 1, tabRect.top + 2),
			BPoint(tabRect.right - 1, tabBotton),
			colors[COLOR_TAB_SHADOW]);
	} else {
		fDrawingEngine->StrokeLine(
			BPoint(tabRect.left + 2, tabRect.bottom - 1),
			BPoint(tabRect.right, tabRect.bottom - 1),
			colors[COLOR_TAB_SHADOW]);
	}

	// fill
	BGradientLinear gradient;
	gradient.SetStart(tabRect.LeftTop());
	gradient.AddColor(colors[COLOR_TAB_LIGHT], 0);
	gradient.AddColor(colors[COLOR_TAB], 255);

	if (tab->look != kLeftTitledWindowLook) {
		gradient.SetEnd(tabRect.LeftBottom());
		fDrawingEngine->FillRect(BRect(tabRect.left + 2, tabRect.top + 2,
			tabRect.right - 2, tabBotton), gradient);
	} else {
		gradient.SetEnd(tabRect.RightTop());
		fDrawingEngine->FillRect(BRect(tabRect.left + 2, tabRect.top + 2,
			tabRect.right, tabRect.bottom - 2), gradient);
	}

	_DrawTitle(tab, tabRect);

	_DrawButtons(tab, invalid);
}


/**
 * @brief Renders the truncated title text into the tab.
 *
 * The text is drawn in B_OP_OVER so the gradient fill bleeds through, then
 * the drawing mode is restored to B_OP_COPY for performance on subsequent
 * paints.
 *
 * @param _tab Tab whose title is being drawn.
 * @param rect Tab area to update, in screen coordinates.
 */
void
DefaultDecorator::_DrawTitle(Decorator::Tab* _tab, BRect rect)
{
	STRACE(("_DrawTitle(%f,%f,%f,%f)\n", rect.left, rect.top, rect.right,
		rect.bottom));

	Decorator::Tab* tab = static_cast<Decorator::Tab*>(_tab);

	const BRect& tabRect = tab->tabRect;
	const BRect& closeRect = tab->closeRect;
	const BRect& zoomRect = tab->zoomRect;

	ComponentColors colors;
	_GetComponentColors(COMPONENT_TAB, colors, tab);

	fDrawingEngine->SetDrawingMode(B_OP_OVER);
	fDrawingEngine->SetHighColor(colors[COLOR_TAB_TEXT]);
	fDrawingEngine->SetFont(fDrawState.Font());

	// figure out position of text
	font_height fontHeight;
	fDrawState.Font().GetHeight(fontHeight);

	BPoint titlePos;
	if (tab->look != kLeftTitledWindowLook) {
		titlePos.x = closeRect.IsValid() ? closeRect.right + tab->textOffset
			: tabRect.left + tab->textOffset;
		titlePos.y = floorf(((tabRect.top + 2.0) + tabRect.bottom
			+ fontHeight.ascent + fontHeight.descent) / 2.0
			- fontHeight.descent + 0.5);
	} else {
		titlePos.x = floorf(((tabRect.left + 2.0) + tabRect.right
			+ fontHeight.ascent + fontHeight.descent) / 2.0
			- fontHeight.descent + 0.5);
		titlePos.y = zoomRect.IsValid() ? zoomRect.top - tab->textOffset
			: tabRect.bottom - tab->textOffset;
	}

	fDrawingEngine->DrawString(tab->truncatedTitle, tab->truncatedTitleLength,
		titlePos);

	fDrawingEngine->SetDrawingMode(B_OP_COPY);
}


/**
 * @brief Paints the close button by blitting a cached pre-rendered bitmap.
 *
 * The cache key combines focus and pressed state so each transition
 * (focused-up, focused-down, unfocused-up, unfocused-down) hits the right
 * artwork.
 *
 * @param _tab   Tab whose close button is being drawn.
 * @param direct true to bypass double buffering (used for instant button
 *               feedback during a click).
 * @param rect   Button rectangle, in screen coordinates.
 */
void
DefaultDecorator::_DrawClose(Decorator::Tab* _tab, bool direct, BRect rect)
{
	STRACE(("_DrawClose(%f,%f,%f,%f)\n", rect.left, rect.top, rect.right,
		rect.bottom));

	Decorator::Tab* tab = static_cast<Decorator::Tab*>(_tab);

	int32 index = (tab->buttonFocus ? 0 : 1) + (tab->closePressed ? 0 : 2);
	ServerBitmap* bitmap = tab->closeBitmaps[index];
	if (bitmap == NULL) {
		bitmap = _GetBitmapForButton(tab, COMPONENT_CLOSE_BUTTON,
			tab->closePressed, rect.IntegerWidth(), rect.IntegerHeight());
		tab->closeBitmaps[index] = bitmap;
	}

	_DrawButtonBitmap(bitmap, direct, rect);
}


/**
 * @brief Paints the zoom button by blitting a cached pre-rendered bitmap.
 *
 * Skipped when the button rectangle is degenerate (less than one pixel
 * wide), which happens when the zoom button has been hidden.
 *
 * @param _tab   Tab whose zoom button is being drawn.
 * @param direct true to bypass double buffering for instant feedback.
 * @param rect   Button rectangle, in screen coordinates.
 */
void
DefaultDecorator::_DrawZoom(Decorator::Tab* _tab, bool direct, BRect rect)
{
	STRACE(("_DrawZoom(%f,%f,%f,%f)\n", rect.left, rect.top, rect.right,
		rect.bottom));

	if (rect.IntegerWidth() < 1)
		return;

	Decorator::Tab* tab = static_cast<Decorator::Tab*>(_tab);
	int32 index = (tab->buttonFocus ? 0 : 1) + (tab->zoomPressed ? 0 : 2);
	ServerBitmap* bitmap = tab->zoomBitmaps[index];
	if (bitmap == NULL) {
		bitmap = _GetBitmapForButton(tab, COMPONENT_ZOOM_BUTTON,
			tab->zoomPressed, rect.IntegerWidth(), rect.IntegerHeight());
		tab->zoomBitmaps[index] = bitmap;
	}

	_DrawButtonBitmap(bitmap, direct, rect);
}


/**
 * @brief No-op: the default decorator does not paint a minimize button.
 *
 * @param tab    Tab (unused).
 * @param direct Draw mode (unused).
 * @param rect   Button rectangle (unused).
 */
void
DefaultDecorator::_DrawMinimize(Decorator::Tab* tab, bool direct, BRect rect)
{
	// This decorator doesn't have this button
}


// #pragma mark - Private methods


/**
 * @brief Blits a cached button bitmap to @a rect, optionally bypassing the
 *        back buffer.
 *
 * Saves and restores the engine's drawing mode and copy-to-front setting so
 * the call has no side effects on subsequent paints.
 *
 * @param bitmap Pre-rendered button artwork; no-op when NULL.
 * @param direct true to draw straight to the front buffer.
 * @param rect   Destination rectangle.
 */
void
DefaultDecorator::_DrawButtonBitmap(ServerBitmap* bitmap, bool direct,
	BRect rect)
{
	if (bitmap == NULL)
		return;

	bool copyToFrontEnabled = fDrawingEngine->CopyToFrontEnabled();
	fDrawingEngine->SetCopyToFrontEnabled(direct);
	drawing_mode oldMode;
	fDrawingEngine->SetDrawingMode(B_OP_OVER, oldMode);
	fDrawingEngine->DrawBitmap(bitmap, rect.OffsetToCopy(0, 0), rect);
	fDrawingEngine->SetDrawingMode(oldMode);
	fDrawingEngine->SetCopyToFrontEnabled(copyToFrontEnabled);
}


/**
 * @brief Draws a small bevel-shaded rectangle, used as the artwork inside
 *        the close and zoom buttons.
 *
 * The pressed state inverts the gradient so the button visually recesses.
 *
 * @param engine Drawing engine to render through.
 * @param rect   Button rectangle.
 * @param down   true to draw the rectangle recessed.
 * @param colors Six-element button palette from _GetComponentColors().
 */
void
DefaultDecorator::_DrawBlendedRect(DrawingEngine* engine, const BRect rect,
	bool down, const ComponentColors& colors)
{
	// figure out which colors to use
	rgb_color startColor, endColor;
	if (down) {
		startColor = tint_color(colors[COLOR_BUTTON], B_DARKEN_1_TINT);
		endColor = colors[COLOR_BUTTON_LIGHT];
	} else {
		startColor = tint_color(colors[COLOR_BUTTON], B_LIGHTEN_MAX_TINT);
		endColor = colors[COLOR_BUTTON];
	}

	// fill
	BRect fillRect(rect.InsetByCopy(1.0f, 1.0f));

	BGradientLinear gradient;
	gradient.SetStart(fillRect.LeftTop());
	gradient.SetEnd(fillRect.RightBottom());
	gradient.AddColor(startColor, 0);
	gradient.AddColor(endColor, 255);

	engine->FillRect(fillRect, gradient);

	// outline
	engine->StrokeRect(rect, tint_color(colors[COLOR_BUTTON], B_DARKEN_2_TINT));
}


/**
 * @brief Returns a cached pre-rendered button bitmap, generating it on first
 *        use.
 *
 * The cache is keyed on component, pressed state, dimensions, and the two
 * relevant button colors so palette or DPI changes regenerate the artwork
 * naturally. The cache is process-wide and shared across all decorator
 * instances.
 *
 * @param tab    Tab whose button is being rendered (drives palette).
 * @param item   COMPONENT_CLOSE_BUTTON or COMPONENT_ZOOM_BUTTON.
 * @param down   true if the button should be drawn pressed.
 * @param width  Bitmap width in pixels.
 * @param height Bitmap height in pixels.
 * @return Cached or newly-generated bitmap, or NULL on allocation failure.
 *
 * @todo Replace the linked list with a hash map and free entries when the
 *       app_server shuts down.
 */
ServerBitmap*
DefaultDecorator::_GetBitmapForButton(Decorator::Tab* tab, Component item,
	bool down, int32 width, int32 height)
{
	// TODO: the list of shared bitmaps is never freed
	/** @brief Linked-list entry caching a single rendered button bitmap
	           keyed by component, pressed state, size, and colors. */
	struct decorator_bitmap {
		Component			item;
		bool				down;
		int32				width;
		int32				height;
		rgb_color			baseColor;
		rgb_color			lightColor;
		UtilityBitmap*		bitmap;
		decorator_bitmap*	next;
	};

	static BLocker sBitmapListLock("decorator lock", true);
	static decorator_bitmap* sBitmapList = NULL;

	ComponentColors colors;
	_GetComponentColors(item, colors, tab);

	BAutolock locker(sBitmapListLock);

	// search our list for a matching bitmap
	// TODO: use a hash map instead?
	decorator_bitmap* current = sBitmapList;
	while (current) {
		if (current->item == item && current->down == down
			&& current->width == width && current->height == height
			&& current->baseColor == colors[COLOR_BUTTON]
			&& current->lightColor == colors[COLOR_BUTTON_LIGHT]) {
			return current->bitmap;
		}

		current = current->next;
	}

	static BitmapDrawingEngine* sBitmapDrawingEngine = NULL;

	// didn't find any bitmap, create a new one
	if (sBitmapDrawingEngine == NULL)
		sBitmapDrawingEngine = new(std::nothrow) BitmapDrawingEngine();
	if (sBitmapDrawingEngine == NULL
		|| sBitmapDrawingEngine->SetSize(width, height) != B_OK)
		return NULL;

	BRect rect(0, 0, width - 1, height - 1);

	STRACE(("DefaultDecorator creating bitmap for %s %s at size %ldx%ld\n",
		item == COMPONENT_CLOSE_BUTTON ? "close" : "zoom",
		down ? "down" : "up", width, height));
	switch (item) {
		case COMPONENT_CLOSE_BUTTON:
			_DrawBlendedRect(sBitmapDrawingEngine, rect, down, colors);
			break;

		case COMPONENT_ZOOM_BUTTON:
		{
			sBitmapDrawingEngine->FillRect(rect, B_TRANSPARENT_COLOR);
				// init the background

			float inset = floorf(width / 4.0);
			BRect zoomRect(rect);
			zoomRect.left += inset;
			zoomRect.top += inset;
			_DrawBlendedRect(sBitmapDrawingEngine, zoomRect, down, colors);

			inset = floorf(width / 2.1);
			zoomRect = rect;
			zoomRect.right -= inset;
			zoomRect.bottom -= inset;
			_DrawBlendedRect(sBitmapDrawingEngine, zoomRect, down, colors);
			break;
		}

		default:
			break;
	}

	UtilityBitmap* bitmap = sBitmapDrawingEngine->ExportToBitmap(width, height,
		B_RGB32);
	if (bitmap == NULL)
		return NULL;

	// bitmap ready, put it into the list
	decorator_bitmap* entry = new(std::nothrow) decorator_bitmap;
	if (entry == NULL) {
		delete bitmap;
		return NULL;
	}

	entry->item = item;
	entry->down = down;
	entry->width = width;
	entry->height = height;
	entry->bitmap = bitmap;
	entry->baseColor = colors[COLOR_BUTTON];
	entry->lightColor = colors[COLOR_BUTTON_LIGHT];
	entry->next = sBitmapList;
	sBitmapList = entry;

	return bitmap;
}


/**
 * @brief Convenience wrapper that maps a Component to its enclosing Region
 *        and forwards to GetComponentColors() with the current highlight.
 *
 * @param component Component to fetch colors for.
 * @param _colors   Output palette array.
 * @param tab       Tab driving the focused/non-focused selection.
 */
void
DefaultDecorator::_GetComponentColors(Component component,
	ComponentColors _colors, Decorator::Tab* tab)
{
	// get the highlight for our component
	Region region = REGION_NONE;
	switch (component) {
		case COMPONENT_TAB:
			region = REGION_TAB;
			break;
		case COMPONENT_CLOSE_BUTTON:
			region = REGION_CLOSE_BUTTON;
			break;
		case COMPONENT_ZOOM_BUTTON:
			region = REGION_ZOOM_BUTTON;
			break;
		case COMPONENT_LEFT_BORDER:
			region = REGION_LEFT_BORDER;
			break;
		case COMPONENT_RIGHT_BORDER:
			region = REGION_RIGHT_BORDER;
			break;
		case COMPONENT_TOP_BORDER:
			region = REGION_TOP_BORDER;
			break;
		case COMPONENT_BOTTOM_BORDER:
			region = REGION_BOTTOM_BORDER;
			break;
		case COMPONENT_RESIZE_CORNER:
			region = REGION_RIGHT_BOTTOM_CORNER;
			break;
	}

	return GetComponentColors(component, RegionHighlight(region), _colors, tab);
}
