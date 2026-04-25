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
 * @file SATGroup.cpp
 * @brief Constraint-based group of Stack-and-Tile windows.
 *
 * Implements the geometric data structures that power Stack and Tile:
 *  - Tab represents a horizontal or vertical guideline backed by an LP
 *    Variable;
 *  - Crossing is the meeting point of one horizontal and one vertical Tab
 *    and carries the four Corners that surround it;
 *  - WindowArea is a rectangular region of the tab grid occupied by one or
 *    more windows (multiple windows in the same area means tab-stacked);
 *  - SATGroup ties everything together and owns the LinearProgramming
 *    solver instance, plus the logic that splits a group into pieces when a
 *    window is removed and reconnects fragments into new groups.
 *
 * @see SATWindow, StackAndTile, LinearProgramming::LinearSpec
 */


#include "SATGroup.h"

#include <vector>

#include <Debug.h>
#include <Message.h>

#include "Desktop.h"

#include "SATWindow.h"
#include "StackAndTile.h"
#include "Window.h"


using namespace std;
using namespace LinearProgramming;


/** @brief Penalty for the soft equality keeping the area at its current size. */
const float kExtentPenalty = 1;
/** @brief Heavier penalty applied while the area is being actively resized. */
const float kHighPenalty = 100;
/** @brief Hard penalty for the min/max inequality constraints. */
const float kInequalityPenalty = 10000;


/**
 * @brief Constructs a WindowArea spanning the rectangle defined by four crossings.
 *
 * Constraints are not registered yet; Init() must be called once a group has
 * been chosen so the LP solver can be wired up.
 *
 * @param leftTop     Crossing at the upper-left corner.
 * @param rightTop    Crossing at the upper-right corner.
 * @param leftBottom  Crossing at the lower-left corner.
 * @param rightBottom Crossing at the lower-right corner.
 */
WindowArea::WindowArea(Crossing* leftTop, Crossing* rightTop,
	Crossing* leftBottom, Crossing* rightBottom)
	:
	fGroup(NULL),

	fLeftTopCrossing(leftTop),
	fRightTopCrossing(rightTop),
	fLeftBottomCrossing(leftBottom),
	fRightBottomCrossing(rightBottom),

	fMinWidthConstraint(NULL),
	fMinHeightConstraint(NULL),
	fMaxWidthConstraint(NULL),
	fMaxHeightConstraint(NULL),
	fWidthConstraint(NULL),
	fHeightConstraint(NULL)
{
}


/**
 * @brief Tears down the area, notifies the group, and releases solver constraints.
 *
 * Notifies the owning SATGroup so it can decide whether the remaining areas
 * still form one connected component, removes corner annotations from the
 * surrounding crossings, drops itself from the group's area list, and
 * releases the LP constraints.
 */
WindowArea::~WindowArea()
{
	if (fGroup)
		fGroup->WindowAreaRemoved(this);

	_CleanupCorners();
	fGroup->fWindowAreaList.RemoveItem(this);

	_UninitConstraints();
}


/**
 * @brief Registers the area with @a group and creates its LP constraints.
 *
 * Adds six constraints to the group's LinearSpec: minimum width/height
 * (kGE), maximum width/height (kLE with @c kInequalityPenalty), and the
 * soft size equalities used as the optimisation target.
 *
 * @param group The group that will own the area; must not be NULL.
 * @return true if registration and every constraint allocation succeeded.
 */
bool
WindowArea::Init(SATGroup* group)
{
	_UninitConstraints();

	if (group == NULL || group->fWindowAreaList.AddItem(this) == false)
		return false;

	fGroup = group;

	LinearSpec* linearSpec = fGroup->GetLinearSpec();

	fMinWidthConstraint = linearSpec->AddConstraint(1.0, RightVar(), -1.0,
		LeftVar(), kGE, 0);
	fMinHeightConstraint = linearSpec->AddConstraint(1.0, BottomVar(), -1.0,
		TopVar(), kGE, 0);

	fMaxWidthConstraint = linearSpec->AddConstraint(1.0, RightVar(), -1.0,
		LeftVar(), kLE, 0, kInequalityPenalty, kInequalityPenalty);
	fMaxHeightConstraint = linearSpec->AddConstraint(1.0, BottomVar(), -1.0,
		TopVar(), kLE, 0, kInequalityPenalty, kInequalityPenalty);

	// Width and height have soft constraints
	fWidthConstraint = linearSpec->AddConstraint(1.0, RightVar(), -1.0,
		LeftVar(), kEQ, 0, kExtentPenalty,
		kExtentPenalty);
	fHeightConstraint = linearSpec->AddConstraint(-1.0, TopVar(), 1.0,
		BottomVar(), kEQ, 0, kExtentPenalty,
		kExtentPenalty);

	if (!fMinWidthConstraint || !fMinHeightConstraint || !fWidthConstraint
		|| !fHeightConstraint || !fMaxWidthConstraint
		|| !fMaxHeightConstraint)
		return false;

	return true;
}


/**
 * @brief Solves the group's LP and applies the result to every WindowArea.
 *
 * Anchors the parent window's current frame as a temporary equality so the
 * solver does not drift, raises the soft-size penalties to bias the answer
 * toward keeping that frame, and then iterates Solve() until it returns
 * @c kOptimal (up to 15 attempts). On success every area in the group is
 * moved to match the new variable values.
 *
 * @note The temporary anchor constraints are removed before returning, and
 *       the soft-size penalties are restored to kExtentPenalty.
 */
void
WindowArea::DoGroupLayout()
{
	SATWindow* parentWindow = fWindowLayerOrder.ItemAt(0);
	if (parentWindow == NULL)
		return;

	BRect frame = parentWindow->CompleteWindowFrame();
	// Make it also work for solver which don't support negative variables
	frame.OffsetBy(kMakePositiveOffset, kMakePositiveOffset);

	// adjust window size soft constraints
	fWidthConstraint->SetRightSide(frame.Width());
	fHeightConstraint->SetRightSide(frame.Height());

	LinearSpec* linearSpec = fGroup->GetLinearSpec();
	Constraint* leftConstraint = linearSpec->AddConstraint(1.0, LeftVar(),
		kEQ, frame.left);
	Constraint* topConstraint = linearSpec->AddConstraint(1.0, TopVar(), kEQ,
		frame.top);

	// give soft constraints a high penalty
	fWidthConstraint->SetPenaltyNeg(kHighPenalty);
	fWidthConstraint->SetPenaltyPos(kHighPenalty);
	fHeightConstraint->SetPenaltyNeg(kHighPenalty);
	fHeightConstraint->SetPenaltyPos(kHighPenalty);

	// After we set the new parameter solve and apply the new layout.
	ResultType result;
	for (int32 tries = 0; tries < 15; tries++) {
		result = fGroup->GetLinearSpec()->Solve();
		if (result == kInfeasible) {
			debug_printf("can't solve constraints!\n");
			break;
		}
		if (result == kOptimal) {
			const WindowAreaList& areas = fGroup->GetAreaList();
			for (int32 i = 0; i < areas.CountItems(); i++) {
				WindowArea* area = areas.ItemAt(i);
				area->_MoveToSAT(parentWindow);
			}
			break;
		}
	}

	// set penalties back to normal
	fWidthConstraint->SetPenaltyNeg(kExtentPenalty);
	fWidthConstraint->SetPenaltyPos(kExtentPenalty);
	fHeightConstraint->SetPenaltyNeg(kExtentPenalty);
	fHeightConstraint->SetPenaltyPos(kExtentPenalty);

	linearSpec->RemoveConstraint(leftConstraint);
	linearSpec->RemoveConstraint(topConstraint);
}


/**
 * @brief Refreshes the area's min/max constraints from the member windows.
 *
 * Thin wrapper that delegates to _UpdateConstraintValues() so callers do
 * not need to know about the private helper.
 */
void
WindowArea::UpdateSizeLimits()
{
	_UpdateConstraintValues();
}


/**
 * @brief Updates the soft width/height equalities to match @a frame.
 *
 * Called when an external resize has changed the area's actual size and the
 * solver must learn about it without re-running a full layout pass.
 *
 * @param frame New outer frame for the area.
 */
void
WindowArea::UpdateSizeConstaints(const BRect& frame)
{
	// adjust window size soft constraints
	fWidthConstraint->SetRightSide(frame.Width());
	fHeightConstraint->SetRightSide(frame.Height());
}


/**
 * @brief Reorders an existing window inside the area's stack to @a index.
 *
 * @param window The window already a member of the area.
 * @param index  Destination index in fWindowList.
 * @return true if the move succeeded.
 */
