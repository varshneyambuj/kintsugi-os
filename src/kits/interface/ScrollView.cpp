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
 *   Copyright 2004-2015, Axel Dörfler / Copyright 2009 Stephan Aßmus /
 *   Copyright 2014-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       Axel Dörfler, axeld@pinc-software.de
 *       John Scipione, jscpione@gmail.com
 */


/**
 * @file ScrollView.cpp
 * @brief Implementation of BScrollView, a view with attached scroll bars
 *
 * BScrollView wraps an existing BView (the "target") and adds optional
 * horizontal and/or vertical BScrollBar instances. It automatically sizes and
 * positions the scroll bars relative to the target view.
 *
 * @see BScrollBar, BView
 */


#include <ScrollView.h>

#include <ControlLook.h>
#include <LayoutUtils.h>
#include <Message.h>
#include <Region.h>
#include <Window.h>

#include <binary_compatibility/Interface.h>


/** @brief Pixel thickness of the border when @c B_FANCY_BORDER is used. */
static const float kFancyBorderSize = 2;

/** @brief Pixel thickness of the border when @c B_PLAIN_BORDER is used. */
static const float kPlainBorderSize = 1;


/**
 * @brief Construct a BScrollView with legacy (non-layout) resizing.
 *
 * Creates the scroll view in the frame derived from @a target's frame plus
 * the space needed for any requested scroll bars and the border.  This
 * constructor is intended for use in non-layout-managed hierarchies.
 *
 * @param name         The view name, used for scripting and debugging.
 * @param target       The child view to be scrolled.  Ownership is transferred
 *                     to the scroll view.
 * @param resizingMode Legacy resizing flags (e.g. @c B_FOLLOW_ALL).
 * @param flags        View creation flags forwarded to BView.
 * @param horizontal   If true, a horizontal scroll bar is added.
 * @param vertical     If true, a vertical scroll bar is added.
 * @param border       The border style: @c B_NO_BORDER, @c B_PLAIN_BORDER, or
 *                     @c B_FANCY_BORDER.
 * @see BScrollBar, BView
 */
BScrollView::BScrollView(const char* name, BView* target, uint32 resizingMode,
	uint32 flags, bool horizontal, bool vertical, border_style border)
	:
	BView(BRect(), name, resizingMode, _ModifyFlags(flags, target, border)),
	fTarget(target),
	fBorder(border)
{
	_Init(horizontal, vertical);
}


/**
 * @brief Construct a layout-aware BScrollView.
 *
 * Creates the scroll view without an explicit frame; the layout engine is
 * responsible for sizing and positioning.  Use this constructor when the
 * scroll view participates in a BLayout hierarchy.
 *
 * @param name       The view name, used for scripting and debugging.
 * @param target     The child view to be scrolled.  Ownership is transferred
 *                   to the scroll view.
 * @param flags      View creation flags forwarded to BView.
 * @param horizontal If true, a horizontal scroll bar is added.
 * @param vertical   If true, a vertical scroll bar is added.
 * @param border     The border style: @c B_NO_BORDER, @c B_PLAIN_BORDER, or
 *                   @c B_FANCY_BORDER.
 * @see BScrollBar, BLayout
 */
BScrollView::BScrollView(const char* name, BView* target, uint32 flags,
	bool horizontal, bool vertical, border_style border)
	:
	BView(name, _ModifyFlags(flags, target, border)),
	fTarget(target),
	fBorder(border)
{
	_Init(horizontal, vertical);
}


/**
 * @brief Unarchive constructor — reconstruct a BScrollView from a BMessage.
 *
 * Restores the border style and re-identifies the target view and scroll
 * bars from the archived child list.  For layout-managed archives the child
 * search is repeated in AllUnarchived() once all children have been attached.
 *
 * @param archive The archive message previously produced by Archive().
 * @see Archive()
 * @see AllUnarchived()
 * @see Instantiate()
 */
BScrollView::BScrollView(BMessage* archive)
	:
	BView(archive),
	fHighlighted(false)
{
	int32 border;
	fBorder = archive->FindInt32("_style", &border) == B_OK ?
		(border_style)border : B_FANCY_BORDER;

	// in a shallow archive, we may not have a target anymore. We must
	// be prepared for this case

	// don't confuse our scroll bars with our (eventual) target
	int32 firstBar = 0;
	if (!archive->FindBool("_no_target_")) {
		fTarget = ChildAt(0);
		firstBar++;
	} else
		fTarget = NULL;

	// search for our scroll bars
	// This will not work for managed archives (when the layout kit is used).
	// In that case the children are attached later, and we perform the search
	// again in the AllUnarchived method.

	fHorizontalScrollBar = NULL;
	fVerticalScrollBar = NULL;

	BView* view;
	while ((view = ChildAt(firstBar++)) != NULL) {
		BScrollBar *bar = dynamic_cast<BScrollBar *>(view);
		if (bar == NULL)
			continue;

		if (bar->Orientation() == B_HORIZONTAL)
			fHorizontalScrollBar = bar;
		else if (bar->Orientation() == B_VERTICAL)
			fVerticalScrollBar = bar;
	}

	fPreviousWidth = uint16(Bounds().Width());
	fPreviousHeight = uint16(Bounds().Height());

}


/**
 * @brief Destroy the BScrollView.
 *
 * The target view and scroll bars are child views and are destroyed by the
 * BView destructor; no explicit cleanup is required here.
 */
BScrollView::~BScrollView()
{
}


// #pragma mark - Archiving


/**
 * @brief Create a new BScrollView from an archive message.
 *
 * @param archive The BMessage produced by Archive().
 * @return A newly allocated BScrollView, or NULL if @a archive does not
 *         represent a valid BScrollView instance.
 * @see Archive()
 */
BArchivable*
BScrollView::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BScrollView"))
		return new BScrollView(archive);

	return NULL;
}


