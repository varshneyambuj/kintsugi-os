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
 *   Copyright 2006 Ingo Weinhold / Copyright 2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */


/**
 * @file SplitView.cpp
 * @brief Implementation of BSplitView, a view with resizable split panes
 *
 * BSplitView combines BSplitLayout with interactive divider handling. The user
 * can drag the dividers to resize panes. BSplitView manages cursor changes and
 * mouse tracking for the drag interaction.
 *
 * @see BSplitLayout, BView
 */


#include <SplitView.h>

#include <stdio.h>

#include <Archivable.h>
#include <ControlLook.h>
#include <Cursor.h>

#include "SplitLayout.h"


/**
 * @brief Construct a BSplitView with a given orientation and splitter spacing.
 *
 * Creates an internal BSplitLayout and passes it to the BView base class.
 * The view is configured with B_WILL_DRAW, B_FULL_UPDATE_ON_RESIZE, and
 * B_INVALIDATE_AFTER_LAYOUT flags.
 *
 * @param orientation B_HORIZONTAL to stack panes left-to-right, or B_VERTICAL
 *                    to stack them top-to-bottom.
 * @param spacing     The pixel width of the splitter handle between panes.
 */
BSplitView::BSplitView(orientation orientation, float spacing)
	:
	BView(NULL,
		B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_INVALIDATE_AFTER_LAYOUT,
		fSplitLayout = new BSplitLayout(orientation, spacing))
{
}


/**
 * @brief Construct a BSplitView from an archived BMessage.
 *
 * @param from The archive message produced by Archive(). The layout is
 *             recovered in AllUnarchived() once the full object graph is
 *             available.
 * @see Instantiate()
 * @see AllUnarchived()
 */
BSplitView::BSplitView(BMessage* from)
	:
	BView(BUnarchiver::PrepareArchive(from)),
	fSplitLayout(NULL)
{
	BUnarchiver(from).Finish();
}


/**
 * @brief Destroy the BSplitView.
 *
 * The BSplitLayout is owned by the BView layout system and is therefore
 * not deleted explicitly here.
 */
BSplitView::~BSplitView()
{
}


/**
 * @brief Set all four insets of the split layout individually.
 *
 * Each value is first passed through BControlLook::ComposeSpacing() to
 * resolve semantic spacing constants before being applied.
 *
 * @param left   Space between the left edge of the view and its first pane.
 * @param top    Space between the top edge of the view and its first pane.
 * @param right  Space between the last pane and the right edge of the view.
 * @param bottom Space between the last pane and the bottom edge of the view.
 */
void
BSplitView::SetInsets(float left, float top, float right, float bottom)
{
	left = BControlLook::ComposeSpacing(left);
	top = BControlLook::ComposeSpacing(top);
	right = BControlLook::ComposeSpacing(right);
	bottom = BControlLook::ComposeSpacing(bottom);

	fSplitLayout->SetInsets(left, top, right, bottom);
}


/**
 * @brief Set horizontal and vertical insets symmetrically.
 *
 * @param horizontal Applied to both the left and right edges.
 * @param vertical   Applied to both the top and bottom edges.
 */
void
BSplitView::SetInsets(float horizontal, float vertical)
{
	horizontal = BControlLook::ComposeSpacing(horizontal);
	vertical = BControlLook::ComposeSpacing(vertical);
	fSplitLayout->SetInsets(horizontal, vertical, horizontal, vertical);
}


/**
 * @brief Set a uniform inset on all four edges.
 *
 * @param insets The spacing value applied to every edge after composing.
 */
void
BSplitView::SetInsets(float insets)
{
	insets = BControlLook::ComposeSpacing(insets);
	fSplitLayout->SetInsets(insets, insets, insets, insets);
}


/**
 * @brief Retrieve the current insets of the split layout.
 *
 * @param left   Receives the left inset in pixels.
 * @param top    Receives the top inset in pixels.
 * @param right  Receives the right inset in pixels.
 * @param bottom Receives the bottom inset in pixels.
 */
void
BSplitView::GetInsets(float* left, float* top, float* right,
	float* bottom) const
{
	fSplitLayout->GetInsets(left, top, right, bottom);
}


