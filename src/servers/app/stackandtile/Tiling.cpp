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
 *   Copyright 2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 */


/**
 * @file Tiling.cpp
 * @brief Edge-tiling snapping behaviour for Stack and Tile.
 *
 * Implements SATTiling, the snapping policy that fits a dragged window into
 * a free rectangular slot of an existing SATGroup. The algorithm walks every
 * Crossing in the group, picks the ones whose distance to the drop window's
 * frame is below kMaxMatchingDistance, then expands a rectangle until it hits
 * other windows and scores it with _FreeAreaError so the closest size match
 * wins.
 *
 * @see SATGroup, WindowArea, Crossing, SATSnappingBehaviour
 */


#include "Tiling.h"


#include "SATWindow.h"
#include "StackAndTile.h"
#include "Window.h"


using namespace std;


//#define DEBUG_TILING

#ifdef DEBUG_TILING
#	define STRACE_TILING(x...) debug_printf("SAT Tiling: "x)
#else
#	define STRACE_TILING(x...) ;
#endif


/**
 * @brief Constructs the tiling behaviour bound to one SATWindow.
 *
 * Initialises the cached free-area tabs to NULL via _ResetSearchResults().
 *
 * @param window The window this behaviour represents during a SAT drag.
 */
SATTiling::SATTiling(SATWindow* window)
	:
	fSATWindow(window),
	fFreeAreaGroup(NULL)
{
	_ResetSearchResults();
}


/** @brief Destructor; no resources owned. */
SATTiling::~SATTiling()
{

}


/**
 * @brief Looks for a free rectangular slot in @a group sized like this window.
 *
 * Resets any previous search, validates that the dragged window and the
 * target group are tileable, then asks _FindFreeAreaInGroup() to locate a
 * slot. On success the matched group is highlighted to preview the drop.
 *
 * @param group The candidate SATGroup currently being probed.
 * @return true if a viable slot was found and highlighted.
 * @note A window cannot tile into its own group; that case returns false.
 */
bool
SATTiling::FindSnappingCandidates(SATGroup* group)
{
	_ResetSearchResults();

	if (_IsTileableWindow(fSATWindow->GetWindow()) == false
		|| (group->CountItems() == 1
			&& _IsTileableWindow(group->WindowAt(0)->GetWindow()) == false))
		return false;
	if (fSATWindow->GetGroup() == group)
		return false;

	if (_FindFreeAreaInGroup(group)) {
		fFreeAreaGroup = group;
		_HighlightWindows(fFreeAreaGroup, true);
		return true;
	}

	return false;
}


/**
 * @brief Commits the pending tiling gesture by inserting the window.
 *
 * Adds fSATWindow to fFreeAreaGroup using the four matched tabs, then
 * triggers a re-layout on the first window of the new group so the solver
 * positions every member. The search state is cleared either way.
 *
 * @return true if the window was added and the layout was re-solved, false
 *         if no slot was pending or the insertion failed.
 */
bool
SATTiling::JoinCandidates()
{
	if (!fFreeAreaGroup)
		return false;

	if (!fFreeAreaGroup->AddWindow(fSATWindow, fFreeAreaLeft, fFreeAreaTop,
		fFreeAreaRight, fFreeAreaBottom)) {
		_ResetSearchResults();
		return false;
	}

	fFreeAreaGroup->WindowAt(0)->DoGroupLayout();

	_ResetSearchResults();
	return true;
}


/**
 * @brief Detaches this window from its group when the look becomes untileable.
 *
 * Removes the window from its current SATGroup if the new look (e.g. borderless)
 * disqualifies it from tiling.
 *
 * @param look The new window_look reported by the server window.
 */
void
SATTiling::WindowLookChanged(window_look look)
{
	SATGroup* group = fSATWindow->GetGroup();
	if (group == NULL)
		return;
	if (_IsTileableWindow(fSATWindow->GetWindow()) == false)
		group->RemoveWindow(fSATWindow);
}


/**
 * @brief Returns whether @a window's look permits edge-tiling.
 *
 * Accepts document, titled, floating, modal, and bordered looks; everything
 * else (including borderless) is rejected.
 *
 * @param window The candidate window.
 * @return true when the window can participate in tiling.
 */
bool
SATTiling::_IsTileableWindow(Window* window)
{
	if (window->Look() == B_DOCUMENT_WINDOW_LOOK)
		return true;
	if (window->Look() == B_TITLED_WINDOW_LOOK)
		return true;
	if (window->Look() == B_FLOATING_WINDOW_LOOK)
		return true;
	if (window->Look() == B_MODAL_WINDOW_LOOK)
		return true;
	if (window->Look() == B_BORDERED_WINDOW_LOOK)
		return true;
	return false;
}