/**
 * @brief Archive this BScrollView into a BMessage.
 *
 * Stores the border style (when it is not the default @c B_FANCY_BORDER) and
 * a flag indicating whether there is no target view.  The highlighted state
 * is intentionally not archived because it tracks transient focus state.
 *
 * @param archive The message to archive into.
 * @param deep    If true, child views (target and scroll bars) are archived
 *                recursively by the BView base class.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
BScrollView::Archive(BMessage* archive, bool deep) const
{
	status_t status = BView::Archive(archive, deep);
	if (status != B_OK)
		return status;

	// If this is a deep archive, the BView class will take care
	// of our children.

	if (status == B_OK && fBorder != B_FANCY_BORDER)
		status = archive->AddInt32("_style", fBorder);
	if (status == B_OK && fTarget == NULL)
		status = archive->AddBool("_no_target_", true);

	// The highlighted state is not archived, but since it is
	// usually (or should be) used to indicate focus, this
	// is probably the right thing to do.

	return status;
}


/**
 * @brief Complete unarchiving once all children have been attached.
 *
 * For layout-managed archives children are not yet present during the
 * constructor, so this hook re-identifies the target view and scroll bars,
 * then wires up the scroll bar targets and notifies the target view.
 *
 * @param archive The original archive message.
 * @return B_OK on success, or an error code forwarded from BView.
 * @see Archive()
 */
status_t
BScrollView::AllUnarchived(const BMessage* archive)
{
	status_t result = BView::AllUnarchived(archive);
	if (result != B_OK)
		return result;

	// search for our scroll bars and target
	int32 firstBar = 0;
	BView* view;
	while ((view = ChildAt(firstBar++)) != NULL) {
		BScrollBar *bar = dynamic_cast<BScrollBar *>(view);
		// We assume that the first non-scrollbar child view is the target.
		// So the target view can't be a BScrollBar, but who would do that?
		if (bar == NULL) {
			// in a shallow archive, we may not have a target anymore. We must
			// be prepared for this case
			if (fTarget == NULL && !archive->FindBool("_no_target_"))
				fTarget = view;
			continue;
		}

		if (bar->Orientation() == B_HORIZONTAL)
			fHorizontalScrollBar = bar;
		else if (bar->Orientation() == B_VERTICAL)
			fVerticalScrollBar = bar;
	}

	// Now connect the bars to the target, and make the target aware of them
	if (fHorizontalScrollBar)
		fHorizontalScrollBar->SetTarget(fTarget);
	if (fVerticalScrollBar)
		fVerticalScrollBar->SetTarget(fTarget);

	if (fTarget)
		fTarget->TargetedByScrollView(this);

	fPreviousWidth = uint16(Bounds().Width());
	fPreviousHeight = uint16(Bounds().Height());

	return B_OK;
}


// #pragma mark - Hook methods


/**
 * @brief Adjust scroll bar sizes when the view is added to a window.
 *
 * When only one scroll bar is present and the scroll view is positioned in
 * the bottom-right corner of a @c B_DOCUMENT_WINDOW_LOOK window, the single
 * bar is shortened to leave room for the window's resize knob.
 *
 * @see DetachedFromWindow()
 */
void
BScrollView::AttachedToWindow()
{
	BView::AttachedToWindow();

	if ((fHorizontalScrollBar == NULL && fVerticalScrollBar == NULL)
		|| (fHorizontalScrollBar != NULL && fVerticalScrollBar != NULL)
		|| Window()->Look() != B_DOCUMENT_WINDOW_LOOK) {
		return;
	}

	// If we have only one bar, we need to check if we are in the
	// bottom right edge of a window with the B_DOCUMENT_LOOK to
	// adjust the size of the bar to acknowledge the resize knob.

	BRect bounds = ConvertToScreen(Bounds());
	BRect windowBounds = Window()->Frame();

	if (bounds.right - _BorderSize() != windowBounds.right
		|| bounds.bottom - _BorderSize() != windowBounds.bottom) {
		return;
	}

	if (fHorizontalScrollBar != NULL)
		fHorizontalScrollBar->ResizeBy(-B_V_SCROLL_BAR_WIDTH, 0);
	else if (fVerticalScrollBar != NULL)
		fVerticalScrollBar->ResizeBy(0, -B_H_SCROLL_BAR_HEIGHT);
}


/**
 * @brief Called when the view is removed from its window.
 *
 * Forwards the notification to BView; no additional cleanup is required.
 *
 * @see AttachedToWindow()
 */
void
BScrollView::DetachedFromWindow()
{
	BView::DetachedFromWindow();
}


/**
 * @brief Called after all children have been attached to the window.
 *
 * Forwards the notification to BView.
 */
void
BScrollView::AllAttached()
{
	BView::AllAttached();
}


/**
 * @brief Called after all children have been detached from the window.
 *
 * Forwards the notification to BView.
 */
void
BScrollView::AllDetached()
{
	BView::AllDetached();
}


/**
 * @brief Draw the scroll view border for the given update rectangle.
 *
 * Delegates to @c be_control_look->DrawScrollViewFrame(), passing the
 * current border style, the bounding rectangles of any scroll bars, and
 * the @c B_FOCUSED flag when the border is highlighted and the window is
 * active.
 *
 * @param updateRect The region that needs to be redrawn.
 */
void
BScrollView::Draw(BRect updateRect)
{
	uint32 flags = 0;
	if (fHighlighted && Window()->IsActive())
		flags |= BControlLook::B_FOCUSED;

	BRect rect(Bounds());
	rgb_color base = ui_color(B_PANEL_BACKGROUND_COLOR);

	BRect verticalScrollBarFrame(0, 0, -1, -1);
	if (fVerticalScrollBar)
		verticalScrollBarFrame = fVerticalScrollBar->Frame();

	BRect horizontalScrollBarFrame(0, 0, -1, -1);
	if (fHorizontalScrollBar)
		horizontalScrollBarFrame = fHorizontalScrollBar->Frame();

	be_control_look->DrawScrollViewFrame(this, rect, updateRect,
		verticalScrollBarFrame, horizontalScrollBarFrame, base, fBorder,
		flags, fBorders);
}


