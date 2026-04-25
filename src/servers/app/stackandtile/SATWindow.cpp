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
 *   Copyright 2010-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 *       Clemens Zeidler, haiku@clemens-zeidler.de
 */


/**
 * @file SATWindow.cpp
 * @brief Per-window glue between a server Window and Stack and Tile.
 *
 * Implements SATWindow, which couples a server-side Window with the SATGroup
 * and WindowArea it currently belongs to. The class owns the SATStacking and
 * SATTiling snapping behaviours, caches the user-supplied size limits before
 * the decorator inflated them, and updates tab/border highlights to reflect
 * the in-progress drag.
 *
 * @see SATGroup, WindowArea, SATStacking, SATTiling
 */


#include "SATWindow.h"

#include <Debug.h>

#include "StackAndTilePrivate.h"

#include "Desktop.h"
#include "SATGroup.h"
#include "ServerApp.h"
#include "Window.h"


using namespace BPrivate;


// #pragma mark -


/**
 * @brief Constructs a SATWindow wrapping @a window for the @a sat listener.
 *
 * Generates a stable per-window id, snapshots the window's current size
 * limits and dimensions so they can be restored if the window leaves a group,
 * and registers the stacking and tiling snapping behaviours.
 *
 * @param sat    Owning StackAndTile listener.
 * @param window The server window being wrapped; must not be NULL.
 */
SATWindow::SATWindow(StackAndTile* sat, Window* window)
	:
	fWindow(window),
	fStackAndTile(sat),

	fWindowArea(NULL),

	fOngoingSnapping(NULL),
	fSATStacking(this),
	fSATTiling(this)
{
	fId = _GenerateId();

	fDesktop = fWindow->Desktop();

	// read initial limit values
	fWindow->GetSizeLimits(&fOriginalMinWidth, &fOriginalMaxWidth,
		&fOriginalMinHeight, &fOriginalMaxHeight);
	BRect frame = fWindow->Frame();
	fOriginalWidth = frame.Width();
	fOriginalHeight = frame.Height();

	fSATSnappingBehaviourList.AddItem(&fSATStacking);
	fSATSnappingBehaviourList.AddItem(&fSATTiling);
}


/**
 * @brief Detaches the window from its group, if any, before destruction.
 *
 * Removing from the group restores the window's original size limits and
 * geometry via the RemovedFromGroup() hook chain.
 */
SATWindow::~SATWindow()
{
	if (fWindowArea != NULL)
		fWindowArea->Group()->RemoveWindow(this);
}


/**
 * @brief Returns the SATDecorator currently attached to the wrapped window.
 *
 * @return The decorator cast to SATDecorator, or NULL if the window has no
 *         decorator.
 */
SATDecorator*
SATWindow::GetDecorator() const
{
	return static_cast<SATDecorator*>(fWindow->Decorator());
}


/**
 * @brief Returns the SATGroup that owns this window, creating one on demand.
 *
 * If the window is not yet in a group, allocates a fresh single-window group
 * and adds the window to it. Single-window groups have their tab positions
 * synchronised with the current window frame so the constraint solver starts
 * from a sensible state.
 *
 * @return The group containing this window, or NULL if memory allocation or
 *         registration failed.
 */
SATGroup*
SATWindow::GetGroup()
{
	if (fWindowArea == NULL) {
		SATGroup* group = new (std::nothrow)SATGroup;
		if (group == NULL)
			return group;
		BReference<SATGroup> groupRef;
		groupRef.SetTo(group, true);

		/* AddWindow also will trigger the window to hold a reference on the new
		group. */
		if (group->AddWindow(this, NULL, NULL, NULL, NULL) == false)
			return NULL;
	}

	ASSERT(fWindowArea != NULL);

	 // manually set the tabs of the single window
	if (PositionManagedBySAT() == false) {
		BRect frame = CompleteWindowFrame();
		fWindowArea->LeftTopCrossing()->VerticalTab()->SetPosition(frame.left);
		fWindowArea->LeftTopCrossing()->HorizontalTab()->SetPosition(frame.top);
		fWindowArea->RightBottomCrossing()->VerticalTab()->SetPosition(
			frame.right);
		fWindowArea->RightBottomCrossing()->HorizontalTab()->SetPosition(
			frame.bottom);
	}

	return fWindowArea->Group();
}


