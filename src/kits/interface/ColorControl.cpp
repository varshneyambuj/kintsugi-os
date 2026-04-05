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
 *   Copyright 2001-2025 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Alexandre Deckner, alex@zappotek.com
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jérôme Duval
 *       Marc Flerackers, mflerackers@androme.be
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file ColorControl.cpp
 * @brief Implementation of BColorControl, a color picker control
 *
 * BColorControl displays a color selection palette in one of several cell-based
 * or ramp-based layouts. It notifies its target when the selected color changes,
 * and supports both 8-bit (256-color) and 32-bit (true-color) modes.
 *
 * @see BControl, BColorConversion
 */


#include <ColorControl.h>

#include <algorithm>

#include <stdio.h>
#include <stdlib.h>

#include <ControlLook.h>
#include <Bitmap.h>
#include <TextControl.h>
#include <Region.h>
#include <Screen.h>
#include <SystemCatalog.h>
#include <Window.h>

using BPrivate::gSystemCatalog;

#include <binary_compatibility/Interface.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ColorControl"

/** @brief Internal message code sent when the user edits an RGB text field. */
static const uint32 kMsgColorEntered = 'ccol';

/** @brief Minimum allowed cell size in pixels for palette-mode display. */
static const float kMinCellSize = 6.0f;

/** @brief Stroke width (in pixels) used to draw the selector ring outline. */
static const float kSelectorPenSize = 2.0f;

/** @brief Radius (in pixels) of the color selector ellipse drawn on each ramp. */
static const float kSelectorSize = 4.0f;

/** @brief Horizontal padding (in pixels) between the selector edge and the ramp border. */
static const float kSelectorHSpacing = 2.0f;

/** @brief Horizontal gap (in pixels) between the palette area and the RGB text fields. */
static const float kTextFieldsHSpacing = 6.0f;

/** @brief Reference font size used to scale cell size proportionally to the current font. */
static const float kDefaultFontSize = 12.0f;

/** @brief Inset (in pixels) between the outer view frame and the palette bevel border. */
static const float kBevelSpacing = 2.0f;

/** @brief Number of color ramps rendered in true-color mode (white, red, green, blue). */
static const uint32 kRampCount = 4;


/**
 * @brief Constructs a BColorControl with an explicit frame rectangle.
 *
 * Creates all three RGB text controls, sets the initial color to black, and
 * calls ResizeToPreferred() so the view fits the requested cell size and layout.
 *
 * @param leftTop        Top-left corner of the control in its parent's coordinate system.
 * @param layout         Initial palette cell arrangement (e.g. B_CELLS_32x8).
 * @param cellSize       Logical cell size in points; scaled by the current font size.
 * @param name           Name used to identify this view in the view hierarchy.
 * @param message        Invocation message sent to the target when the color changes.
 * @param useOffscreen   When @c true, the palette is pre-rendered into an offscreen
 *                       bitmap for flicker-free updates.
 *
 * @see _InitData(), SetCellSize(), SetLayout()
 */
BColorControl::BColorControl(BPoint leftTop, color_control_layout layout,
	float cellSize, const char* name, BMessage* message, bool useOffscreen)
	:
	BControl(BRect(leftTop, leftTop), name, NULL, message,
		B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW | B_NAVIGABLE),
	fRedText(NULL),
	fGreenText(NULL),
	fBlueText(NULL),
	fOffscreenBitmap(NULL)
{
	_InitData(layout, cellSize, useOffscreen, NULL);
}


/**
 * @brief Archive-restoration constructor.
 *
 * Reconstructs a BColorControl from a flattened BMessage produced by Archive().
 * The child text controls are located via FindView() rather than re-created, so
 * the archived child views must already be present in @a data.
 *
 * @param data  Archive message previously written by Archive().
 *
 * @see Archive(), Instantiate()
 */
BColorControl::BColorControl(BMessage* data)
	:
	BControl(data),
	fRedText(NULL),
	fGreenText(NULL),
	fBlueText(NULL),
	fOffscreenBitmap(NULL)
{
	int32 layout;
	float cellSize;
	bool useOffscreen;

	data->FindInt32("_layout", &layout);
	data->FindFloat("_csize", &cellSize);
	data->FindBool("_use_off", &useOffscreen);

	_InitData((color_control_layout)layout, cellSize, useOffscreen, data);
}


/**
 * @brief Destroys the BColorControl and frees the offscreen bitmap, if any.
 */
BColorControl::~BColorControl()
{
	delete fOffscreenBitmap;
}


/**
 * @brief Initializes all internal state shared by every constructor path.
 *
 * Detects whether the main screen is in 8-bit palette mode, computes the row
 * and column counts from @a layout, scales the cell size to the current font,
 * and creates (or reconnects) the three RGB text controls. When @a data is
 * non-NULL the function is in restore mode and looks up existing child views
 * instead of creating new ones.
 *
 * @param layout        Desired cell arrangement for palette mode.
 * @param size          Requested cell size in points.
 * @param useOffscreen  @c true to allocate and populate an offscreen bitmap.
 * @param data          Non-NULL when restoring from an archive; @c NULL on
 *                      first-time construction.
 *
 * @note Palette-mode detection reads the main screen color space only once at
 *       construction time; runtime workspace or color-space changes are handled
 *       separately via B_SCREEN_CHANGED in MessageReceived().
 */