/**
 * @brief Tries every corner orientation when searching @a group for a slot.
 *
 * Calls the four-argument overload once per Corner::position_t and returns on
 * the first success.
 *
 * @param group The candidate group.
 * @return true if any corner orientation produced a usable slot.
 */
bool
SATTiling::_FindFreeAreaInGroup(SATGroup* group)
{
	if (_FindFreeAreaInGroup(group, Corner::kLeftTop))
		return true;
	if (_FindFreeAreaInGroup(group, Corner::kRightTop))
		return true;
	if (_FindFreeAreaInGroup(group, Corner::kLeftBottom))
		return true;
	if (_FindFreeAreaInGroup(group, Corner::kRightBottom))
		return true;

	return false;
}


/**
 * @brief Finds a free area aligned to one specific corner of the drop window.
 *
 * Iterates over the group's vertical tabs and their crossings, asking
 * _InteresstingCrossing() to filter candidates by distance, then
 * _FindFreeArea() to grow a rectangle from each surviving crossing.
 *
 * @param group The candidate group.
 * @param cor   Which corner of the dragged window is being matched at the
 *              crossing.
 * @return true if a slot was found and the fFreeAreaXxx tabs were set.
 */
bool
SATTiling::_FindFreeAreaInGroup(SATGroup* group, Corner::position_t cor)
{
	BRect windowFrame = fSATWindow->CompleteWindowFrame();

	const TabList* verticalTabs = group->VerticalTabs();
	for (int i = 0; i < verticalTabs->CountItems(); i++) {
		Tab* tab = verticalTabs->ItemAt(i);
		const CrossingList* crossingList = tab->GetCrossingList();
		for (int c = 0; c < crossingList->CountItems(); c++) {
			Crossing* crossing = crossingList->ItemAt(c);
			if (_InteresstingCrossing(crossing, cor, windowFrame)) {
				if (_FindFreeArea(group, crossing, cor, windowFrame)) {
					STRACE_TILING("SATTiling: free area found; corner %i\n",
						cor);
					return true;
				}
			}
		}
	}

	return false;
}


/** @brief Sentinel returned by distance heuristics when no match is possible. */
const float kNoMatch = 999.f;
/** @brief Maximum tolerated distance, in pixels, between window edge and tab. */
const float kMaxMatchingDistance = 12.f;


/**
 * @brief Tests whether @a crossing is a viable anchor for the given corner.
 *
 * Rejects crossings whose opposite corner is already used; otherwise measures
 * how far the matched window edge sits from the crossing's tabs and how far
 * the orthogonal coordinate is from the corresponding window border. A
 * crossing is interesting when at least one direction lies within
 * kMaxMatchingDistance and is bordered by an existing window.
 *
 * @param crossing    The crossing being evaluated.
 * @param cor         Which corner of the drop window is being matched.
 * @param windowFrame The drop window's full frame including decorator.
 * @return true if the crossing is close enough to align the drop window.
 */
