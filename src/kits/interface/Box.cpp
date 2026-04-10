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
 *   Copyright 2001-2022 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Axel Dörfler, axeld@pinc-software.de
 *       Marc Flerackers, mflerackers@androme.be
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file Box.cpp
 * @brief Implementation of BBox, a decorative grouping container with an optional label
 *
 * BBox draws a labeled border around a group of child views. It supports plain
 * borders, beveled borders, and etched borders, as well as a label that can be
 * a text string or an arbitrary BView.
 *
 * @see BView, BGroupLayout
 */


#include <Box.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ControlLook.h>
#include <Layout.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <Region.h>

#include <binary_compatibility/Interface.h>


/** @brief Cached layout metrics computed by _ValidateLayoutData(). */
struct BBox::LayoutData {
	LayoutData()
		: valid(false)
	{
	}

	BRect	label_box;		/**< @brief Bounding box of the label string or label view;
	                         *         for a string label, excludes the descent. */
	BRect	insets;			/**< @brief Per-edge insets induced by the border style and label
	                         *         height. */
	BSize	min;            /**< @brief Minimum layout size. */
	BSize	max;            /**< @brief Maximum layout size. */
	BSize	preferred;      /**< @brief Preferred layout size. */
	BAlignment alignment;   /**< @brief Layout alignment derived from child constraints. */
	bool	valid;			/**< @brief True when the fields above are up to date. */
};


/**
 * @brief Construct a BBox with a legacy frame rectangle.
 *
 * @param frame        The position and size of the box in its parent's
 *                     coordinate system.
 * @param name         The view name.
 * @param resizingMode Combination of B_FOLLOW_* flags.
 * @param flags        Additional view flags; B_WILL_DRAW and B_FRAME_EVENTS are
 *                     always added.
 * @param border       The initial border style (B_FANCY_BORDER by default in
 *                     the header, but specified explicitly here).
 */
BBox::BBox(BRect frame, const char* name, uint32 resizingMode, uint32 flags,
		border_style border)
	:
	BView(frame, name, resizingMode, flags  | B_WILL_DRAW | B_FRAME_EVENTS),
	  fStyle(border)
{
	_InitObject();
}


/**
 * @brief Construct a layout-aware BBox without an explicit frame.
 *
 * @param name   The view name.
 * @param flags  Additional view flags; B_WILL_DRAW and B_FRAME_EVENTS are
 *               always added.
 * @param border The initial border style.
 * @param child  An optional first child view. If non-NULL it is added
 *               immediately via AddChild().
 */
BBox::BBox(const char* name, uint32 flags, border_style border, BView* child)
	:
	BView(name, flags | B_WILL_DRAW | B_FRAME_EVENTS),
	fStyle(border)
{
	_InitObject();

	if (child)
		AddChild(child);
}


/**
 * @brief Construct a BBox suitable as a separator or navigational jump target.
 *
 * Uses B_NAVIGABLE_JUMP so that Tab navigation can use the box as a
 * group boundary. No frame is specified; the layout engine sets the size.
 *
 * @param border The initial border style.
 * @param child  An optional first child view.
 */
BBox::BBox(border_style border, BView* child)
	:
	BView(NULL, B_WILL_DRAW | B_FRAME_EVENTS | B_NAVIGABLE_JUMP),
	fStyle(border)
{
	_InitObject();

	if (child)
		AddChild(child);
}


/**
 * @brief Construct a BBox from an archived BMessage.
 *
 * @param archive The archive message produced by Archive().
 * @see Instantiate()
 * @see Archive()
 */
BBox::BBox(BMessage* archive)
	:
	BView(archive),
	fStyle(B_FANCY_BORDER)
{
	_InitObject(archive);
}


/**
 * @brief Destroy the BBox.
 *
 * Clears the label (freeing the string or deleting the label view) and
 * releases the cached layout data.
 */