void
BColorControl::_InitData(color_control_layout layout, float size,
	bool useOffscreen, BMessage* data)
{
	fPaletteMode = BScreen(B_MAIN_SCREEN_ID).ColorSpace() == B_CMAP8;
		//TODO: we don't support workspace and colorspace changing for now
		//		so we take the main_screen colorspace at startup
	fColumns = layout;
	fRows = 256 / fColumns;

	_SetCellSize(size);

	fSelectedPaletteColorIndex = -1;
	fPreviousSelectedPaletteColorIndex = -1;
	fFocusedRamp = !fPaletteMode && IsFocus() ? 1 : -1;
	fClickedRamp = -1;

	const char* red = B_TRANSLATE_MARK("Red:");
	const char* green = B_TRANSLATE_MARK("Green:");
	const char* blue = B_TRANSLATE_MARK("Blue:");
	red = gSystemCatalog.GetString(red, "ColorControl");
	green = gSystemCatalog.GetString(green, "ColorControl");
	blue = gSystemCatalog.GetString(blue, "ColorControl");

	if (data != NULL) {
		fRedText = (BTextControl*)FindView("_red");
		fGreenText = (BTextControl*)FindView("_green");
		fBlueText = (BTextControl*)FindView("_blue");

		int32 value = 0;
		data->FindInt32("_val", &value);

		SetValue(value);
	} else {
		BRect textRect(0.0f, 0.0f, 0.0f, 0.0f);
		float labelWidth = std::max(StringWidth(red),
			std::max(StringWidth(green), StringWidth(blue)))
				+ kTextFieldsHSpacing;
		textRect.right = labelWidth + StringWidth("999999");
			// enough room for 3 digits plus 3 digits of padding
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		float labelHeight = fontHeight.ascent + fontHeight.descent;
		textRect.bottom = labelHeight;

		// red

		fRedText = new BTextControl(textRect, "_red", red, "0",
			new BMessage(kMsgColorEntered), B_FOLLOW_LEFT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE);
		fRedText->SetDivider(labelWidth);

		for (int32 i = 0; i < 256; i++)
			fRedText->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fRedText->TextView()->AllowChar(i);
		fRedText->TextView()->SetMaxBytes(3);

		// green

		textRect.OffsetBy(0, _TextRectOffset());
		fGreenText = new BTextControl(textRect, "_green", green, "0",
			new BMessage(kMsgColorEntered), B_FOLLOW_LEFT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE);
		fGreenText->SetDivider(labelWidth);

		for (int32 i = 0; i < 256; i++)
			fGreenText->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fGreenText->TextView()->AllowChar(i);
		fGreenText->TextView()->SetMaxBytes(3);

		// blue

		textRect.OffsetBy(0, _TextRectOffset());
		fBlueText = new BTextControl(textRect, "_blue", blue, "0",
			new BMessage(kMsgColorEntered), B_FOLLOW_LEFT | B_FOLLOW_TOP,
			B_WILL_DRAW | B_NAVIGABLE);
		fBlueText->SetDivider(labelWidth);

		for (int32 i = 0; i < 256; i++)
			fBlueText->TextView()->DisallowChar(i);
		for (int32 i = '0'; i <= '9'; i++)
			fBlueText->TextView()->AllowChar(i);
		fBlueText->TextView()->SetMaxBytes(3);

		AddChild(fRedText);
		AddChild(fGreenText);
		AddChild(fBlueText);
	}

	fRedText->SetHighUIColor(B_PANEL_TEXT_COLOR);
	fBlueText->SetHighUIColor(B_PANEL_TEXT_COLOR);
	fGreenText->SetHighUIColor(B_PANEL_TEXT_COLOR);

	// right align rgb values so that they line up
	fRedText->SetAlignment(B_ALIGN_LEFT, B_ALIGN_RIGHT);
	fGreenText->SetAlignment(B_ALIGN_LEFT, B_ALIGN_RIGHT);
	fBlueText->SetAlignment(B_ALIGN_LEFT, B_ALIGN_RIGHT);

	ResizeToPreferred();

	if (useOffscreen) {
		if (fOffscreenBitmap != NULL) {
			BRect bounds = _PaletteFrame();
			fOffscreenBitmap = new BBitmap(bounds, B_RGB32, true, false);
			BView* offscreenView = new BView(bounds, "off_view", 0, 0);

			fOffscreenBitmap->Lock();
			fOffscreenBitmap->AddChild(offscreenView);
			fOffscreenBitmap->Unlock();
		}
	} else {
		delete fOffscreenBitmap;
		fOffscreenBitmap = NULL;
	}
}


/**
 * @brief Positions the palette frame and the three RGB text controls.
 *
 * Computes fPaletteFrame from the current column count, row count, and cell
 * size, then decides whether the text controls are stacked one-per-ramp or
 * packed tightly at the top, and moves them to their final positions to the
 * right of the palette area.
 *
 * @note Call ResizeToPreferred() after this method to propagate the new layout
 *       to the view's frame.
 */
void
BColorControl::_LayoutView()
{
	fPaletteFrame.Set(0, 0, fColumns * fCellSize, fRows * fCellSize);
	fPaletteFrame.OffsetBy(kBevelSpacing, kBevelSpacing);
	if (!fPaletteMode) {
		// Reduce the inner space by 1 pixel so that the frame
		// is exactly rows * cellsize pixels in height
		fPaletteFrame.bottom -= 1;
	}

	float rampHeight = (float)(fRows * fCellSize / kRampCount);
	float offset = _TextRectOffset();
	float y = 0;
	if (rampHeight > fRedText->Frame().Height()) {
		// there is enough room to fit kRampCount labels,
		// shift text controls down by one ramp
		offset = rampHeight;
		y = floorf(offset + (offset - fRedText->Frame().Height()) / 2);
	}

	BRect rect = _PaletteFrame();
	fRedText->MoveTo(rect.right + kTextFieldsHSpacing, y);

	y += offset;
	fGreenText->MoveTo(rect.right + kTextFieldsHSpacing, y);

	y += offset;
	fBlueText->MoveTo(rect.right + kTextFieldsHSpacing, y);
}


/**
 * @brief Creates a BColorControl instance from a flattened archive message.
 *
 * @param data  Archive message produced by Archive().
 * @return A newly allocated BColorControl on success, or @c NULL if @a data
 *         does not describe a BColorControl object.
 *
 * @see Archive()
 */
BArchivable*
BColorControl::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BColorControl"))
		return new BColorControl(data);

	return NULL;
}


/**
 * @brief Flattens the BColorControl into a BMessage for persistence or cloning.
 *
 * Stores the palette layout, cell size, and offscreen-bitmap flag in addition
 * to all fields archived by BControl::Archive().
 *
 * @param data  Destination message to receive the archived fields.
 * @param deep  When @c true, child views (the RGB text controls) are also archived.
 * @return @c B_OK on success, or the first error code encountered while adding
 *         fields to @a data.
 *
 * @see Instantiate()
 */
status_t
BColorControl::Archive(BMessage* data, bool deep) const
{
	status_t status = BControl::Archive(data, deep);

	if (status == B_OK)
		status = data->AddInt32("_layout", Layout());

	if (status == B_OK)
		status = data->AddFloat("_csize", fCellSize);

	if (status == B_OK)
		status = data->AddBool("_use_off", fOffscreenBitmap != NULL);

	return status;
}


/**
 * @brief Overrides BView::SetLayout() to expose the inherited method hidden
 *        by the color_control_layout overload.
 *
 * C++ name lookup hides all base-class overloads when a derived class declares
 * a method with the same name. This forwarder restores access to
 * BControl::SetLayout(BLayout*) so that layout managers can attach a BLayout
 * to this view.
 *
 * @param layout  The BLayout to associate with this view.
 *
 * @see SetLayout(color_control_layout)
 */