/**
 * @brief Return the current pixel spacing between panes.
 *
 * @return The splitter spacing in pixels.
 * @see SetSpacing()
 */
float
BSplitView::Spacing() const
{
	return fSplitLayout->Spacing();
}


/**
 * @brief Set the pixel spacing between panes.
 *
 * @param spacing The new spacing in pixels.
 * @see Spacing()
 */
void
BSplitView::SetSpacing(float spacing)
{
	fSplitLayout->SetSpacing(spacing);
}


/**
 * @brief Return the current split orientation.
 *
 * @return B_HORIZONTAL or B_VERTICAL.
 * @see SetOrientation()
 */
orientation
BSplitView::Orientation() const
{
	return fSplitLayout->Orientation();
}


/**
 * @brief Change the split orientation.
 *
 * @param orientation B_HORIZONTAL to arrange panes side by side, or
 *                    B_VERTICAL to stack them.
 * @see Orientation()
 */
void
BSplitView::SetOrientation(orientation orientation)
{
	fSplitLayout->SetOrientation(orientation);
}


/**
 * @brief Return the pixel thickness of the splitter handle.
 *
 * @return Splitter handle size in pixels.
 * @see SetSplitterSize()
 */
float
BSplitView::SplitterSize() const
{
	return fSplitLayout->SplitterSize();
}


/**
 * @brief Set the pixel thickness of the splitter handle.
 *
 * @param size The new splitter handle thickness in pixels.
 * @see SplitterSize()
 */
void
BSplitView::SetSplitterSize(float size)
{
	fSplitLayout->SetSplitterSize(size);
}


/**
 * @brief Return the number of child panes currently managed by the layout.
 *
 * @return The pane count (not counting splitter items).
 */
int32
BSplitView::CountItems() const
{
	return fSplitLayout->CountItems();
}


/**
 * @brief Return the weight of the pane at the given index.
 *
 * @param index Zero-based pane index.
 * @return The weight as a positive floating-point value.
 * @see SetItemWeight()
 */
float
BSplitView::ItemWeight(int32 index) const
{
	return fSplitLayout->ItemWeight(index);
}


/**
 * @brief Return the weight of a specific layout item.
 *
 * @param item The layout item to query.
 * @return The weight as a positive floating-point value.
 * @see SetItemWeight()
 */
float
BSplitView::ItemWeight(BLayoutItem* item) const
{
	return fSplitLayout->ItemWeight(item);
}


/**
 * @brief Set the relative weight of the pane at the given index.
 *
 * @param index             Zero-based pane index.
 * @param weight            The new weight; larger values claim more space.
 * @param invalidateLayout  If true, trigger an immediate layout update.
 * @see ItemWeight()
 */
void
BSplitView::SetItemWeight(int32 index, float weight, bool invalidateLayout)
{
	fSplitLayout->SetItemWeight(index, weight, invalidateLayout);
}


/**
 * @brief Set the relative weight of a specific layout item.
 *
 * @param item   The layout item whose weight should be changed.
 * @param weight The new weight value.
 * @see ItemWeight()
 */
void
BSplitView::SetItemWeight(BLayoutItem* item, float weight)
{
	fSplitLayout->SetItemWeight(item, weight);
}


/**
 * @brief Return whether the pane at the given index can be collapsed.
 *
 * @param index Zero-based pane index.
 * @return True if the pane may be collapsed to zero size, false otherwise.
 * @see SetCollapsible()
 */
bool
BSplitView::IsCollapsible(int32 index) const
{
	return fSplitLayout->IsCollapsible(index);
}


/**
 * @brief Set whether all panes can be collapsed.
 *
 * @param collapsible True to allow all panes to collapse, false to prevent it.
 * @see IsCollapsible()
 */
void
BSplitView::SetCollapsible(bool collapsible)
{
	fSplitLayout->SetCollapsible(collapsible);
}


/**
 * @brief Set whether a specific pane can be collapsed.
 *
 * @param index       Zero-based pane index.
 * @param collapsible True to allow the pane to collapse, false to prevent it.
 * @see IsCollapsible()
 */