/**
 * @brief Routes a SAT protocol message to the matching subsystem handler.
 *
 * Reads the target subsystem from @a link and currently dispatches the
 * stacking subsystem to StackingEventHandler::HandleMessage().
 *
 * @param sender The originating SATWindow.
 * @param link   Reader supplying the remaining payload.
 * @param reply  Writer used to send the response.
 * @return true if the target was recognised, false otherwise.
 */
bool
SATWindow::HandleMessage(SATWindow* sender, BPrivate::LinkReceiver& link,
	BPrivate::LinkSender& reply)
{
	int32 target;
	link.Read<int32>(&target);
	if (target == kStacking)
		return StackingEventHandler::HandleMessage(sender, link, reply);

	return false;
}


/**
 * @brief Migrates this window's WindowArea into another SATGroup.
 *
 * Used while splitting and re-merging groups. Delegates to
 * WindowArea::PropagateToGroup() which reuses the destination group's tabs.
 *
 * @param group Destination group; must not be NULL.
 * @return true if the area was migrated, false if this window has no area or
 *         the migration failed.
 */
bool
SATWindow::PropagateToGroup(SATGroup* group)
{
	if (fWindowArea == NULL)
		return false;
	return fWindowArea->PropagateToGroup(group);
}


/**
 * @brief Hook invoked by SATGroup after this window joined an area.
 *
 * Records @a area as the current WindowArea so subsequent operations can
 * route through the new group.
 *
 * @param group The destination group.
 * @param area  The WindowArea this window was placed in.
 * @return Always true.
 */
bool
SATWindow::AddedToGroup(SATGroup* group, WindowArea* area)
{
	STRACE_SAT("SATWindow::AddedToGroup group: %p window %s\n", group,
		fWindow->Title());
	fWindowArea = area;
	return true;
}


/**
 * @brief Hook invoked by SATGroup when this window has just left @a group.
 *
 * Restores the user-supplied size limits and geometry. If the group is now a
 * single window, also restores that remaining window so it stops behaving as
 * a tile.
 *
 * @param group          The group being left.
 * @param stayBelowMouse If true, also reposition the window so the mouse
 *                       remains over its tab/border after the resize.
 * @return Always true.
 */
bool
SATWindow::RemovedFromGroup(SATGroup* group, bool stayBelowMouse)
{
	STRACE_SAT("SATWindow::RemovedFromGroup group: %p window %s\n", group,
		fWindow->Title());

	_RestoreOriginalSize(stayBelowMouse);
	if (group->CountItems() == 1)
		group->WindowAt(0)->_RestoreOriginalSize(false);

	return true;
}


/**
 * @brief Stacks @a child as a new tab on top of this window's WindowArea.
 *
 * Adds the child to the same group and area, re-solves the layout, and
 * pushes the child onto the underlying server-side WindowStack so the two
 * windows share their tabs. Rolls back the group change if the server stack
 * refuses the new member.
 *
 * @param child The window to stack onto this one; must not be NULL.
 * @return true on success, false on allocation/stack failures.
 */
bool
SATWindow::StackWindow(SATWindow* child)
{
	SATGroup* group = GetGroup();
	WindowArea* area = GetWindowArea();
	if (!group || !area)
		return false;

	if (group->AddWindow(child, area, this) == false)
		return false;

	DoGroupLayout();

	if (fWindow->AddWindowToStack(child->GetWindow()) == false) {
		group->RemoveWindow(child);
		DoGroupLayout();
		return false;
	}

	return true;
}


/**
 * @brief Detaches the wrapped window from @a area and notifies behaviours.
 *
 * Caches the tab x-position for use during _RestoreOriginalSize() (so the
 * mouse can stay over the same tab), detaches the window from its server-side
 * stack, forwards the event to every snapping behaviour, and clears
 * fWindowArea.
 *
 * @param area The area being left.
 */
void
SATWindow::RemovedFromArea(WindowArea* area)
{
	SATDecorator* decorator = GetDecorator();
	if (decorator != NULL)
		fOldTabLocatiom = decorator->TabRect(fWindow->PositionInStack()).left;

	fWindow->DetachFromWindowStack(true);
	for (int i = 0; i < fSATSnappingBehaviourList.CountItems(); i++)
		fSATSnappingBehaviourList.ItemAt(i)->RemovedFromArea(area);

	fWindowArea = NULL;
}


/**
 * @brief Forwards a server-side window-look change to all snapping behaviours.
 *
 * Each behaviour decides independently whether the new look is still
 * compatible (e.g. tiling rejects borderless looks) and may detach this
 * window from its group as a result.
 *
 * @param look The new window_look value reported by the server.
 */
