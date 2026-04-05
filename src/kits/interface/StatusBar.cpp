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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Joseph Groover <looncraz@looncraz.net>
 */


/**
 * @file StatusBar.cpp
 * @brief Implementation of BStatusBar, a progress indicator control
 *
 * BStatusBar displays a labeled progress bar filled to a percentage of its
 * range. It supports sub-labels, trail text, and smooth update messages for
 * animated progress indication.
 *
 * @see BControl, BView, BControlLook
 */


#include <StatusBar.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ControlLook.h>
#include <Layout.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <Region.h>

#include <binary_compatibility/Interface.h>

/** @brief Internal flag bit indicating the bar color was set by the application. */
enum internalFlags {
	kCustomBarColor = 1
};


/**
 * @brief Construct a frame-based BStatusBar with labels.
 *
 * @param frame          Initial bounds rectangle in the parent's coordinate system.
 * @param name           Internal view name used for lookup and archiving.
 * @param label          Left-side label displayed above the bar. May be NULL.
 * @param trailingLabel  Right-side label displayed above the bar. May be NULL.
 */
BStatusBar::BStatusBar(BRect frame, const char *name, const char *label,
		const char *trailingLabel)
	:
	BView(frame, name, B_FOLLOW_LEFT | B_FOLLOW_TOP, B_WILL_DRAW),
	fLabel(label),
	fTrailingLabel(trailingLabel)
{
	_InitObject();
}


/**
 * @brief Construct a layout-managed BStatusBar without an explicit frame.
 *
 * @param name           Internal view name.
 * @param label          Left-side label displayed above the bar. May be NULL.
 * @param trailingLabel  Right-side label displayed above the bar. May be NULL.
 */
BStatusBar::BStatusBar(const char *name, const char *label,
		const char *trailingLabel)
	:
	BView(BRect(0, 0, -1, -1), name, B_FOLLOW_LEFT | B_FOLLOW_TOP,
		B_WILL_DRAW | B_SUPPORTS_LAYOUT),
	fLabel(label),
	fTrailingLabel(trailingLabel)
{
	_InitObject();
}


/**
 * @brief Unarchive constructor: restore a BStatusBar from a BMessage.
 *
 * Reads labels, text strings, bar height, bar color, current value, and
 * maximum value from \a archive.
 *
 * @param archive  The archive message produced by Archive().
 * @see Instantiate(), Archive()
 */
BStatusBar::BStatusBar(BMessage *archive)
	:
	BView(archive)
{
	_InitObject();

	archive->FindString("_label", &fLabel);
	archive->FindString("_tlabel", &fTrailingLabel);

	archive->FindString("_text", &fText);
	archive->FindString("_ttext", &fTrailingText);

	float floatValue;
	if (archive->FindFloat("_high", &floatValue) == B_OK) {
		fBarHeight = floatValue;
		fCustomBarHeight = true;
	}

	int32 color;
	if (archive->FindInt32("_bcolor", (int32 *)&color) == B_OK) {
		fBarColor = *(rgb_color *)&color;
		fInternalFlags |= kCustomBarColor;
	}

	if (archive->FindFloat("_val", &floatValue) == B_OK)
		fCurrent = floatValue;
	if (archive->FindFloat("_max", &floatValue) == B_OK)
		fMax = floatValue;
}


/**
 * @brief Destroy the BStatusBar.
 */
BStatusBar::~BStatusBar()
{
}


/**
 * @brief Create a new BStatusBar from an archived BMessage.
 *
 * @param archive  The archive message to instantiate from.
 * @return A new BStatusBar if \a archive is valid, or NULL if validation fails.
 * @see Archive()
 */
BArchivable *
BStatusBar::Instantiate(BMessage *archive)
{
	if (validate_instantiation(archive, "BStatusBar"))
		return new BStatusBar(archive);

	return NULL;
}


/**
 * @brief Archive this BStatusBar into a BMessage.
 *
 * Stores any non-default bar height, custom bar color, current value, maximum
 * value, text strings, and label strings into \a archive.
 *
 * @param archive  The message to archive into.
 * @param deep     Passed to BView::Archive(); no additional children are stored.
 * @return B_OK on success, or a negative error code on failure.
 * @see Instantiate()
 */