BBox::~BBox()
{
	_ClearLabel();

	delete fLayoutData;
}


/**
 * @brief Create a BBox instance from an archived BMessage.
 *
 * @param archive The archive message to instantiate from.
 * @return A newly allocated BBox on success, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BBox::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BBox"))
		return new BBox(archive);

	return NULL;
}


/**
 * @brief Archive the BBox into a BMessage.
 *
 * Stores the string label, a flag when a view label is present, and the
 * border style (only when it differs from B_FANCY_BORDER) in addition to
 * all data archived by BView.
 *
 * @param archive The message to archive into.
 * @param deep    If true, child views are archived recursively.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
BBox::Archive(BMessage* archive, bool deep) const
{
	status_t ret = BView::Archive(archive, deep);

	if (fLabel && ret == B_OK)
		ret = archive->AddString("_label", fLabel);

	if (fLabelView && ret == B_OK)
		ret = archive->AddBool("_lblview", true);

	if (fStyle != B_FANCY_BORDER && ret == B_OK)
		ret = archive->AddInt32("_style", fStyle);

	return ret;
}


/**
 * @brief Set the border style drawn around the box.
 *
 * Immediately invalidates the layout and schedules a redraw when the style
 * actually changes.
 *
 * @param border B_PLAIN_BORDER, B_FANCY_BORDER, or B_NO_BORDER.
 * @see Border()
 */
void
BBox::SetBorder(border_style border)
{
	if (border == fStyle)
		return;

	fStyle = border;

	InvalidateLayout();

	if (Window() != NULL && LockLooper()) {
		Invalidate();
		UnlockLooper();
	}
}


/**
 * @brief Return the current border style.
 *
 * @return The active border_style constant.
 * @see SetBorder()
 */
border_style
BBox::Border() const
{
	return fStyle;
}


//! This function is not part of the R5 API and is not yet finalized yet
float
BBox::TopBorderOffset()
{
	_ValidateLayoutData();

	if (fLabel != NULL || fLabelView != NULL)
		return fLayoutData->label_box.Height() / 2;

	return 0;
}


//! This function is not part of the R5 API and is not yet finalized yet
BRect
BBox::InnerFrame()
{
	_ValidateLayoutData();

	BRect frame(Bounds());
	frame.left += fLayoutData->insets.left;
	frame.top += fLayoutData->insets.top;
	frame.right -= fLayoutData->insets.right;
	frame.bottom -= fLayoutData->insets.bottom;

	return frame;
}


/**
 * @brief Set the box label to a text string.
 *
 * Clears any existing label (string or view), copies the new string, and
 * invalidates the layout and display.
 *
 * @param string The label text, or NULL to remove the label entirely.
 * @see Label()
 * @see SetLabel(BView*)
 */
void
BBox::SetLabel(const char* string)
{
	_ClearLabel();

	if (string)
		fLabel = strdup(string);

	InvalidateLayout();

	if (Window())
		Invalidate();
}


/**
 * @brief Set the box label to a BView.
 *
 * Clears any existing label, then inserts @a viewLabel as the first child
 * positioned at (10, 0). The view is sized to its preferred size during the
 * next layout pass.
 *
 * @param viewLabel The view to use as the label, or NULL to remove the label.
 * @return B_OK always.
 * @see LabelView()
 * @see SetLabel(const char*)
 */
status_t
BBox::SetLabel(BView* viewLabel)
{
	_ClearLabel();

	if (viewLabel) {
		fLabelView = viewLabel;
		fLabelView->MoveTo(10.0f, 0.0f);
		AddChild(fLabelView, ChildAt(0));
	}

	InvalidateLayout();

	if (Window())
		Invalidate();

	return B_OK;
}


/**
 * @brief Return the current string label.
 *
 * @return The label string, or NULL if no string label is set (including when
 *         a view label is in use).
 * @see SetLabel(const char*)
 */