void
SATWindow::WindowLookChanged(window_look look)
{
	for (int i = 0; i < fSATSnappingBehaviourList.CountItems(); i++)
		fSATSnappingBehaviourList.ItemAt(i)->WindowLookChanged(look);
}


/**
 * @brief Iterates every group and asks the snapping behaviours for a match.
 *
 * Stops after the first behaviour that claims a group, recording it in
 * fOngoingSnapping so JoinCandidates() can later commit the result. Skips
 * groups that consist entirely of non-normal windows and self.
 *
 * @note Only fires for B_NORMAL_WINDOW_FEEL windows; other feels never enter
 *       SAT snapping.
 */
void
SATWindow::FindSnappingCandidates()
{
	fOngoingSnapping = NULL;

	if (fWindow->Feel() != B_NORMAL_WINDOW_FEEL)
		return;

	GroupIterator groupIterator(fStackAndTile, GetWindow()->Desktop());
	for (SATGroup* group = groupIterator.NextGroup(); group;
		group = groupIterator.NextGroup()) {
		if (group->CountItems() == 1
			&& group->WindowAt(0)->GetWindow()->Feel() != B_NORMAL_WINDOW_FEEL)
			continue;
		for (int i = 0; i < fSATSnappingBehaviourList.CountItems(); i++) {
			if (fSATSnappingBehaviourList.ItemAt(i)->FindSnappingCandidates(
				group)) {
				fOngoingSnapping = fSATSnappingBehaviourList.ItemAt(i);
				return;
			}
		}
	}
}


/**
 * @brief Commits the ongoing snapping gesture, if any, and clears the state.
 *
 * @return true if a behaviour was active and JoinCandidates() succeeded,
 *         false otherwise.
 */
bool
SATWindow::JoinCandidates()
{
	if (!fOngoingSnapping)
		return false;
	bool status = fOngoingSnapping->JoinCandidates();
	fOngoingSnapping = NULL;

	return status;
}


/**
 * @brief Triggers a constraint re-solve on the current WindowArea.
 *
 * No-op for windows that are alone in their group, since the layout solver
 * has nothing to balance against.
 */
void
SATWindow::DoGroupLayout()
{
	if (!PositionManagedBySAT())
		return;

	if (fWindowArea != NULL)
		fWindowArea->DoGroupLayout();
}


/**
 * @brief Loosens the window's max size so it can fit @a targetFrame.
 *
 * Strips the decorator insets from @a targetFrame and bumps the window's
 * max width/height upward whenever @a targetFrame would exceed them, so the
 * solver can place the window at the requested size.
 *
 * @param targetFrame Desired outer frame including decorator.
 */
void
SATWindow::AdjustSizeLimits(BRect targetFrame)
{
	SATDecorator* decorator = GetDecorator();
	if (decorator == NULL)
		return;

	targetFrame.right -= 2 * (int32)decorator->BorderWidth();
	targetFrame.bottom -= 2 * (int32)decorator->BorderWidth()
		+ (int32)decorator->TabHeight() + 1;

	int32 minWidth, maxWidth;
	int32 minHeight, maxHeight;
	GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);

	if (maxWidth < targetFrame.Width())
		maxWidth = targetFrame.IntegerWidth();
	if (maxHeight < targetFrame.Height())
		maxHeight = targetFrame.IntegerHeight();

	fWindow->SetSizeLimits(minWidth, maxWidth, minHeight, maxHeight);
}


/**
 * @brief Updates the cached pre-SAT size limits and triggers a re-solve.
 *
 * Called when the client (or the server) changes the official limits while
 * the window is in a group; the area solver needs to know about it so the
 * tile rectangle never violates the new limits.
 *
 * @param minWidth  Minimum width supplied by the client.
 * @param maxWidth  Maximum width supplied by the client.
 * @param minHeight Minimum height supplied by the client.
 * @param maxHeight Maximum height supplied by the client.
 */
void
SATWindow::SetOriginalSizeLimits(int32 minWidth, int32 maxWidth,
	int32 minHeight, int32 maxHeight)
{
	fOriginalMinWidth = minWidth;
	fOriginalMaxWidth = maxWidth;
	fOriginalMinHeight = minHeight;
	fOriginalMaxHeight = maxHeight;

	if (fWindowArea != NULL)
		fWindowArea->UpdateSizeLimits();
}