bool
SATTiling::_InteresstingCrossing(Crossing* crossing,
	Corner::position_t cor, BRect& windowFrame)
{
	const Corner* corner = crossing->GetOppositeCorner(cor);
	if (corner->status != Corner::kFree)
		return false;

	float hTabPosition = crossing->HorizontalTab()->Position();
	float vTabPosition = crossing->VerticalTab()->Position();
	float hBorder = 0., vBorder = 0.;
	float vDistance = -1., hDistance = -1.;
	bool windowAtH = false, windowAtV = false;
	switch (cor) {
		case Corner::kLeftTop:
			if (crossing->RightBottomCorner()->status == Corner::kUsed)
				return false;
			vBorder = windowFrame.left;
			hBorder = windowFrame.top;
			if (crossing->LeftBottomCorner()->status == Corner::kUsed)
				windowAtV = true;
			if (crossing->RightTopCorner()->status == Corner::kUsed)
				windowAtH = true;
			vDistance = vTabPosition - vBorder;
			hDistance = hTabPosition - hBorder;
			break;
		case Corner::kRightTop:
			if (crossing->LeftBottomCorner()->status == Corner::kUsed)
				return false;
			vBorder = windowFrame.right;
			hBorder = windowFrame.top;
			if (crossing->RightBottomCorner()->status == Corner::kUsed)
				windowAtV = true;
			if (crossing->LeftTopCorner()->status == Corner::kUsed)
				windowAtH = true;
			vDistance = vBorder - vTabPosition;
			hDistance = hTabPosition - hBorder;
			break;
		case Corner::kLeftBottom:
			if (crossing->RightTopCorner()->status == Corner::kUsed)
				return false;
			vBorder = windowFrame.left;
			hBorder = windowFrame.bottom;
			if (crossing->LeftTopCorner()->status == Corner::kUsed)
				windowAtV = true;
			if (crossing->RightBottomCorner()->status == Corner::kUsed)
				windowAtH = true;
			vDistance = vTabPosition - vBorder;
			hDistance = hBorder - hTabPosition;
			break;
		case Corner::kRightBottom:
			if (crossing->LeftTopCorner()->status == Corner::kUsed)
				return false;
			vBorder = windowFrame.right;
			hBorder = windowFrame.bottom;
			if (crossing->RightTopCorner()->status == Corner::kUsed)
				windowAtV = true;
			if (crossing->LeftBottomCorner()->status == Corner::kUsed)
				windowAtH = true;
			vDistance = vBorder - vTabPosition;
			hDistance = hBorder - hTabPosition;
			break;
	};

	bool hValid = false;
	if (windowAtH && fabs(hDistance) < kMaxMatchingDistance
		&& vDistance  < kMaxMatchingDistance)
		hValid = true;
	bool vValid = false;
	if (windowAtV && fabs(vDistance) < kMaxMatchingDistance
		&& hDistance  < kMaxMatchingDistance)
		vValid = true;
	if (!hValid && !vValid)
		return false;

	return true;
};


/** @brief Score returned for slots that fail validation; effectively infinity. */
const float kBigAreaError = 1E+17;


/**
 * @brief Grows a candidate rectangle from @a crossing and picks the best fit.
 *
 * Starting from the anchor corner described by @a corner, walks outward along
 * the orthogonal tab lists, calling _CheckArea() for each (h, v) end-tab pair.
 * The combination with the smallest _FreeAreaError() (closest to the drop
 * window's size) is stored in fFreeAreaLeft/Right/Top/Bottom.
 *
 * @param group       The group being searched.
 * @param crossing    Anchor crossing produced by _InteresstingCrossing().
 * @param corner      Which corner of the drop window the anchor matches.
 * @param windowFrame The drop window's full frame.
 * @return true if any valid slot was found.
 */
bool
SATTiling::_FindFreeArea(SATGroup* group, const Crossing* crossing,
	Corner::position_t corner, BRect& windowFrame)
{
	fFreeAreaLeft = fFreeAreaRight = fFreeAreaTop = fFreeAreaBottom = NULL;

	const TabList* hTabs = group->HorizontalTabs();
	const TabList* vTabs = group->VerticalTabs();
	int32 hIndex = hTabs->IndexOf(crossing->HorizontalTab());
	if (hIndex < 0)
		return false;
	int32 vIndex = vTabs->IndexOf(crossing->VerticalTab());
	if (vIndex < 0)
		return false;

	Tab** endHTab = NULL, **endVTab = NULL;
	int8 vSearchDirection = 1, hSearchDirection = 1;
	switch (corner) {
		case Corner::kLeftTop:
			fFreeAreaLeft = crossing->VerticalTab();
			fFreeAreaTop = crossing->HorizontalTab();
			endHTab = &fFreeAreaBottom;
			endVTab = &fFreeAreaRight;
			vSearchDirection = 1;
			hSearchDirection = 1;
			break;
		case Corner::kRightTop:
			fFreeAreaRight = crossing->VerticalTab();
			fFreeAreaTop = crossing->HorizontalTab();
			endHTab = &fFreeAreaBottom;
			endVTab = &fFreeAreaLeft;
			vSearchDirection = -1;
			hSearchDirection = 1;
			break;
		case Corner::kLeftBottom:
			fFreeAreaLeft = crossing->VerticalTab();
			fFreeAreaBottom = crossing->HorizontalTab();
			endHTab = &fFreeAreaTop;
			endVTab = &fFreeAreaRight;
			vSearchDirection = 1;
			hSearchDirection = -1;
			break;
		case Corner::kRightBottom:
			fFreeAreaRight = crossing->VerticalTab();
			fFreeAreaBottom = crossing->HorizontalTab();
			endHTab = &fFreeAreaTop;
			endVTab = &fFreeAreaLeft;
			vSearchDirection = -1;
			hSearchDirection = -1;
			break;
	};

	Tab* bestLeftTab = NULL, *bestRightTab = NULL, *bestTopTab = NULL,
		*bestBottomTab = NULL;
	float bestError = kBigAreaError;
	float error;
	bool stop = false;
	bool found = false;
	int32 v = vIndex;
	do {
		v += vSearchDirection;
		*endVTab = vTabs->ItemAt(v);
		int32 h = hIndex;
		do {
			h += hSearchDirection;
			*endHTab = hTabs->ItemAt(h);
			if (!_CheckArea(group, corner, windowFrame, error)) {
				if (h == hIndex + hSearchDirection)
					stop = true;
				break;
			}
			found = true;
			if (error < bestError) {
				bestError = error;
				bestLeftTab = fFreeAreaLeft;
				bestRightTab = fFreeAreaRight;
				bestTopTab = fFreeAreaTop;
				bestBottomTab = fFreeAreaBottom;
			}
		} while (*endHTab);
		if (stop)
			break;
	} while (*endVTab);
	if (!found)
		return false;

	fFreeAreaLeft = bestLeftTab;
	fFreeAreaRight = bestRightTab;
	fFreeAreaTop = bestTopTab;
	fFreeAreaBottom = bestBottomTab;

	return true;
}