const char*
BBox::Label() const
{
	return fLabel;
}


/**
 * @brief Return the current view label.
 *
 * @return The label BView, or NULL if no view label is set.
 * @see SetLabel(BView*)
 */
BView*
BBox::LabelView() const
{
	return fLabelView;
}


/**
 * @brief Draw the box border and label within the given update rectangle.
 *
 * Clips out the label area before drawing the border so that the border does
 * not overwrite the label. After the border is drawn, the clip is removed and
 * the label string is rendered (if any).
 *
 * @param updateRect The portion of the view that requires redrawing.
 */
void
BBox::Draw(BRect updateRect)
{
	_ValidateLayoutData();

	PushState();

	BRect labelBox = BRect(0, 0, 0, 0);
	if (fLabel != NULL) {
		labelBox = fLayoutData->label_box;
		BRegion update(updateRect);
		update.Exclude(labelBox);

		ConstrainClippingRegion(&update);
	} else if (fLabelView != NULL)
		labelBox = fLabelView->Bounds();

	switch (fStyle) {
		case B_FANCY_BORDER:
			_DrawFancy(labelBox);
			break;

		case B_PLAIN_BORDER:
			_DrawPlain(labelBox);
			break;

		default:
			break;
	}

	if (fLabel != NULL) {
		ConstrainClippingRegion(NULL);

		font_height fontHeight;
		GetFontHeight(&fontHeight);

		// offset label up by 1/6 the font height
		float lineHeight = fontHeight.ascent + fontHeight.descent;
		float yOffset = roundf(lineHeight / 6.0f);

		SetHighColor(ui_color(B_PANEL_TEXT_COLOR));
		DrawString(fLabel, BPoint(10.0f, fontHeight.ascent - yOffset));
	}

	PopState();
}


/**
 * @brief Hook called when the box is attached to a window.
 *
 * Adopts the parent's colors and ensures the low color matches the view color
 * so that the label background is drawn correctly. Switches to system colors
 * when the view color is transparent.
 */
void
BBox::AttachedToWindow()
{
	AdoptParentColors();

	// Force low color to match view color for proper label drawing.
	float viewTint = B_NO_TINT;
	float lowTint = B_NO_TINT;

	if (LowUIColor(&lowTint) != ViewUIColor(&viewTint) || viewTint != lowTint)
		SetLowUIColor(ViewUIColor(), viewTint);
	else if (LowColor() != ViewColor())
		SetLowColor(ViewColor());

	if (ViewColor() == B_TRANSPARENT_COLOR)
		AdoptSystemColors();

	// The box could have been resized in the mean time
	fBounds = Bounds().OffsetToCopy(0, 0);
}


/**
 * @brief Hook called when the box is removed from its window.
 *
 * Delegates directly to BView::DetachedFromWindow().
 */
void
BBox::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Hook called after all views in the hierarchy have been attached.
 *
 * Delegates directly to BView::AllAttached().
 */
void
BBox::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Hook called after all views in the hierarchy have been detached.
 *
 * Delegates directly to BView::AllDetached().
 */
void
BBox::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Hook called when the box's frame is resized.
 *
 * Invalidates only the border strips along the edges that changed size,
 * avoiding a full redraw. Updates the cached bounds snapshot for future
 * comparisons.
 *
 * @param width  The new width of the box in pixels.
 * @param height The new height of the box in pixels.
 */