/**
 * @brief Called when the view's position changes.
 *
 * Forwards the notification to BView; no additional work is required.
 *
 * @param newPosition The view's new left-top position in its parent's
 *                    coordinate system.
 */
void
BScrollView::FrameMoved(BPoint newPosition)
{
	BView::FrameMoved(newPosition);
}


/**
 * @brief Update scroll bar ranges and invalidate border regions on resize.
 *
 * When the target supports layout but not @c B_SCROLL_VIEW_AWARE, the scroll
 * bar ranges, steps, and proportions are recalculated from the target's
 * preferred size.  Border regions that have appeared or disappeared due to
 * the size change are also invalidated so the border is redrawn correctly.
 *
 * @param newWidth  The new width of the view in pixels.
 * @param newHeight The new height of the view in pixels.
 */
void
BScrollView::FrameResized(float newWidth, float newHeight)
{
	BView::FrameResized(newWidth, newHeight);

	const BRect bounds = Bounds();

	if (fTarget != NULL && (fTarget->Flags() & B_SUPPORTS_LAYOUT) != 0
			&& (fTarget->Flags() & B_SCROLL_VIEW_AWARE) == 0) {
		BSize size = fTarget->PreferredSize();
		if (fHorizontalScrollBar != NULL) {
			float delta = size.Width() - bounds.Width(),
				proportion = bounds.Width() / size.Width();
			if (delta < 0)
				delta = 0;

			fHorizontalScrollBar->SetRange(0, delta);
			fHorizontalScrollBar->SetSteps(be_plain_font->Size() * 1.33,
				bounds.Width());
			fHorizontalScrollBar->SetProportion(proportion);
		}
		if (fVerticalScrollBar != NULL) {
			float delta = size.Height() - bounds.Height(),
				proportion = bounds.Height() / size.Height();
			if (delta < 0)
				delta = 0;

			fVerticalScrollBar->SetRange(0, delta);
			fVerticalScrollBar->SetSteps(be_plain_font->Size() * 1.33,
				bounds.Height());
			fVerticalScrollBar->SetProportion(proportion);
		}
	}

	if (fBorder == B_NO_BORDER)
		return;

	float border = _BorderSize() - 1;

	if (fHorizontalScrollBar != NULL && fVerticalScrollBar != NULL) {
		BRect scrollCorner(bounds);
		scrollCorner.left = min_c(
			fPreviousWidth - fVerticalScrollBar->Frame().Height(),
			fHorizontalScrollBar->Frame().right + 1);
		scrollCorner.top = min_c(
			fPreviousHeight - fHorizontalScrollBar->Frame().Width(),
			fVerticalScrollBar->Frame().bottom + 1);
		Invalidate(scrollCorner);
	}

	// changes in newWidth

	if (bounds.Width() > fPreviousWidth) {
		// invalidate the region between the old and the new right border
		BRect rect = bounds;
		rect.left += fPreviousWidth - border;
		rect.right--;
		Invalidate(rect);
	} else if (bounds.Width() < fPreviousWidth) {
		// invalidate the region of the new right border
		BRect rect = bounds;
		rect.left = rect.right - border;
		Invalidate(rect);
	}

	// changes in newHeight

	if (bounds.Height() > fPreviousHeight) {
		// invalidate the region between the old and the new bottom border
		BRect rect = bounds;
		rect.top += fPreviousHeight - border;
		rect.bottom--;
		Invalidate(rect);
	} else if (bounds.Height() < fPreviousHeight) {
		// invalidate the region of the new bottom border
		BRect rect = bounds;
		rect.top = rect.bottom - border;
		Invalidate(rect);
	}

	fPreviousWidth = uint16(bounds.Width());
	fPreviousHeight = uint16(bounds.Height());
}


/**
 * @brief Forward an incoming message to BView's default handler.
 *
 * @param message The message to handle.
 */
void
BScrollView::MessageReceived(BMessage* message)
{
	BView::MessageReceived(message);
}


/**
 * @brief Forward a mouse-down event to BView.
 *
 * @param where The click position in the view's coordinate system.
 */
void
BScrollView::MouseDown(BPoint where)
{
	BView::MouseDown(where);
}


/**
 * @brief Forward a mouse-moved event to BView.
 *
 * @param where       The current cursor position in the view's coordinate
 *                    system.
 * @param code        The transit code (@c B_ENTERED_VIEW, @c B_INSIDE_VIEW,
 *                    @c B_EXITED_VIEW, or @c B_OUTSIDE_VIEW).
 * @param dragMessage The drag-and-drop message, or NULL if no drag is active.
 */
void
BScrollView::MouseMoved(BPoint where, uint32 code,
	const BMessage* dragMessage)
{
	BView::MouseMoved(where, code, dragMessage);
}


/**
 * @brief Forward a mouse-up event to BView.
 *
 * @param where The release position in the view's coordinate system.
 */
void
BScrollView::MouseUp(BPoint where)
{
	BView::MouseUp(where);
}


/**
 * @brief Redraw the border highlight when the window activation state changes.
 *
 * When the scroll view has a highlighted border the entire view is
 * invalidated so the focus indicator is repainted with the correct colour
 * for the new activation state.
 *
 * @param active true if the window just became active, false if it became
 *               inactive.
 */
void
BScrollView::WindowActivated(bool active)
{
	if (fHighlighted)
		Invalidate();

	BView::WindowActivated(active);
}


// #pragma mark - Size methods


/**
 * @brief Return the preferred size as separate width and height values.
 *
 * Convenience wrapper around PreferredSize() for callers that need the
 * dimensions as individual floats rather than a BSize.
 *
 * @param[out] _width  Set to the preferred width, if not NULL.
 * @param[out] _height Set to the preferred height, if not NULL.
 * @see PreferredSize()
 */
void
BScrollView::GetPreferredSize(float* _width, float* _height)
{
	BSize size = PreferredSize();

	if (_width)
		*_width = size.width;

	if (_height)
		*_height = size.height;
}