void
BColorControl::SetLayout(BLayout* layout)
{
	// We need to implement this method, since we have another SetLayout()
	// method and C++ has this special method hiding "feature".
	BControl::SetLayout(layout);
}


/**
 * @brief Sets the currently selected color by its packed 32-bit integer value.
 *
 * The value is encoded as 0xRRGGBB00 (red in the high byte, blue in the
 * second-lowest byte, alpha forced to 255). In palette mode the nearest
 * indexed color is looked up on the screen; in true-color mode the palette
 * ramp area is invalidated so Draw() repaints the selectors. The three RGB
 * text controls are always updated to reflect the clamped color.
 *
 * @param value  Packed color value in BControl's native encoding.
 *
 * @see ValueAsColor(), SetValue(rgb_color)
 */
void
BColorControl::SetValue(int32 value)
{
	rgb_color c1 = ValueAsColor();
	rgb_color c2;
	c2.red = (value & 0xFF000000) >> 24;
	c2.green = (value & 0x00FF0000) >> 16;
	c2.blue = (value & 0x0000FF00) >> 8;
	c2.alpha = 255;

	if (fPaletteMode) {
		//workaround when two indexes have the same color
		rgb_color c
			= BScreen(Window()).ColorForIndex(fSelectedPaletteColorIndex);
		c.alpha = 255;
		if (fSelectedPaletteColorIndex == -1 || c != c2) {
				//here SetValue hasn't been called by mouse tracking
			fSelectedPaletteColorIndex = BScreen(Window()).IndexForColor(c2);
		}

		c2 = BScreen(Window()).ColorForIndex(fSelectedPaletteColorIndex);

		Invalidate(_PaletteSelectorFrame(fPreviousSelectedPaletteColorIndex));
		Invalidate(_PaletteSelectorFrame(fSelectedPaletteColorIndex));

		fPreviousSelectedPaletteColorIndex = fSelectedPaletteColorIndex;
	} else if (c1 != c2)
		Invalidate();

	// Set the value here, since BTextControl will trigger
	// Window()->UpdateIfNeeded() which will cause us to draw the indicators
	// at the old offset.
	if (Value() != value)
		BControl::SetValueNoUpdate(value);

	// the textcontrols have to be updated even when the color
	// hasn't changed since the value is clamped upstream
	// and the textcontrols would still show the unclamped value
	char string[4];
	sprintf(string, "%d", c2.red);
	fRedText->SetText(string);
	sprintf(string, "%d", c2.green);
	fGreenText->SetText(string);
	sprintf(string, "%d", c2.blue);
	fBlueText->SetText(string);
}


/**
 * @brief Returns the current color as an rgb_color struct.
 *
 * Unpacks the 32-bit integer stored by BControl::Value() into red, green,
 * and blue components. Alpha is always returned as 255.
 *
 * @return The currently selected color.
 *
 * @see SetValue()
 */
rgb_color
BColorControl::ValueAsColor()
{
	int32 value = Value();
	rgb_color color;

	color.red = (value & 0xFF000000) >> 24;
	color.green = (value & 0x00FF0000) >> 16;
	color.blue = (value & 0x0000FF00) >> 8;
	color.alpha = 255;

	return color;
}


/**
 * @brief Enables or disables the control and its three RGB text fields.
 *
 * Forwards the call to BControl::SetEnabled() and then propagates the enabled
 * state to fRedText, fGreenText, and fBlueText so that keyboard input into
 * the text fields is also gated by the enabled state.
 *
 * @param enabled  @c true to enable, @c false to disable.
 */
void
BColorControl::SetEnabled(bool enabled)
{
	BControl::SetEnabled(enabled);

	fRedText->SetEnabled(enabled);
	fGreenText->SetEnabled(enabled);
	fBlueText->SetEnabled(enabled);
}


/**
 * @brief Called when the view is attached to a window.
 *
 * Adopts the parent view's colors, redirects the text-control messages to
 * this view as their target, and populates the offscreen bitmap if one was
 * requested.
 *
 * @see DetachedFromWindow(), _InitOffscreen()
 */
void
BColorControl::AttachedToWindow()
{
	BControl::AttachedToWindow();

	AdoptParentColors();

	fRedText->SetTarget(this);
	fGreenText->SetTarget(this);
	fBlueText->SetTarget(this);

	if (fOffscreenBitmap != NULL)
		_InitOffscreen();
}


/**
 * @brief Handles messages directed at this control.
 *
 * Processes three cases:
 * - A dropped B_RGB_COLOR_TYPE message sets the color and invokes the target.
 * - kMsgColorEntered (sent by the text controls) parses the three text fields
 *   and applies the entered color.
 * - B_SCREEN_CHANGED rebuilds the entire control when the display switches
 *   between 8-bit palette mode and true-color mode.
 *
 * All other messages are forwarded to BControl::MessageReceived().
 *
 * @param message  The incoming message to handle.
 */
void
BColorControl::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && IsEnabled()) {
		char* name;
		type_code type;
		rgb_color* color;
		ssize_t size;
		if (message->GetInfo(B_RGB_COLOR_TYPE, 0, &name, &type) == B_OK
			&& message->FindData(name, type, (const void**)&color, &size) == B_OK) {
			SetValue(*color);
			Invoke(message);
		}
	}

	switch (message->what) {
		case kMsgColorEntered:
		{
			rgb_color color;
			color.red = min_c(strtol(fRedText->Text(), NULL, 10), 255);
			color.green = min_c(strtol(fGreenText->Text(), NULL, 10), 255);
			color.blue = min_c(strtol(fBlueText->Text(), NULL, 10), 255);
			color.alpha = 255;

			SetValue(color);
			Invoke();
			break;
		}

		case B_SCREEN_CHANGED:
		{
			BRect frame;
			uint32 mode;
			if (message->FindRect("frame", &frame) == B_OK
				&& message->FindInt32("mode", (int32*)&mode) == B_OK) {
				if ((fPaletteMode && mode == B_CMAP8)
					|| (!fPaletteMode && mode != B_CMAP8)) {
					// not switching to or from B_CMAP8, break
					break;
				}

				// fake an archive message (so we don't rebuild views)
				BMessage* data = new BMessage();
				data->AddInt32("_val", Value());

				// reinititialize
				bool useOffscreen = fOffscreenBitmap != NULL;
				_InitData((color_control_layout)fColumns, fCellSize,
					useOffscreen, data);
				if (useOffscreen)
					_InitOffscreen();

				// cleanup
				delete data;
			}
			break;
		}

		default:
			BControl::MessageReceived(message);
			break;
	}
}