void
BSplitView::SetCollapsible(int32 index, bool collapsible)
{
	fSplitLayout->SetCollapsible(index, collapsible);
}


/**
 * @brief Set the collapsible flag for a range of panes.
 *
 * @param first       Zero-based index of the first pane in the range.
 * @param last        Zero-based index of the last pane in the range (inclusive).
 * @param collapsible True to allow the panes to collapse, false to prevent it.
 * @see IsCollapsible()
 */
void
BSplitView::SetCollapsible(int32 first, int32 last, bool collapsible)
{
	fSplitLayout->SetCollapsible(first, last, collapsible);
}


/**
 * @brief Return whether the pane at the given index is currently collapsed.
 *
 * @param index Zero-based pane index.
 * @return True if the pane is collapsed to zero size, false otherwise.
 * @see SetItemCollapsed()
 */
bool
BSplitView::IsItemCollapsed(int32 index) const
{
	return fSplitLayout->IsItemCollapsed(index);
}


/**
 * @brief Collapse or expand the pane at the given index.
 *
 * @param index     Zero-based pane index.
 * @param collapsed True to collapse the pane, false to expand it.
 * @see IsItemCollapsed()
 */
void
BSplitView::SetItemCollapsed(int32 index, bool collapsed)
{
	fSplitLayout->SetItemCollapsed(index, collapsed);
}


/**
 * @brief Add a child view using the standard BView child ordering.
 *
 * This overload delegates to BView::AddChild() and does not assign a layout
 * weight. Use AddChild(BView*, float) to add a view as a weighted split pane.
 *
 * @param child   The view to add.
 * @param sibling If non-NULL, the new child is inserted before this sibling.
 */
void
BSplitView::AddChild(BView* child, BView* sibling)
{
	BView::AddChild(child, sibling);
}


/**
 * @brief Add a child view as a weighted pane at the end of the split.
 *
 * @param child  The view to add as a new pane.
 * @param weight The initial weight determining how much space this pane
 *               receives relative to other panes.
 * @return True on success, false if the layout rejected the item.
 */
bool
BSplitView::AddChild(BView* child, float weight)
{
	return fSplitLayout->AddView(child, weight);
}


/**
 * @brief Insert a child view as a weighted pane at a specific index.
 *
 * @param index  Zero-based position at which to insert the new pane.
 * @param child  The view to add.
 * @param weight The initial weight for the new pane.
 * @return True on success, false if the layout rejected the item.
 */
bool
BSplitView::AddChild(int32 index, BView* child, float weight)
{
	return fSplitLayout->AddView(index, child, weight);
}


/**
 * @brief Add a BLayoutItem as a new pane at the end of the split.
 *
 * @param child The layout item to add.
 * @return True on success, false if the layout rejected the item.
 */
bool
BSplitView::AddChild(BLayoutItem* child)
{
	return fSplitLayout->AddItem(child);
}


/**
 * @brief Add a BLayoutItem as a weighted pane at the end of the split.
 *
 * @param child  The layout item to add.
 * @param weight The initial weight for this pane.
 * @return True on success, false if the layout rejected the item.
 */
bool
BSplitView::AddChild(BLayoutItem* child, float weight)
{
	return fSplitLayout->AddItem(child, weight);
}


/**
 * @brief Insert a BLayoutItem as a weighted pane at a specific index.
 *
 * @param index  Zero-based position at which to insert the item.
 * @param child  The layout item to add.
 * @param weight The initial weight for this pane.
 * @return True on success, false if the layout rejected the item.
 */
bool
BSplitView::AddChild(int32 index, BLayoutItem* child, float weight)
{
	return fSplitLayout->AddItem(index, child, weight);
}


/**
 * @brief Hook called when this view is attached to a window.
 *
 * Adopts the parent view's colors so that the splitter background blends
 * naturally with the surrounding UI.
 */
void
BSplitView::AttachedToWindow()
{
	AdoptParentColors();
}


/**
 * @brief Draw all splitter handles within the given update rectangle.
 *
 * Iterates over every pair of adjacent panes, retrieves each splitter's
 * frame from the layout, and calls DrawSplitter() to render it. The
 * currently dragged splitter is passed as the pressed handle.
 *
 * @param updateRect The portion of the view that requires redrawing.
 */