/**
 * @brief Returns whether any existing window overlaps the candidate rectangle.
 *
 * Walks the group's horizontal tabs and their crossings; for every used
 * upper-left corner, intersects the owning WindowArea's frame with the slot
 * (inset by 1 px) to allow shared borders.
 *
 * @param group The group whose windows are tested.
 * @return true if at least one window overlaps the slot interior.
 */
bool
SATTiling::_HasOverlapp(SATGroup* group)
{
	BRect areaRect = _FreeAreaSize();
	areaRect.InsetBy(1., 1.);

	const TabList* hTabs = group->HorizontalTabs();
	for (int32 h = 0; h < hTabs->CountItems(); h++) {
		Tab* hTab = hTabs->ItemAt(h);
		if (hTab->Position() >= areaRect.bottom)
			return false;
		const CrossingList* crossings = hTab->GetCrossingList();
		for (int32 i = 0; i <  crossings->CountItems(); i++) {
			Crossing* leftTopCrossing = crossings->ItemAt(i);
			Tab* vTab = leftTopCrossing->VerticalTab();
			if (vTab->Position() > areaRect.right)
				continue;
			Corner* corner = leftTopCrossing->RightBottomCorner();
			if (corner->status != Corner::kUsed)
				continue;
			BRect rect = corner->windowArea->Frame();
			if (areaRect.Intersects(rect))
				return true;
		}
	}
	return false;
}


/**
 * @brief Validates the current candidate rectangle and scores it.
 *
 * Ensures the slot has a minimum size, contains the anchor corner, and does
 * not overlap any existing window. On success @a error receives the squared
 * deviation between the slot's dimensions and the drop window's frame.
 *
 * @param group       The group being searched.
 * @param corner      The anchor corner of the drop window.
 * @param windowFrame The drop window's full frame.
 * @param error       Output score; lower is better. Set to kBigAreaError on
 *                    failure.
 * @return true if the slot is acceptable.
 */
bool
SATTiling::_CheckArea(SATGroup* group, Corner::position_t corner,
	BRect& windowFrame, float& error)
{
	error = kBigAreaError;
	if (!_CheckMinFreeAreaSize())
		return false;
	// check if corner is in the free area
	if (!_IsCornerInFreeArea(corner, windowFrame))
		return false;

	error = _FreeAreaError(windowFrame);
	if (!_HasOverlapp(group))
		return true;
	return false;
}


/**
 * @brief Returns whether the candidate rectangle is large enough to be useful.
 *
 * Fully bounded slots (both opposing tabs set) must be at least
 * 2 * kMaxMatchingDistance wide / tall in the constrained direction.
 *
 * @return true if the slot satisfies the minimum size in every constrained
 *         direction.
 */
bool
SATTiling::_CheckMinFreeAreaSize()
{
	// check if the area has a minimum size
	if (fFreeAreaLeft && fFreeAreaRight
		&& fFreeAreaRight->Position() - fFreeAreaLeft->Position()
			< 2 * kMaxMatchingDistance)
		return false;
	if (fFreeAreaBottom && fFreeAreaTop
		&& fFreeAreaBottom->Position() - fFreeAreaTop->Position()
			< 2 * kMaxMatchingDistance)
		return false;
	return true;
}


