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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Frans van Nispen (xlr8@tref.nl)
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file StringView.cpp
 * @brief Implementation of BStringView, a non-editable text display view
 *
 * BStringView draws a single line of static text in the current view font. It
 * supports horizontal alignment and automatically resizes to fit the text when
 * layout-managed.
 *
 * @see BView, BFont, BControlLook
 */


#include <StringView.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <LayoutUtils.h>
#include <Message.h>
#include <PropertyInfo.h>
#include <StringList.h>
#include <View.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/** @brief Scripting property table for BStringView exposing Text and Alignment. */
static property_info sPropertyList[] = {
	{
		"Text",
		{ B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
		{ B_STRING_TYPE }
	},
	{
		"Alignment",
		{ B_GET_PROPERTY, B_SET_PROPERTY },
		{ B_DIRECT_SPECIFIER },
		NULL, 0,
		{ B_INT32_TYPE }
	},

	{ 0 }
};


/**
 * @brief Construct a frame-based BStringView.
 *
 * @param frame        Initial bounds rectangle in the parent's coordinate system.
 * @param name         Internal view name used for lookup and archiving.
 * @param text         Static text to display; a copy is made. May be NULL.
 * @param resizingMode BView resizing flags (e.g. B_FOLLOW_LEFT | B_FOLLOW_TOP).
 * @param flags        Additional view flags; B_FULL_UPDATE_ON_RESIZE is always set.
 */
BStringView::BStringView(BRect frame, const char* name, const char* text,
	uint32 resizingMode, uint32 flags)
	:
	BView(frame, name, resizingMode, flags | B_FULL_UPDATE_ON_RESIZE),
	fText(text ? strdup(text) : NULL),
	fTruncation(B_NO_TRUNCATION),
	fAlign(B_ALIGN_LEFT),
	fPreferredSize(text ? _StringWidth(text) : 0.0, -1)
{
}


/**
 * @brief Construct a layout-managed BStringView without an explicit frame.
 *
 * @param name   Internal view name.
 * @param text   Static text to display; a copy is made. May be NULL.
 * @param flags  Additional view flags; B_FULL_UPDATE_ON_RESIZE is always set.
 */
BStringView::BStringView(const char* name, const char* text, uint32 flags)
	:
	BView(name, flags | B_FULL_UPDATE_ON_RESIZE),
	fText(text ? strdup(text) : NULL),
	fTruncation(B_NO_TRUNCATION),
	fAlign(B_ALIGN_LEFT),
	fPreferredSize(text ? _StringWidth(text) : 0.0, -1)
{
}


/**
 * @brief Unarchive constructor: restore a BStringView from a BMessage.
 *
 * Reads the text, alignment, and truncation mode from \a archive.
 *
 * @param archive  The archive message produced by Archive().
 * @see Instantiate(), Archive()
 */
BStringView::BStringView(BMessage* archive)
	:
	BView(archive),
	fText(NULL),
	fTruncation(B_NO_TRUNCATION),
	fPreferredSize(0, -1)
{
	fAlign = (alignment)archive->GetInt32("_align", B_ALIGN_LEFT);
	fTruncation = (uint32)archive->GetInt32("_truncation", B_NO_TRUNCATION);

	const char* text = archive->GetString("_text", NULL);

	SetText(text);
	SetFlags(Flags() | B_FULL_UPDATE_ON_RESIZE);
}


/**
 * @brief Destroy the BStringView and free the text buffer.
 */
BStringView::~BStringView()
{
	free(fText);
}


// #pragma mark - Archiving methods


/**
 * @brief Create a new BStringView from an archived BMessage.
 *
 * @param data  The archive message to instantiate from.
 * @return A new BStringView if \a data is valid, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BStringView::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "BStringView"))
		return NULL;

	return new BStringView(data);
}


/**
 * @brief Archive this BStringView into a BMessage.
 *
 * Stores the text string, truncation mode (if not B_NO_TRUNCATION), and
 * alignment flag into \a data.
 *
 * @param data  The message to archive into.
 * @param deep  Passed to BView::Archive(); no additional children are stored.
 * @return B_OK on success, or a negative error code on failure.
 * @see Instantiate()
 */
status_t
BStringView::Archive(BMessage* data, bool deep) const
{
	status_t status = BView::Archive(data, deep);

	if (status == B_OK && fText)
		status = data->AddString("_text", fText);
	if (status == B_OK && fTruncation != B_NO_TRUNCATION)
		status = data->AddInt32("_truncation", fTruncation);
	if (status == B_OK)
		status = data->AddInt32("_align", fAlign);

	return status;
}


// #pragma mark - Hook methods


/**
 * @brief Synchronise the view's colors with those of its parent when attached.
 *
 * Inherits the parent's UI color (or explicit view color) so the label blends
 * seamlessly with its background. If no parent color is available and the view
 * is transparent, AdoptSystemColors() is used as a fallback.
 */
void
BStringView::AttachedToWindow()
{
	if (HasDefaultColors())
		SetHighUIColor(B_PANEL_TEXT_COLOR);

	BView* parent = Parent();

	if (parent != NULL) {
		float tint = B_NO_TINT;
		color_which which = parent->ViewUIColor(&tint);

		if (which != B_NO_COLOR) {
			SetViewUIColor(which, tint);
			SetLowUIColor(which, tint);
		} else {
			SetViewColor(parent->ViewColor());
			SetLowColor(ViewColor());
		}
	}

	if (ViewColor() == B_TRANSPARENT_COLOR)
		AdoptSystemColors();
}


/**
 * @brief Called when the view is detached from its window.
 *
 * Forwards the notification to BView::DetachedFromWindow().
 */
void
BStringView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Called after all views in the hierarchy have been attached.
 *
 * Forwards the notification to BView::AllAttached().
 */
void
BStringView::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Called after all views in the hierarchy have been detached.
 *
 * Forwards the notification to BView::AllDetached().
 */
void
BStringView::AllDetached()
{
	BView::AllDetached();
}


// #pragma mark - Layout methods


/**
 * @brief Forward a focus change to the base BView.
 *
 * BStringView does not render a focus indicator, but the notification is
 * forwarded for completeness.
 *
 * @param focus  true if focus is being acquired, false if released.
 */
void
BStringView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
}