void
BBox::FrameResized(float width, float height)
{
	BRect bounds(Bounds());

	// invalidate the regions that the app_server did not
	// (for removing the previous or drawing the new border)
	if (fStyle != B_NO_BORDER) {
		// TODO: this must be made part of the be_control_look stuff!
		int32 borderSize = fStyle == B_PLAIN_BORDER ? 0 : 2;

		// Horizontal
		BRect invalid(bounds);
		if (fBounds.Width() < bounds.Width()) {
			// enlarging
			invalid.left = bounds.left + fBounds.right - borderSize;
			invalid.right = bounds.left + fBounds.right;

			Invalidate(invalid);
		} else if (fBounds.Width() > bounds.Width()) {
			// shrinking
			invalid.left = bounds.left + bounds.right - borderSize;

			Invalidate(invalid);
		}

		// Vertical
		invalid = bounds;
		if (fBounds.Height() < bounds.Height()) {
			// enlarging
			invalid.top = bounds.top + fBounds.bottom - borderSize;
			invalid.bottom = bounds.top + fBounds.bottom;

			Invalidate(invalid);
		} else if (fBounds.Height() > bounds.Height()) {
			// shrinking
			invalid.top = bounds.top + bounds.bottom - borderSize;

			Invalidate(invalid);
		}
	}

	fBounds.right = width;
	fBounds.bottom = height;
}


/**
 * @brief Handle an incoming message.
 *
 * Delegates directly to BView::MessageReceived().
 *
 * @param message The message to process.
 */
void
BBox::MessageReceived(BMessage* message)
{
	BView::MessageReceived(message);
}


/**
 * @brief Handle a mouse-button-down event.
 *
 * Delegates directly to BView::MouseDown().
 *
 * @param point The cursor position in view coordinates.
 */
void
BBox::MouseDown(BPoint point)
{
	BView::MouseDown(point);
}


/**
 * @brief Handle a mouse-button-up event.
 *
 * Delegates directly to BView::MouseUp().
 *
 * @param point The cursor position in view coordinates.
 */
void
BBox::MouseUp(BPoint point)
{
	BView::MouseUp(point);
}


/**
 * @brief Hook called when the parent window gains or loses activation.
 *
 * Delegates directly to BView::WindowActivated().
 *
 * @param active True if the window became active, false if it lost activation.
 */
void
BBox::WindowActivated(bool active)
{
	BView::WindowActivated(active);
}


/**
 * @brief Handle cursor movement events.
 *
 * Delegates directly to BView::MouseMoved().
 *
 * @param point   The current cursor position in view coordinates.
 * @param transit One of B_ENTERED_VIEW, B_INSIDE_VIEW, B_EXITED_VIEW, or
 *                B_OUTSIDE_VIEW.
 * @param message The drag-and-drop message, or NULL.
 */
void
BBox::MouseMoved(BPoint point, uint32 transit, const BMessage* message)
{
	BView::MouseMoved(point, transit, message);
}


/**
 * @brief Hook called when the box's frame position changes.
 *
 * Delegates directly to BView::FrameMoved().
 *
 * @param newLocation The new top-left corner in the parent's coordinate system.
 */
void
BBox::FrameMoved(BPoint newLocation)
{
	BView::FrameMoved(newLocation);
}


/**
 * @brief Resolve a scripting specifier to a target handler.
 *
 * Delegates directly to BView::ResolveSpecifier().
 *
 * @param message   The scripting message.
 * @param index     The specifier index.
 * @param specifier The current specifier message.
 * @param what      The specifier form constant.
 * @param property  The property name string.
 * @return The handler that should process the scripting message.
 */
BHandler*
BBox::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
	return BView::ResolveSpecifier(message, index, specifier, what, property);
}


/**
 * @brief Resize the box to its preferred size, never making it smaller.
 *
 * Computes the preferred size, clamps it so that the box is not made smaller
 * than its current dimensions, and calls BView::ResizeTo().
 */
void
BBox::ResizeToPreferred()
{
	float width, height;
	GetPreferredSize(&width, &height);

	// make sure the box don't get smaller than it already is
	if (width < Bounds().Width())
		width = Bounds().Width();
	if (height < Bounds().Height())
		height = Bounds().Height();

	BView::ResizeTo(width, height);
}