/**
 * @brief Squared size mismatch between the slot and @a windowFrame.
 *
 * Each unbounded direction (no opposing tab) contributes a large penalty so
 * the search prefers fully bounded slots.
 *
 * @param windowFrame The drop window's full frame.
 * @return Sum of squared deltas in the bounded directions plus penalties.
 */
float
SATTiling::_FreeAreaError(BRect& windowFrame)
{
	const float kEndTabError = 9999999;
	float error = 0.;
	if (fFreeAreaLeft && fFreeAreaRight)
		error += pow(fFreeAreaRight->Position() - fFreeAreaLeft->Position()
			- windowFrame.Width(), 2);
	else
		error += kEndTabError;
	if (fFreeAreaBottom && fFreeAreaTop)
		error += pow(fFreeAreaBottom->Position() - fFreeAreaTop->Position()
			- windowFrame.Height(), 2);
	else
		error += kEndTabError;
	return error;
}


/**
 * @brief Confirms that @a corner of @a frame lies inside the candidate slot.
 *
 * Checks that the opposite edges of the slot lie strictly beyond the
 * matched corner by at least kMaxMatchingDistance, ensuring the drop window
 * actually fits past the anchor.
 *
 * @param corner The anchor corner of the drop window.
 * @param frame  The drop window's full frame.
 * @return true if the corner sits inside the slot with enough slack.
 */
bool
SATTiling::_IsCornerInFreeArea(Corner::position_t corner, BRect& frame)
{
	BRect freeArea = _FreeAreaSize();

	switch (corner) {
		case Corner::kLeftTop:
			if (freeArea.bottom - kMaxMatchingDistance > frame.top
				&& freeArea.right - kMaxMatchingDistance > frame.left)
				return true;
			break;
		case Corner::kRightTop:
			if (freeArea.bottom - kMaxMatchingDistance > frame.top
				&& freeArea.left + kMaxMatchingDistance < frame.right)
				return true;
			break;
		case Corner::kLeftBottom:
			if (freeArea.top + kMaxMatchingDistance < frame.bottom
				&& freeArea.right - kMaxMatchingDistance > frame.left)
				return true;
			break;
		case Corner::kRightBottom:
			if (freeArea.top + kMaxMatchingDistance < frame.bottom
				&& freeArea.left + kMaxMatchingDistance < frame.right)
				return true;
			break;
	}

	return false;
}


/**
 * @brief Returns the rectangle described by the four cached free-area tabs.
 *
 * For each side that has no tab assigned, substitutes a very large finite
 * sentinel (positive or negative) so the rectangle remains valid for
 * intersection tests.
 *
 * @return The slot rectangle in group coordinates.
 */
BRect
SATTiling::_FreeAreaSize()
{
	// not to big to be be able to add/sub small float values
	const float kBigValue = 9999999.;
	float left = fFreeAreaLeft ? fFreeAreaLeft->Position() : -kBigValue;
	float right = fFreeAreaRight ? fFreeAreaRight->Position() : kBigValue;
	float top = fFreeAreaTop ? fFreeAreaTop->Position() : -kBigValue;
	float bottom = fFreeAreaBottom ? fFreeAreaBottom->Position() : kBigValue;
	return BRect(left, top, right, bottom);
}


/**
 * @brief Highlights every neighbour bordering the candidate slot.
 *
 * Searches each of the four sides for windows that share a tab with the slot
 * and asks _SearchHighlightWindow() to highlight their facing border. If at
 * least one neighbour was found per side, the dragged window's matching
 * border is highlighted too so the user sees both halves of the join.
 *
 * @param group     The candidate group.
 * @param highlight true to draw the highlight, false to remove it.
 */