/**
 * @brief Report the natural (preferred) dimensions of the view.
 *
 * Validates and returns the cached preferred size, which is wide enough to
 * display the full text without truncation and tall enough for the current font.
 *
 * @param _width   If non-NULL, receives the preferred width in pixels.
 * @param _height  If non-NULL, receives the preferred height in pixels.
 */
void
BStringView::GetPreferredSize(float* _width, float* _height)
{
	_ValidatePreferredSize();

	if (_width)
		*_width = fPreferredSize.width;

	if (_height)
		*_height = fPreferredSize.height;
}


/**
 * @brief Return the minimum size needed to display the view.
 *
 * Composes the validated preferred size with any explicit minimum size set
 * on the view via BView::SetExplicitMinSize().
 *
 * @return The minimum BSize for this view.
 */
BSize
BStringView::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Return the maximum size of the view.
 *
 * Composes the validated preferred size with any explicit maximum size, so
 * the view does not grow beyond what is needed to display its text.
 *
 * @return The maximum BSize for this view.
 */
BSize
BStringView::MaxSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Return the preferred layout size of the view.
 *
 * @return The preferred BSize composed with any explicit preferred size.
 */
BSize
BStringView::PreferredSize()
{
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(),
		_ValidatePreferredSize());
}


/**
 * @brief Resize the view to exactly fit its text content.
 *
 * For non-left-aligned views the existing width is preserved when it already
 * exceeds the natural text width, preventing unnecessary shrinking.
 */
void
BStringView::ResizeToPreferred()
{
	float width, height;
	GetPreferredSize(&width, &height);

	// Resize the width only for B_ALIGN_LEFT (if its large enough already, that is)
	if (Bounds().Width() > width && Alignment() != B_ALIGN_LEFT)
		width = Bounds().Width();

	BView::ResizeTo(width, height);
}