/**
 * @brief Draws the color palette or ramp area and the selection indicators.
 *
 * When an offscreen bitmap is available it is blitted directly; otherwise
 * _DrawColorArea() paints into this view. Selection indicators are always
 * drawn on top via _DrawSelectors().
 *
 * @param updateRect  The region that requires repainting, in view coordinates.
 *
 * @see _DrawColorArea(), _DrawSelectors()
 */
void
BColorControl::Draw(BRect updateRect)
{
	if (fOffscreenBitmap != NULL)
		DrawBitmap(fOffscreenBitmap, B_ORIGIN);
	else
		_DrawColorArea(this, updateRect);

	_DrawSelectors(this);
}


/**
 * @brief Renders the palette grid or the four color ramps into @a target.
 *
 * In palette mode, draws a bordered grid and fills each cell with the
 * corresponding system color. In true-color mode, draws four horizontal
 * gradient ramps (white/gray, red, green, blue) using _DrawColorRamp().
 * Only cells or ramp columns that intersect @a updateRect are redrawn.
 *
 * @param target      The BView to draw into (may be an offscreen view).
 * @param updateRect  Clipping rectangle for incremental updates.
 *
 * @see _DrawColorRamp(), _RampFrame(), Draw()
 */
void
BColorControl::_DrawColorArea(BView* target, BRect updateRect)
{
	BRect rect = _PaletteFrame();
	bool enabled = IsEnabled();

	rgb_color base = ViewColor();
	rgb_color darken1 = tint_color(base, B_DARKEN_1_TINT);

	uint32 flags = be_control_look->Flags(this);
	be_control_look->DrawTextControlBorder(target, rect, updateRect,
		base, flags);

	if (fPaletteMode) {
		int colBegin = max_c(0, -1 + int(updateRect.left) / int(fCellSize));
		int colEnd = min_c(fColumns,
			2 + int(updateRect.right) / int(fCellSize));
		int rowBegin = max_c(0, -1 + int(updateRect.top) / int(fCellSize));
		int rowEnd = min_c(fRows, 2 + int(updateRect.bottom)
			/ int(fCellSize));

		// grid
		target->SetHighColor(enabled ? darken1 : base);

		for (int xi = 0; xi < fColumns + 1; xi++) {
			float x = fPaletteFrame.left + float(xi) * fCellSize;
			target->StrokeLine(BPoint(x, fPaletteFrame.top),
				BPoint(x, fPaletteFrame.bottom));
		}

		for (int yi = 0; yi < fRows + 1; yi++) {
			float y = fPaletteFrame.top + float(yi) * fCellSize;
			target->StrokeLine(BPoint(fPaletteFrame.left, y),
				BPoint(fPaletteFrame.right, y));
		}

		// colors
		for (int col = colBegin; col < colEnd; col++) {
			for (int row = rowBegin; row < rowEnd; row++) {
				uint8 colorIndex = row * fColumns + col;
				float x = fPaletteFrame.left + col * fCellSize;
				float y = fPaletteFrame.top + row * fCellSize;

				target->SetHighColor(system_colors()->color_list[colorIndex]);
				target->FillRect(BRect(x + 1, y + 1,
					x + fCellSize - 1, y + fCellSize - 1));
			}
		}
	} else {
		rgb_color white = { 255, 255, 255, 255 };
		rgb_color red   = { 255, 0, 0, 255 };
		rgb_color green = { 0, 255, 0, 255 };
		rgb_color blue  = { 0, 0, 255, 255 };

		rgb_color compColor = { 0, 0, 0, 255 };
		if (!enabled) {
			compColor.red = compColor.green = compColor.blue = 156;
			red.red = green.green = blue.blue = 70;
			white.red = white.green = white.blue = 70;
		}
		_DrawColorRamp(_RampFrame(0), target, white, compColor, 0, false,
			updateRect);
		_DrawColorRamp(_RampFrame(1), target, red, compColor, 0, false,
			updateRect);
		_DrawColorRamp(_RampFrame(2), target, green, compColor, 0, false,
			updateRect);
		_DrawColorRamp(_RampFrame(3), target, blue, compColor, 0, false,
			updateRect);
	}
}


/**
 * @brief Draws the selection indicators on top of the palette or ramp area.
 *
 * In palette mode, strokes a white rectangle around the currently selected
 * color cell. In true-color mode, draws a double-ring ellipse selector on each
 * of the three color ramps (red, green, blue) at the position corresponding to
 * the current channel value. The focused ramp receives an additional inner ring
 * to indicate keyboard focus.
 *
 * @param target  The BView to draw into.
 *
 * @see _SelectorPosition(), _RampFrame(), Draw()
 */
void
BColorControl::_DrawSelectors(BView* target)
{
	rgb_color base = ViewColor();
	rgb_color lightenmax = tint_color(base, B_LIGHTEN_MAX_TINT);

	if (fPaletteMode) {
		if (fSelectedPaletteColorIndex != -1) {
			target->SetHighColor(lightenmax);
			target->StrokeRect(
				_PaletteSelectorFrame(fSelectedPaletteColorIndex));
		}
	} else {
		rgb_color color = ValueAsColor();
		target->SetHighColor(255, 255, 255);
		target->SetLowColor(0, 0, 0);

		int components[4] = { color.alpha, color.red, color.green, color.blue };

		for (int i = 1; i < 4; i++) {
			BPoint center = _SelectorPosition(_RampFrame(i), components[i]);

			target->SetPenSize(kSelectorPenSize);
			target->StrokeEllipse(center, kSelectorSize / 2, kSelectorSize / 2);
			target->SetPenSize(kSelectorPenSize / 2);
			target->StrokeEllipse(center, kSelectorSize, kSelectorSize,
				B_SOLID_LOW);
			if (i == fFocusedRamp) {
				target->StrokeEllipse(center,
					kSelectorSize / 2, kSelectorSize / 2, B_SOLID_LOW);
			}
		}

		target->SetPenSize(1.0f);
	}
}