void
SATTiling::_HighlightWindows(SATGroup* group, bool highlight)
{
	const TabList* hTabs = group->HorizontalTabs();
	const TabList* vTabs = group->VerticalTabs();
	// height light windows at all four sites
	bool leftWindowsFound = _SearchHighlightWindow(fFreeAreaLeft, fFreeAreaTop, fFreeAreaBottom, hTabs,
		fFreeAreaTop ? Corner::kLeftBottom : Corner::kLeftTop,
		Decorator::REGION_RIGHT_BORDER, highlight);

	bool topWindowsFound = _SearchHighlightWindow(fFreeAreaTop, fFreeAreaLeft, fFreeAreaRight, vTabs,
		fFreeAreaLeft ? Corner::kRightTop : Corner::kLeftTop,
		Decorator::REGION_BOTTOM_BORDER, highlight);

	bool rightWindowsFound = _SearchHighlightWindow(fFreeAreaRight, fFreeAreaTop, fFreeAreaBottom, hTabs,
		fFreeAreaTop ? Corner::kRightBottom : Corner::kRightTop,
		Decorator::REGION_LEFT_BORDER, highlight);

	bool bottomWindowsFound = _SearchHighlightWindow(fFreeAreaBottom, fFreeAreaLeft, fFreeAreaRight,
		vTabs, fFreeAreaLeft ? Corner::kRightBottom : Corner::kLeftBottom,
		Decorator::REGION_TOP_BORDER, highlight);

	if (leftWindowsFound)
		fSATWindow->HighlightBorders(Decorator::REGION_LEFT_BORDER, highlight);
	if (topWindowsFound)
		fSATWindow->HighlightBorders(Decorator::REGION_TOP_BORDER, highlight);
	if (rightWindowsFound)
		fSATWindow->HighlightBorders(Decorator::REGION_RIGHT_BORDER, highlight);
	if (bottomWindowsFound) {
		fSATWindow->HighlightBorders(Decorator::REGION_BOTTOM_BORDER,
			highlight);
	}
}


/**
 * @brief Walks one slot edge and highlights every WindowArea that touches it.
 *
 * Iterates between the two orthogonal tabs that bound the edge, looks up each
 * crossing on @a tab, and if its @a areaCorner refers to a real WindowArea
 * highlights that area's facing @a region.
 *
 * @param tab           Edge tab of the slot, or NULL for an unbounded edge.
 * @param firstOrthTab  Tab at one end of the search interval, or NULL.
 * @param secondOrthTab Tab at the other end, or NULL.
 * @param orthTabs      Sorted list of orthogonal tabs in the group.
 * @param areaCorner    Which corner of the crossing identifies the neighbour.
 * @param region        Decorator region to highlight on each neighbour.
 * @param highlight     true to highlight, false to clear.
 * @return true if at least one neighbour was highlighted along the edge.
 */
bool
SATTiling::_SearchHighlightWindow(Tab* tab, Tab* firstOrthTab,
	Tab* secondOrthTab, const TabList* orthTabs, Corner::position_t areaCorner,
	Decorator::Region region, bool highlight)
{
	bool windowsFound = false;

	if (!tab)
		return false;

	int8 searchDir = 1;
	Tab* startOrthTab = NULL;
	Tab* endOrthTab = NULL;
	if (firstOrthTab) {
		searchDir = 1;
		startOrthTab = firstOrthTab;
		endOrthTab = secondOrthTab;
	}
	else if (secondOrthTab) {
		searchDir = -1;
		startOrthTab = secondOrthTab;
		endOrthTab = firstOrthTab;
	}
	else
		return false;

	int32 index = orthTabs->IndexOf(startOrthTab);
	if (index < 0)
		return false;

	for (; index < orthTabs->CountItems() && index >= 0; index += searchDir) {
		Tab* orthTab = orthTabs->ItemAt(index);
		if (orthTab == endOrthTab)
 			break;
		Crossing* crossing = tab->FindCrossing(orthTab);
		if (!crossing)
			continue;
		Corner* corner = crossing->GetCorner(areaCorner);
		if (corner->windowArea) {
			_HighlightWindows(corner->windowArea, region,  highlight);
			windowsFound = true;
		}
	}
	return windowsFound;
}


/**
 * @brief Highlights the topmost window of @a area on the requested side.
 *
 * @param area      Neighbour WindowArea sharing an edge with the slot.
 * @param region    Decorator region to highlight on the top window.
 * @param highlight true to highlight, false to clear.
 */
void
SATTiling::_HighlightWindows(WindowArea* area, Decorator::Region region,
	bool highlight)
{
	const SATWindowList& list = area->LayerOrder();
	SATWindow* topWindow = list.ItemAt(list.CountItems() - 1);
	if (topWindow == NULL)
		return;
	topWindow->HighlightBorders(region, highlight);
}


/**
 * @brief Drops any pending tiling highlight and clears the slot tabs.
 *
 * Safe to call repeatedly; if no group is currently highlighted the call is
 * a no-op.
 */
void
SATTiling::_ResetSearchResults()
{
	if (!fFreeAreaGroup)
		return;

	_HighlightWindows(fFreeAreaGroup, false);
	fFreeAreaGroup = NULL;
}