/**
 * @brief Return the layout alignment hint for this view.
 *
 * Composes the horizontal alignment (from fAlign) with a vertical
 * B_ALIGN_MIDDLE hint so layout managers can position the view correctly.
 *
 * @return A BAlignment combining the text alignment and vertical centering.
 */
BAlignment
BStringView::LayoutAlignment()
{
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(),
		BAlignment(fAlign, B_ALIGN_MIDDLE));
}


// #pragma mark - More hook methods


/**
 * @brief Called when the view's frame origin changes.
 *
 * Forwards the notification to BView::FrameMoved().
 *
 * @param newPosition  The view's new top-left position in the parent's coordinates.
 */
void
BStringView::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/**
 * @brief Called when the view's frame is resized.
 *
 * Forwards the notification to BView::FrameResized().
 *
 * @param newWidth   The view's new width.
 * @param newHeight  The view's new height.
 */
void
BStringView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);
}


/**
 * @brief Draw the text string within the view's bounds.
 *
 * Supports multi-line text (newline-delimited), horizontal alignment
 * (left/center/right), and optional mid-truncation when the text is wider
 * than the view bounds.
 *
 * @param updateRect  The portion of the view that needs repainting.
 */
void
BStringView::Draw(BRect updateRect)
{
	if (!fText)
		return;

	if (LowUIColor() == B_NO_COLOR)
		SetLowColor(ViewColor());

	font_height fontHeight;
	GetFontHeight(&fontHeight);

	BRect bounds = Bounds();

	BStringList lines;
	BString(fText).Split("\n", false, lines);
	for (int i = 0; i < lines.CountStrings(); i++) {
		const char* text = lines.StringAt(i).String();
		float width = StringWidth(text);
		BString truncated;
		if (fTruncation != B_NO_TRUNCATION && width > bounds.Width()) {
			// The string needs to be truncated
			// TODO: we should cache this
			truncated = lines.StringAt(i);
			TruncateString(&truncated, fTruncation, bounds.Width());
			text = truncated.String();
			width = StringWidth(text);
		}

		float y = (bounds.top + bounds.bottom - ceilf(fontHeight.descent))
			- ceilf(fontHeight.ascent + fontHeight.descent + fontHeight.leading)
				* (lines.CountStrings() - i - 1);
		float x;
		switch (fAlign) {
			case B_ALIGN_RIGHT:
				x = bounds.Width() - width;
				break;

			case B_ALIGN_CENTER:
				x = (bounds.Width() - width) / 2.0;
				break;

			default:
				x = 0.0;
				break;
		}

		DrawString(text, BPoint(x, y));
	}
}


/**
 * @brief Handle scripting messages for the Text and Alignment properties.
 *
 * Processes B_GET_PROPERTY and B_SET_PROPERTY scripting messages for the
 * "Text" and "Alignment" properties. Unknown messages are forwarded to
 * BView::MessageReceived().
 *
 * @param message  The scripting message to handle.
 */
void
BStringView::MessageReceived(BMessage* message)
{
	if (message->what == B_GET_PROPERTY || message->what == B_SET_PROPERTY) {
		int32 index;
		BMessage specifier;
		int32 form;
		const char* property;
		if (message->GetCurrentSpecifier(&index, &specifier, &form, &property)
				!= B_OK) {
			BView::MessageReceived(message);
			return;
		}

		BMessage reply(B_REPLY);
		bool handled = false;
		if (strcmp(property, "Text") == 0) {
			if (message->what == B_GET_PROPERTY) {
				reply.AddString("result", fText);
				handled = true;
			} else {
				const char* text;
				if (message->FindString("data", &text) == B_OK) {
					SetText(text);
					reply.AddInt32("error", B_OK);
					handled = true;
				}
			}
		} else if (strcmp(property, "Alignment") == 0) {
			if (message->what == B_GET_PROPERTY) {
				reply.AddInt32("result", (int32)fAlign);
				handled = true;
			} else {
				int32 align;
				if (message->FindInt32("data", &align) == B_OK) {
					SetAlignment((alignment)align);
					reply.AddInt32("error", B_OK);
					handled = true;
				}
			}
		}

		if (handled) {
			message->SendReply(&reply);
			return;
		}
	}

	BView::MessageReceived(message);
}