/**
 * @brief Resize the scroll view to its preferred size.
 *
 * Does nothing if the view is not currently attached to a window.
 *
 * @see GetPreferredSize()
 * @see PreferredSize()
 */
void
BScrollView::ResizeToPreferred()
{
	if (Window() == NULL)
		return;
	BView::ResizeToPreferred();
}


/**
 * @brief Grant or remove input focus.
 *
 * Forwards the focus change to BView without additional action; subclasses
 * can override to respond to focus transitions.
 *
 * @param focus true to grant focus, false to remove it.
 */
void
BScrollView::MakeFocus(bool focus)
{
	BView::MakeFocus(focus);
}


/**
 * @brief Return the minimum size of the scroll view.
 *
 * Computes the minimum size by adding the border and scroll bar dimensions
 * to the target's own minimum size (or a default of 16×16 if there is no
 * target).  The result is composed with any explicit minimum size set by
 * the user.
 *
 * @return The minimum BSize for layout negotiation.
 * @see MaxSize()
 * @see PreferredSize()
 */
BSize
BScrollView::MinSize()
{
	BSize size = _ComputeSize(fTarget != NULL ? fTarget->MinSize()
		: BSize(16, 16));

	return BLayoutUtils::ComposeSize(ExplicitMinSize(), size);
}


/**
 * @brief Return the maximum size of the scroll view.
 *
 * Computes the maximum size by adding the border and scroll bar dimensions
 * to the target's own maximum size (or @c B_SIZE_UNLIMITED if there is no
 * target).  The result is composed with any explicit maximum size set by
 * the user.
 *
 * @return The maximum BSize for layout negotiation.
 * @see MinSize()
 * @see PreferredSize()
 */
BSize
BScrollView::MaxSize()
{
	BSize size = _ComputeSize(fTarget != NULL ? fTarget->MaxSize()
		: BSize(B_SIZE_UNLIMITED, B_SIZE_UNLIMITED));

	return BLayoutUtils::ComposeSize(ExplicitMaxSize(), size);
}


/**
 * @brief Return the preferred size of the scroll view.
 *
 * Computes the preferred size by adding the border and scroll bar dimensions
 * to the target's preferred size (or 32×32 if there is no target).  The
 * result is composed with any explicit preferred size set by the user.
 *
 * @return The preferred BSize for layout negotiation.
 * @see MinSize()
 * @see MaxSize()
 */
BSize
BScrollView::PreferredSize()
{
	BSize size = _ComputeSize(fTarget != NULL ? fTarget->PreferredSize()
		: BSize(32, 32));

	return BLayoutUtils::ComposeSize(ExplicitPreferredSize(), size);
}


// #pragma mark - BScrollView methods


/**
 * @brief Return the scroll bar for the given orientation.
 *
 * @param direction @c B_HORIZONTAL or @c B_VERTICAL.
 * @return The requested BScrollBar, or NULL if no bar was created for that
 *         orientation.
 * @see SetBorder()
 */
BScrollBar*
BScrollView::ScrollBar(orientation direction) const
{
	if (direction == B_HORIZONTAL)
		return fHorizontalScrollBar;

	return fVerticalScrollBar;
}


/**
 * @brief Change the border style of the scroll view.
 *
 * For layout-aware views the border change triggers DoLayout() and a full
 * invalidate.  For legacy non-layout views the view and all its children
 * are repositioned and resized to reflect the new border thickness.
 *
 * @param border The new border style: @c B_NO_BORDER, @c B_PLAIN_BORDER, or
 *               @c B_FANCY_BORDER.
 * @see Border()
 * @see SetBorderHighlighted()
 */
void
BScrollView::SetBorder(border_style border)
{
	if (fBorder == border)
		return;

	if ((Flags() & B_SUPPORTS_LAYOUT) != 0) {
		fBorder = border;
		SetFlags(_ModifyFlags(Flags(), fTarget, border));

		DoLayout();
		Invalidate();
		return;
	}

	float offset = _BorderSize() - _BorderSize(border);
	float resize = 2 * offset;

	float horizontalGap = 0, verticalGap = 0;
	float change = 0;
	if (border == B_NO_BORDER || fBorder == B_NO_BORDER) {
		if (fHorizontalScrollBar != NULL)
			verticalGap = border != B_NO_BORDER ? 1 : -1;
		if (fVerticalScrollBar != NULL)
			horizontalGap = border != B_NO_BORDER ? 1 : -1;

		change = border != B_NO_BORDER ? -1 : 1;
		if (fHorizontalScrollBar == NULL || fVerticalScrollBar == NULL)
			change *= 2;
	}

	fBorder = border;

	int32 savedResizingMode = 0;
	if (fTarget != NULL) {
		savedResizingMode = fTarget->ResizingMode();
		fTarget->SetResizingMode(B_FOLLOW_NONE);
	}

	MoveBy(offset, offset);
	ResizeBy(-resize - horizontalGap, -resize - verticalGap);

	if (fTarget != NULL) {
		fTarget->MoveBy(-offset, -offset);
		fTarget->SetResizingMode(savedResizingMode);
	}

	if (fHorizontalScrollBar != NULL) {
		fHorizontalScrollBar->MoveBy(-offset - verticalGap, offset + verticalGap);
		fHorizontalScrollBar->ResizeBy(resize + horizontalGap - change, 0);
	}
	if (fVerticalScrollBar != NULL) {
		fVerticalScrollBar->MoveBy(offset + horizontalGap, -offset - horizontalGap);
		fVerticalScrollBar->ResizeBy(0, resize + verticalGap - change);
	}

	SetFlags(_ModifyFlags(Flags(), fTarget, border));
}


/**
 * @brief Return the current border style.
 *
 * @return The active border_style value.
 * @see SetBorder()
 */
border_style
BScrollView::Border() const
{
	return fBorder;
}