bool
WindowArea::MoveWindowToPosition(SATWindow* window, int32 index)
{
	int32 oldIndex = fWindowList.IndexOf(window);
	ASSERT(oldIndex != index);
	return fWindowList.MoveItem(oldIndex, index);
}


/**
 * @brief Returns the topmost window in the area's z-order, or NULL if empty.
 *
 * @return The window at the end of fWindowLayerOrder.
 */
SATWindow*
WindowArea::TopWindow()
{
	return fWindowLayerOrder.ItemAt(fWindowLayerOrder.CountItems() - 1);
}


/**
 * @brief Recomputes min/max/current size constraints from current members.
 *
 * Walks every window in the area, takes the maximum of their min limits and
 * the maximum of their max limits (so every window can satisfy them), clamps
 * the result to the solver-friendly range, inflates by the decorator
 * footprint, and pushes the values into the six size constraints.
 *
 * @note A solver-imposed upper bound of kMaxSolverValue keeps the LP from
 *       drifting into ill-conditioned territory when a member declares a
 *       practically unlimited maximum size.
 */
void
WindowArea::_UpdateConstraintValues()
{
	SATWindow* topWindow = TopWindow();
	if (topWindow == NULL)
		return;

	int32 minWidth, maxWidth;
	int32 minHeight, maxHeight;
	SATWindow* window = fWindowList.ItemAt(0);
	window->GetSizeLimits(&minWidth, &maxWidth, &minHeight, &maxHeight);
	for (int32 i = 1; i < fWindowList.CountItems(); i++) {
		window = fWindowList.ItemAt(i);
		// size limit constraints
		int32 minW, maxW;
		int32 minH, maxH;
		window->GetSizeLimits(&minW, &maxW, &minH, &maxH);
		if (minWidth < minW)
			minWidth = minW;
		if (minHeight < minH)
			minHeight = minH;
		if (maxWidth < maxW)
			maxWidth = maxW;
		if (maxHeight < maxH)
			maxHeight = maxH;
	}
	// the current solver don't like big values
	const int32 kMaxSolverValue = 5000;
	if (minWidth > kMaxSolverValue)
		minWidth = kMaxSolverValue;
	if (minHeight > kMaxSolverValue)
		minHeight = kMaxSolverValue;
	if (maxWidth > kMaxSolverValue)
		maxWidth = kMaxSolverValue;
	if (maxHeight > kMaxSolverValue)
		maxHeight = kMaxSolverValue;

	topWindow->AddDecorator(&minWidth, &maxWidth, &minHeight, &maxHeight);
	fMinWidthConstraint->SetRightSide(minWidth);
	fMinHeightConstraint->SetRightSide(minHeight);

	fMaxWidthConstraint->SetRightSide(maxWidth);
	fMaxHeightConstraint->SetRightSide(maxHeight);

	BRect frame = topWindow->CompleteWindowFrame();
	fWidthConstraint->SetRightSide(frame.Width());
	fHeightConstraint->SetRightSide(frame.Height());
}


/**
 * @brief Inserts @a window into the area's window/layer lists.
 *
 * Optionally places the window after a specified peer; otherwise appends.
 * The first window initialises the corner annotations of the surrounding
 * crossings so neighbouring areas can find this one.
 *
 * @param window The window being added; must not be NULL.
 * @param after  Optional anchor; when supplied, @a window is placed at
 *               IndexOf(@a after) + 1.
 * @return true on success, false if either list refused the insertion.
 */
bool
WindowArea::_AddWindow(SATWindow* window, SATWindow* after)
{
	if (after) {
		int32 indexAfter = fWindowList.IndexOf(after);
		if (!fWindowList.AddItem(window, indexAfter + 1))
			return false;
	} else if (fWindowList.AddItem(window) == false)
		return false;

	AcquireReference();

	if (fWindowList.CountItems() <= 1)
		_InitCorners();

	fWindowLayerOrder.AddItem(window);

	_UpdateConstraintValues();
	return true;
}


/**
 * @brief Removes @a window from the area's window/layer lists.
 *
 * Updates the size constraints to reflect the smaller member set, notifies
 * the window via RemovedFromArea(), and releases the reference acquired in
 * _AddWindow().
 *
 * @param window The window being removed.
 * @return true if @a window was a member of this area.
 */
bool
WindowArea::_RemoveWindow(SATWindow* window)
{
	if (!fWindowList.RemoveItem(window))
		return false;

	fWindowLayerOrder.RemoveItem(window);
	_UpdateConstraintValues();

	window->RemovedFromArea(this);
	ReleaseReference();
	return true;
}


/** @brief Returns the Tab forming the area's left edge. */
Tab*
WindowArea::LeftTab()
{
	return fLeftTopCrossing->VerticalTab();
}


/** @brief Returns the Tab forming the area's right edge. */
Tab*
WindowArea::RightTab()
{
	return fRightBottomCrossing->VerticalTab();
}


/** @brief Returns the Tab forming the area's top edge. */
Tab*
WindowArea::TopTab()
{
	return fLeftTopCrossing->HorizontalTab();
}


/** @brief Returns the Tab forming the area's bottom edge. */
Tab*
WindowArea::BottomTab()
{
	return fRightBottomCrossing->HorizontalTab();
}


/**
 * @brief Returns the area's outer rectangle in group coordinates.
 *
 * @return BRect spanning the four bounding crossings.
 */
BRect
WindowArea::Frame()
{
	return BRect(fLeftTopCrossing->VerticalTab()->Position(),
		fLeftTopCrossing->HorizontalTab()->Position(),
		fRightBottomCrossing->VerticalTab()->Position(),
		fRightBottomCrossing->HorizontalTab()->Position());
}


/**
 * @brief Migrates this area into a different SATGroup.
 *
 * Resolves the four bounding crossings against @a group (creating tabs and
 * crossings as needed), reinitialises the area against the new solver, and
 * moves every member window from the old group's window list to the new one.
 * Used when SATGroup::_SplitGroupIfNecessary() splits a disconnected piece.
 *
 * @param group Destination group; must not be NULL.
 * @return true if the migration succeeded; on failure the area may end up
 *         partially uninitialised and its caller must clean up.
 */
bool
WindowArea::PropagateToGroup(SATGroup* group)
{
	BReference<Crossing> newLeftTop = _CrossingByPosition(fLeftTopCrossing,
		group);
	BReference<Crossing> newRightTop = _CrossingByPosition(fRightTopCrossing,
		group);
	BReference<Crossing> newLeftBottom = _CrossingByPosition(
		fLeftBottomCrossing, group);
	BReference<Crossing> newRightBottom = _CrossingByPosition(
		fRightBottomCrossing, group);

	if (!newLeftTop || !newRightTop || !newLeftBottom || !newRightBottom)
		return false;

	// hold a ref to the crossings till we cleaned up everything
	BReference<Crossing> oldLeftTop = fLeftTopCrossing;
	BReference<Crossing> oldRightTop = fRightTopCrossing;
	BReference<Crossing> oldLeftBottom = fLeftBottomCrossing;
	BReference<Crossing> oldRightBottom = fRightBottomCrossing;

	fLeftTopCrossing = newLeftTop;
	fRightTopCrossing = newRightTop;
	fLeftBottomCrossing = newLeftBottom;
	fRightBottomCrossing = newRightBottom;

	_InitCorners();

	BReference<SATGroup> oldGroup = fGroup;
	// manage constraints
	if (Init(group) == false)
		return false;

	oldGroup->fWindowAreaList.RemoveItem(this);
	for (int32 i = 0; i < fWindowList.CountItems(); i++) {
		SATWindow* window = fWindowList.ItemAt(i);
		if (oldGroup->fSATWindowList.RemoveItem(window) == false)
			return false;
		if (group->fSATWindowList.AddItem(window) == false) {
			_UninitConstraints();
			return false;
		}
	}

	_UpdateConstraintValues();

	return true;
}


/**
 * @brief Promotes @a window to the top of the area's z-order.
 *
 * @param window The window to surface.
 * @return true if the window was a member of the layer list.
 */
bool
WindowArea::MoveToTopLayer(SATWindow* window)
{
	if (!fWindowLayerOrder.RemoveItem(window))
		return false;
	return fWindowLayerOrder.AddItem(window);
}


/**
 * @brief Removes every constraint owned by this area from the LP solver.
 *
 * Safe to call when the area has not yet been initialised; in that case the
 * constraint pointers are NULL and the LinearSpec calls become no-ops.
 */