status_t
BStatusBar::Archive(BMessage *archive, bool deep) const
{
	status_t err = BView::Archive(archive, deep);
	if (err < B_OK)
		return err;

	if (fCustomBarHeight)
		err = archive->AddFloat("_high", fBarHeight);

	if (err == B_OK && fInternalFlags & kCustomBarColor)
		err = archive->AddInt32("_bcolor", (const uint32 &)fBarColor);

	if (err == B_OK && fCurrent != 0)
		err = archive->AddFloat("_val", fCurrent);
	if (err == B_OK && fMax != 100 )
		err = archive->AddFloat("_max", fMax);

	if (err == B_OK && fText.Length())
		err = archive->AddString("_text", fText);
	if (err == B_OK && fTrailingText.Length())
		err = archive->AddString("_ttext", fTrailingText);

	if (err == B_OK && fLabel.Length())
		err = archive->AddString("_label", fLabel);
	if (err == B_OK && fTrailingLabel.Length())
		err = archive->AddString ("_tlabel", fTrailingLabel);

	return err;
}


// #pragma mark -


/**
 * @brief Finish attaching the status bar to its window.
 *
 * Resizes the view height to its preferred value, sets a transparent view
 * color, adopts the parent's colors, initialises the text divider position,
 * and applies the system bar color if no application-specific color is set.
 */
void
BStatusBar::AttachedToWindow()
{
	// resize so that the height fits
	float width, height;
	GetPreferredSize(&width, &height);
	ResizeTo(Bounds().Width(), height);

	SetViewColor(B_TRANSPARENT_COLOR);

	AdoptParentColors();

	fTextDivider = Bounds().Width();

	if ((fInternalFlags & kCustomBarColor) == 0)
		fBarColor = ui_color(B_STATUS_BAR_COLOR);
}


/**
 * @brief Called when the view is detached from its window.
 *
 * Forwards the notification to BView::DetachedFromWindow().
 */
void
BStatusBar::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Called after all views in the hierarchy have been attached.
 *
 * Forwards the notification to BView::AllAttached().
 */
void
BStatusBar::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Called after all views in the hierarchy have been detached.
 *
 * Forwards the notification to BView::AllDetached().
 */
void
BStatusBar::AllDetached()
{
	BView::AllDetached();
}


// #pragma mark -


/**
 * @brief Called when the owning window is activated or deactivated.
 *
 * Forwards the notification to BView::WindowActivated().
 *
 * @param state  true if the window became active, false if it deactivated.
 */
void
BStatusBar::WindowActivated(bool state)
{
	BView::WindowActivated(state);
}


/**
 * @brief Forward a focus change to the base BView.
 *
 * @param state  true to acquire focus, false to release it.
 */
void
BStatusBar::MakeFocus(bool state)
{
	BView::MakeFocus(state);
}


// #pragma mark -


/**
 * @brief Report the natural (preferred) dimensions of the status bar.
 *
 * The preferred width is the sum of all label and text string widths plus a
 * small margin. The preferred height combines the (optional) label area with
 * the bar height.
 *
 * @param _width   If non-NULL, receives the preferred width in pixels.
 * @param _height  If non-NULL, receives the preferred height in pixels.
 */
void
BStatusBar::GetPreferredSize(float* _width, float* _height)
{
	if (_width) {
		// AttachedToWindow() might not have been called yet
		*_width = ceilf(StringWidth(fLabel.String()))
			+ ceilf(StringWidth(fTrailingLabel.String()))
			+ ceilf(StringWidth(fText.String()))
			+ ceilf(StringWidth(fTrailingText.String()))
			+ 5;
	}

	if (_height) {
		float labelHeight = 0;
		if (_HasText()) {
			font_height fontHeight;
			GetFontHeight(&fontHeight);
			labelHeight = ceilf(fontHeight.ascent + fontHeight.descent) + 6;
		}

		*_height = labelHeight + BarHeight();
	}
}


/**
 * @brief Return the minimum layout size of the status bar.
 *
 * @return The minimum BSize composed with any explicit minimum size.
 */