/**
 * @brief Set which sides of the border are drawn.
 *
 * Only meaningful for layout-aware scroll views (@c B_SUPPORTS_LAYOUT must
 * be set).  Triggers a layout pass and full invalidate so the new set of
 * visible borders takes effect immediately.
 *
 * @param borders A bitmask of @c BControlLook border flags (e.g.
 *                @c BControlLook::B_ALL_BORDERS).
 * @see Borders()
 */
void
BScrollView::SetBorders(uint32 borders)
{
	if (fBorders == borders || (Flags() & B_SUPPORTS_LAYOUT) == 0)
		return;

	fBorders = borders;
	DoLayout();
	Invalidate();
}


/**
 * @brief Return the current set of drawn border sides.
 *
 * @return A bitmask of @c BControlLook border flags.
 * @see SetBorders()
 */
uint32
BScrollView::Borders() const
{
	return fBorders;
}


/**
 * @brief Highlight or un-highlight the scroll view's fancy border.
 *
 * Highlighting is typically used to indicate that the contained target view
 * has keyboard focus.  Only effective when the border style is
 * @c B_FANCY_BORDER; returns @c B_ERROR for other border styles.  The four
 * border edge strips are individually invalidated to minimise redraw.
 *
 * @param highlight true to draw the border with focus highlighting, false to
 *                  draw it normally.
 * @return B_OK on success.
 * @retval B_ERROR If the border style is not @c B_FANCY_BORDER.
 * @see IsBorderHighlighted()
 * @see SetBorder()
 */
status_t
BScrollView::SetBorderHighlighted(bool highlight)
{
	if (fHighlighted == highlight)
		return B_OK;

	if (fBorder != B_FANCY_BORDER)
		// highlighting only works for B_FANCY_BORDER
		return B_ERROR;

	fHighlighted = highlight;

	if (fHorizontalScrollBar != NULL)
		fHorizontalScrollBar->SetBorderHighlighted(highlight);
	if (fVerticalScrollBar != NULL)
		fVerticalScrollBar->SetBorderHighlighted(highlight);

	BRect bounds = Bounds();
	bounds.InsetBy(1, 1);

	Invalidate(BRect(bounds.left, bounds.top, bounds.right, bounds.top));
	Invalidate(BRect(bounds.left, bounds.top + 1, bounds.left,
		bounds.bottom - 1));
	Invalidate(BRect(bounds.right, bounds.top + 1, bounds.right,
		bounds.bottom - 1));
	Invalidate(BRect(bounds.left, bounds.bottom, bounds.right, bounds.bottom));

	return B_OK;
}


/**
 * @brief Return whether the border is currently highlighted.
 *
 * @return true if the fancy border is drawn with focus highlighting.
 * @see SetBorderHighlighted()
 */
bool
BScrollView::IsBorderHighlighted() const
{
	return fHighlighted;
}


/**
 * @brief Replace the scrolled target view.
 *
 * Detaches the existing target (without deleting it), then adds the new
 * target as the topmost child, connects the scroll bars to it, and notifies
 * it via BView::TargetedByScrollView().
 *
 * @param target The new target view, or NULL to remove the current target.
 * @see Target()
 */
void
BScrollView::SetTarget(BView* target)
{
	if (fTarget == target)
		return;

	if (fTarget != NULL) {
		fTarget->TargetedByScrollView(NULL);
		RemoveChild(fTarget);

		// we are not supposed to delete the view
	}

	fTarget = target;
	if (fHorizontalScrollBar != NULL)
		fHorizontalScrollBar->SetTarget(target);
	if (fVerticalScrollBar != NULL)
		fVerticalScrollBar->SetTarget(target);

	if (target != NULL) {
		float borderSize = _BorderSize();
		target->MoveTo((fBorders & BControlLook::B_LEFT_BORDER) != 0
			? borderSize : 0, (fBorders & BControlLook::B_TOP_BORDER) != 0
				? borderSize : 0);
		BRect innerFrame = _InnerFrame();
		target->ResizeTo(innerFrame.Width() - 1, innerFrame.Height() - 1);
		target->TargetedByScrollView(this);

		AddChild(target, ChildAt(0));
			// This way, we are making sure that the target will
			// be added top most in the list (which is important
			// for unarchiving)
	}

	SetFlags(_ModifyFlags(Flags(), fTarget, fBorder));
}


/**
 * @brief Return the current scrolled target view.
 *
 * @return The target BView, or NULL if no target is set.
 * @see SetTarget()
 */
BView*
BScrollView::Target() const
{
	return fTarget;
}


// #pragma mark - Scripting methods



/**
 * @brief Resolve a scripting specifier to the appropriate BHandler.
 *
 * Delegates to BView's default implementation.
 *
 * @param message   The scripting message.
 * @param index     The index of the current specifier in the message.
 * @param specifier The current specifier message.
 * @param what      The specifier form constant.
 * @param property  The property name being targeted.
 * @return The BHandler that should handle the scripted request.
 */
BHandler*
BScrollView::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return BView::ResolveSpecifier(message, index, specifier, what, property);
}


/**
 * @brief Add the supported scripting suites to a reply message.
 *
 * Delegates to BView's default implementation.
 *
 * @param message The reply message to populate with suite information.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BScrollView::GetSupportedSuites(BMessage* message)
{
	return BView::GetSupportedSuites(message);
}


//	#pragma mark - Perform


/**
 * @brief Dispatch a binary-compatibility perform code.
 *
 * Handles layout-related perform codes (@c PERFORM_CODE_MIN_SIZE,
 * @c PERFORM_CODE_MAX_SIZE, @c PERFORM_CODE_PREFERRED_SIZE,
 * @c PERFORM_CODE_LAYOUT_ALIGNMENT, @c PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH,
 * @c PERFORM_CODE_GET_HEIGHT_FOR_WIDTH, @c PERFORM_CODE_SET_LAYOUT,
 * @c PERFORM_CODE_LAYOUT_INVALIDATED, @c PERFORM_CODE_DO_LAYOUT) and
 * forwards unknown codes to BView::Perform().
 *
 * @param code  The perform operation code.
 * @param _data In/out data buffer whose layout depends on @a code.
 * @return B_OK if the code was handled, otherwise the result of
 *         BView::Perform().
 */