/**
 * @brief Fills a single color ramp rectangle with a linear gradient.
 *
 * Iterates over the columns of @a rect that intersect @a updateRect, computing
 * the interpolated color for each column based on @a baseColor and @a compColor,
 * and draws a vertical line per column using BView::AddLine() for efficiency.
 *
 * @param rect        Bounding rectangle of this ramp within the view.
 * @param target      The BView to draw into.
 * @param baseColor   The full-intensity channel color (e.g., pure red).
 * @param compColor   The zero-intensity color added as an offset.
 * @param flag        Reserved for future use; currently unused.
 * @param focused     Reserved for future use; currently unused.
 * @param updateRect  Clip rectangle; only columns intersecting this rect are drawn.
 *
 * @see _DrawColorArea(), _RampFrame()
 */
void
BColorControl::_DrawColorRamp(BRect rect, BView* target,
	rgb_color baseColor, rgb_color compColor, int16 flag, bool focused,
	BRect updateRect)
{
	float width = rect.Width() + 1;
	rgb_color color = ValueAsColor();
	color.alpha = 255;

	updateRect = updateRect & rect;

	if (updateRect.IsValid() && updateRect.Width() >= 0) {
		target->BeginLineArray((int32)updateRect.Width() + 1);

		for (float i = (updateRect.left - rect.left);
				i <= (updateRect.right - rect.left) + 1; i++) {
			if (baseColor.red == 255)
				color.red = (uint8)(i * 255 / width) + compColor.red;
			if (baseColor.green == 255)
				color.green = (uint8)(i * 255 / width) + compColor.green;
			if (baseColor.blue == 255)
				color.blue = (uint8)(i * 255 / width) + compColor.blue;

			target->AddLine(BPoint(rect.left + i, rect.top),
				BPoint(rect.left + i, rect.bottom - 1), color);
		}

		target->EndLineArray();
	}
}


/**
 * @brief Computes the center point of the selector ellipse for a given shade.
 *
 * Maps the 0–255 channel value @a shade to a horizontal position within
 * @a rampRect, accounting for the selector radius and horizontal spacing so
 * the selector never clips outside the ramp border.
 *
 * @param rampRect  The bounding rectangle of the ramp.
 * @param shade     The channel value (0–255) to map to an x-coordinate.
 * @return The center point of the selector circle in view coordinates.
 *
 * @see _DrawSelectors(), _RampFrame()
 */
BPoint
BColorControl::_SelectorPosition(const BRect& rampRect, uint8 shade) const
{
	float radius = kSelectorSize / 2 + kSelectorPenSize / 2;

	return BPoint(rampRect.left + kSelectorHSpacing + radius +
		shade * (rampRect.Width() - 2 * (kSelectorHSpacing + radius)) / 255,
		rampRect.top + rampRect.Height() / 2);
}


/**
 * @brief Returns the outer bounding rectangle of the palette or ramp area.
 *
 * Expands fPaletteFrame outward by kBevelSpacing on all sides to include the
 * bevel border drawn around the color area.
 *
 * @return The outer palette frame in view coordinates.
 *
 * @see _RampFrame(), _LayoutView()
 */
BRect
BColorControl::_PaletteFrame() const
{
	return fPaletteFrame.InsetByCopy(-kBevelSpacing, -kBevelSpacing);
}


/**
 * @brief Returns the bounding rectangle of a single color ramp strip.
 *
 * Divides fPaletteFrame evenly into kRampCount horizontal bands and returns
 * the band at index @a rampIndex (0 = white/gray, 1 = red, 2 = green, 3 = blue).
 *
 * @param rampIndex  Zero-based index of the ramp (0–3).
 * @return The bounding rectangle of the requested ramp in view coordinates.
 *
 * @see _DrawColorRamp(), _SelectorPosition(), kRampCount
 */
BRect
BColorControl::_RampFrame(uint8 rampIndex) const
{
	float rampHeight = (float)(fRows * fCellSize / kRampCount);

	return BRect(fPaletteFrame.left,
		fPaletteFrame.top + float(rampIndex) * rampHeight,
		fPaletteFrame.right,
		fPaletteFrame.top + float(rampIndex + 1) * rampHeight);
}


/**
 * @brief Scales the requested cell size to the current font and enforces the minimum.
 *
 * The cell size is proportional to the ratio of the current font size to
 * kDefaultFontSize, so the palette scales naturally when the system font
 * changes. The result is clamped to kMinCellSize.
 *
 * @param size  Requested cell size in font-size-relative points.
 *
 * @see SetCellSize(), kMinCellSize, kDefaultFontSize
 */
void
BColorControl::_SetCellSize(float size)
{
	BFont font;
	GetFont(&font);
	fCellSize = std::max(kMinCellSize,
		ceilf(size * font.Size() / kDefaultFontSize));
}


/**
 * @brief Returns the vertical spacing between successive RGB text controls.
 *
 * Takes the larger of the text control's own height and one-third of the
 * palette area's height, ensuring the controls fit within the ramp bands when
 * the palette is tall enough.
 *
 * @return Vertical offset in pixels between adjacent text control tops.
 */
float
BColorControl::_TextRectOffset()
{
	return std::max(fRedText->Bounds().Height(),
		ceilf(_PaletteFrame().Height() / 3));
}


/**
 * @brief Returns the bounding rectangle of the selector drawn over a palette cell.
 *
 * Converts the flat color index @a colorIndex to a row/column position within
 * fPaletteFrame and returns the corresponding cell rectangle.
 *
 * @param colorIndex  Index into the system color list (0–255).
 * @return The cell rectangle in view coordinates.
 *
 * @see _DrawSelectors(), SetValue()
 */
BRect
BColorControl::_PaletteSelectorFrame(uint8 colorIndex) const
{
	uint32 row = colorIndex / fColumns;
	uint32 column = colorIndex % fColumns;
	float x = fPaletteFrame.left + column * fCellSize;
	float y = fPaletteFrame.top + row * fCellSize;
	return BRect(x, y, x + fCellSize, y + fCellSize);
}


/**
 * @brief Renders the color area into the offscreen bitmap's child view.
 *
 * Locks the offscreen bitmap, retrieves its embedded BView, calls
 * _DrawColorArea() to paint the full palette or ramp content, and syncs the
 * view before unlocking. Must be called after the bitmap has been allocated.
 *
 * @see AttachedToWindow(), _DrawColorArea()
 */
void
BColorControl::_InitOffscreen()
{
	if (fOffscreenBitmap->Lock()) {
		BView* offscreenView = fOffscreenBitmap->ChildAt((int32)0);
		if (offscreenView != NULL) {
			_DrawColorArea(offscreenView, _PaletteFrame());
			offscreenView->Sync();
		}
		fOffscreenBitmap->Unlock();
	}
}