/**
 * @brief Returns the cached pre-SAT size limits, decorator-corrected.
 *
 * Pins the minimum dimension to the current frame size when the window is
 * not resizable in that direction, so the solver does not try to shrink a
 * fixed-size window.
 *
 * @param minWidth  Out: minimum width.
 * @param maxWidth  Out: maximum width.
 * @param minHeight Out: minimum height.
 * @param maxHeight Out: maximum height.
 */
void
SATWindow::GetSizeLimits(int32* minWidth, int32* maxWidth, int32* minHeight,
	int32* maxHeight) const
{
	*minWidth = fOriginalMinWidth;
	*minHeight = fOriginalMinHeight;
	*maxWidth = fOriginalMaxWidth;
	*maxHeight = fOriginalMaxHeight;

	SATDecorator* decorator = GetDecorator();
	if (decorator == NULL)
		return;

	int32 minDecorWidth = 1, maxDecorWidth = 1;
	int32 minDecorHeight = 1, maxDecorHeight = 1;
	decorator->GetSizeLimits(&minDecorWidth, &minDecorHeight,
		&maxDecorWidth, &maxDecorHeight);

	// if no size limit is set but the window is not resizeable choose the
	// current size as limit
	if (IsHResizeable() == false && fOriginalMinWidth <= minDecorWidth)
		*minWidth = (int32)fOriginalWidth;
	if (IsVResizeable() == false && fOriginalMinHeight <= minDecorHeight)
		*minHeight = (int32)fOriginalHeight;

	if (*minWidth > *maxWidth)
		*maxWidth = *minWidth;
	if (*minHeight > *maxHeight)
		*maxHeight = *minHeight;
}


/**
 * @brief Inflates the supplied min/max size pair by the decorator's footprint.
 *
 * Adds the border width on every side and the tab height plus one extra pixel
 * on the top so the resulting limits describe the outer frame size.
 *
 * @param minWidth  In/out minimum width.
 * @param maxWidth  In/out maximum width.
 * @param minHeight In/out minimum height.
 * @param maxHeight In/out maximum height.
 */
void
SATWindow::AddDecorator(int32* minWidth, int32* maxWidth, int32* minHeight,
	int32* maxHeight)
{
	SATDecorator* decorator = GetDecorator();
	if (decorator == NULL)
		return;

	*minWidth += 2 * (int32)decorator->BorderWidth();
	*minHeight += 2 * (int32)decorator->BorderWidth()
		+ (int32)decorator->TabHeight() + 1;
	*maxWidth += 2 * (int32)decorator->BorderWidth();
	*maxHeight += 2 * (int32)decorator->BorderWidth()
		+ (int32)decorator->TabHeight() + 1;
}


/**
 * @brief Expands a content-area frame to include the decorator footprint.
 *
 * Used to translate between the content frame and the outer frame seen by the
 * SAT solver.
 *
 * @param frame In/out frame; expanded in place.
 */
void
SATWindow::AddDecorator(BRect& frame)
{
	SATDecorator* decorator = GetDecorator();
	if (!decorator)
		return;
	frame.left -= decorator->BorderWidth();
	frame.right += decorator->BorderWidth() + 1;
	frame.top -= decorator->BorderWidth() + decorator->TabHeight() + 1;
	frame.bottom += decorator->BorderWidth();
}


/**
 * @brief External-resize hook that snapshots the new size and re-solves.
 *
 * Caches the user-supplied dimensions in the resizable directions so future
 * group exits restore the right size, then propagates the new outer frame to
 * the WindowArea's solver constraints.
 */
void
SATWindow::Resized()
{
	bool hResizeable = IsHResizeable();
	bool vResizeable = IsVResizeable();
	if (hResizeable == false && vResizeable == false)
		return;

	BRect frame = fWindow->Frame();
	if (hResizeable)
		fOriginalWidth = frame.Width();
	if (vResizeable)
		fOriginalHeight = frame.Height();

	if (fWindowArea != NULL)
		fWindowArea->UpdateSizeConstaints(CompleteWindowFrame());
}


/**
 * @brief Returns whether the wrapped window allows horizontal resizing.
 *
 * Checks both the window look (modal/bordered/borderless are fixed) and the
 * B_NOT_RESIZABLE / B_NOT_H_RESIZABLE flags.
 *
 * @return true when the window may be resized horizontally.
 */