void
WindowArea::_UninitConstraints()
{
	if (fGroup != NULL) {
		LinearSpec* linearSpec = fGroup->GetLinearSpec();

		if (linearSpec != NULL) {
			linearSpec->RemoveConstraint(fMinWidthConstraint, true);
			linearSpec->RemoveConstraint(fMinHeightConstraint, true);
			linearSpec->RemoveConstraint(fMaxWidthConstraint, true);
			linearSpec->RemoveConstraint(fMaxHeightConstraint, true);
			linearSpec->RemoveConstraint(fWidthConstraint, true);
			linearSpec->RemoveConstraint(fHeightConstraint, true);
		}
	}

	fMinWidthConstraint = NULL;
	fMinHeightConstraint = NULL;
	fMaxWidthConstraint = NULL;
	fMaxHeightConstraint = NULL;
	fWidthConstraint = NULL;
	fHeightConstraint = NULL;
}


/**
 * @brief Returns the crossing in @a group at the same position as @a crossing.
 *
 * Looks up (or creates) the matching horizontal and vertical tabs in @a group
 * and returns the crossing where they meet, allocating a new Crossing if
 * necessary. Used by PropagateToGroup() to recreate the area's geometry in a
 * new group.
 *
 * @param crossing Source crossing whose tab positions are matched.
 * @param group    Target group.
 * @return A reference to the resulting crossing, or NULL on allocation
 *         failure.
 */
BReference<Crossing>
WindowArea::_CrossingByPosition(Crossing* crossing, SATGroup* group)
{
	BReference<Crossing> crossRef = NULL;

	Tab* oldHTab = crossing->HorizontalTab();
	BReference<Tab> hTab = group->FindHorizontalTab(oldHTab->Position());
	if (!hTab)
		hTab = group->_AddHorizontalTab(oldHTab->Position());
	if (!hTab)
		return crossRef;

	Tab* oldVTab = crossing->VerticalTab();
	crossRef = hTab->FindCrossing(oldVTab->Position());
	if (crossRef)
		return crossRef;

	BReference<Tab> vTab = group->FindVerticalTab(oldVTab->Position());
	if (!vTab)
		vTab = group->_AddVerticalTab(oldVTab->Position());
	if (!vTab)
		return crossRef;

	return hTab->AddCrossing(vTab);
}


/**
 * @brief Marks this area's owned corners as kUsed and adjacent ones as kFree.
 *
 * Each crossing has four corners; the one pointing into the area is kUsed,
 * and the two flanking corners are eligible neighbours, so their state moves
 * from kNotDockable to kFree where appropriate.
 */
void
WindowArea::_InitCorners()
{
	_SetToWindowCorner(fLeftTopCrossing->RightBottomCorner());
	_SetToNeighbourCorner(fLeftTopCrossing->LeftBottomCorner());
	_SetToNeighbourCorner(fLeftTopCrossing->RightTopCorner());

	_SetToWindowCorner(fRightTopCrossing->LeftBottomCorner());
	_SetToNeighbourCorner(fRightTopCrossing->LeftTopCorner());
	_SetToNeighbourCorner(fRightTopCrossing->RightBottomCorner());

	_SetToWindowCorner(fLeftBottomCrossing->RightTopCorner());
	_SetToNeighbourCorner(fLeftBottomCrossing->LeftTopCorner());
	_SetToNeighbourCorner(fLeftBottomCrossing->RightBottomCorner());

	_SetToWindowCorner(fRightBottomCrossing->LeftTopCorner());
	_SetToNeighbourCorner(fRightBottomCrossing->LeftBottomCorner());
	_SetToNeighbourCorner(fRightBottomCrossing->RightTopCorner());
}


/**
 * @brief Inverse of _InitCorners(); releases corner annotations on removal.
 *
 * Releases the kUsed marker on the area's own corners and reverts the
 * surrounding kFree corners to kNotDockable when no other area is keeping
 * them dockable (the @c opponent argument).
 */
void
WindowArea::_CleanupCorners()
{
	_UnsetWindowCorner(fLeftTopCrossing->RightBottomCorner());
	_UnsetNeighbourCorner(fLeftTopCrossing->LeftBottomCorner(),
		fLeftBottomCrossing->LeftTopCorner());
	_UnsetNeighbourCorner(fLeftTopCrossing->RightTopCorner(),
		fLeftBottomCrossing->LeftTopCorner());

	_UnsetWindowCorner(fRightTopCrossing->LeftBottomCorner());
	_UnsetNeighbourCorner(fRightTopCrossing->LeftTopCorner(),
		fLeftBottomCrossing->RightTopCorner());
	_UnsetNeighbourCorner(fRightTopCrossing->RightBottomCorner(),
		fLeftBottomCrossing->RightTopCorner());

	_UnsetWindowCorner(fLeftBottomCrossing->RightTopCorner());
	_UnsetNeighbourCorner(fLeftBottomCrossing->LeftTopCorner(),
		fLeftBottomCrossing->LeftBottomCorner());
	_UnsetNeighbourCorner(fLeftBottomCrossing->RightBottomCorner(),
		fLeftBottomCrossing->LeftBottomCorner());

	_UnsetWindowCorner(fRightBottomCrossing->LeftTopCorner());
	_UnsetNeighbourCorner(fRightBottomCrossing->LeftBottomCorner(),
		fRightBottomCrossing->RightBottomCorner());
	_UnsetNeighbourCorner(fRightBottomCrossing->RightTopCorner(),
		fRightBottomCrossing->RightBottomCorner());
}


/**
 * @brief Marks @a corner as occupied by this WindowArea.
 *
 * @param corner The corner being claimed.
 */
void
WindowArea::_SetToWindowCorner(Corner* corner)
{
	corner->status = Corner::kUsed;
	corner->windowArea = this;
}


/**
 * @brief Promotes a corner from kNotDockable to kFree if applicable.
 *
 * Used on the corners flanking a freshly-claimed corner so future tiling
 * gestures can attach a neighbour there.
 *
 * @param neighbour The corner whose status may be relaxed.
 */
void
WindowArea::_SetToNeighbourCorner(Corner* neighbour)
{
	if (neighbour->status == Corner::kNotDockable)
		neighbour->status = Corner::kFree;
}


/**
 * @brief Releases the kUsed annotation on @a corner.
 *
 * @param corner The corner that this area is releasing.
 */
void
WindowArea::_UnsetWindowCorner(Corner* corner)
{
	corner->status = Corner::kFree;
	corner->windowArea = NULL;
}


/**
 * @brief Reverts a kFree corner to kNotDockable when no other area uses it.
 *
 * The @a opponent corner is the other neighbour of the same crossing-of-
 * crossings configuration; if it is not kUsed, no living area depends on
 * @a neighbour being dockable, so we revert it.
 *
 * @param neighbour Corner to potentially revert.
 * @param opponent  Adjacent corner whose state gates the revert.
 */
void
WindowArea::_UnsetNeighbourCorner(Corner* neighbour, Corner* opponent)
{
	if (neighbour->status == Corner::kFree && opponent->status != Corner::kUsed)
		neighbour->status = Corner::kNotDockable;
}


/**
 * @brief Applies the solver's result to the top window of this area.
 *
 * Translates the LP variable values back to screen coordinates (subtracting
 * kMakePositiveOffset), adjusts the window's size limits to admit the new
 * frame, and moves and resizes the wrapped server window via the desktop.
 *
 * @param triggerWindow The window whose move/resize triggered the layout
 *                      pass; used to look up the desktop and current
 *                      workspace.
 */
void
WindowArea::_MoveToSAT(SATWindow* triggerWindow)
{
	SATWindow* topWindow = TopWindow();
	// if there is no window in the group we are done
	if (topWindow == NULL)
		return;

	BRect frameSAT(LeftVar()->Value() - kMakePositiveOffset,
		TopVar()->Value() - kMakePositiveOffset,
		RightVar()->Value() - kMakePositiveOffset,
		BottomVar()->Value() - kMakePositiveOffset);
	topWindow->AdjustSizeLimits(frameSAT);

	BRect frame = topWindow->CompleteWindowFrame();
	float deltaToX = round(frameSAT.left - frame.left);
	float deltaToY = round(frameSAT.top - frame.top);
	frame.OffsetBy(deltaToX, deltaToY);
	float deltaByX = round(frameSAT.right - frame.right);
	float deltaByY = round(frameSAT.bottom - frame.bottom);

	int32 workspace = triggerWindow->GetWindow()->CurrentWorkspace();
	if (workspace < 0)
		workspace = triggerWindow->GetWindow()->PriorWorkspace();
	Desktop* desktop = triggerWindow->GetWindow()->Desktop();
	desktop->MoveWindowBy(topWindow->GetWindow(), deltaToX, deltaToY, workspace);
	// Update frame to the new position
	desktop->ResizeWindowBy(topWindow->GetWindow(), deltaByX, deltaByY);

	UpdateSizeConstaints(frameSAT);
}