BSize
BStatusBar::MinSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), BSize(width, height));
}


/**
 * @brief Return the maximum layout size of the status bar.
 *
 * The height is fixed at the preferred height; the width is unlimited so the
 * bar can fill its container horizontally.
 *
 * @return The maximum BSize composed with any explicit maximum size.
 */
BSize
BStatusBar::MaxSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		BSize(B_SIZE_UNLIMITED, height));
}


/**
 * @brief Return the preferred layout size of the status bar.
 *
 * @return The preferred BSize composed with any explicit preferred size.
 */
BSize
BStatusBar::PreferredSize()
{
	float width, height;
	GetPreferredSize(&width, &height);

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		BSize(width, height));
}


/**
 * @brief Resize the view to its preferred dimensions.
 *
 * Forwards to BView::ResizeToPreferred().
 */
void
BStatusBar::ResizeToPreferred()
{
	BView::ResizeToPreferred();
}


/**
 * @brief Called when the view's frame origin changes.
 *
 * Forwards the notification to BView::FrameMoved().
 *
 * @param newPosition  The view's new top-left position in the parent's coordinates.
 */
void
BStatusBar::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/**
 * @brief Called when the view is resized; triggers a full repaint.
 *
 * @param newWidth   The view's new width.
 * @param newHeight  The view's new height.
 */
void
BStatusBar::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
	Invalidate();
}


// #pragma mark -


/**
 * @brief Draw the status bar including label text and the filled progress bar.
 *
 * Paints the label/text area above the bar (when text is present), then
 * delegates bar rendering to BControlLook::DrawStatusBar(). Unchanged
 * regions outside the bar frame are filled with the low (background) color.
 *
 * @param updateRect  The rectangle that needs repainting.
 */
void
BStatusBar::Draw(BRect updateRect)
{
	rgb_color backgroundColor = LowColor();

	font_height fontHeight;
	GetFontHeight(&fontHeight);
	BRect barFrame = _BarFrame(&fontHeight);
	BRect outerFrame = barFrame.InsetByCopy(-2, -2);

	BRegion background(updateRect);
	background.Exclude(outerFrame);
	FillRegion(&background, B_SOLID_LOW);

	// Draw labels/texts

	BRect rect = outerFrame;
	rect.top = 0;
	rect.bottom = outerFrame.top - 1;

	if (updateRect.Intersects(rect)) {
		// update labels
		BString leftText;
		leftText << fLabel << fText;

		BString rightText;
		rightText << fTrailingText << fTrailingLabel;

		float baseLine = ceilf(fontHeight.ascent) + 1;
		fTextDivider = rect.right;

		BFont font;
		GetFont(&font);

		if (rightText.Length()) {
			font.TruncateString(&rightText, B_TRUNCATE_BEGINNING,
				rect.Width());
			fTextDivider -= StringWidth(rightText.String());
		}

		if (leftText.Length()) {
			float width = max_c(0.0, fTextDivider - rect.left);
			font.TruncateString(&leftText, B_TRUNCATE_END, width);
		}

		rgb_color textColor = ui_color(B_PANEL_TEXT_COLOR);

		if (backgroundColor != ui_color(B_PANEL_BACKGROUND_COLOR)) {
			if (backgroundColor.IsLight())
				textColor = make_color(0, 0, 0, 255);
			else
				textColor = make_color(255, 255, 255, 255);
		}

		SetHighColor(textColor);

		if (leftText.Length())
			DrawString(leftText.String(), BPoint(rect.left, baseLine));

		if (rightText.Length())
			DrawString(rightText.String(), BPoint(fTextDivider, baseLine));
	}

	// Draw bar

	if (!updateRect.Intersects(outerFrame))
		return;

	rect = outerFrame;

	be_control_look->DrawStatusBar(this, rect, updateRect,
		backgroundColor, fBarColor, _BarPosition(barFrame));
}