bool
SATWindow::IsHResizeable() const
{
	if (fWindow->Look() == B_MODAL_WINDOW_LOOK
		|| fWindow->Look() == B_BORDERED_WINDOW_LOOK
		|| fWindow->Look() == B_NO_BORDER_WINDOW_LOOK
		|| (fWindow->Flags() & B_NOT_RESIZABLE) != 0
		|| (fWindow->Flags() & B_NOT_H_RESIZABLE) != 0)
		return false;
	return true;
}


/**
 * @brief Returns whether the wrapped window allows vertical resizing.
 *
 * Same logic as IsHResizeable() but tests B_NOT_V_RESIZABLE.
 *
 * @return true when the window may be resized vertically.
 */
bool
SATWindow::IsVResizeable() const
{
	if (fWindow->Look() == B_MODAL_WINDOW_LOOK
		|| fWindow->Look() == B_BORDERED_WINDOW_LOOK
		|| fWindow->Look() == B_NO_BORDER_WINDOW_LOOK
		|| (fWindow->Flags() & B_NOT_RESIZABLE) != 0
		|| (fWindow->Flags() & B_NOT_V_RESIZABLE) != 0)
		return false;
	return true;
}


/**
 * @brief Returns the window's outer frame including the decorator.
 *
 * For windows that are visible on a different workspace than the one being
 * inspected, falls back to the per-workspace anchor position so cross-
 * workspace groups stay coherent.
 *
 * @return Outer frame in screen coordinates.
 */
BRect
SATWindow::CompleteWindowFrame()
{
	BRect frame = fWindow->Frame();
	if (fDesktop && fWindow->IsVisible()
		&& fDesktop->CurrentWorkspace() != fWindow->CurrentWorkspace()) {
		window_anchor& anchor = fWindow->Anchor(fWindow->CurrentWorkspace());
		if (anchor.position != kInvalidWindowPosition)
			frame.OffsetTo(anchor.position);
	} else if (fDesktop && !fWindow->IsVisible() && fWindow->PriorWorkspace() >= 0
		&& fDesktop->CurrentWorkspace() != fWindow->PriorWorkspace()) {
		window_anchor& anchor = fWindow->Anchor(fWindow->PriorWorkspace());
		if (anchor.position != kInvalidWindowPosition)
			frame.OffsetTo(anchor.position);
	}

	AddDecorator(frame);
	return frame;
}


/**
 * @brief Returns whether SAT actively manages this window's geometry.
 *
 * True only when the window shares its group with at least one other window;
 * solo groups behave like ordinary windows.
 *
 * @return true if SAT will move the window when its group is solved.
 */
bool
SATWindow::PositionManagedBySAT()
{
	if (fWindowArea == NULL || fWindowArea->Group()->CountItems() == 1)
		return false;

	return true;
}


/**
 * @brief Toggles the SAT highlight on the window's tab and tab buttons.
 *
 * Sets the HIGHLIGHT_STACK_AND_TILE level on REGION_TAB, REGION_CLOSE_BUTTON,
 * and REGION_ZOOM_BUTTON of the current tab and asks the top of the stack to
 * repaint the dirty region.
 *
 * @param active true to draw the highlight, false to remove it.
 * @return true if a decorator was available and the highlight was applied.
 */
bool
SATWindow::HighlightTab(bool active)
{
	SATDecorator* decorator = GetDecorator();
	if (!decorator)
		return false;

	int32 tabIndex = fWindow->PositionInStack();
	BRegion dirty;
	uint8 highlight = active ?  SATDecorator::HIGHLIGHT_STACK_AND_TILE : 0;
	decorator->SetRegionHighlight(Decorator::REGION_TAB, highlight, &dirty,
		tabIndex);
	decorator->SetRegionHighlight(Decorator::REGION_CLOSE_BUTTON, highlight,
		&dirty, tabIndex);
	decorator->SetRegionHighlight(Decorator::REGION_ZOOM_BUTTON, highlight,
		&dirty, tabIndex);

	fWindow->TopLayerStackWindow()->ProcessDirtyRegion(dirty);
	return true;
}


/**
 * @brief Toggles the SAT highlight on a single decorator border region.
 *
 * @param region The decorator region (left/right/top/bottom border) to mark.
 * @param active true to draw the highlight, false to remove it.
 * @return true if a decorator was available and the highlight was applied.
 */