/** @brief Constructs a Corner with default kNotDockable status and no owner. */
Corner::Corner()
	:
	status(kNotDockable),
	windowArea(NULL)
{

}


/**
 * @brief Logs the corner's status, plus the titles of any attached windows.
 *
 * Debug aid; writes through debug_printf().
 */
void
Corner::Trace() const
{
	switch (status) {
		case kFree:
			debug_printf("free corner\n");
			break;

		case kUsed:
		{
			debug_printf("attached windows:\n");
			const SATWindowList& list = windowArea->WindowList();
			for (int i = 0; i < list.CountItems(); i++) {
				debug_printf("- %s\n", list.ItemAt(i)->GetWindow()->Title());
			}
			break;
		}

		case kNotDockable:
			debug_printf("not dockable\n");
			break;
	};
}


/**
 * @brief Constructs a Crossing at the intersection of two tabs.
 *
 * Holds strong references to both tabs so the crossing keeps the underlying
 * tabs alive.
 *
 * @param vertical   Vertical tab.
 * @param horizontal Horizontal tab.
 */
Crossing::Crossing(Tab* vertical, Tab* horizontal)
	:
	fVerticalTab(vertical),
	fHorizontalTab(horizontal)
{
}


/**
 * @brief Removes the crossing from both tabs' crossing lists.
 *
 * Tabs may then be released by their last reference holders.
 */
Crossing::~Crossing()
{
	fVerticalTab->RemoveCrossing(this);
	fHorizontalTab->RemoveCrossing(this);
}


/**
 * @brief Returns the corner at the requested position around this crossing.
 *
 * @param corner Which corner to retrieve (kLeftTop, kRightTop, ...).
 * @return Pointer to the corner; never NULL.
 */
Corner*
Crossing::GetCorner(Corner::position_t corner) const
{
	return &const_cast<Corner*>(fCorners)[corner];
}


/**
 * @brief Returns the corner diagonally opposite @a corner.
 *
 * Position constants are arranged so kLeftTop (0) and kRightBottom (3) are
 * opposites, as are kRightTop (1) and kLeftBottom (2).
 *
 * @param corner The reference corner.
 * @return Pointer to the corner at position (3 - @a corner).
 */
Corner*
Crossing::GetOppositeCorner(Corner::position_t corner) const
{
	return &const_cast<Corner*>(fCorners)[3 - corner];
}


/** @brief Returns the vertical tab participating in the crossing. */
Tab*
Crossing::VerticalTab() const
{
	return fVerticalTab;
}


/** @brief Returns the horizontal tab participating in the crossing. */
Tab*
Crossing::HorizontalTab() const
{
	return fHorizontalTab;
}


/**
 * @brief Logs the status of all four corners; debug aid.
 *
 * Writes through debug_printf() in the same format as Corner::Trace().
 */
void
Crossing::Trace() const
{
	debug_printf("left-top corner: ");
	fCorners[Corner::kLeftTop].Trace();
	debug_printf("right-top corner: ");
	fCorners[Corner::kRightTop].Trace();
	debug_printf("left-bottom corner: ");
	fCorners[Corner::kLeftBottom].Trace();
	debug_printf("right-bottom corner: ");
	fCorners[Corner::kRightBottom].Trace();
}


/**
 * @brief Constructs a Tab bound to a solver Variable.
 *
 * @param group       The group that owns the variable.
 * @param variable    LP variable that backs the tab's position.
 * @param orientation kVertical or kHorizontal.
 */
Tab::Tab(SATGroup* group, Variable* variable, orientation_t orientation)
	:
	fGroup(group),
	fVariable(variable),
	fOrientation(orientation)
{

}


/**
 * @brief Removes the tab from its group's tab list.
 *
 * Routed via SATGroup so the group's sorted/unsorted bookkeeping stays in
 * sync.
 */
Tab::~Tab()
{
	if (fOrientation == kVertical)
		fGroup->_RemoveVerticalTab(this);
	else
		fGroup->_RemoveHorizontalTab(this);
}


/**
 * @brief Returns the tab's position in screen coordinates.
 *
 * Subtracts kMakePositiveOffset so callers see the natural coordinate even
 * though the solver works on a positive-only axis.
 *
 * @return Position in screen coordinates.
 */
float
Tab::Position() const
{
	return (float)fVariable->Value() - kMakePositiveOffset;
}


/**
 * @brief Sets the tab's position in screen coordinates.
 *
 * Adds kMakePositiveOffset internally to keep the solver variable positive.
 *
 * @param position Desired position in screen coordinates.
 */
void
Tab::SetPosition(float position)
{
	fVariable->SetValue(position + kMakePositiveOffset);
}


/** @brief Returns the tab's orientation (kVertical / kHorizontal). */
Tab::orientation_t
Tab::Orientation() const
{
	return fOrientation;
}


/**
 * @brief Adds an equality constraint binding @a variable to this tab's variable.
 *
 * Used to anchor a window's solver variable to a tab so the tab moves with
 * the window.
 *
 * @param variable LP variable to tie to the tab.
 * @return The new constraint; ownership is transferred to the caller.
 */
Constraint*
Tab::Connect(Variable* variable)
{
	return fVariable->IsEqual(variable);
}


/**
 * @brief Creates a Crossing between this tab and @a tab.
 *
 * Allocates a Crossing oriented so the vertical tab is always the v member
 * and the horizontal tab is always the h member, and registers it on both
 * tabs' crossing lists.
 *
 * @param tab The orthogonal tab participating in the crossing.
 * @return A reference to the new crossing, or NULL if the orientations are
 *         incompatible or allocation failed.
 */
BReference<Crossing>
Tab::AddCrossing(Tab* tab)
{
	if (tab->Orientation() == fOrientation)
		return NULL;

	Tab* vTab = (fOrientation == kVertical) ? this : tab;
	Tab* hTab = (fOrientation == kHorizontal) ? this : tab;

	Crossing* crossing = new (std::nothrow)Crossing(vTab, hTab);
	if (!crossing)
		return NULL;

	if (!fCrossingList.AddItem(crossing)) {
		return NULL;
	}
	if (!tab->fCrossingList.AddItem(crossing)) {
		fCrossingList.RemoveItem(crossing);
		return NULL;
	}

	BReference<Crossing> crossingRef(crossing, true);
	return crossingRef;
}


/**
 * @brief Removes @a crossing from this tab's crossing list.
 *
 * Validates that one of the crossing's tabs is this tab before removing.
 *
 * @param crossing The crossing being detached.
 * @return true if @a crossing was a member of this tab.
 */
bool
Tab::RemoveCrossing(Crossing* crossing)
{
	Tab* vTab = crossing->VerticalTab();
	Tab* hTab = crossing->HorizontalTab();

	if (vTab != this && hTab != this)
		return false;
	fCrossingList.RemoveItem(crossing);

	return true;
}


/**
 * @brief Returns the index of the crossing that involves @a tab.
 *
 * @param tab The orthogonal tab to search for.
 * @return Index in fCrossingList, or -1 if not found.
 */
int32
Tab::FindCrossingIndex(Tab* tab)
{
	if (fOrientation == kVertical) {
		for (int32 i = 0; i < fCrossingList.CountItems(); i++) {
			if (fCrossingList.ItemAt(i)->HorizontalTab() == tab)
				return i;
		}
	} else {
		for (int32 i = 0; i < fCrossingList.CountItems(); i++) {
			if (fCrossingList.ItemAt(i)->VerticalTab() == tab)
				return i;
		}
	}
	return -1;
}


/**
 * @brief Returns the index of the crossing whose orthogonal tab is at @a pos.
 *
 * Float comparisons use a 0.0001 tolerance to absorb rounding noise from the
 * LP solver.
 *
 * @param pos Position of the orthogonal tab to match.
 * @return Index in fCrossingList, or -1 if no crossing is close enough.
 */