/**
 * @brief Invalidates the bounding box of a ramp selector so it is repainted.
 *
 * Calculates the invalidation radius from kSelectorSize and kSelectorPenSize,
 * expanding it when @a focused is @c true to include the outer focus ring.
 * Does nothing in palette mode or for out-of-range ramp indices.
 *
 * @param ramp    Ramp index (1 = red, 2 = green, 3 = blue); values outside
 *                [1, 3] are silently ignored.
 * @param color   The current color used to locate the selector position.
 * @param focused @c true if the ramp currently has keyboard focus, which
 *                requires a larger invalidation region.
 *
 * @see _SelectorPosition(), _RampFrame(), MakeFocus()
 */
void
BColorControl::_InvalidateSelector(int16 ramp, rgb_color color, bool focused)
{
	if (fPaletteMode)
		return;

	if (ramp < 1 || ramp > 3)
		return;

	float invalidateRadius = focused
		? kSelectorSize + kSelectorPenSize / 2
		: kSelectorSize / 2 + kSelectorPenSize;

	uint8 colorValue = ramp == 1 ? color.red : ramp == 2 ? color.green
		: color.blue;

	BPoint pos = _SelectorPosition(_RampFrame(ramp), colorValue);
	Invalidate(BRect(pos.x - invalidateRadius, pos.y - invalidateRadius,
		pos.x + invalidateRadius, pos.y + invalidateRadius));
}


/**
 * @brief Sets the display cell size and resizes the control to fit.
 *
 * Delegates to _SetCellSize() for font-scaled clamping, then calls
 * ResizeToPreferred() to recompute the layout and adjust the view frame.
 *
 * @param size  Desired cell size in font-size-relative points.
 *
 * @see CellSize(), _SetCellSize()
 */
void
BColorControl::SetCellSize(float size)
{
	_SetCellSize(size);
	ResizeToPreferred();
}


/**
 * @brief Returns the current (font-scaled) cell size in pixels.
 *
 * @return The cell size as stored in fCellSize after font scaling.
 *
 * @see SetCellSize()
 */
float
BColorControl::CellSize() const
{
	return fCellSize;
}


/**
 * @brief Changes the palette cell arrangement and refreshes the display.
 *
 * Updates fColumns and fRows to match @a layout, then calls ResizeToPreferred()
 * and Invalidate() to rebuild the view geometry and repaint.
 *
 * @param layout  One of B_CELLS_4x64, B_CELLS_8x32, B_CELLS_16x16,
 *                B_CELLS_32x8, or B_CELLS_64x4.
 *
 * @see Layout(), SetLayout(BLayout*)
 */
void
BColorControl::SetLayout(color_control_layout layout)
{
	switch (layout) {
		case B_CELLS_4x64:
			fColumns = 4;
			fRows = 64;
			break;

		case B_CELLS_8x32:
			fColumns = 8;
			fRows = 32;
			break;

		case B_CELLS_16x16:
			fColumns = 16;
			fRows = 16;
			break;

		case B_CELLS_32x8:
			fColumns = 32;
			fRows = 8;
			break;

		case B_CELLS_64x4:
			fColumns = 64;
			fRows = 4;
			break;
	}

	ResizeToPreferred();
	Invalidate();
}


/**
 * @brief Returns the current palette cell layout constant.
 *
 * Examines fColumns and fRows to reconstruct the color_control_layout enum
 * value. Defaults to B_CELLS_32x8 if the current state does not match any
 * known layout (which should not occur during normal use).
 *
 * @return The active color_control_layout value.
 *
 * @see SetLayout(color_control_layout)
 */
color_control_layout
BColorControl::Layout() const
{
	if (fColumns == 4 && fRows == 64)
		return B_CELLS_4x64;

	if (fColumns == 8 && fRows == 32)
		return B_CELLS_8x32;

	if (fColumns == 16 && fRows == 16)
		return B_CELLS_16x16;

	if (fColumns == 32 && fRows == 8)
		return B_CELLS_32x8;

	if (fColumns == 64 && fRows == 4)
		return B_CELLS_64x4;

	return B_CELLS_32x8;
}


/**
 * @brief Called when the parent window gains or loses activation.
 *
 * Forwards to BControl::WindowActivated() for default handling such as
 * updating focus ring rendering.
 *
 * @param state  @c true if the window became active, @c false if it deactivated.
 */
void
BColorControl::WindowActivated(bool state)
{
	BControl::WindowActivated(state);
}


/**
 * @brief Handles keyboard navigation and color adjustment in true-color mode.
 *
 * In true-color mode:
 * - Up/Down arrows cycle the keyboard focus among the three color ramps.
 * - Left/Right arrows decrement or increment the focused channel by 1, or by 5
 *   when the key is being held down (auto-repeat count exceeds 4).
 *
 * All key events are also forwarded to BControl::KeyDown() for default
 * handling (tab navigation, etc.).
 *
 * @param bytes     Pointer to the UTF-8 byte sequence of the pressed key.
 * @param numBytes  Length of the byte sequence.
 *
 * @see MakeFocus(), SetValue(), fFocusedRamp
 */
void
BColorControl::KeyDown(const char* bytes, int32 numBytes)
{
	if (IsFocus() && !fPaletteMode && numBytes == 1) {
		rgb_color color = ValueAsColor();

		switch (bytes[0]) {
			case B_UP_ARROW:
			{
				int16 oldFocus = fFocusedRamp;
				fFocusedRamp--;
				if (fFocusedRamp < 1)
					fFocusedRamp = 3;

				_InvalidateSelector(oldFocus, color, true);
				_InvalidateSelector(fFocusedRamp, color, true);
				break;
			}

			case B_DOWN_ARROW:
			{
				int16 oldFocus = fFocusedRamp;
				fFocusedRamp++;
				if (fFocusedRamp > 3)
					fFocusedRamp = 1;

				_InvalidateSelector(oldFocus, color, true);
				_InvalidateSelector(fFocusedRamp, color, true);
				break;
			}

			case B_LEFT_ARROW:
			{
				bool goFaster = false;
				if (Window() != NULL) {
					BMessage* message = Window()->CurrentMessage();
					if (message != NULL && message->what == B_KEY_DOWN) {
						int32 repeats = 0;
						if (message->FindInt32("be:key_repeat", &repeats)
								== B_OK && repeats > 4) {
							goFaster = true;
						}
					}
				}

				if (fFocusedRamp == 1) {
					if (goFaster && color.red >= 5)
						color.red -= 5;
					else if (color.red > 0)
						color.red--;
				} else if (fFocusedRamp == 2) {
					if (goFaster && color.green >= 5)
						color.green -= 5;
					else if (color.green > 0)
						color.green--;
				} else if (fFocusedRamp == 3) {
				 	if (goFaster && color.blue >= 5)
						color.blue -= 5;
					else if (color.blue > 0)
						color.blue--;
				}

				SetValue(color);
				Invoke();
				break;
			}

			case B_RIGHT_ARROW:
			{
				bool goFaster = false;
				if (Window() != NULL) {
					BMessage* message = Window()->CurrentMessage();
					if (message != NULL && message->what == B_KEY_DOWN) {
						int32 repeats = 0;
						if (message->FindInt32("be:key_repeat", &repeats)
								== B_OK && repeats > 4) {
							goFaster = true;
						}
					}
				}

				if (fFocusedRamp == 1) {
					if (goFaster && color.red <= 250)
						color.red += 5;
					else if (color.red < 255)
						color.red++;
				} else if (fFocusedRamp == 2) {
					if (goFaster && color.green <= 250)
						color.green += 5;
					else if (color.green < 255)
						color.green++;
				} else if (fFocusedRamp == 3) {
				 	if (goFaster && color.blue <= 250)
						color.blue += 5;
					else if (color.blue < 255)
						color.blue++;
				}

				SetValue(color);
				Invoke();
				break;
			}
		}
	}

	BControl::KeyDown(bytes, numBytes);
}


