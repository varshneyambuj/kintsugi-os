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
 *   Copyright 2001-2009, Stephan Aßmus. All rights reserved.
 *   Copyright 2001-2009, Ingo Weinhold. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file SeparatorView.cpp
 * @brief Implementation of BSeparatorView, a visual separator view
 *
 * BSeparatorView draws a horizontal or vertical separator line, typically used
 * to visually divide sections of a BGroupView or BBox. It supports etched and
 * plain border styles.
 *
 * @see BView, BGroupView
 */


#include "SeparatorView.h"

#include <new>

#include <math.h>
#include <stdio.h>

#include <ControlLook.h>
#include <LayoutUtils.h>
#include <Region.h>


/** @brief Minimum length (in pixels) of the visible line on each side of the label. */
static const float kMinBorderLength = 5.0f;


// TODO: Actually implement alignment support!
// TODO: More testing, especially archiving.


/**
 * @brief Construct a minimal BSeparatorView with no label.
 *
 * Creates an anonymous separator with the given orientation and border style,
 * centred alignment, and no text or view label.
 *
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL line direction.
 * @param border      Border style: @c B_PLAIN_BORDER, @c B_FANCY_BORDER,
 *                    or @c B_NO_BORDER.
 * @see BSeparatorView(const char*, const char*, orientation, border_style, const BAlignment&)
 */
BSeparatorView::BSeparatorView(orientation orientation, border_style border)
	:
	BView(NULL, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init(NULL, NULL, orientation, BAlignment(B_ALIGN_HORIZONTAL_CENTER,
		B_ALIGN_VERTICAL_CENTER), border);
}


/**
 * @brief Construct a named BSeparatorView with a text label.
 *
 * The text label is rendered along the separator line according to
 * \a alignment. Bold font is used by default.
 *
 * @param name        Internal view name (not displayed).
 * @param label       Text to display alongside the separator line; may be NULL.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL line direction.
 * @param border      Border style applied to the separator line.
 * @param alignment   Alignment of the label within the separator's bounds.
 */
BSeparatorView::BSeparatorView(const char* name, const char* label,
	orientation orientation, border_style border, const BAlignment& alignment)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init(label, NULL, orientation, alignment, border);
}


/**
 * @brief Construct a named BSeparatorView with a custom label view.
 *
 * Ownership of \a labelView is transferred to this separator; it is
 * added as a child view and will be deleted when the separator is
 * destroyed or a new label view is set.
 *
 * @param name        Internal view name (not displayed).
 * @param labelView   A BView to display in place of a text label; may be NULL.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL line direction.
 * @param border      Border style applied to the separator line.
 * @param alignment   Alignment of \a labelView within the separator's bounds.
 */
BSeparatorView::BSeparatorView(const char* name, BView* labelView,
	orientation orientation, border_style border, const BAlignment& alignment)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init(NULL, labelView, orientation, alignment, border);
}


/**
 * @brief Construct an anonymous BSeparatorView with a text label.
 *
 * Convenience constructor: the view name is set to an empty string and
 * a text label is drawn along the line.
 *
 * @param label       Text to display alongside the separator line; may be NULL.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL line direction.
 * @param border      Border style applied to the separator line.
 * @param alignment   Alignment of the label within the separator's bounds.
 */
BSeparatorView::BSeparatorView(const char* label,
	orientation orientation, border_style border, const BAlignment& alignment)
	:
	BView("", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init(label, NULL, orientation, alignment, border);
}


/**
 * @brief Construct an anonymous BSeparatorView with a custom label view.
 *
 * Convenience constructor: the view name is set to an empty string.
 * Ownership of \a labelView is transferred to this separator.
 *
 * @param labelView   A BView to display in place of a text label; may be NULL.
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL line direction.
 * @param border      Border style applied to the separator line.
 * @param alignment   Alignment of \a labelView within the separator's bounds.
 */
BSeparatorView::BSeparatorView(BView* labelView,
	orientation orientation, border_style border, const BAlignment& alignment)
	:
	BView("", B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE)
{
	_Init(NULL, labelView, orientation, alignment, border);
}


/**
 * @brief Reconstruct a BSeparatorView from an archive message.
 *
 * Reads back the label string or label-view name, orientation, alignment,
 * and border style that were saved by Archive(). This is the unarchiving
 * constructor invoked by Instantiate().
 *
 * @param archive The archive message previously produced by Archive().
 * @see Instantiate(), Archive()
 */