/**
 * @brief Return the preferred width and height of the box.
 *
 * Validates cached layout data and returns the stored preferred size.
 *
 * @param _width  Set to the preferred width in pixels, or left unchanged if NULL.
 * @param _height Set to the preferred height in pixels, or left unchanged if NULL.
 */
void
BBox::GetPreferredSize(float* _width, float* _height)
{
	_ValidateLayoutData();

	if (_width)
		*_width = fLayoutData->preferred.width;
	if (_height)
		*_height = fLayoutData->preferred.height;
}


/**
 * @brief Set or remove keyboard focus for this box.
 *
 * Delegates directly to BView::MakeFocus().
 *
 * @param focused True to give focus, false to relinquish it.
 */
void
BBox::MakeFocus(bool focused)
{
	BView::MakeFocus(focused);
}


/**
 * @brief Fill a BMessage with the scripting suites this object supports.
 *
 * Delegates directly to BView::GetSupportedSuites().
 *
 * @param message The message to populate.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BBox::GetSupportedSuites(BMessage* message)
{
	return BView::GetSupportedSuites(message);
}


/**
 * @brief Dispatch a binary-compatibility perform code.
 *
 * Handles perform codes for MinSize, MaxSize, PreferredSize, LayoutAlignment,
 * HasHeightForWidth, GetHeightForWidth, SetLayout, LayoutInvalidated, and
 * DoLayout. Unrecognized codes are forwarded to BView::Perform().
 *
 * @param code  The perform code constant (PERFORM_CODE_*).
 * @param _data Pointer to the code-specific data structure.
 * @return B_OK for recognized codes, or the result from BView::Perform().
 */