status_t
BScrollView::Perform(perform_code code, void* _data)
{
	switch (code) {
		case PERFORM_CODE_MIN_SIZE:
			((perform_data_min_size*)_data)->return_value
				= BScrollView::MinSize();
			return B_OK;

		case PERFORM_CODE_MAX_SIZE:
			((perform_data_max_size*)_data)->return_value
				= BScrollView::MaxSize();
			return B_OK;

		case PERFORM_CODE_PREFERRED_SIZE:
			((perform_data_preferred_size*)_data)->return_value
				= BScrollView::PreferredSize();
			return B_OK;

		case PERFORM_CODE_LAYOUT_ALIGNMENT:
			((perform_data_layout_alignment*)_data)->return_value
				= BScrollView::LayoutAlignment();
			return B_OK;

		case PERFORM_CODE_HAS_HEIGHT_FOR_WIDTH:
			((perform_data_has_height_for_width*)_data)->return_value
				= BScrollView::HasHeightForWidth();
			return B_OK;

		case PERFORM_CODE_GET_HEIGHT_FOR_WIDTH:
		{
			perform_data_get_height_for_width* data
				= (perform_data_get_height_for_width*)_data;
			BScrollView::GetHeightForWidth(data->width, &data->min, &data->max,
				&data->preferred);
			return B_OK;
		}

		case PERFORM_CODE_SET_LAYOUT:
		{
			perform_data_set_layout* data = (perform_data_set_layout*)_data;
			BScrollView::SetLayout(data->layout);
			return B_OK;
		}

		case PERFORM_CODE_LAYOUT_INVALIDATED:
		{
			perform_data_layout_invalidated* data
				= (perform_data_layout_invalidated*)_data;
			BScrollView::LayoutInvalidated(data->descendants);
			return B_OK;
		}

		case PERFORM_CODE_DO_LAYOUT:
		{
			BScrollView::DoLayout();
			return B_OK;
		}
	}

	return BView::Perform(code, _data);
}


//	#pragma mark - Protected methods


/**
 * @brief Called when the layout has been invalidated.
 *
 * Hook for subclasses; the base implementation does nothing.
 *
 * @param descendants true if descendant layouts were also invalidated.
 */
void
BScrollView::LayoutInvalidated(bool descendants)
{
}


/**
 * @brief Position and size the target and scroll bars according to the layout.
 *
 * If the view does not support layout (@c B_SUPPORTS_LAYOUT is not set), the
 * method returns immediately.  If a user-provided layout is attached, the
 * base class version is called.  Otherwise the inner frame is computed from
 * the current bounds, the target is positioned within it, and the scroll bars
 * are aligned around it via _AlignScrollBars().
 *
 * @see _InnerFrame()
 * @see _AlignScrollBars()
 */
void
BScrollView::DoLayout()
{
	if ((Flags() & B_SUPPORTS_LAYOUT) == 0)
		return;

	// If the user set a layout, we let the base class version call its hook.
	if (GetLayout() != NULL) {
		BView::DoLayout();
		return;
	}

	BRect innerFrame = _InnerFrame();

	if (fTarget != NULL) {
		fTarget->MoveTo(innerFrame.left, innerFrame.top);
		fTarget->ResizeTo(innerFrame.Width(), innerFrame.Height());

		//BLayoutUtils::AlignInFrame(fTarget, fTarget->Bounds());
	}

	_AlignScrollBars(fHorizontalScrollBar != NULL, fVerticalScrollBar != NULL,
		innerFrame);
}


// #pragma mark - Private methods


/**
 * @brief Shared initialisation logic called by all public constructors.
 *
 * Creates any requested scroll bars, positions them around the target frame,
 * adds all children, and records the initial bounds so that FrameResized()
 * can compute delta regions for border invalidation.
 *
 * @param horizontal If true, a horizontal scroll bar is created.
 * @param vertical   If true, a vertical scroll bar is created.
 */
void
BScrollView::_Init(bool horizontal, bool vertical)
{
	fHorizontalScrollBar = NULL;
	fVerticalScrollBar = NULL;
	fHighlighted = false;
	fBorders = BControlLook::B_ALL_BORDERS;

	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	if (horizontal) {
		fHorizontalScrollBar = new BScrollBar(BRect(0, 0, 14, 14), "_HSB_",
			fTarget, 0, 1000, B_HORIZONTAL);
		AddChild(fHorizontalScrollBar);
	}

	if (vertical) {
		fVerticalScrollBar = new BScrollBar(BRect(0, 0, 14, 14), "_VSB_",
			fTarget, 0, 1000, B_VERTICAL);
		AddChild(fVerticalScrollBar);
	}

	if ((Flags() & B_SUPPORTS_LAYOUT) == 0) {
		BRect frame = _ComputeFrame(fTarget, fHorizontalScrollBar,
			fVerticalScrollBar, fBorder, BControlLook::B_ALL_BORDERS);
		MoveTo(frame.LeftTop());
		ResizeTo(frame.Size());
	}

	BRect targetFrame;
	if (fTarget) {
		// layout target and add it
		fTarget->TargetedByScrollView(this);
		fTarget->MoveTo(B_ORIGIN);

		if (fBorder != B_NO_BORDER)
			fTarget->MoveBy(_BorderSize(), _BorderSize());

		AddChild(fTarget);
		targetFrame = fTarget->Frame();
	} else {
		// no target specified
		targetFrame = Bounds();
		if (horizontal)
			targetFrame.bottom -= fHorizontalScrollBar->PreferredSize().Height() + 1;
		if (vertical)
			targetFrame.right -= fVerticalScrollBar->PreferredSize().Width() + 1;
		if (fBorder == B_FANCY_BORDER) {
			targetFrame.bottom--;
			targetFrame.right--;
		}
	}

	_AlignScrollBars(horizontal, vertical, targetFrame);

	fPreviousWidth = uint16(Bounds().Width());
	fPreviousHeight = uint16(Bounds().Height());
}