BSeparatorView::BSeparatorView(BMessage* archive)
	:
	BView(archive),
	fLabel(),
	fLabelView(NULL),
	fOrientation(B_HORIZONTAL),
	fAlignment(BAlignment(B_ALIGN_HORIZONTAL_CENTER,
		B_ALIGN_VERTICAL_CENTER)),
	fBorder(B_FANCY_BORDER)
{
	// NULL archives will be caught by BView.

	const char* label;
	if (archive->FindString("_labelview", &label) == B_OK) {
		fLabelView = FindView(label);
	} else if (archive->FindString("_label", &label) == B_OK) {
		fLabel.SetTo(label);
	}

	int32 orientation;
	if (archive->FindInt32("_orientation", &orientation) == B_OK)
		fOrientation = (enum orientation)orientation;

	int32 hAlignment;
	int32 vAlignment;
	if (archive->FindInt32("_halignment", &hAlignment) == B_OK
		&& archive->FindInt32("_valignment", &vAlignment) == B_OK) {
		fAlignment.horizontal = (alignment)hAlignment;
		fAlignment.vertical = (vertical_alignment)vAlignment;
	}

	int32 borderStyle;
	if (archive->FindInt32("_border", &borderStyle) != B_OK)
		fBorder = (border_style)borderStyle;
}


/**
 * @brief Destroy the BSeparatorView.
 *
 * Child views (including any label view) are removed and deleted by the
 * BView base class destructor. The BString label is freed automatically.
 */
BSeparatorView::~BSeparatorView()
{
}


// #pragma mark - Archiving


/**
 * @brief Create a new BSeparatorView from an archive message.
 *
 * @param archive The archive message to instantiate from.
 * @return A newly allocated BSeparatorView on success, or NULL if
 *         \a archive is not a valid BSeparatorView archive or allocation
 *         fails.
 * @see Archive()
 */
BArchivable*
BSeparatorView::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BSeparatorView"))
		return new (std::nothrow) BSeparatorView(archive);

	return NULL;
}


/**
 * @brief Archive the BSeparatorView into a BMessage.
 *
 * Stores the label (or label-view name), orientation, horizontal alignment,
 * vertical alignment, and border style in addition to the base class fields.
 *
 * @param into The message to archive into.
 * @param deep If true, child views are archived recursively (passed through
 *             to BView::Archive()).
 * @return @c B_OK on success, or a negative error code if any field could
 *         not be added to \a into.
 * @see Instantiate()
 */
status_t
BSeparatorView::Archive(BMessage* into, bool deep) const
{
	// TODO: Test this.
	status_t result = BView::Archive(into, deep);
	if (result != B_OK)
		return result;

	if (fLabelView != NULL)
		result = into->AddString("_labelview", fLabelView->Name());
	else
		result = into->AddString("_label", fLabel.String());

	if (result == B_OK)
		result = into->AddInt32("_orientation", fOrientation);

	if (result == B_OK)
		result = into->AddInt32("_halignment", fAlignment.horizontal);

	if (result == B_OK)
		result = into->AddInt32("_valignment", fAlignment.vertical);

	if (result == B_OK)
		result = into->AddInt32("_border", fBorder);

	return result;
}


// #pragma mark -


/**
 * @brief Draw the separator line and optional label into \a updateRect.
 *
 * Determines background and foreground colours from the parent view when
 * available, then clips around the label area before delegating the actual
 * border rendering to @c be_control_look. The text label (if any) is drawn
 * last in the appropriate orientation.
 *
 * @param updateRect The rectangular region that needs to be redrawn.
 * @note Alignment support for the label is not yet fully implemented
 *       (see TODO comment in source).
 * @see BControlLook::DrawBorder(), SetOrientation(), SetLabel()
 */