/**
 * @brief Handle B_UPDATE_STATUS_BAR, B_RESET_STATUS_BAR, and B_COLORS_UPDATED messages.
 *
 * B_UPDATE_STATUS_BAR calls Update() with the delta and optional text strings.
 * B_RESET_STATUS_BAR calls Reset() with optional new label strings.
 * B_COLORS_UPDATED refreshes the bar color from the system palette when no
 * application-specific color has been set.
 *
 * @param message  The BMessage to process.
 */
void
BStatusBar::MessageReceived(BMessage *message)
{
	switch(message->what) {
		case B_UPDATE_STATUS_BAR:
		{
			float delta;
			const char *text = NULL, *trailing_text = NULL;

			message->FindFloat("delta", &delta);
			message->FindString("text", &text);
			message->FindString("trailing_text", &trailing_text);

			Update(delta, text, trailing_text);

			break;
		}

		case B_RESET_STATUS_BAR:
		{
			const char *label = NULL, *trailing_label = NULL;

			message->FindString("label", &label);
			message->FindString("trailing_label", &trailing_label);

			Reset(label, trailing_label);

			break;
		}

		case B_COLORS_UPDATED:
		{
			rgb_color color;
			if (message->FindColor(ui_color_name(B_STATUS_BAR_COLOR), &color) == B_OK) {
				// Change the bar color IF we don't have an application-set color.
				if ((fInternalFlags & kCustomBarColor) == 0)
					fBarColor = color;
			}

			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Forward a mouse-button-down event to the base BView.
 *
 * @param point  Mouse position in view coordinates.
 */
void
BStatusBar::MouseDown(BPoint point)
{
	BView::MouseDown(point);
}


/**
 * @brief Forward a mouse-button-up event to the base BView.
 *
 * @param point  Mouse position in view coordinates.
 */
void
BStatusBar::MouseUp(BPoint point)
{
	BView::MouseUp(point);
}


/**
 * @brief Forward a mouse-moved event to the base BView.
 *
 * @param point    Current mouse position in view coordinates.
 * @param transit  Entry/exit transit code.
 * @param message  Drag-and-drop message, or NULL if none.
 */
void
BStatusBar::MouseMoved(BPoint point, uint32 transit, const BMessage *message)
{
	BView::MouseMoved(point, transit, message);
}


// #pragma mark -


/**
 * @brief Set the fill color of the progress bar.
 *
 * Marks the color as application-defined so that subsequent B_COLORS_UPDATED
 * system notifications do not override it.
 *
 * @param color  The new bar fill color.
 */
void
BStatusBar::SetBarColor(rgb_color color)
{
	fInternalFlags |= kCustomBarColor;
	fBarColor = color;

	Invalidate();
}


/**
 * @brief Set the pixel height of the filled bar region.
 *
 * Stores a custom bar height and resizes the view (or invalidates the layout)
 * so the new height is reflected immediately.
 *
 * @param barHeight  The desired bar height in pixels.
 */
void
BStatusBar::SetBarHeight(float barHeight)
{
	float oldHeight = BarHeight();

	fCustomBarHeight = true;
	fBarHeight = barHeight;

	if (barHeight == oldHeight)
		return;

	// resize so that the height fits
	if ((Flags() & B_SUPPORTS_LAYOUT) != 0)
		InvalidateLayout();
	else {
		float width, height;
		GetPreferredSize(&width, &height);
		ResizeTo(Bounds().Width(), height);
	}
}


/**
 * @brief Set the dynamic sub-label displayed to the left of the bar.
 *
 * The sub-text is combined with the static left label for display. Triggers
 * a layout invalidation if the presence of text changes.
 *
 * @param string  The new sub-label text, or NULL to clear it.
 */
void
BStatusBar::SetText(const char* string)
{
	_SetTextData(fText, string, fLabel, false);
}


/**
 * @brief Set the dynamic sub-label displayed to the right of the bar.
 *
 * The trailing sub-text is combined with the static trailing label for display.
 *
 * @param string  The new trailing sub-label text, or NULL to clear it.
 */
void
BStatusBar::SetTrailingText(const char* string)
{
	_SetTextData(fTrailingText, string, fTrailingLabel, true);
}


/**
 * @brief Set the maximum value of the progress bar.
 *
 * @param max  The new maximum. The bar is full when CurrentValue() == max.
 * @note For binary-compatibility reasons this method does not invalidate the
 *       view; the visual update occurs the next time Update() or SetTo() is
 *       called.
 */
void
BStatusBar::SetMaxValue(float max)
{
	// R5 and/or Zeta's SetMaxValue does not trigger an invalidate here.
	// this is probably not ideal behavior, but it does break apps in some cases
	// as observed with SpaceMonitor.
	// TODO: revisit this when we break binary compatibility
	fMax = max;
}


/**
 * @brief Advance the progress bar by \a delta and optionally update its text labels.
 *
 * If \a text or \a trailingText is NULL the existing string is preserved.
 * Delegates to SetTo() after computing the new value.
 *
 * @param delta        Amount to add to the current value.
 * @param text         New left sub-label, or NULL to keep the current one.
 * @param trailingText New right sub-label, or NULL to keep the current one.
 */
void
BStatusBar::Update(float delta, const char* text, const char* trailingText)
{
	// If any of these are NULL, the existing text remains (BeBook)
	if (text == NULL)
		text = fText.String();
	if (trailingText == NULL)
		trailingText = fTrailingText.String();
	BStatusBar::SetTo(fCurrent + delta, text, trailingText);
}


/**
 * @brief Reset the bar to zero and optionally replace the static labels.
 *
 * Clears both dynamic text strings, resets the current value to 0 and the
 * maximum to 100, then triggers a full repaint.
 *
 * @param label          Replacement left label, or NULL to clear it.
 * @param trailingLabel  Replacement right label, or NULL to clear it.
 */
void
BStatusBar::Reset(const char *label, const char *trailingLabel)
{
	// Reset replaces the label and trailing label with copies of the
	// strings passed as arguments. If either argument is NULL, the
	// label or trailing label will be deleted and erased.
	fLabel = label ? label : "";
	fTrailingLabel = trailingLabel ? trailingLabel : "";

	// Reset deletes and erases any text or trailing text
	fText = "";
	fTrailingText = "";

	fCurrent = 0;
	fMax = 100;

	Invalidate();
}


/**
 * @brief Set the current progress value and text labels, then repaint the changed area.
 *
 * Clamps \a value to [0, fMax], computes the old and new fill positions, and
 * invalidates only the portion of the bar rect that changed, minimising
 * unnecessary redrawing.
 *
 * @param value        The new absolute progress value (clamped to [0, MaxValue()]).
 * @param text         New left sub-label, or NULL to clear it.
 * @param trailingText New right sub-label, or NULL to clear it.
 */
void
BStatusBar::SetTo(float value, const char* text, const char* trailingText)
{
	SetText(text);
	SetTrailingText(trailingText);

	if (value > fMax)
		value = fMax;
	else if (value < 0)
		value = 0;
	if (value == fCurrent)
		return;

	BRect barFrame = _BarFrame();
	float oldPosition = _BarPosition(barFrame);

	fCurrent = value;

	float newPosition = _BarPosition(barFrame);
	if (oldPosition == newPosition)
		return;

	// update only the part of the status bar with actual changes
	BRect update = barFrame;
	if (oldPosition < newPosition) {
		update.left = floorf(max_c(oldPosition - 1, update.left));
		update.right = ceilf(newPosition);
	} else {
		update.left = floorf(max_c(newPosition - 1, update.left));
		update.right = ceilf(oldPosition);
	}

	// TODO: Ask the BControlLook in the first place about dirty rect.
	update.InsetBy(-1, -1);

	Invalidate(update);
}


/**
 * @brief Return the current progress value.
 *
 * @return The value last set by SetTo() or Update(), in the range [0, MaxValue()].
 */
float
BStatusBar::CurrentValue() const
{
	return fCurrent;
}


/**
 * @brief Return the maximum value of the progress bar.
 *
 * @return The maximum value; the bar is full when CurrentValue() equals this.
 */
float
BStatusBar::MaxValue() const
{
	return fMax;
}


/**
 * @brief Return the bar fill color.
 *
 * @return The rgb_color used to fill the filled portion of the bar.
 */
rgb_color
BStatusBar::BarColor() const
{
	return fBarColor;
}


/**
 * @brief Return the pixel height of the progress bar region.
 *
 * When no custom height has been set, the height is computed from the current
 * font metrics (ascent + descent + 5) and cached for subsequent calls.
 *
 * @return The bar height in pixels, always rounded up to the nearest integer.
 */
float
BStatusBar::BarHeight() const
{
	if (!fCustomBarHeight && fBarHeight == -1) {
		// the default bar height is as height as the label
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		const_cast<BStatusBar *>(this)->fBarHeight = fontHeight.ascent
			+ fontHeight.descent + 5;
	}

	return ceilf(fBarHeight);
}


/**
 * @brief Return the dynamic sub-label displayed to the left.
 *
 * @return The string set by SetText(), or an empty string if none is set.
 */
const char *
BStatusBar::Text() const
{
	return fText.String();
}


/**
 * @brief Return the dynamic sub-label displayed to the right.
 *
 * @return The string set by SetTrailingText(), or an empty string if none is set.
 */
const char *
BStatusBar::TrailingText() const
{
	return fTrailingText.String();
}


/**
 * @brief Return the static left-side label.
 *
 * @return The label string supplied at construction or via Reset().
 */
const char *
BStatusBar::Label() const
{
	return fLabel.String();
}


/**
 * @brief Return the static right-side label.
 *
 * @return The trailing label string supplied at construction or via Reset().
 */
const char *
BStatusBar::TrailingLabel() const
{
	return fTrailingLabel.String();
}


// #pragma mark -


/**
 * @brief Resolve a scripting specifier to the appropriate handler.
 *
 * Forwards all specifiers to BView::ResolveSpecifier(); BStatusBar does not
 * expose additional scripting properties.
 *
 * @param message    The scripting message containing the specifier chain.
 * @param index      Index of the current specifier in the chain.
 * @param specifier  The current specifier message.
 * @param what       The specifier form constant.
 * @param property   The property name string.
 * @return The result of BView::ResolveSpecifier().
 */
BHandler *
BStatusBar::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char *property)
{
	return BView::ResolveSpecifier(message, index, specifier, what, property);
}


/**
 * @brief Add the supported scripting suites to a BMessage.
 *
 * Forwards to BView::GetSupportedSuites(); no additional suites are defined.
 *
 * @param data  The message to populate with suite information.
 * @return The result of BView::GetSupportedSuites().
 */
status_t
BStatusBar::GetSupportedSuites(BMessage* data)
{
	return BView::GetSupportedSuites(data);
}


/**
 * @brief Dispatch a low-level perform code to the appropriate virtual method.
 *
 * Handles the standard set of layout perform codes (MinSize, MaxSize,
 * PreferredSize, LayoutAlignment, HasHeightForWidth, GetHeightForWidth,
 * SetLayout, LayoutInvalidated, DoLayout) by calling the corresponding
 * BStatusBar methods directly. Unknown codes are forwarded to BView::Perform().
 *
 * @param code   One of the PERFORM_CODE_* constants.
 * @param _data  Pointer to the perform-specific data structure.
 * @return B_OK if the code was handled, otherwise the result of BView::Perform().
 */
status_t
BStatusBar::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BStatusBar::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BStatusBar::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BStatusBar::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BStatusBar::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BStatusBar::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BStatusBar::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BStatusBar::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BStatusBar::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BStatusBar::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


// #pragma mark -


extern "C" void
_ReservedStatusBar1__10BStatusBar(BStatusBar* self, float value,
	const char* text, const char* trailingText)
{
	self->BStatusBar::SetTo(value, text, trailingText);
}


void BStatusBar::_ReservedStatusBar2() {}
void BStatusBar::_ReservedStatusBar3() {}
void BStatusBar::_ReservedStatusBar4() {}


BStatusBar &
BStatusBar::operator=(const BStatusBar& other)
{
	return *this;
}


// #pragma mark -


/**
 * @brief Initialise all member variables to their default values.
 *
 * Called from every constructor. Sets fMax to 100, fCurrent to 0, fBarHeight
 * to -1 (auto), and enables B_FRAME_EVENTS so FrameResized() is called.
 */
void
BStatusBar::_InitObject()
{
	fMax = 100.0;
	fCurrent = 0.0;

	fBarHeight = -1.0;
	fTextDivider = Bounds().Width();

	fCustomBarHeight = false;
	fInternalFlags = 0;

	SetFlags(Flags() | B_FRAME_EVENTS);
}


/**
 * @brief Update one of the dynamic text strings and trigger a partial repaint.
 *
 * Compares the new string against the stored value; if different, updates
 * the string, conditionally invalidates the layout (when text presence
 * changes), and repaints the label area above the bar.
 *
 * @param text          The BString member to update (fText or fTrailingText).
 * @param source        The new string value, or NULL to set an empty string.
 * @param combineWith   The companion static label to concatenate for width
 *                      measurement (fLabel or fTrailingLabel).
 * @param rightAligned  If true, \a text precedes \a combineWith in the display
 *                      order (trailing side); otherwise the order is reversed.
 */
void
BStatusBar::_SetTextData(BString& text, const char* source,
	const BString& combineWith, bool rightAligned)
{
	if (source == NULL)
		source = "";

	// If there were no changes, we don't have to do anything
	if (text == source)
		return;

	bool oldHasText = _HasText();
	text = source;

	BString newString;
	if (rightAligned)
		newString << text << combineWith;
	else
		newString << combineWith << text;

	if (oldHasText != _HasText())
		InvalidateLayout();

	font_height fontHeight;
	GetFontHeight(&fontHeight);

//	Invalidate(BRect(position, 0, position + invalidateWidth,
//		ceilf(fontHeight.ascent) + ceilf(fontHeight.descent)));
// TODO: redrawing the entire area takes care of the edge case
// where the left side string changes because of truncation and
// part of it needs to be redrawn as well.
	Invalidate(BRect(0, 0, Bounds().right,
		ceilf(fontHeight.ascent) + ceilf(fontHeight.descent)));
}


/**
 * @brief Return the inner bar frame excluding the surrounding two-pixel bevel.
 *
 * The top edge is positioned below the label area when text is present;
 * otherwise it starts two pixels from the top. The optional \a fontHeight
 * pointer avoids a redundant GetFontHeight() call when the caller already
 * has the metrics.
 *
 * @param fontHeight  Pre-fetched font metrics, or NULL to fetch them internally.
 * @return The inner BRect of the progress bar fill area.
 */
BRect
BStatusBar::_BarFrame(const font_height* fontHeight) const
{
	float top = 2;
	if (_HasText()) {
		if (fontHeight == NULL) {
			font_height height;
			GetFontHeight(&height);
			top = ceilf(height.ascent + height.descent) + 6;
		} else
			top = ceilf(fontHeight->ascent + fontHeight->descent) + 6;
	}

	return BRect(2, top, Bounds().right - 2, top + BarHeight() - 4);
}


/**
 * @brief Compute the x-coordinate of the right edge of the filled portion.
 *
 * Maps the current progress value linearly onto the bar frame width.
 * Returns barFrame.left - 1 (one pixel left of the bar) when fCurrent is 0,
 * indicating an empty bar.
 *
 * @param barFrame  The inner bar frame returned by _BarFrame().
 * @return The pixel x-position of the fill boundary.
 */
float
BStatusBar::_BarPosition(const BRect& barFrame) const
{
	if (fCurrent == 0)
		return barFrame.left - 1;

	return roundf(barFrame.left - 1
		+ (fCurrent * (barFrame.Width() + 3) / fMax));
}


/**
 * @brief Return whether the view should reserve vertical space for the label area.
 *
 * In legacy (non-layout) mode this always returns true to preserve the BeOS
 * R5 fixed-height behavior. In layout mode it returns true only when at least
 * one of the four text strings is non-empty.
 *
 * @return true if label/text area space should be included in the preferred height.
 */
bool
BStatusBar::_HasText() const
{
	// Force BeOS behavior where the size of the BStatusBar always included
	// room for labels, even when there weren't any.
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		return true;
	return fLabel.Length() > 0 || fTrailingLabel.Length() > 0
		|| fTrailingText.Length() > 0 || fText.Length() > 0;
}