int32
Tab::FindCrossingIndex(float pos)
{
	if (fOrientation == kVertical) {
		for (int32 i = 0; i < fCrossingList.CountItems(); i++) {
			if (fabs(fCrossingList.ItemAt(i)->HorizontalTab()->Position() - pos)
				< 0.0001)
				return i;
		}
	} else {
		for (int32 i = 0; i < fCrossingList.CountItems(); i++) {
			if (fabs(fCrossingList.ItemAt(i)->VerticalTab()->Position() - pos)
				< 0.0001)
				return i;
		}
	}
	return -1;
}


/**
 * @brief Returns the crossing involving @a tab, or NULL.
 *
 * @param tab The orthogonal tab to search for.
 * @return Pointer to the crossing, or NULL.
 */
Crossing*
Tab::FindCrossing(Tab* tab)
{
	return fCrossingList.ItemAt(FindCrossingIndex(tab));
}


/**
 * @brief Returns the crossing whose orthogonal tab is at @a tabPosition.
 *
 * @param tabPosition Position of the orthogonal tab.
 * @return Pointer to the crossing, or NULL.
 */
Crossing*
Tab::FindCrossing(float tabPosition)
{
	return fCrossingList.ItemAt(FindCrossingIndex(tabPosition));
}


/** @brief Returns the read-only list of crossings on this tab. */
const CrossingList*
Tab::GetCrossingList() const
{
	return &fCrossingList;
}


/**
 * @brief Compares two tabs by position; used to keep tab lists sorted.
 *
 * @param tab1 First tab.
 * @param tab2 Second tab.
 * @return Negative if @a tab1 is to the left/above @a tab2, positive
 *         otherwise.
 */
int
Tab::CompareFunction(const Tab* tab1, const Tab* tab2)
{
	if (tab1->Position() < tab2->Position())
		return -1;

	return 1;
}


/**
 * @brief Constructs an empty SATGroup with a fresh LinearSpec solver.
 *
 * The group starts with no tabs, no areas, and no active window. A
 * fresh LinearSpec is allocated lazily in the initializer; a failed
 * allocation simply leaves fLinearSpec NULL and AddWindow() will refuse
 * to add anything.
 */
SATGroup::SATGroup()
	:
	fLinearSpec(new(std::nothrow) LinearSpec()),
	fHorizontalTabsSorted(false),
	fVerticalTabsSorted(false),
	fActiveWindow(NULL)
{
}


/**
 * @brief Releases the LinearSpec and asserts the group is empty.
 *
 * The destructor traps in @c debugger() if any windows are still attached;
 * SATWindow detaches itself in its own destructor, so reaching this state
 * indicates a leaked owner.
 */
SATGroup::~SATGroup()
{
	// Should be empty
	if (fSATWindowList.CountItems() > 0)
		debugger("Deleting a SATGroup which is not empty");
	//while (fSATWindowList.CountItems() > 0)
	//	RemoveWindow(fSATWindowList.ItemAt(0));

	fLinearSpec->ReleaseReference();
}


/**
 * @brief Creates a new WindowArea defined by four tabs and adds @a window to it.
 *
 * Any of @a left, @a top, @a right, @a bottom may be NULL, in which case a
 * fresh tab is allocated. The four corner crossings are likewise created on
 * demand. Once the area exists, the window is delegated to the
 * AddWindow(window, area) overload below.
 *
 * @param window The window to add; must not be NULL.
 * @param left   Left tab or NULL to create one.
 * @param top    Top tab or NULL to create one.
 * @param right  Right tab or NULL to create one.
 * @param bottom Bottom tab or NULL to create one.
 * @return true on success, false on allocation failure or area init error.
 */
bool
SATGroup::AddWindow(SATWindow* window, Tab* left, Tab* top, Tab* right,
	Tab* bottom)
{
	STRACE_SAT("SATGroup::AddWindow\n");

	// first check if we have to create tabs and missing corners.
	BReference<Tab> leftRef, rightRef, topRef, bottomRef;
	BReference<Crossing> leftTopRef, rightTopRef, leftBottomRef, rightBottomRef;

	if (left != NULL && top != NULL)
		leftTopRef = left->FindCrossing(top);
	if (right != NULL && top != NULL)
		rightTopRef = right->FindCrossing(top);
	if (left != NULL && bottom != NULL)
		leftBottomRef = left->FindCrossing(bottom);
	if (right != NULL && bottom != NULL)
		rightBottomRef = right->FindCrossing(bottom);

	if (left == NULL) {
		leftRef = _AddVerticalTab();
		left = leftRef.Get();
	}
	if (top == NULL) {
		topRef = _AddHorizontalTab();
		top = topRef.Get();
	}
	if (right == NULL) {
		rightRef = _AddVerticalTab();
		right = rightRef.Get();
	}
	if (bottom == NULL) {
		bottomRef = _AddHorizontalTab();
		bottom = bottomRef.Get();
	}
	if (left == NULL || top == NULL || right == NULL || bottom == NULL)
		return false;

	if (leftTopRef == NULL) {
		leftTopRef = left->AddCrossing(top);
		if (leftTopRef == NULL)
			return false;
	}
	if (!rightTopRef) {
		rightTopRef = right->AddCrossing(top);
		if (!rightTopRef)
			return false;
	}
	if (!leftBottomRef) {
		leftBottomRef = left->AddCrossing(bottom);
		if (!leftBottomRef)
			return false;
	}
	if (!rightBottomRef) {
		rightBottomRef = right->AddCrossing(bottom);
		if (!rightBottomRef)
			return false;
	}

	WindowArea* area = new(std::nothrow) WindowArea(leftTopRef, rightTopRef,
		leftBottomRef, rightBottomRef);
	if (area == NULL)
		return false;
	// the area register itself in our area list
	if (area->Init(this) == false) {
		delete area;
		return false;
	}
	// delete the area if AddWindow failed / release our reference on it
	BReference<WindowArea> areaRef(area, true);

	return AddWindow(window, area);
}


/**
 * @brief Adds @a window to an existing WindowArea.
 *
 * Updates the area's window list, the group's master list, and notifies
 * the window via AddedToGroup(). Rolls back partial state on failure.
 *
 * @param window The window being added; must not be NULL.
 * @param area   Destination area; must not be NULL.
 * @param after  Optional anchor inside the area's stack.
 * @return true on success.
 */
bool
SATGroup::AddWindow(SATWindow* window, WindowArea* area, SATWindow* after)
{
	if (!area->_AddWindow(window, after))
		return false;

	if (!fSATWindowList.AddItem(window)) {
		area->_RemoveWindow(window);
		return false;
	}

	if (!window->AddedToGroup(this, area)) {
		area->_RemoveWindow(window);
		fSATWindowList.RemoveItem(window);
		return false;
	}

	return true;
}


/**
 * @brief Removes @a window from the group and re-solves the layout.
 *
 * Holds a reference to the window's WindowArea while detaching so that
 * releasing the last group reference does not free the area before the
 * removal hooks have run. Re-runs the layout when at least two windows
 * remain so the survivors fill the freed space.
 *
 * @param window         The window to remove; must be a member.
 * @param stayBelowMouse If true, restore the window so the cursor still
 *                       sits over the same decorator region.
 * @return true if the window was a member of the group.
 */
bool
SATGroup::RemoveWindow(SATWindow* window, bool stayBelowMouse)
{
	if (!fSATWindowList.RemoveItem(window))
		return false;

	// We need the area a little bit longer because the area could hold the
	// last reference to the group.
	BReference<WindowArea> area = window->GetWindowArea();
	if (area.IsSet())
		area->_RemoveWindow(window);

	window->RemovedFromGroup(this, stayBelowMouse);

	if (CountItems() >= 2)
		WindowAt(0)->DoGroupLayout();

	return true;
}


/** @brief Returns the number of SATWindow members in the group. */
int32
SATGroup::CountItems()
{
	return fSATWindowList.CountItems();
}


/**
 * @brief Returns the window at @a index in the group's master list.
 *
 * @param index Position in fSATWindowList.
 * @return The window, or NULL if @a index is out of range.
 */
SATWindow*
SATGroup::WindowAt(int32 index)
{
	return fSATWindowList.ItemAt(index);
}


/** @brief Returns the window remembered as active by SetActiveWindow(). */
SATWindow*
SATGroup::ActiveWindow() const
{
	return fActiveWindow;
}


/**
 * @brief Records @a window as the group's currently active window.
 *
 * Used by group-navigation shortcuts to remember which member should regain
 * focus when the user returns to this group.
 *
 * @param window The window to remember.
 */
void
SATGroup::SetActiveWindow(SATWindow* window)
{
	fActiveWindow = window;
}