void
BSeparatorView::Draw(BRect updateRect)
{
	rgb_color bgColor = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color highColor = ui_color(B_PANEL_TEXT_COLOR);

	if (BView* parent = Parent()) {
		if (parent->ViewColor() != B_TRANSPARENT_COLOR) {
			bgColor = parent->ViewColor();
			highColor = parent->HighColor();
		}
	}

	BRect labelBounds;
	if (fLabelView != NULL) {
		labelBounds = fLabelView->Frame();
	} else if (fLabel.CountChars() > 0) {
		labelBounds = _MaxLabelBounds();
		float labelWidth = StringWidth(fLabel.String());
		if (fOrientation == B_HORIZONTAL) {
			switch (fAlignment.horizontal) {
				case B_ALIGN_LEFT:
				default:
					labelBounds.right = labelBounds.left + labelWidth;
					break;

				case B_ALIGN_RIGHT:
					labelBounds.left = labelBounds.right - labelWidth;
					break;

				case B_ALIGN_CENTER:
					labelBounds.left = (labelBounds.left + labelBounds.right
						- labelWidth) / 2;
					labelBounds.right = labelBounds.left + labelWidth;
					break;
			}
		} else {
			switch (fAlignment.vertical) {
				case B_ALIGN_TOP:
				default:
					labelBounds.bottom = labelBounds.top + labelWidth;
					break;

				case B_ALIGN_BOTTOM:
					labelBounds.top = labelBounds.bottom - labelWidth;
					break;

				case B_ALIGN_MIDDLE:
					labelBounds.top = (labelBounds.top + labelBounds.bottom
						- labelWidth) / 2;
					labelBounds.bottom = labelBounds.top + labelWidth;
					break;
			}
		}
	}

	BRect bounds = Bounds();
	BRegion region(bounds);
	if (labelBounds.IsValid()) {
		region.Exclude(labelBounds);
		ConstrainClippingRegion(&region);
	}

	float borderSize = _BorderSize();
	if (borderSize > 0.0f) {
		if (fOrientation == B_HORIZONTAL) {
			bounds.top = floorf((bounds.top + bounds.bottom + 1 - borderSize)
				/ 2);
			bounds.bottom = bounds.top + borderSize - 1;
			region.Exclude(bounds);
			be_control_look->DrawBorder(this, bounds, updateRect, bgColor,
				fBorder, 0, BControlLook::B_TOP_BORDER);
		} else {
			bounds.left = floorf((bounds.left + bounds.right + 1 - borderSize)
				/ 2);
			bounds.right = bounds.left + borderSize - 1;
			region.Exclude(bounds);
			be_control_look->DrawBorder(this, bounds, updateRect, bgColor,
				fBorder, 0, BControlLook::B_LEFT_BORDER);
		}
		if (labelBounds.IsValid())
			region.Include(labelBounds);

		ConstrainClippingRegion(&region);
	}

	SetLowColor(bgColor);
	FillRect(updateRect, B_SOLID_LOW);

	if (fLabel.CountChars() > 0) {
		font_height fontHeight;
		GetFontHeight(&fontHeight);

		SetHighColor(highColor);

		if (fOrientation == B_HORIZONTAL) {
			DrawString(fLabel.String(), BPoint(labelBounds.left,
				labelBounds.top + ceilf(fontHeight.ascent)));
		} else {
			DrawString(fLabel.String(), BPoint(labelBounds.left
				+ ceilf(fontHeight.ascent), labelBounds.bottom));
		}
	}
}


/**
 * @brief Compute and return the view's preferred size.
 *
 * The preferred width and height are derived from the label's natural
 * dimensions plus @c kMinBorderLength padding on each end of the line.
 * For a vertical separator, label width and height are swapped before
 * adding the padding. Either output pointer may be NULL.
 *
 * @param[out] _width  Set to the preferred width in pixels, or unchanged
 *                     if NULL.
 * @param[out] _height Set to the preferred height in pixels, or unchanged
 *                     if NULL.
 * @see MinSize(), MaxSize(), PreferredSize()
 */
void
BSeparatorView::GetPreferredSize(float* _width, float* _height)
{
	float width = 0.0f;
	float height = 0.0f;

	if (fLabelView != NULL) {
		fLabelView->GetPreferredSize(&width, &height);
	} else if (fLabel.CountChars() > 0) {
		width = StringWidth(fLabel.String());
		font_height fontHeight;
		GetFontHeight(&fontHeight);
		height = ceilf(fontHeight.ascent) + ceilf(fontHeight.descent);
		if (fOrientation == B_VERTICAL) {
			// swap width and height
			float temp = height;
			height = width;
			width = temp;
		}
	}

	float borderSize = _BorderSize();

	// Add some room for the border
	if (fOrientation == B_HORIZONTAL) {
		width += kMinBorderLength * 2.0f;
		height = max_c(height, borderSize - 1.0f);
	} else {
		height += kMinBorderLength * 2.0f;
		width = max_c(width, borderSize - 1.0f);
	}

	if (_width != NULL)
		*_width = width;

	if (_height != NULL)
		*_height = height;
}


/**
 * @brief Return the minimum size of the separator view.
 *
 * Uses the preferred size as the baseline minimum and composes it with any
 * explicit minimum size set by the caller.
 *
 * @return The composed minimum BSize.
 * @see GetPreferredSize(), MaxSize(), PreferredSize()
 */