/**
 * @brief Return the pixel thickness of the current border style.
 *
 * Convenience overload that uses the instance's @c fBorder field.
 *
 * @return The border thickness in pixels.
 * @see _BorderSize(border_style)
 */
float
BScrollView::_BorderSize() const
{
	return _BorderSize(fBorder);
}


/**
 * @brief Compute the rectangle available for the target view.
 *
 * Starts from the view's bounds, insets by the border thickness on the
 * enabled border sides, and then subtracts the preferred heights/widths of
 * any present scroll bars.
 *
 * @return The inner BRect in the view's coordinate system.
 * @see _InsetBorders()
 */
BRect
BScrollView::_InnerFrame() const
{
	BRect frame = Bounds();
	_InsetBorders(frame, fBorder, fBorders);

	float borderSize = _BorderSize();

	if (fHorizontalScrollBar != NULL) {
		frame.bottom -= fHorizontalScrollBar->PreferredSize().Height();
		if (borderSize == 0)
			frame.bottom--;
	}
	if (fVerticalScrollBar != NULL) {
		frame.right -= fVerticalScrollBar->PreferredSize().Width();
		if (borderSize == 0)
			frame.right--;
	}

	return frame;
}


/**
 * @brief Compute the overall scroll view size for a given target size.
 *
 * Wraps the target size in a temporary BRect and delegates to the static
 * _ComputeFrame() overload, then returns the resulting BSize.
 *
 * @param targetSize The size of the target view.
 * @return The total BSize that the scroll view would occupy.
 * @see _ComputeFrame()
 */
BSize
BScrollView::_ComputeSize(BSize targetSize) const
{
	BRect frame = _ComputeFrame(
		BRect(0, 0, targetSize.width, targetSize.height));

	return BSize(frame.Width(), frame.Height());
}


/**
 * @brief Compute the scroll view frame for a given target rectangle.
 *
 * Instance-level convenience that calls the static overload with the
 * current scroll bar pointers, border style, and border-side mask.
 *
 * @param targetRect The frame of the target view.
 * @return The frame the scroll view would occupy.
 * @see _ComputeFrame(BRect, BScrollBar*, BScrollBar*, border_style, uint32)
 */
BRect
BScrollView::_ComputeFrame(BRect targetRect) const
{
	return _ComputeFrame(targetRect, fHorizontalScrollBar,
		fVerticalScrollBar, fBorder, fBorders);
}


/**
 * @brief Position and resize the scroll bars to align with a target frame.
 *
 * Moves and resizes the horizontal bar immediately below the target frame
 * and the vertical bar immediately to the right.  When both bars are
 * present, or when a border is drawn, each bar is extended by one pixel so
 * that it overlaps the border or the corner square between the two bars.
 *
 * @param horizontal  true if a horizontal scroll bar exists.
 * @param vertical    true if a vertical scroll bar exists.
 * @param targetFrame The frame of the target view in the scroll view's
 *                    coordinate system.
 */
void
BScrollView::_AlignScrollBars(bool horizontal, bool vertical, BRect targetFrame)
{
	if (horizontal) {
		BRect rect = targetFrame;
		rect.top = rect.bottom + 1;
		rect.bottom = rect.top + fHorizontalScrollBar->PreferredSize().Height();
		if (fBorder != B_NO_BORDER || vertical) {
			// extend scrollbar so that it overlaps one pixel with vertical
			// scrollbar
			rect.right++;
		}

		if (fBorder != B_NO_BORDER) {
			// the scrollbar draws part of the surrounding frame on the left
			rect.left--;
		}

		fHorizontalScrollBar->MoveTo(rect.left, rect.top);
		fHorizontalScrollBar->ResizeTo(rect.Width(), rect.Height());
	}

	if (vertical) {
		BRect rect = targetFrame;
		rect.left = rect.right + 1;
		rect.right = rect.left + fVerticalScrollBar->PreferredSize().Width();
		if (fBorder != B_NO_BORDER || horizontal) {
			// extend scrollbar so that it overlaps one pixel with vertical
			// scrollbar
			rect.bottom++;
		}

		if (fBorder != B_NO_BORDER) {
			// the scrollbar draws part of the surrounding frame on the left
			rect.top--;
		}

		fVerticalScrollBar->MoveTo(rect.left, rect.top);
		fVerticalScrollBar->ResizeTo(rect.Width(), rect.Height());
	}
}


/*!	This static method is used to calculate the frame that the
	ScrollView will cover depending on the frame of its target
	and which border style is used.
	It is used in the constructor and at other places.
*/
/**
 * @brief Compute the outer scroll view frame from a target frame and scroll
 *        bar configuration.
 *
 * Expands @a frame to include the preferred sizes of any supplied scroll
 * bars, clamps to the scroll bars' minimum sizes when needed, then expands
 * further by the border thickness on the enabled sides.  When the border
 * has zero thickness an extra pixel is added for each present scroll bar to
 * close the gap between bar and target.
 *
 * @param frame      The frame of the target view.
 * @param horizontal The horizontal BScrollBar, or NULL if none.
 * @param vertical   The vertical BScrollBar, or NULL if none.
 * @param border     The border style to apply.
 * @param borders    Bitmask of border sides that are drawn.
 * @return The frame that the BScrollView should occupy.
 */