/**
 * @brief Returns the group's horizontal tabs, sorted by position.
 *
 * Sorts lazily on first access after a tab insertion or removal.
 *
 * @return Read-only list of tabs, top to bottom.
 */
const TabList*
SATGroup::HorizontalTabs()
{
	if (!fHorizontalTabsSorted) {
		fHorizontalTabs.SortItems(Tab::CompareFunction);
		fHorizontalTabsSorted = true;
	}
	return &fHorizontalTabs;
}


/**
 * @brief Returns the group's vertical tabs, sorted by position.
 *
 * Sorts lazily on first access after a tab insertion or removal.
 *
 * @return Read-only list of tabs, left to right.
 */
const TabList*
SATGroup::VerticalTabs()
{
	if (!fVerticalTabsSorted) {
		fVerticalTabs.SortItems(Tab::CompareFunction);
		fVerticalTabsSorted = true;
	}
	return &fVerticalTabs;
}


/**
 * @brief Looks up a horizontal tab by exact position.
 *
 * @param position The position to match (within 0.00001).
 * @return The tab if any matches, NULL otherwise.
 */
Tab*
SATGroup::FindHorizontalTab(float position)
{
	return _FindTab(fHorizontalTabs, position);
}


/**
 * @brief Looks up a vertical tab by exact position.
 *
 * @param position The position to match (within 0.00001).
 * @return The tab if any matches, NULL otherwise.
 */
Tab*
SATGroup::FindVerticalTab(float position)
{
	return _FindTab(fVerticalTabs, position);
}


/**
 * @brief Hook called by WindowArea's destructor to drive split-detection.
 *
 * Forwards to _SplitGroupIfNecessary() which decides whether the remaining
 * areas still form one connected component.
 *
 * @param area The area being destroyed.
 */
void
SATGroup::WindowAreaRemoved(WindowArea* area)
{
	_SplitGroupIfNecessary(area);
}


/**
 * @brief Reconstructs a group from a previously-archived BMessage.
 *
 * Allocates the requested number of horizontal and vertical tabs, then walks
 * the message's "area" sub-messages to reattach the matching SATWindows
 * (looked up by id via @a sat) into reconstituted WindowAreas, stacking
 * subsequent windows of the same area onto the first one.
 *
 * @param archive Flattened group description produced by ArchiveGroup().
 * @param sat     StackAndTile listener used to resolve window ids to
 *                live windows.
 * @return B_OK on success, B_NO_MEMORY if any allocation failed, or another
 *         status_t code from BMessage on a malformed archive.
 */
status_t
SATGroup::RestoreGroup(const BMessage& archive, StackAndTile* sat)
{
	// create new group
	SATGroup* group = new (std::nothrow)SATGroup;
	if (group == NULL)
		return B_NO_MEMORY;
	BReference<SATGroup> groupRef;
	groupRef.SetTo(group, true);

	int32 nHTabs, nVTabs;
	status_t status;
	status = archive.FindInt32("htab_count", &nHTabs);
	if (status != B_OK)
		return status;
	status = archive.FindInt32("vtab_count", &nVTabs);
	if (status != B_OK)
		return status;

	vector<BReference<Tab> > tempHTabs;
	for (int i = 0; i < nHTabs; i++) {
		BReference<Tab> tab = group->_AddHorizontalTab();
		if (!tab)
			return B_NO_MEMORY;
		tempHTabs.push_back(tab);
	}
	vector<BReference<Tab> > tempVTabs;
	for (int i = 0; i < nVTabs; i++) {
		BReference<Tab> tab = group->_AddVerticalTab();
		if (!tab)
			return B_NO_MEMORY;
		tempVTabs.push_back(tab);
	}

	BMessage areaArchive;
	for (int32 i = 0; archive.FindMessage("area", i, &areaArchive) == B_OK;
		i++) {
		uint32 leftTab, rightTab, topTab, bottomTab;
		if (areaArchive.FindInt32("left_tab", (int32*)&leftTab) != B_OK
			|| areaArchive.FindInt32("right_tab", (int32*)&rightTab) != B_OK
			|| areaArchive.FindInt32("top_tab", (int32*)&topTab) != B_OK
			|| areaArchive.FindInt32("bottom_tab", (int32*)&bottomTab) != B_OK)
			return B_ERROR;

		if (leftTab >= tempVTabs.size() || rightTab >= tempVTabs.size())
			return B_BAD_VALUE;
		if (topTab >= tempHTabs.size() || bottomTab >= tempHTabs.size())
			return B_BAD_VALUE;

		Tab* left = tempVTabs[leftTab];
		Tab* right = tempVTabs[rightTab];
		Tab* top = tempHTabs[topTab];
		Tab* bottom = tempHTabs[bottomTab];

		// adding windows to area
		uint64 windowId;
		SATWindow* prevWindow = NULL;
		for (int32 i = 0; areaArchive.FindInt64("window", i,
			(int64*)&windowId) == B_OK; i++) {
			SATWindow* window = sat->FindSATWindow(windowId);
			if (!window)
				continue;

			if (prevWindow == NULL) {
				if (!group->AddWindow(window, left, top, right, bottom))
					continue;
				prevWindow = window;
			} else {
				if (!prevWindow->StackWindow(window))
					continue;
				prevWindow = window;
			}
		}
	}
	return B_OK;
}


/**
 * @brief Serialises the group into a BMessage suitable for RestoreGroup().
 *
 * Records the tab counts and, for every WindowArea, the indices of its four
 * bounding tabs plus the persistent ids of every member window.
 *
 * @param archive Output archive.
 * @return B_OK; the underlying BMessage Add calls do not fail in practice.
 */
status_t
SATGroup::ArchiveGroup(BMessage& archive)
{
	archive.AddInt32("htab_count", fHorizontalTabs.CountItems());
	archive.AddInt32("vtab_count", fVerticalTabs.CountItems());

	for (int i = 0; i < fWindowAreaList.CountItems(); i++) {
		WindowArea* area = fWindowAreaList.ItemAt(i);
		int32 leftTab = fVerticalTabs.IndexOf(area->LeftTab());
		int32 rightTab = fVerticalTabs.IndexOf(area->RightTab());
		int32 topTab = fHorizontalTabs.IndexOf(area->TopTab());
		int32 bottomTab = fHorizontalTabs.IndexOf(area->BottomTab());

		BMessage areaMessage;
		areaMessage.AddInt32("left_tab", leftTab);
		areaMessage.AddInt32("right_tab", rightTab);
		areaMessage.AddInt32("top_tab", topTab);
		areaMessage.AddInt32("bottom_tab", bottomTab);

		const SATWindowList& windowList = area->WindowList();
		for (int a = 0; a < windowList.CountItems(); a++)
			areaMessage.AddInt64("window", windowList.ItemAt(a)->Id());

		archive.AddMessage("area", &areaMessage);
	}
	return B_OK;
}


/**
 * @brief Allocates a new horizontal tab at @a position and registers it.
 *
 * Asks the LP solver for a fresh variable, wraps it in a Tab, and inserts
 * the tab into fHorizontalTabs (marking the list dirty so the next
 * HorizontalTabs() call re-sorts).
 *
 * @param position Initial position for the tab.
 * @return Reference to the new tab, or NULL on allocation failure.
 */
BReference<Tab>
SATGroup::_AddHorizontalTab(float position)
{
	if (fLinearSpec == NULL)
		return NULL;
	Variable* variable = fLinearSpec->AddVariable();
	if (variable == NULL)
		return NULL;

	Tab* tab = new (std::nothrow)Tab(this, variable, Tab::kHorizontal);
	if (tab == NULL)
		return NULL;
	BReference<Tab> tabRef(tab, true);

	if (!fHorizontalTabs.AddItem(tab))
		return NULL;

	fHorizontalTabsSorted = false;
	tabRef->SetPosition(position);
	return tabRef;
}


/**
 * @brief Allocates a new vertical tab at @a position and registers it.
 *
 * Mirror of _AddHorizontalTab() for vertical orientation.
 *
 * @param position Initial position for the tab.
 * @return Reference to the new tab, or NULL on allocation failure.
 */
BReference<Tab>
SATGroup::_AddVerticalTab(float position)
{
	if (fLinearSpec == NULL)
		return NULL;
	Variable* variable = fLinearSpec->AddVariable();
	if (variable == NULL)
		return NULL;

	Tab* tab = new (std::nothrow)Tab(this, variable, Tab::kVertical);
	if (tab == NULL)
		return NULL;
	BReference<Tab> tabRef(tab, true);

	if (!fVerticalTabs.AddItem(tab))
		return NULL;

	fVerticalTabsSorted = false;
	tabRef->SetPosition(position);
	return tabRef;
}