BSize
BSeparatorView::MinSize()
{
	BSize size;
	GetPreferredSize(&size.width, &size.height);
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Return the maximum size of the separator view.
 *
 * The cross-axis dimension is fixed at the minimum size, while the
 * along-axis dimension is set to @c B_SIZE_UNLIMITED so the separator
 * stretches to fill all available space. The result is composed with any
 * explicit maximum size set by the caller.
 *
 * @return The composed maximum BSize.
 * @see GetPreferredSize(), MinSize(), PreferredSize()
 */
BSize
BSeparatorView::MaxSize()
{
	BSize size(MinSize());
	if (fOrientation == B_HORIZONTAL)
		size.width = B_SIZE_UNLIMITED;
	else
		size.height = B_SIZE_UNLIMITED;

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


/**
 * @brief Return the preferred size of the separator view.
 *
 * Delegates to GetPreferredSize() and composes the result with any explicit
 * preferred size set by the caller.
 *
 * @return The composed preferred BSize.
 * @see GetPreferredSize(), MinSize(), MaxSize()
 */
BSize
BSeparatorView::PreferredSize()
{
	BSize size;
	GetPreferredSize(&size.width, &size.height);

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


// #pragma mark -


/**
 * @brief Change the orientation of the separator line.
 *
 * When switched to @c B_VERTICAL the font is rotated 90 degrees so that
 * any text label reads from bottom to top. The view is then invalidated to
 * trigger a redraw, and the layout is invalidated to force a new size
 * negotiation.
 *
 * @param orientation @c B_HORIZONTAL or @c B_VERTICAL.
 * @see SetAlignment(), SetBorderStyle()
 */
void
BSeparatorView::SetOrientation(orientation orientation)
{
	if (orientation == fOrientation)
		return;

	fOrientation = orientation;

	BFont font;
	GetFont(&font);
	if (fOrientation == B_VERTICAL)
		font.SetRotation(90.0f);

	SetFont(&font);

	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Change the alignment used to position the label within the separator.
 *
 * Has no effect if the new alignment equals the current one. Otherwise the
 * view and its layout are invalidated to trigger a redraw.
 *
 * @param aligment The new alignment for the label.
 * @see SetOrientation(), SetLabel()
 * @note The parameter name "aligment" (single 'n') matches the public API
 *       spelling and is preserved intentionally.
 */
void
BSeparatorView::SetAlignment(const BAlignment& aligment)
{
	if (aligment == fAlignment)
		return;

	fAlignment = aligment;

	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Change the border style of the separator line.
 *
 * Has no effect if \a border equals the current style. Otherwise the view
 * is invalidated to trigger a redraw. The layout is not invalidated because
 * the preferred size depends only on the border thickness, which changes
 * between styles but does not require a new layout pass in practice.
 *
 * @param border The new border style: @c B_PLAIN_BORDER, @c B_FANCY_BORDER,
 *               or @c B_NO_BORDER.
 * @see SetOrientation(), SetAlignment()
 */
void
BSeparatorView::SetBorderStyle(border_style border)
{
	if (border == fBorder)
		return;

	fBorder = border;

	Invalidate();
}


/**
 * @brief Set the text label displayed alongside the separator line.
 *
 * A NULL pointer is treated as an empty string (no label). Has no effect if
 * the new label text equals the current text. Otherwise the view and its
 * layout are invalidated.
 *
 * @param label The new label text, or NULL to clear the label.
 * @see SetLabel(BView*, bool), SetAlignment()
 */
void
BSeparatorView::SetLabel(const char* label)
{
	if (label == NULL)
		label = "";

	if (fLabel == label)
		return;

	fLabel.SetTo(label);

	Invalidate();
	InvalidateLayout();
}


/**
 * @brief Replace the label view displayed alongside the separator line.
 *
 * The old label view is removed from the view hierarchy. If \a deletePrevious
 * is true it is also deleted. The new \a view (if non-NULL) is added as a
 * child. Has no effect if \a view is already the current label view.
 *
 * @param view           The replacement label view, or NULL to remove the
 *                       label view entirely.
 * @param deletePrevious If true, the previously installed label view is
 *                       deleted after being removed; if false, the caller
 *                       is responsible for its lifetime.
 * @see SetLabel(const char*)
 */
void
BSeparatorView::SetLabel(BView* view, bool deletePrevious)
{
	if (fLabelView == view)
		return;

	if (fLabelView != NULL) {
		fLabelView->RemoveSelf();
		if (deletePrevious)
			delete fLabelView;
	}

	fLabelView = view;

	if (fLabelView != NULL)
		AddChild(view);
}


/**
 * @brief Hook for future binary-compatible extensions (FBC).
 *
 * Forwards to BView::Perform() unchanged. Subclasses should not override
 * this method.
 *
 * @param code Perform code identifying the virtual method to call.
 * @param data Opaque argument whose meaning depends on \a code.
 * @return The result from BView::Perform().
 */
status_t
BSeparatorView::Perform(perform_code code, void* data)
{
	return BView::Perform(code, data);
}


// #pragma mark - protected/private


/**
 * @brief Position and size the label view within the separator's bounds.
 *
 * Called by the layout system after size negotiation is complete. If a
 * label view is installed, it is aligned within the region reserved for
 * labels (see _MaxLabelBounds()) according to the current BAlignment.
 *
 * @see SetLabel(BView*, bool), SetAlignment()
 */
void
BSeparatorView::DoLayout()
{
	BView::DoLayout();

	if (fLabelView == NULL)
		return;

	BRect bounds = _MaxLabelBounds();
	BRect frame = BLayoutUtils::AlignInFrame(bounds, fLabelView->MaxSize(),
		fAlignment);

	fLabelView->MoveTo(frame.LeftTop());
	fLabelView->ResizeTo(frame.Width(), frame.Height());
}


/**
 * @brief Common initialisation helper called by all non-archive constructors.
 *
 * Sets the bold font, makes the view background transparent, then delegates
 * to the public SetLabel(), SetOrientation(), and SetAlignment() methods to
 * apply the supplied parameters consistently.
 *
 * @param label       Initial text label; may be NULL.
 * @param labelView   Initial label view (takes ownership); may be NULL.
 * @param orientation Initial line orientation.
 * @param alignment   Initial label alignment.
 * @param border      Initial border style.
 */
void
BSeparatorView::_Init(const char* label, BView* labelView,
	orientation orientation, BAlignment alignment, border_style border)
{
	fLabel = "";
	fLabelView = NULL;

	fOrientation = B_HORIZONTAL;
	fAlignment = alignment;
	fBorder = border;

	SetFont(be_bold_font);
	SetViewColor(B_TRANSPARENT_32_BIT);

	SetLabel(label);
	SetLabel(labelView, true);
	SetOrientation(orientation);
}


/**
 * @brief Return the pixel thickness of the separator's border line.
 *
 * Maps each @c border_style to a fixed thickness: @c B_PLAIN_BORDER → 1,
 * @c B_FANCY_BORDER → 2, and @c B_NO_BORDER (or any unknown value) → 0.
 *
 * @return The border thickness in pixels.
 * @note Ideally these values should come from BControlLook; this is a
 *       known TODO in the source.
 * @see SetBorderStyle()
 */
float
BSeparatorView::_BorderSize() const
{
	// TODO: Get these values from the BControlLook class.
	switch (fBorder) {
		case B_PLAIN_BORDER:
			return 1.0f;

		case B_FANCY_BORDER:
			return 2.0f;

		case B_NO_BORDER:
		default:
			return 0.0f;
	}
}


/**
 * @brief Return the maximum rectangle available for the label.
 *
 * Insets the view's bounds by @c kMinBorderLength along the axis of the
 * separator line so that the border is always visible on both sides of the
 * label.
 *
 * @return A BRect representing the area in which the label may be drawn
 *         or positioned.
 * @see DoLayout(), Draw()
 */
BRect
BSeparatorView::_MaxLabelBounds() const
{
	BRect bounds = Bounds();
	if (fOrientation == B_HORIZONTAL)
		bounds.InsetBy(kMinBorderLength, 0.0f);
	else
		bounds.InsetBy(0.0f, kMinBorderLength);

	return bounds;
}


// #pragma mark - FBC padding


void BSeparatorView::_ReservedSeparatorView1() {}
void BSeparatorView::_ReservedSeparatorView2() {}
void BSeparatorView::_ReservedSeparatorView3() {}
void BSeparatorView::_ReservedSeparatorView4() {}
void BSeparatorView::_ReservedSeparatorView5() {}
void BSeparatorView::_ReservedSeparatorView6() {}
void BSeparatorView::_ReservedSeparatorView7() {}
void BSeparatorView::_ReservedSeparatorView8() {}
void BSeparatorView::_ReservedSeparatorView9() {}
void BSeparatorView::_ReservedSeparatorView10() {}