void
BSplitView::Draw(BRect updateRect)
{
	// draw the splitters
	int32 draggedSplitterIndex = fSplitLayout->DraggedSplitter();
	int32 count = fSplitLayout->CountItems();
	for (int32 i = 0; i < count - 1; i++) {
		BRect frame = fSplitLayout->SplitterItemFrame(i);
		DrawSplitter(frame, updateRect, Orientation(),
			draggedSplitterIndex == i);
	}
}


/**
 * @brief Draw on top of child views after they have been rendered.
 *
 * Delegates directly to BView::DrawAfterChildren().
 *
 * @param r The update rectangle.
 */
void
BSplitView::DrawAfterChildren(BRect r)
{
	return BView::DrawAfterChildren(r);
}


/**
 * @brief Handle a mouse-button-down event to begin dragging a splitter.
 *
 * Acquires the full pointer event mask so that mouse-move and mouse-up
 * events are delivered even when the cursor leaves the view. Starts the
 * drag if the click lands on a splitter handle.
 *
 * @param where The cursor position in view coordinates.
 */
void
BSplitView::MouseDown(BPoint where)
{
	SetMouseEventMask(B_POINTER_EVENTS,
		B_LOCK_WINDOW_FOCUS | B_SUSPEND_VIEW_FOCUS);

	if (fSplitLayout->StartDraggingSplitter(where))
		Invalidate();
}


/**
 * @brief Handle a mouse-button-up event to finish dragging a splitter.
 *
 * Commits the splitter position, triggers a layout recalculation, and
 * redraws the view.
 *
 * @param where The cursor position in view coordinates at the time of release.
 */
void
BSplitView::MouseUp(BPoint where)
{
	if (fSplitLayout->StopDraggingSplitter()) {
		Relayout();
		Invalidate();
	}
}


/**
 * @brief Handle cursor movement, updating the cursor shape and dragging an
 *        active splitter.
 *
 * Shows a resize cursor whenever the pointer is over a splitter handle or a
 * drag is in progress. While dragging, invalidates the old and new splitter
 * frames so they are redrawn at the new position.
 *
 * @param where   The current cursor position in view coordinates.
 * @param transit One of B_ENTERED_VIEW, B_INSIDE_VIEW, B_EXITED_VIEW, or
 *                B_OUTSIDE_VIEW.
 * @param message The drag-and-drop message, or NULL when not in a drag session.
 */
void
BSplitView::MouseMoved(BPoint where, uint32 transit, const BMessage* message)
{
	BCursor cursor(B_CURSOR_ID_SYSTEM_DEFAULT);

	int32 splitterIndex = fSplitLayout->DraggedSplitter();

	if (splitterIndex >= 0 || fSplitLayout->IsAboveSplitter(where)) {
		if (Orientation() == B_VERTICAL)
			cursor = BCursor(B_CURSOR_ID_RESIZE_NORTH_SOUTH);
		else
			cursor = BCursor(B_CURSOR_ID_RESIZE_EAST_WEST);
	}

	if (splitterIndex >= 0) {
		BRect oldFrame = fSplitLayout->SplitterItemFrame(splitterIndex);
		if (fSplitLayout->DragSplitter(where)) {
			Invalidate(oldFrame);
			Invalidate(fSplitLayout->SplitterItemFrame(splitterIndex));
		}
	}

	SetViewCursor(&cursor, true);
}


/**
 * @brief Handle an incoming message.
 *
 * Delegates directly to BView::MessageReceived().
 *
 * @param message The message to process.
 */
void
BSplitView::MessageReceived(BMessage* message)
{
	return BView::MessageReceived(message);
}


/**
 * @brief Prevent callers from replacing the internal BSplitLayout.
 *
 * BSplitView owns its layout and does not permit it to be substituted.
 * This override is intentionally a no-op.
 *
 * @param layout Ignored.
 */
void
BSplitView::SetLayout(BLayout* layout)
{
	// not allowed
}