/*static*/ BRect
BScrollView::_ComputeFrame(BRect frame, BScrollBar* horizontal,
	BScrollBar* vertical, border_style border, uint32 borders)
{
	if (vertical != NULL)
		frame.right += vertical->PreferredSize().Width();
	if (horizontal != NULL)
		frame.bottom += horizontal->PreferredSize().Height();

	// Take the other minimum dimensions into account, too, but only if
	// the frame already has a greater-than-zero value for them. Otherwise,
	// non-layouted applications could wind up with broken layouts.
	if (vertical != NULL) {
		const float minHeight = vertical->MinSize().Height();
		if (frame.Height() > 0 && frame.Height() < minHeight)
			frame.bottom += minHeight - frame.Height();
	}
	if (horizontal != NULL) {
		const float minWidth = horizontal->MinSize().Width();
		if (frame.Width() > 0 && frame.Width() < minWidth)
			frame.right += minWidth - frame.Width();
	}

	_InsetBorders(frame, border, borders, true);

	if (_BorderSize(border) == 0) {
		if (vertical != NULL)
			frame.right++;
		if (horizontal != NULL)
			frame.bottom++;
	}

	return frame;
}


/**
 * @brief Compute the outer scroll view frame from a target view pointer.
 *
 * Convenience overload that reads the target's current frame (falling back
 * to a 16×16 default if @a target is NULL) and delegates to the rectangle
 * overload.
 *
 * @param target     The target view whose frame is used, or NULL.
 * @param horizontal The horizontal BScrollBar, or NULL if none.
 * @param vertical   The vertical BScrollBar, or NULL if none.
 * @param border     The border style to apply.
 * @param borders    Bitmask of border sides that are drawn.
 * @return The frame that the BScrollView should occupy.
 * @see _ComputeFrame(BRect, BScrollBar*, BScrollBar*, border_style, uint32)
 */
/*static*/ BRect
BScrollView::_ComputeFrame(BView *target, BScrollBar* horizontal,
	BScrollBar* vertical, border_style border, uint32 borders)
{
	return _ComputeFrame(target != NULL ? target->Frame()
		: BRect(0, 0, 16, 16), horizontal, vertical, border, borders);
}


/**
 * @brief Return the pixel thickness for the given border style.
 *
 * @param border The border style to query.
 * @return @c kFancyBorderSize for @c B_FANCY_BORDER,
 *         @c kPlainBorderSize for @c B_PLAIN_BORDER, or 0 for
 *         @c B_NO_BORDER.
 */
/*static*/ float
BScrollView::_BorderSize(border_style border)
{
	if (border == B_FANCY_BORDER)
		return kFancyBorderSize;
	if (border == B_PLAIN_BORDER)
		return kPlainBorderSize;

	return 0;
}


/**
 * @brief Augment view creation flags to satisfy border and layout requirements.
 *
 * Adds @c B_FRAME_EVENTS when the target is layout-aware but not scroll-view
 * aware (so that FrameResized() can update scroll ranges).  Adds
 * @c B_WILL_DRAW and either @c B_FULL_UPDATE_ON_RESIZE or @c B_FRAME_EVENTS
 * when a border is present so the border is always repainted correctly.
 *
 * @param flags  The flags as supplied by the caller.
 * @param target The target view, or NULL.
 * @param border The border style.
 * @return The adjusted flags value.
 */
/*static*/ uint32
BScrollView::_ModifyFlags(uint32 flags, BView* target, border_style border)
{
	if (target != NULL && (target->Flags() & B_SUPPORTS_LAYOUT) != 0
			&& (target->Flags() & B_SCROLL_VIEW_AWARE) == 0)
		flags |= B_FRAME_EVENTS;

	// We either need B_FULL_UPDATE_ON_RESIZE or B_FRAME_EVENTS if we have
	// to draw a border.
	if (border != B_NO_BORDER) {
		flags |= B_WILL_DRAW | ((flags & B_FULL_UPDATE_ON_RESIZE) != 0 ?
			0 : B_FRAME_EVENTS);
	}

	return flags;
}


/**
 * @brief Inset (or expand) a rectangle by the border thickness on each
 *        enabled side.
 *
 * When @a expand is false the rectangle is shrunk inward by the border size
 * on each side included in @a borders; when @a expand is true the signs are
 * reversed so the rectangle grows outward.
 *
 * @param[in,out] frame  The rectangle to modify in place.
 * @param         border The border style that determines the thickness.
 * @param         borders Bitmask of border sides to include
 *                        (e.g. @c BControlLook::B_ALL_BORDERS).
 * @param         expand If true, expand the rectangle; if false, inset it.
 */
/*static*/ void
BScrollView::_InsetBorders(BRect& frame, border_style border, uint32 borders, bool expand)
{
	float borderSize = _BorderSize(border);
	if (expand)
		borderSize = -borderSize;
	if ((borders & BControlLook::B_LEFT_BORDER) != 0)
		frame.left += borderSize;
	if ((borders & BControlLook::B_TOP_BORDER) != 0)
		frame.top += borderSize;
	if ((borders & BControlLook::B_RIGHT_BORDER) != 0)
		frame.right -= borderSize;
	if ((borders & BControlLook::B_BOTTOM_BORDER) != 0)
		frame.bottom -= borderSize;
}


//	#pragma mark - FBC and forbidden


/**
 * @brief Forbidden copy-assignment operator (FBC padding).
 *
 * @return A reference to this object (assignment is a no-op).
 */
BScrollView&
BScrollView::operator=(const BScrollView &)
{
	return *this;
}


void BScrollView::_ReservedScrollView1() {}
void BScrollView::_ReservedScrollView2() {}
void BScrollView::_ReservedScrollView3() {}
void BScrollView::_ReservedScrollView4() {}


/**
 * @brief GCC-2/GCC-3 binary-compatibility trampoline for InvalidateLayout().
 *
 * Dispatches @c PERFORM_CODE_LAYOUT_INVALIDATED via Perform() so that the
 * correct virtual override is called regardless of the ABI in use.
 *
 * @param view        The BScrollView whose layout should be invalidated.
 * @param descendants If true, descendant layouts are also invalidated.
 */
extern "C" void
B_IF_GCC_2(InvalidateLayout__11BScrollViewb,
	_ZN11BScrollView16InvalidateLayoutEb)(BScrollView* view, bool descendants)
{
	perform_data_layout_invalidated data;
	data.descendants = descendants;

	view->Perform(PERFORM_CODE_LAYOUT_INVALIDATED, &data);
}