/**
 * @brief Called when a mouse button is released over the control.
 *
 * Clears the active ramp tracking state and releases the mouse event capture
 * that was established in MouseDown().
 *
 * @param point  The cursor position in view coordinates at button release.
 *
 * @see MouseDown(), MouseMoved()
 */
void
BColorControl::MouseUp(BPoint point)
{
	fClickedRamp = -1;
	SetTracking(false);
}


/**
 * @brief Handles a mouse button press to select a color.
 *
 * Ignores the event when the control is disabled or the click falls outside
 * the palette area. In palette mode, maps the click to a color cell index and
 * sets that indexed color. In true-color mode, determines which ramp the click
 * landed on and sets the corresponding channel to the interpolated shade.
 * Begins tracking to support drag-to-change via MouseMoved().
 *
 * @param point  The cursor position in view coordinates at button press.
 *
 * @see MouseUp(), MouseMoved(), SetValue()
 */
void
BColorControl::MouseDown(BPoint point)
{
	if (!IsEnabled())
		return;
	if (!fPaletteFrame.Contains(point))
		return;

	if (fPaletteMode) {
		int col = (int)((point.x - fPaletteFrame.left) / fCellSize);
		int row = (int)((point.y - fPaletteFrame.top) / fCellSize);
		int colorIndex = row * fColumns + col;
		if (colorIndex >= 0 && colorIndex < 256) {
			fSelectedPaletteColorIndex = colorIndex;
			SetValue(system_colors()->color_list[colorIndex]);
		}
	} else {
		rgb_color color = ValueAsColor();

		uint8 shade = (unsigned char)max_c(0,
			min_c((point.x - _RampFrame(0).left) * 255
				/ _RampFrame(0).Width(), 255));

		if (_RampFrame(0).Contains(point)) {
			color.red = color.green = color.blue = shade;
			fClickedRamp = 0;
		} else if (_RampFrame(1).Contains(point)) {
			color.red = shade;
			fClickedRamp = 1;
		} else if (_RampFrame(2).Contains(point)) {
			color.green = shade;
			fClickedRamp = 2;
		} else if (_RampFrame(3).Contains(point)) {
			color.blue = shade;
			fClickedRamp = 3;
		}

		SetValue(color);
	}

	Invoke();

	SetTracking(true);
	SetMouseEventMask(B_POINTER_EVENTS,
		B_NO_POINTER_HISTORY | B_LOCK_WINDOW_FOCUS);
}


/**
 * @brief Tracks the cursor to update the color while a button is held down.
 *
 * Does nothing when the control is not in tracking mode. In palette mode,
 * updates the selected color as the cursor moves over different cells. In
 * true-color mode, updates the channel associated with fClickedRamp based on
 * the cursor's horizontal position within the ramp area.
 *
 * @param point    The current cursor position in view coordinates.
 * @param transit  Entry/exit transit code (B_INSIDE_VIEW, B_EXITED_VIEW, etc.).
 * @param message  Non-NULL when a drag-and-drop is in progress; @c NULL otherwise.
 *
 * @see MouseDown(), MouseUp(), SetValue()
 */
void
BColorControl::MouseMoved(BPoint point, uint32 transit,
	const BMessage* message)
{
	if (!IsTracking())
		return;

	if (fPaletteMode && fPaletteFrame.Contains(point)) {
		int col = (int)((point.x - fPaletteFrame.left) / fCellSize);
		int row = (int)((point.y - fPaletteFrame.top) / fCellSize);
		int colorIndex = row * fColumns + col;
		if (colorIndex >= 0 && colorIndex < 256) {
			fSelectedPaletteColorIndex = colorIndex;
			SetValue(system_colors()->color_list[colorIndex]);
		}
	} else {
		if (fClickedRamp < 0 || fClickedRamp > 3)
			return;

		rgb_color color = ValueAsColor();

		uint8 shade = (unsigned char)max_c(0,
			min_c((point.x - _RampFrame(0).left) * 255
				/ _RampFrame(0).Width(), 255));

		if (fClickedRamp == 0)
			color.red = color.green = color.blue = shade;
		else if (fClickedRamp == 1)
			color.red = shade;
		else if (fClickedRamp == 2)
			color.green = shade;
		else if (fClickedRamp == 3)
			color.blue = shade;

		SetValue(color);
	}

	Invoke();
}


/**
 * @brief Called when the view is removed from its window.
 *
 * Forwards to BControl::DetachedFromWindow() for default cleanup.
 *
 * @see AttachedToWindow()
 */
void
BColorControl::DetachedFromWindow()
{
	BControl::DetachedFromWindow();
}


/**
 * @brief Returns the minimum size needed to display the control at its current settings.
 *
 * Computes the required width as the palette width plus text-field spacing and
 * the width of the widest text control. The height is the larger of the palette
 * height and the bottom edge of the blue text field.
 *
 * @param[out] _width   Receives the preferred width, or is left unchanged if NULL.
 * @param[out] _height  Receives the preferred height, or is left unchanged if NULL.
 *
 * @see ResizeToPreferred(), PreferredSize()
 */