status_t
BBox::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BBox::MinSize();
			return B_OK;
		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BBox::MaxSize();
			return B_OK;
		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BBox::PreferredSize();
			return B_OK;
		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BBox::LayoutAlignment();
			return B_OK;
		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BBox::HasHeightForWidth();
			return B_OK;
		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BBox::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}
		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BBox::SetLayout(data->layout);
			return B_OK;
		}
		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BBox::LayoutInvalidated(data->descendants);
			return B_OK;
		}
		case PERFORM_CODE_DO_LAYOUT:
		{
			BBox::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


/**
 * @brief Return the minimum layout size of the box.
 *
 * Incorporates the minimum size imposed by the label width and border
 * insets, then composes with any explicit minimum set by the caller.
 *
 * @return The smallest BSize the layout engine may assign to this box.
 */
BSize
BBox::MinSize()
{
	_ValidateLayoutData();

	BSize size = (GetLayout() ? GetLayout()->MinSize() : fLayoutData->min);
	if (size.width < fLayoutData->min.width)
		size.width = fLayoutData->min.width;
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Return the maximum layout size of the box.
 *
 * @return The largest BSize the layout engine should assign to this box.
 */
BSize
BBox::MaxSize()
{
	_ValidateLayoutData();

	BSize size = (GetLayout() ? GetLayout()->MaxSize() : fLayoutData->max);
	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


/**
 * @brief Return the preferred layout size of the box.
 *
 * @return The ideal BSize as determined by the child layout or cached data,
 *         composed with any explicit preferred size.
 */
BSize
BBox::PreferredSize()
{
	_ValidateLayoutData();

	BSize size = (GetLayout() ? GetLayout()->PreferredSize()
		: fLayoutData->preferred);
	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


/**
 * @brief Return the layout alignment of the box.
 *
 * Derives the alignment from the child layout or the cached default, then
 * composes it with any explicit alignment set on this view.
 *
 * @return The BAlignment the layout engine should use for this box.
 */
BAlignment
BBox::LayoutAlignment()
{
	_ValidateLayoutData();

	BAlignment alignment = (GetLayout() ? GetLayout()->Alignment()
			: fLayoutData->alignment);
	return BLayoutUtils::ComposeAlignment(ExplicitAlignment(), alignment);
}


/**
 * @brief Mark cached layout data as invalid when the layout is invalidated.
 *
 * @param descendants True when child layouts were also invalidated (unused
 *                    here but required by the BView override signature).
 */
void
BBox::LayoutInvalidated(bool descendants)
{
	fLayoutData->valid = false;
}


/**
 * @brief Perform a layout pass, positioning the label view and the child view.
 *
 * When a BLayout is set, the base class handles child views. The label view
 * is temporarily removed from the child list so the layout engine does not
 * try to size it, then re-added afterwards. The label view is always moved to
 * its computed position regardless. When no layout is set, the single non-label
 * child is positioned within the inset rectangle.
 */
void
BBox::DoLayout()
{
	// Bail out, if we shan't do layout.
	if (!(Flags() & B_SUPPORTS_LAYOUT))
		return;

	BLayout* layout = GetLayout();

	// If the user set a layout, let the base class version call its
	// hook. In case when we have BView as a label, remove it from child list
	// so it won't be layouted with the rest of views and add it again
	// after that.
	if (layout != NULL) {
		if (fLabelView)
			RemoveChild(fLabelView);

		BView::DoLayout();

		if (fLabelView != NULL) {
			DisableLayoutInvalidation();
				// don't trigger a relayout
			AddChild(fLabelView, ChildAt(0));
			EnableLayoutInvalidation();
		}
	}

	_ValidateLayoutData();

	// Even if the user set a layout, restore label view to it's
	// desired position.

	// layout the label view
	if (fLabelView != NULL) {
		fLabelView->MoveTo(fLayoutData->label_box.LeftTop());
		fLabelView->ResizeTo(fLayoutData->label_box.Size());
	}

	// If we have layout return here and do not layout the child
	if (layout != NULL)
		return;

	// layout the child
	BView* child = _Child();
	if (child != NULL) {
		BRect frame(Bounds());
		frame.left += fLayoutData->insets.left;
		frame.top += fLayoutData->insets.top;
		frame.right -= fLayoutData->insets.right;
		frame.bottom -= fLayoutData->insets.bottom;

		if ((child->Flags() & B_SUPPORTS_LAYOUT) != 0)
			BLayoutUtils::AlignInFrame(child, frame);
		else
			child->MoveTo(frame.LeftTop());
	}
}


void BBox::_ReservedBox1() {}
void BBox::_ReservedBox2() {}


BBox &
BBox::operator=(const BBox &)
{
	return *this;
}


/**
 * @brief Perform one-time initialization shared by all constructors.
 *
 * Snapshots the initial bounds, zeroes the label pointers, allocates the
 * LayoutData structure, applies the bold font family and size defaults (unless
 * overridden by the archive), reads label and style from the archive when
 * provided, and adopts system colors.
 *
 * @param archive Optional archive message from which to restore label and
 *                style. Pass NULL (the default) when constructing fresh.
 */
void
BBox::_InitObject(BMessage* archive)
{
	fBounds = Bounds().OffsetToCopy(0, 0);

	fLabel = NULL;
	fLabelView = NULL;
	fLayoutData = new LayoutData;

	int32 flags = 0;

	BFont font(be_bold_font);

	if (!archive || !archive->HasString("_fname"))
		flags = B_FONT_FAMILY_AND_STYLE;

	if (!archive || !archive->HasFloat("_fflt"))
		flags |= B_FONT_SIZE;

	if (flags != 0)
		SetFont(&font, flags);

	if (archive != NULL) {
		const char* string;
		if (archive->FindString("_label", &string) == B_OK)
			SetLabel(string);

		bool fancy;
		int32 style;

		if (archive->FindBool("_style", &fancy) == B_OK)
			fStyle = fancy ? B_FANCY_BORDER : B_PLAIN_BORDER;
		else if (archive->FindInt32("_style", &style) == B_OK)
			fStyle = (border_style)style;

		bool hasLabelView;
		if (archive->FindBool("_lblview", &hasLabelView) == B_OK)
			fLabelView = ChildAt(0);
	}

	AdoptSystemColors();
}


/**
 * @brief Render the box using a plain single-pixel border.
 *
 * When the box has zero width or height it is drawn as a separator line.
 * Otherwise four lines are drawn with a light top-left bevel and a dark
 * bottom-right shadow.
 *
 * @param labelBox The area occupied by the label, used to set the top of the
 *                 border rectangle correctly via TopBorderOffset().
 */
void
BBox::_DrawPlain(BRect labelBox)
{
	BRect rect = Bounds();
	rect.top += TopBorderOffset();

	float lightTint;
	float shadowTint;
	lightTint = B_LIGHTEN_1_TINT;
	shadowTint = B_DARKEN_1_TINT;

	if (rect.Height() == 0.0 || rect.Width() == 0.0) {
		// used as separator
		rgb_color shadow = tint_color(ViewColor(), B_DARKEN_2_TINT);

		SetHighColor(shadow);
		StrokeLine(rect.LeftTop(),rect.RightBottom());
	} else {
		// used as box
		rgb_color light = tint_color(ViewColor(), lightTint);
		rgb_color shadow = tint_color(ViewColor(), shadowTint);

		BeginLineArray(4);
			AddLine(BPoint(rect.left, rect.bottom),
					BPoint(rect.left, rect.top), light);
			AddLine(BPoint(rect.left + 1.0f, rect.top),
					BPoint(rect.right, rect.top), light);
			AddLine(BPoint(rect.left + 1.0f, rect.bottom),
					BPoint(rect.right, rect.bottom), shadow);
			AddLine(BPoint(rect.right, rect.bottom - 1.0f),
					BPoint(rect.right, rect.top + 1.0f), shadow);
		EndLineArray();
	}
}


/**
 * @brief Render the box using the fancy (etched) border style.
 *
 * Delegates to be_control_look->DrawGroupFrame(). When the box is exactly
 * one pixel tall or wide, only the corresponding single border side is drawn
 * so the box can serve as a horizontal or vertical separator.
 *
 * @param labelBox The area occupied by the label; used to set the top of the
 *                 border rectangle via TopBorderOffset().
 */
void
BBox::_DrawFancy(BRect labelBox)
{
	BRect rect = Bounds();
	rect.top += TopBorderOffset();

	rgb_color base = ViewColor();
	if (rect.Height() == 1.0) {
		// used as horizontal separator
		be_control_look->DrawGroupFrame(this, rect, rect, base,
			BControlLook::B_TOP_BORDER);
	} else if (rect.Width() == 1.0) {
		// used as vertical separator
		be_control_look->DrawGroupFrame(this, rect, rect, base,
			BControlLook::B_LEFT_BORDER);
	} else {
		// used as box
		be_control_look->DrawGroupFrame(this, rect, rect, base);
	}
}


/**
 * @brief Release the current label, whether string or view.
 *
 * Frees the string with free() when a string label is set, or removes,
 * deletes, and NULLs the label view when a view label is set.
 */
void
BBox::_ClearLabel()
{
	if (fLabel) {
		free(fLabel);
		fLabel = NULL;
	} else if (fLabelView) {
		fLabelView->RemoveSelf();
		delete fLabelView;
		fLabelView = NULL;
	}
}


/**
 * @brief Return the first child that is not the label view.
 *
 * Iterates through the child list and returns the first BView that is not
 * fLabelView, or NULL when no such child exists.
 *
 * @return The primary content child, or NULL.
 */
BView*
BBox::_Child() const
{
	for (int32 i = 0; BView* view = ChildAt(i); i++) {
		if (view != fLabelView)
			return view;
	}

	return NULL;
}


/**
 * @brief Recompute cached layout metrics when they are out of date.
 *
 * Calculates the label bounding box, per-edge insets induced by the border
 * style and label height, and the minimum, maximum, and preferred sizes.
 * Also derives the layout alignment from the child's constraints. Results are
 * stored in fLayoutData and the valid flag is set to true.
 *
 * @note This is a no-op when fLayoutData->valid is already true.
 */
void
BBox::_ValidateLayoutData()
{
	if (fLayoutData->valid)
		return;

	// compute the label box, width and height
	bool label = true;
	float labelHeight = 0;	// height of the label (pixel count)
	if (fLabel) {
		// leave 6 pixels of the frame, and have a gap of 4 pixels between
		// the frame and the text on either side
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		fLayoutData->label_box.Set(6.0f, 0, 14.0f + StringWidth(fLabel),
			ceilf(fontHeight.ascent));
		labelHeight = ceilf(fontHeight.ascent + fontHeight.descent) + 1;
	} else if (fLabelView) {
		// the label view is placed at (0, 10) at its preferred size
		BSize size = fLabelView->PreferredSize();
		fLayoutData->label_box.Set(10, 0, 10 + size.width, size.height);
		labelHeight = size.height + 1;
	} else
		label = false;

	// border
	switch (fStyle) {
		case B_PLAIN_BORDER:
			fLayoutData->insets.Set(1, 1, 1, 1);
			break;
		case B_FANCY_BORDER:
			fLayoutData->insets.Set(3, 3, 3, 3);
			break;
		case B_NO_BORDER:
		default:
			fLayoutData->insets.Set(0, 0, 0, 0);
			break;
	}

	// if there's a label, the top inset will be dictated by the label
	if (label && labelHeight > fLayoutData->insets.top)
		fLayoutData->insets.top = labelHeight;

	// total number of pixel the border adds
	float addWidth = fLayoutData->insets.left + fLayoutData->insets.right;
	float addHeight = fLayoutData->insets.top + fLayoutData->insets.bottom;

	// compute the minimal width induced by the label
	float minWidth;
	if (label)
		minWidth = fLayoutData->label_box.right + fLayoutData->insets.right;
	else
		minWidth = addWidth - 1;

	BAlignment alignment(B_ALIGN_HORIZONTAL_CENTER, B_ALIGN_VERTICAL_CENTER);

	// finally consider the child constraints, if we shall support layout
	BView* child = _Child();
	if (child && (child->Flags() & B_SUPPORTS_LAYOUT)) {
		BSize min = child->MinSize();
		BSize max = child->MaxSize();
		BSize preferred = child->PreferredSize();

		min.width += addWidth;
		min.height += addHeight;
		preferred.width += addWidth;
		preferred.height += addHeight;
		max.width = BLayoutUtils::AddDistances(max.width, addWidth - 1);
		max.height = BLayoutUtils::AddDistances(max.height, addHeight - 1);

		if (min.width < minWidth)
			min.width = minWidth;
		BLayoutUtils::FixSizeConstraints(min, max, preferred);

		fLayoutData->min = min;
		fLayoutData->max = max;
		fLayoutData->preferred = preferred;

		BAlignment childAlignment = child->LayoutAlignment();
		if (childAlignment.horizontal == B_ALIGN_USE_FULL_WIDTH)
			alignment.horizontal = B_ALIGN_USE_FULL_WIDTH;
		if (childAlignment.vertical == B_ALIGN_USE_FULL_HEIGHT)
			alignment.vertical = B_ALIGN_USE_FULL_HEIGHT;

		fLayoutData->alignment = alignment;
	} else {
		fLayoutData->min.Set(minWidth, addHeight - 1);
		fLayoutData->max.Set(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED);
		fLayoutData->preferred = fLayoutData->min;
		fLayoutData->alignment = alignment;
	}

	fLayoutData->valid = true;
	ResetLayoutInvalidation();
}


extern "C" void
B_IF_GCC_2(InvalidateLayout__4BBoxb, _ZN4BBox16InvalidateLayoutEb)(
	BBox* box, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	box->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