/**
 * @brief Removes @a tab from the horizontal tab list without deleting it.
 *
 * Tabs are reference counted, so the actual memory is freed when the last
 * reference is released by ~Tab().
 *
 * @param tab The tab to remove.
 * @return true if the tab was a member.
 */
bool
SATGroup::_RemoveHorizontalTab(Tab* tab)
{
	if (!fHorizontalTabs.RemoveItem(tab))
		return false;
	fHorizontalTabsSorted = false;
	// don't delete the tab it is reference counted
	return true;
}


/**
 * @brief Removes @a tab from the vertical tab list without deleting it.
 *
 * @param tab The tab to remove.
 * @return true if the tab was a member.
 */
bool
SATGroup::_RemoveVerticalTab(Tab* tab)
{
	if (!fVerticalTabs.RemoveItem(tab))
		return false;
	fVerticalTabsSorted = false;
	// don't delete the tab it is reference counted
	return true;
}


/**
 * @brief Linear search of @a list for a tab whose Position() matches @a position.
 *
 * @param list     Tab list to search.
 * @param position Target position; matching tolerance is 0.00001.
 * @return The matching tab or NULL.
 */
Tab*
SATGroup::_FindTab(const TabList& list, float position)
{
	for (int i = 0; i < list.CountItems(); i++)
		if (fabs(list.ItemAt(i)->Position() - position) < 0.00001)
			return list.ItemAt(i);

	return NULL;
}


/**
 * @brief Splits the group when removing @a removedArea disconnects the rest.
 *
 * Builds an initial seed list from the neighbours of @a removedArea, then
 * repeatedly grows connected components from that seed via _FollowSeed().
 * The first component found shares this group's id (it is the "remainder");
 * each subsequent component is migrated into a freshly-allocated group via
 * _SpawnNewGroup(). Single-window components are simply ungrouped.
 *
 * @param removedArea The area whose disappearance triggered the check.
 */
void
SATGroup::_SplitGroupIfNecessary(WindowArea* removedArea)
{
	// if there are windows stacked in the area we don't need to split
	if (removedArea == NULL || removedArea->WindowList().CountItems() > 1)
		return;

	WindowAreaList neighbourWindows;

	_FillNeighbourList(neighbourWindows, removedArea);

	bool ownGroupProcessed = false;
	WindowAreaList newGroup;
	while (_FindConnectedGroup(neighbourWindows, removedArea, newGroup)) {
		STRACE_SAT("Connected group found; %i window(s)\n",
			(int)newGroup.CountItems());
		if (newGroup.CountItems() == 1
			&& newGroup.ItemAt(0)->WindowList().CountItems() == 1) {
			SATWindow* window = newGroup.ItemAt(0)->WindowList().ItemAt(0);
			RemoveWindow(window);
			_EnsureGroupIsOnScreen(window->GetGroup());
		} else if (ownGroupProcessed)
			_SpawnNewGroup(newGroup);
		else {
			_EnsureGroupIsOnScreen(this);
			ownGroupProcessed = true;
		}

		newGroup.MakeEmpty();
	}
}


/**
 * @brief Collects every WindowArea adjacent to @a area on any of its four sides.
 *
 * @param neighbourWindows Output list to append the neighbours to.
 * @param area             The reference area.
 */
void
SATGroup::_FillNeighbourList(WindowAreaList& neighbourWindows,
	WindowArea* area)
{
	_LeftNeighbours(neighbourWindows, area);
	_RightNeighbours(neighbourWindows, area);
	_TopNeighbours(neighbourWindows, area);
	_BottomNeighbours(neighbourWindows, area);
}


/**
 * @brief Adds every area touching @a parent's left edge to @a neighbourWindows.
 *
 * Walks the crossings on @a parent's left tab and, for each kUsed corner that
 * faces left, checks that the neighbour's vertical extent overlaps
 * @a parent's.
 *
 * @param neighbourWindows Output list.
 * @param parent           Reference area.
 */
void
SATGroup::_LeftNeighbours(WindowAreaList& neighbourWindows, WindowArea* parent)
{
	float startPos = parent->LeftTopCrossing()->HorizontalTab()->Position();
	float endPos = parent->LeftBottomCrossing()->HorizontalTab()->Position();

	Tab* tab = parent->LeftTopCrossing()->VerticalTab();
	const CrossingList* crossingList = tab->GetCrossingList();
	for (int i = 0; i < crossingList->CountItems(); i++) {
		Corner* corner = crossingList->ItemAt(i)->LeftTopCorner();
		if (corner->status != Corner::kUsed)
			continue;

		WindowArea* area = corner->windowArea;
		float pos1 = area->LeftTopCrossing()->HorizontalTab()->Position();
		float pos2 = area->LeftBottomCrossing()->HorizontalTab()->Position();

		if (pos1 < endPos && pos2 > startPos)
			neighbourWindows.AddItem(area);

		if (pos2 > endPos)
			break;
	}
}


/**
 * @brief Adds every area touching @a parent's top edge to @a neighbourWindows.
 *
 * @param neighbourWindows Output list.
 * @param parent           Reference area.
 */
void
SATGroup::_TopNeighbours(WindowAreaList& neighbourWindows, WindowArea* parent)
{
	float startPos = parent->LeftTopCrossing()->VerticalTab()->Position();
	float endPos = parent->RightTopCrossing()->VerticalTab()->Position();

	Tab* tab = parent->LeftTopCrossing()->HorizontalTab();
	const CrossingList* crossingList = tab->GetCrossingList();
	for (int i = 0; i < crossingList->CountItems(); i++) {
		Corner* corner = crossingList->ItemAt(i)->LeftTopCorner();
		if (corner->status != Corner::kUsed)
			continue;

		WindowArea* area = corner->windowArea;
		float pos1 = area->LeftTopCrossing()->VerticalTab()->Position();
		float pos2 = area->RightTopCrossing()->VerticalTab()->Position();

		if (pos1 < endPos && pos2 > startPos)
			neighbourWindows.AddItem(area);

		if (pos2 > endPos)
			break;
	}
}


/**
 * @brief Adds every area touching @a parent's right edge to @a neighbourWindows.
 *
 * @param neighbourWindows Output list.
 * @param parent           Reference area.
 */
void
SATGroup::_RightNeighbours(WindowAreaList& neighbourWindows, WindowArea* parent)
{
	float startPos = parent->RightTopCrossing()->HorizontalTab()->Position();
	float endPos = parent->RightBottomCrossing()->HorizontalTab()->Position();

	Tab* tab = parent->RightTopCrossing()->VerticalTab();
	const CrossingList* crossingList = tab->GetCrossingList();
	for (int i = 0; i < crossingList->CountItems(); i++) {
		Corner* corner = crossingList->ItemAt(i)->RightTopCorner();
		if (corner->status != Corner::kUsed)
			continue;

		WindowArea* area = corner->windowArea;
		float pos1 = area->RightTopCrossing()->HorizontalTab()->Position();
		float pos2 = area->RightBottomCrossing()->HorizontalTab()->Position();

		if (pos1 < endPos && pos2 > startPos)
			neighbourWindows.AddItem(area);

		if (pos2 > endPos)
			break;
	}
}


/**
 * @brief Adds every area touching @a parent's bottom edge to @a neighbourWindows.
 *
 * @param neighbourWindows Output list.
 * @param parent           Reference area.
 */
void
SATGroup::_BottomNeighbours(WindowAreaList& neighbourWindows,
	WindowArea* parent)
{
	float startPos = parent->LeftBottomCrossing()->VerticalTab()->Position();
	float endPos = parent->RightBottomCrossing()->VerticalTab()->Position();

	Tab* tab = parent->LeftBottomCrossing()->HorizontalTab();
	const CrossingList* crossingList = tab->GetCrossingList();
	for (int i = 0; i < crossingList->CountItems(); i++) {
		Corner* corner = crossingList->ItemAt(i)->LeftBottomCorner();
		if (corner->status != Corner::kUsed)
			continue;

		WindowArea* area = corner->windowArea;
		float pos1 = area->LeftBottomCrossing()->VerticalTab()->Position();
		float pos2 = area->RightBottomCrossing()->VerticalTab()->Position();

		if (pos1 < endPos && pos2 > startPos)
			neighbourWindows.AddItem(area);

		if (pos2 > endPos)
			break;
	}
}