/**
 * @brief Archive this BSplitView into a BMessage.
 *
 * Delegates to BView::Archive(); the BSplitLayout is archived as part of the
 * view's layout.
 *
 * @param into The message to archive into.
 * @param deep If true, child views are archived recursively.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
BSplitView::Archive(BMessage* into, bool deep) const
{
	return BView::Archive(into, deep);
}


/**
 * @brief Complete archiving after all objects in the graph have been archived.
 *
 * Delegates to BView::AllArchived().
 *
 * @param archive The archive message being built.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSplitView::AllArchived(BMessage* archive) const
{
	return BView::AllArchived(archive);
}


/**
 * @brief Complete unarchiving once all objects in the graph are available.
 *
 * After calling the base class, casts the restored layout back to
 * BSplitLayout and stores it in fSplitLayout.
 *
 * @param from The original archive message.
 * @return B_OK on success, B_BAD_TYPE if the restored layout is not a
 *         BSplitLayout, or B_ERROR if no layout was found.
 */
status_t
BSplitView::AllUnarchived(const BMessage* from)
{
	status_t err = BView::AllUnarchived(from);
	if (err == B_OK) {
		fSplitLayout = dynamic_cast<BSplitLayout*>(GetLayout());
		if (!fSplitLayout && GetLayout())
			return B_BAD_TYPE;
		else if (!fSplitLayout)
			return B_ERROR;
	}
	return err;
}


/**
 * @brief Create a BSplitView instance from an archived BMessage.
 *
 * @param from The archive message to instantiate from.
 * @return A newly allocated BSplitView on success, or NULL if validation fails.
 * @see Archive()
 */
BArchivable*
BSplitView::Instantiate(BMessage* from)
{
	if (validate_instantiation(from, "BSplitView"))
		return new BSplitView(from);
	return NULL;
}


/**
 * @brief Draw a single splitter handle using the default appearance.
 *
 * This virtual method is called by Draw() for each splitter. Subclasses may
 * override it to provide a custom splitter appearance. The default
 * implementation delegates to _DrawDefaultSplitter().
 *
 * @param frame       The rectangle occupied by the splitter in view coordinates.
 * @param updateRect  The current damage rectangle; can be used to skip
 *                    rendering when the splitter lies outside it.
 * @param orientation The split orientation (B_HORIZONTAL or B_VERTICAL).
 * @param pressed     True when this splitter is currently being dragged.
 */
void
BSplitView::DrawSplitter(BRect frame, const BRect& updateRect,
	orientation orientation, bool pressed)
{
	_DrawDefaultSplitter(this, frame, updateRect, orientation, pressed);
}


/**
 * @brief Render a splitter handle using BControlLook.
 *
 * Passes the activated flag when @a pressed is true and delegates all
 * actual rendering to be_control_look->DrawSplitter().
 *
 * @param view        The view to draw into (usually @c this).
 * @param frame       The rectangle for the splitter handle.
 * @param updateRect  The current damage rectangle.
 * @param orientation The split orientation.
 * @param pressed     True when the handle is actively being dragged.
 */
void
BSplitView::_DrawDefaultSplitter(BView* view, BRect frame,
	const BRect& updateRect, orientation orientation, bool pressed)
{
	uint32 flags = pressed ? BControlLook::B_ACTIVATED : 0;
	be_control_look->DrawSplitter(view, frame, updateRect, view->ViewColor(),
		orientation, flags, 0);
}


/**
 * @brief Dispatch a binary-compatibility perform code.
 *
 * Delegates directly to BView::Perform().
 *
 * @param d   The perform code constant.
 * @param arg Pointer to the code-specific argument structure.
 * @return The result from BView::Perform().
 */
status_t
BSplitView::Perform(perform_code d, void* arg)
{
	return BView::Perform(d, arg);
}


void BSplitView::_ReservedSplitView1() {}
void BSplitView::_ReservedSplitView2() {}
void BSplitView::_ReservedSplitView3() {}
void BSplitView::_ReservedSplitView4() {}
void BSplitView::_ReservedSplitView5() {}
void BSplitView::_ReservedSplitView6() {}
void BSplitView::_ReservedSplitView7() {}
void BSplitView::_ReservedSplitView8() {}
void BSplitView::_ReservedSplitView9() {}
void BSplitView::_ReservedSplitView10() {}