void
BColorControl::GetPreferredSize(float* _width, float* _height)
{
	BRect rect = _PaletteFrame();

	if (rect.Height() < fBlueText->Frame().bottom) {
		// adjust the height to fit
		rect.bottom = fBlueText->Frame().bottom;
	}

	if (_width) {
		*_width = rect.Width() + kTextFieldsHSpacing
			+ fRedText->Bounds().Width();
	}

	if (_height)
		*_height = rect.Height();
}


/**
 * @brief Repositions internal child views and resizes the control to its preferred size.
 *
 * Calls _LayoutView() to recompute the palette frame and text-control positions,
 * then delegates to BControl::ResizeToPreferred() to adjust the view's own frame.
 *
 * @see GetPreferredSize(), _LayoutView()
 */
void
BColorControl::ResizeToPreferred()
{
	_LayoutView();
	BControl::ResizeToPreferred();
}


/**
 * @brief Sends the invocation message to the control's target.
 *
 * Provides a public override point and forwards directly to BControl::Invoke().
 * Pass @c NULL to use the message set at construction time.
 *
 * @param message  Message to send, or @c NULL to use the default message.
 * @return @c B_OK on success, or an error code from BInvoker::Invoke().
 */
status_t
BColorControl::Invoke(BMessage* message)
{
	return BControl::Invoke(message);
}


/**
 * @brief Called when the control's frame position changes.
 *
 * Forwards to BControl::FrameMoved() for default handling.
 *
 * @param newPosition  The new top-left position of the frame in parent coordinates.
 *
 * @see FrameResized()
 */
void
BColorControl::FrameMoved(BPoint newPosition)
{
	BControl::FrameMoved(newPosition);
}


/**
 * @brief Called when the control's frame dimensions change.
 *
 * Forwards to BControl::FrameResized() for default handling.
 *
 * @param newWidth   The new frame width in pixels.
 * @param newHeight  The new frame height in pixels.
 *
 * @see FrameMoved()
 */
void
BColorControl::FrameResized(float newWidth, float newHeight)
{
	BControl::FrameResized(newWidth, newHeight);
}


/**
 * @brief Resolves a scripting specifier to the appropriate message handler.
 *
 * Forwards to BControl::ResolveSpecifier() which handles all standard
 * BControl and BView scripting properties.
 *
 * @param message    The scripting message containing the specifier chain.
 * @param index      Current position in the specifier chain.
 * @param specifier  The specifier at @a index.
 * @param form       The specifier form constant.
 * @param property   The property name string.
 * @return The BHandler that should process the scripting message.
 *
 * @see GetSupportedSuites()
 */
BHandler*
BColorControl::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	return BControl::ResolveSpecifier(message, index, specifier, form,
		property);
}


/**
 * @brief Reports the scripting suites supported by this control.
 *
 * Forwards to BControl::GetSupportedSuites() which advertises the standard
 * suite names for BControl and BView.
 *
 * @param data  Message into which the supported suite names are added.
 * @return @c B_OK on success.
 *
 * @see ResolveSpecifier()
 */
status_t
BColorControl::GetSupportedSuites(BMessage* data)
{
	return BControl::GetSupportedSuites(data);
}


/**
 * @brief Gives or removes keyboard focus from the control.
 *
 * When focus is gained in true-color mode, initializes fFocusedRamp to 1 (the
 * red ramp) so that arrow-key navigation has a defined starting point.
 * When focus is lost, fFocusedRamp is set to -1. Forwards to
 * BControl::MakeFocus() for focus ring repainting.
 *
 * @param focused  @c true to give focus, @c false to remove it.
 *
 * @see KeyDown(), fFocusedRamp
 */
void
BColorControl::MakeFocus(bool focused)
{
	fFocusedRamp = !fPaletteMode && focused ? 1 : -1;
	BControl::MakeFocus(focused);
}


/**
 * @brief Called after all siblings have been attached to the window.
 *
 * Forwards to BControl::AllAttached() for default handling.
 *
 * @see AllDetached(), AttachedToWindow()
 */
void
BColorControl::AllAttached()
{
	BControl::AllAttached();
}


/**
 * @brief Called after all siblings have been detached from the window.
 *
 * Forwards to BControl::AllDetached() for default handling.
 *
 * @see AllAttached(), DetachedFromWindow()
 */
void
BColorControl::AllDetached()
{
	BControl::AllDetached();
}


/**
 * @brief Sets a vector icon for this control.
 *
 * Forwards to BControl::SetIcon(); BColorControl does not use an icon
 * internally but exposes this method so subclasses and callers can assign one.
 *
 * @param icon   Bitmap containing the icon data in a supported format.
 * @param flags  Icon-assignment flags passed to BControl::SetIcon().
 * @return @c B_OK on success, or an error code from BControl::SetIcon().
 */
status_t
BColorControl::SetIcon(const BBitmap* icon, uint32 flags)
{
	return BControl::SetIcon(icon, flags);
}


/**
 * @brief Implements the binary-compatibility perform hook for late-bound virtual calls.
 *
 * Dispatches perform codes introduced after the original ABI freeze to the
 * correct BColorControl virtual method without requiring a vtable slot change.
 * Handled codes include MinSize, MaxSize, PreferredSize, LayoutAlignment,
 * HasHeightForWidth, GetHeightForWidth, SetLayout, LayoutInvalidated,
 * DoLayout, and SetIcon. Unrecognized codes are forwarded to
 * BControl::Perform().
 *
 * @param code   A PERFORM_CODE_* constant identifying the operation.
 * @param _data  Pointer to a perform_data_* struct appropriate for @a code.
 * @return @c B_OK for handled codes, or the result of BControl::Perform()
 *         for unhandled ones.
 *
 * @see BControl::Perform()
 */
status_t
BColorControl::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BColorControl::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BColorControl::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BColorControl::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BColorControl::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BColorControl::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BColorControl::GetHeightForWidth(data->width, &data->min,
				&data->max, &data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BColorControl::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BColorControl::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BColorControl::DoLayout();
			return B_OK;
		}

		case PERFORM_CODE_SET_ICON:
		{
			perform_data_set_icon* data = (perform_data_set_icon*)_data;
			return BColorControl::SetIcon(data->icon, data->flags);
		}
	}

	return BControl::Perform(code, _data);
}


void BColorControl::_ReservedColorControl1() {}
void BColorControl::_ReservedColorControl2() {}
void BColorControl::_ReservedColorControl3() {}
void BColorControl::_ReservedColorControl4() {}


BColorControl &
BColorControl::operator=(const BColorControl &)
{
	return *this;
}