/**
 * @brief Pops one seed off @a seedList and grows a connected component.
 *
 * The popped area becomes the first member of @a newGroup; _FollowSeed()
 * then recursively visits every reachable neighbour, removing them from
 * @a seedList so subsequent calls return the next disconnected component.
 *
 * @param seedList    Working list of remaining seed areas.
 * @param removedArea Area to skip during traversal (acts as a forbidden
 *                    bridge).
 * @param newGroup    Output list collecting the connected component.
 * @return true if a non-empty component was extracted.
 */
bool
SATGroup::_FindConnectedGroup(WindowAreaList& seedList, WindowArea* removedArea,
	WindowAreaList& newGroup)
{
	if (seedList.CountItems() == 0)
		return false;

	WindowArea* area = seedList.RemoveItemAt(0);
	newGroup.AddItem(area);

	_FollowSeed(area, removedArea, seedList, newGroup);
	return true;
}


/**
 * @brief Recursively walks every reachable neighbour of @a area into @a newGroup.
 *
 * Skips @a veto so the removed area cannot bridge two otherwise-disconnected
 * components, and prunes neighbours that have already been added.
 *
 * @param area     Current area being expanded.
 * @param veto     Area to skip (the one being removed).
 * @param seedList Working list whose entries are removed as they are
 *                 absorbed.
 * @param newGroup Output list of all reachable areas.
 */
void
SATGroup::_FollowSeed(WindowArea* area, WindowArea* veto,
	WindowAreaList& seedList, WindowAreaList& newGroup)
{
	WindowAreaList neighbours;
	_FillNeighbourList(neighbours, area);
	for (int i = 0; i < neighbours.CountItems(); i++) {
		WindowArea* currentArea = neighbours.ItemAt(i);
		if (currentArea != veto && !newGroup.HasItem(currentArea)) {
			newGroup.AddItem(currentArea);
			// if we get a area from the seed list it is not a seed any more
			seedList.RemoveItem(currentArea);
		} else {
			// don't _FollowSeed of invalid areas
			neighbours.RemoveItemAt(i);
			i--;
		}
	}

	for (int i = 0; i < neighbours.CountItems(); i++)
		_FollowSeed(neighbours.ItemAt(i), veto, seedList, newGroup);
}


/**
 * @brief Migrates @a newGroup areas into a freshly-allocated SATGroup.
 *
 * Constructs an empty group and calls PropagateToGroup() on every area so
 * its tabs and constraints are recreated in the new solver. The new group
 * is then nudged back onto the screen if necessary.
 *
 * @param newGroup The list of areas being moved out of this group.
 */
void
SATGroup::_SpawnNewGroup(const WindowAreaList& newGroup)
{
	STRACE_SAT("SATGroup::_SpawnNewGroup\n");
	SATGroup* group = new (std::nothrow)SATGroup;
	if (group == NULL)
		return;
	BReference<SATGroup> groupRef;
	groupRef.SetTo(group, true);

	for (int i = 0; i < newGroup.CountItems(); i++)
		newGroup.ItemAt(i)->PropagateToGroup(group);

	_EnsureGroupIsOnScreen(group);
}


/** @brief Minimum on-screen overlap, in pixels, before a group is rescued. */
const float kMinOverlap = 50;
/** @brief Margin used when nudging an off-screen group back into the screen. */
const float kMoveToScreen = 75;


/**
 * @brief Moves a group back onto the screen when its windows fell off.
 *
 * Computes the closest off-screen distance for every member window in each of
 * the four directions; if at least one direction is non-trivially off-screen,
 * picks the smallest offset and applies it via Desktop::MoveWindowBy(), then
 * re-runs DoGroupLayout() so the rest of the group follows.
 *
 * @param group The group to rescue. Returns immediately if the group is
 *              empty or if any of its windows already overlaps the screen by
 *              at least kMinOverlap.
 */
void
SATGroup::_EnsureGroupIsOnScreen(SATGroup* group)
{
	STRACE_SAT("SATGroup::_EnsureGroupIsOnScreen\n");
	if (group == NULL || group->CountItems() < 1)
		return;

	SATWindow* window = group->WindowAt(0);
	Desktop* desktop = window->GetWindow()->Desktop();
	if (desktop == NULL)
		return;

	const float kBigDistance = 1E+10;

	float minLeftDistance = kBigDistance;
	BRect leftRect;
	float minTopDistance = kBigDistance;
	BRect topRect;
	float minRightDistance = kBigDistance;
	BRect rightRect;
	float minBottomDistance = kBigDistance;
	BRect bottomRect;

	BRect screen = window->GetWindow()->Screen()->Frame();
	BRect reducedScreen = screen;
	reducedScreen.InsetBy(kMinOverlap, kMinOverlap);

	for (int i = 0; i < group->CountItems(); i++) {
		SATWindow* window = group->WindowAt(i);
		BRect frame = window->CompleteWindowFrame();
		if (reducedScreen.Intersects(frame))
			return;

		if (frame.right < screen.left + kMinOverlap) {
			float dist = fabs(screen.left - frame.right);
			if (dist < minLeftDistance) {
				minLeftDistance = dist;
				leftRect = frame;
			} else if (dist == minLeftDistance)
				leftRect = leftRect | frame;
		}
		if (frame.top > screen.bottom - kMinOverlap) {
			float dist = fabs(frame.top - screen.bottom);
			if (dist < minBottomDistance) {
				minBottomDistance = dist;
				bottomRect = frame;
			} else if (dist == minBottomDistance)
				bottomRect = bottomRect | frame;
		}
		if (frame.left > screen.right - kMinOverlap) {
			float dist = fabs(frame.left - screen.right);
			if (dist < minRightDistance) {
				minRightDistance = dist;
				rightRect = frame;
			} else if (dist == minRightDistance)
				rightRect = rightRect | frame;
		}
		if (frame.bottom < screen.top + kMinOverlap) {
			float dist = fabs(frame.bottom - screen.top);
			if (dist < minTopDistance) {
				minTopDistance = dist;
				topRect = frame;
			} else if (dist == minTopDistance)
				topRect = topRect | frame;
		}
	}

	BPoint offset;
	if (minLeftDistance < kBigDistance) {
		offset.x = screen.left - leftRect.right + kMoveToScreen;
		_CallculateYOffset(offset, leftRect, screen);
	} else if (minTopDistance < kBigDistance) {
		offset.y = screen.top - topRect.bottom + kMoveToScreen;
		_CallculateXOffset(offset, topRect, screen);
	} else if (minRightDistance < kBigDistance) {
		offset.x = screen.right - rightRect.left - kMoveToScreen;
		_CallculateYOffset(offset, rightRect, screen);
	} else if (minBottomDistance < kBigDistance) {
		offset.y = screen.bottom - bottomRect.top - kMoveToScreen;
		_CallculateXOffset(offset, bottomRect, screen);
	}

	if (offset.x == 0. && offset.y == 0.)
		return;
	STRACE_SAT("move group back to screen: offset x: %f offset y: %f\n",
		offset.x, offset.y);

	desktop->MoveWindowBy(window->GetWindow(), offset.x, offset.y);
	window->DoGroupLayout();
}


/**
 * @brief Adjusts @a offset.x so @a frame ends up on-screen horizontally.
 *
 * Sets a positive offset when the frame is off the left edge and a negative
 * offset when it is off the right; leaves @a offset.x untouched otherwise.
 *
 * @param offset In/out point being constructed.
 * @param frame  Reference window frame.
 * @param screen Screen frame.
 */
void
SATGroup::_CallculateXOffset(BPoint& offset, BRect& frame, BRect& screen)
{
	if (frame.right < screen.left + kMinOverlap)
		offset.x = screen.left - frame.right + kMoveToScreen;
	else if (frame.left > screen.right - kMinOverlap)
		offset.x = screen.right - frame.left - kMoveToScreen;
}


/**
 * @brief Adjusts @a offset.y so @a frame ends up on-screen vertically.
 *
 * Mirror of _CallculateXOffset() for the vertical axis.
 *
 * @param offset In/out point being constructed.
 * @param frame  Reference window frame.
 * @param screen Screen frame.
 */
void
SATGroup::_CallculateYOffset(BPoint& offset, BRect& frame, BRect& screen)
{
	if (frame.top > screen.bottom - kMinOverlap)
		offset.y = screen.bottom - frame.top - kMoveToScreen;
	else if (frame.bottom < screen.top + kMinOverlap)
		offset.y = screen.top - frame.bottom + kMoveToScreen;
}