/**
 * @brief Forward a mouse-button-down event to the base BView.
 *
 * @param point  Mouse position in view coordinates.
 */
void
BStringView::MouseDown(BPoint point)
{
	BView::MouseDown(point);
}


/**
 * @brief Forward a mouse-button-up event to the base BView.
 *
 * @param point  Mouse position in view coordinates.
 */
void
BStringView::MouseUp(BPoint point)
{
	BView::MouseUp(point);
}


/**
 * @brief Forward a mouse-moved event to the base BView.
 *
 * @param point    Current mouse position in view coordinates.
 * @param transit  Entry/exit transit code.
 * @param msg      Drag-and-drop message, or NULL if none.
 */
void
BStringView::MouseMoved(BPoint point, uint32 transit, const BMessage* msg)
{
	BView::MouseMoved(point, transit, msg);
}


// #pragma mark -


/**
 * @brief Change the displayed text string.
 *
 * Frees the old string, stores a copy of \a text, and invalidates the layout
 * if the preferred width changes. Triggers a visual refresh unconditionally.
 *
 * @param text  New text to display, or NULL to clear the label.
 */
void
BStringView::SetText(const char* text)
{
	if ((text && fText && !strcmp(text, fText)) || (!text && !fText))
		return;

	free(fText);
	fText = text ? strdup(text) : NULL;

	float newStringWidth = _StringWidth(fText);
	if (fPreferredSize.width != newStringWidth) {
		fPreferredSize.width = newStringWidth;
		InvalidateLayout();
	}

	Invalidate();
}


/**
 * @brief Return the currently displayed text string.
 *
 * @return A pointer to the internal text buffer, or NULL if no text is set.
 */
const char*
BStringView::Text() const
{
	return fText;
}


/**
 * @brief Set the horizontal alignment of the text within the view.
 *
 * @param flag  One of B_ALIGN_LEFT, B_ALIGN_CENTER, or B_ALIGN_RIGHT.
 */
void
BStringView::SetAlignment(alignment flag)
{
	fAlign = flag;
	Invalidate();
}


/**
 * @brief Return the current horizontal text alignment.
 *
 * @return The alignment value set by SetAlignment().
 */
alignment
BStringView::Alignment() const
{
	return fAlign;
}


/**
 * @brief Set the truncation mode used when the text is wider than the view.
 *
 * @param truncationMode  One of the B_TRUNCATE_* constants, or B_NO_TRUNCATION
 *                        to disable truncation entirely.
 */
void
BStringView::SetTruncation(uint32 truncationMode)
{
	if (fTruncation != truncationMode) {
		fTruncation = truncationMode;
		Invalidate();
	}
}


/**
 * @brief Return the current truncation mode.
 *
 * @return The truncation constant set by SetTruncation().
 */
uint32
BStringView::Truncation() const
{
	return fTruncation;
}


/**
 * @brief Resolve a scripting specifier to the appropriate handler.
 *
 * Returns this view when the specifier matches a supported property (Text or
 * Alignment); otherwise falls through to BView::ResolveSpecifier().
 *
 * @param message    The scripting message containing the specifier chain.
 * @param index      Index of the current specifier in the chain.
 * @param specifier  The current specifier message.
 * @param form       The specifier form constant.
 * @param property   The property name string.
 * @return This BStringView, or the result of BView::ResolveSpecifier().
 */
BHandler*
BStringView::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 form, const char* property)
{
	BPropertyInfo propInfo(sPropertyList);
	if (propInfo.FindMatch(message, 0, specifier, form, property) >= B_OK)
		return this;

	return BView::ResolveSpecifier(message, index, specifier, form, property);
}


/**
 * @brief Add the supported scripting suites to a BMessage.
 *
 * Appends the "suite/vnd.Be-string-view" suite name and the property
 * descriptor list to \a data, then forwards to BView::GetSupportedSuites().
 *
 * @param data  The message to populate with suite information.
 * @return B_OK on success, B_BAD_VALUE if \a data is NULL, or an error code.
 */