bool
SATWindow::HighlightBorders(Decorator::Region region, bool active)
{
	SATDecorator* decorator = GetDecorator();
	if (!decorator)
		return false;

	BRegion dirty;
	uint8 highlight = active ? SATDecorator::HIGHLIGHT_STACK_AND_TILE : 0;
	decorator->SetRegionHighlight(region, highlight, &dirty);

	fWindow->ProcessDirtyRegion(dirty);
	return true;
}


/**
 * @brief Returns the persistent identifier used to restore archived groups.
 *
 * @return The 64-bit id assigned at construction or restored from settings.
 */
uint64
SATWindow::Id()
{
	return fId;
}


/**
 * @brief Restores per-window settings from a serialized BMessage.
 *
 * Currently reads only the persistent id; SetSettings/GetSettings are paired
 * with SATGroup::ArchiveGroup() so groups can be restored across sessions.
 *
 * @param message Archived settings produced by GetSettings().
 * @return true if the id field was present.
 */
bool
SATWindow::SetSettings(const BMessage& message)
{
	uint64 id;
	if (message.FindInt64("window_id", (int64*)&id) != B_OK)
		return false;
	fId = id;
	return true;
}


/**
 * @brief Serialises per-window settings into @a message.
 *
 * @param message Destination archive; receives the persistent id.
 */
void
SATWindow::GetSettings(BMessage& message)
{
	message.AddInt64("window_id", fId);
}


/**
 * @brief Generates a probably-unique 64-bit id for this window.
 *
 * Mixes the high bits of the real-time clock with a 16-bit random tail. Good
 * enough for distinguishing windows within an archived group.
 *
 * @return A new 64-bit identifier.
 */
uint64
SATWindow::_GenerateId()
{
	bigtime_t time = real_time_clock_usecs();
	srand(time);
	int16 randNumber = rand();
	return (time & ~0xFFFF) | randNumber;
}


/**
 * @brief Restores the pre-group size and optionally keeps the mouse anchored.
 *
 * Re-applies the cached size limits, resizes the window so a non-resizable
 * dimension returns to its original measure, and (when @a stayBelowMouse is
 * true) nudges the window so the cursor stays over the same tab or border it
 * was dragging.
 *
 * @param stayBelowMouse If true, also reposition so the mouse stays on the
 *                       same decorator region.
 */
void
SATWindow::_RestoreOriginalSize(bool stayBelowMouse)
{
	// restore size
	fWindow->SetSizeLimits(fOriginalMinWidth, fOriginalMaxWidth,
		fOriginalMinHeight, fOriginalMaxHeight);
	BRect frame = fWindow->Frame();
	float x = 0, y = 0;
	if (IsHResizeable() == false)
		x = fOriginalWidth - frame.Width();
	if (IsVResizeable() == false)
		y = fOriginalHeight - frame.Height();
	fDesktop->ResizeWindowBy(fWindow, x, y);

	if (!stayBelowMouse)
		return;
	// verify that the window stays below the mouse
	BPoint mousePosition;
	int32 buttons;
	fDesktop->GetLastMouseState(&mousePosition, &buttons);
	SATDecorator* decorator = GetDecorator();
	if (decorator == NULL)
		return;
	BRect tabRect = decorator->TitleBarRect();
	if (mousePosition.y < tabRect.bottom && mousePosition.y > tabRect.top
		&& mousePosition.x <= frame.right + decorator->BorderWidth() +1
		&& mousePosition.x >= frame.left + decorator->BorderWidth()) {
		// verify mouse stays on the tab
		float oldOffset = mousePosition.x - fOldTabLocatiom;
		float deltaX = mousePosition.x - (tabRect.left + oldOffset);
		fDesktop->MoveWindowBy(fWindow, deltaX, 0);
	} else {
		// verify mouse stays on the border
		float deltaX = 0;
		float deltaY = 0;
		BRect newFrame = fWindow->Frame();
		if (x != 0 && mousePosition.x > frame.left
			&& mousePosition.x > newFrame.right) {
			deltaX = mousePosition.x - newFrame.right;
			if (mousePosition.x > frame.right)
				deltaX -= mousePosition.x - frame.right;
		}
		if (y != 0 && mousePosition.y > frame.top
			&& mousePosition.y > newFrame.bottom) {
			deltaY = mousePosition.y - newFrame.bottom;
			if (mousePosition.y > frame.bottom)
				deltaY -= mousePosition.y - frame.bottom;
		}
			fDesktop->MoveWindowBy(fWindow, deltaX, deltaY);
	}
}