status_t
BStringView::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = data->AddString("suites", "suite/vnd.Be-string-view");
	if (status != B_OK)
		return status;

	BPropertyInfo propertyInfo(sPropertyList);
	status = data->AddFlat("messages", &propertyInfo);
	if (status != B_OK)
		return status;

	return BView::GetSupportedSuites(data);
}


/**
 * @brief Override the view font and update the cached preferred width.
 *
 * After forwarding to BView::SetFont(), the preferred size width is
 * recalculated and both the visual content and layout are invalidated.
 *
 * @param font  The new font to apply.
 * @param mask  Bitmask of font properties to change (see BView::SetFont()).
 */
void
BStringView::SetFont(const BFont* font, uint32 mask)
{
	BView::SetFont(font, mask);

	fPreferredSize.width = _StringWidth(fText);

	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Invalidate the cached preferred height when the layout is invalidated.
 *
 * Sets fPreferredSize.height to -1 so that the next call to
 * _ValidatePreferredSize() recomputes the height from the current font metrics.
 *
 * @param descendants  True if descendant layouts were also invalidated.
 */
void
BStringView::LayoutInvalidated(bool descendants)
{
	// invalidate cached preferred size
	fPreferredSize.height = -1;
}


// #pragma mark - Perform


/**
 * @brief Dispatch a low-level perform code to the appropriate virtual method.
 *
 * Handles the standard set of layout perform codes (MinSize, MaxSize,
 * PreferredSize, LayoutAlignment, HasHeightForWidth, GetHeightForWidth,
 * SetLayout, LayoutInvalidated, DoLayout) by calling the corresponding
 * BStringView methods directly. Unknown codes are forwarded to BView::Perform().
 *
 * @param code   One of the PERFORM_CODE_* constants.
 * @param _data  Pointer to the perform-specific data structure.
 * @return B_OK if the code was handled, otherwise the result of BView::Perform().
 */
status_t
BStringView::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BStringView::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BStringView::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BStringView::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BStringView::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BStringView::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BStringView::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BStringView::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BStringView::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BStringView::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


// #pragma mark - FBC padding methods


void BStringView::_ReservedStringView1() {}
void BStringView::_ReservedStringView2() {}
void BStringView::_ReservedStringView3() {}


// #pragma mark - Private methods


BStringView&
BStringView::operator=(const BStringView&)
{
	// Assignment not allowed (private)
	return *this;
}


/**
 * @brief Compute and cache the preferred size from the current font metrics and text.
 *
 * If the cached height is invalid (< 0), recalculates it from the font ascent,
 * descent, and leading scaled by the number of newline-delimited lines. The
 * width is always kept in sync by SetText() and SetFont().
 *
 * @return The validated preferred BSize.
 */
BSize
BStringView::_ValidatePreferredSize()
{
	if (fPreferredSize.height < 0) {
		// height
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		int32 lines = 1;
		char* temp = fText ? strchr(fText, '\n') : NULL;
		while (temp != NULL) {
			temp = strchr(temp + 1, '\n');
			lines++;
		};

		fPreferredSize.height = ceilf(fontHeight.ascent + fontHeight.descent
			+ fontHeight.leading) * lines;

		ResetLayoutInvalidation();
	}

	return fPreferredSize;
}


/**
 * @brief Measure the pixel width of the widest line in the text string.
 *
 * Splits the text on newlines and returns the maximum StringWidth() across
 * all lines. Returns 0 if \a text is NULL.
 *
 * @param text  The string to measure; may be NULL.
 * @return The width in pixels of the widest line.
 */
float
BStringView::_StringWidth(const char* text)
{
	if(text == NULL)
		return 0.0f;

	float maxWidth = 0.0f;
	BStringList lines;
	BString(fText).Split("\n", false, lines);
	for (int i = 0; i < lines.CountStrings(); i++) {
		float width = StringWidth(lines.StringAt(i));
		if (maxWidth < width)
			maxWidth = width;
	}
	return maxWidth;
}


extern "C" void
B_IF_GCC_2(InvalidateLayout__11BStringViewb,
	_ZN11BStringView16InvalidateLayoutEb)(BView* view, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	view->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
