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
 * @file StackAndTile.cpp
 * @brief DesktopListener that implements the Stack and Tile feature.
 *
 * StackAndTile observes desktop-wide events (window add/remove, modifier-key
 * presses, mouse and key shortcuts, move/resize/look/feel changes, settings)
 * and dispatches them to per-window SATWindow wrappers and their groups.
 * Holding the right Option key (or the Option modifier) starts a snapping
 * session; releasing it commits the gesture or aborts. Arrow / Tab / PgUp /
 * PgDn shortcuts navigate between window tabs and groups while the modifier
 * is held. The class also exposes the SAT private message protocol for
 * archiving and restoring groups.
 *
 * @see DesktopListener, SATWindow, SATGroup
 */


#include "StackAndTile.h"

#include <Debug.h>

#include "StackAndTilePrivate.h"

#include "Desktop.h"
#include "SATWindow.h"
#include "Tiling.h"
#include "Window.h"


/** @brief Hardware key code of the right Option key, the bare-press SAT trigger. */
static const int32 kRightOptionKey	= 0x67;
/** @brief Hardware key code of Tab, used while SAT is active to walk a stack. */
static const int32 kTabKey			= 0x26;
/** @brief Hardware key code of PageUp, used to walk to the previous group. */
static const int32 kPageUpKey		= 0x21;
/** @brief Hardware key code of PageDown, used to walk to the next group. */
static const int32 kPageDownKey		= 0x36;
/** @brief Hardware key code of LeftArrow, walks to the previous tab in a stack. */
static const int32 kLeftArrowKey	= 0x61;
/** @brief Hardware key code of UpArrow, alias of PageUp for previous-group nav. */
static const int32 kUpArrowKey		= 0x57;
/** @brief Hardware key code of RightArrow, walks to the next tab in a stack. */
static const int32 kRightArrowKey	= 0x63;
/** @brief Hardware key code of DownArrow, alias of PageDown for next-group nav. */
static const int32 kDownArrowKey	= 0x62;

/** @brief Mask of modifier bits inspected when detecting the Option-only state. */
static const int32 kModifiers = B_SHIFT_KEY | B_COMMAND_KEY
	| B_CONTROL_KEY | B_OPTION_KEY | B_MENU_KEY;


using namespace std;


//	#pragma mark - StackAndTile


/**
 * @brief Constructs an unattached StackAndTile listener.
 *
 * The listener becomes useful only after Desktop calls ListenerRegistered()
 * which records the desktop pointer and walks the existing window list.
 */
StackAndTile::StackAndTile()
	:
	fDesktop(NULL),
	fSATKeyPressed(false),
	fCurrentSATWindow(NULL)
{

}


/** @brief Destructor; SAT windows are released by ListenerUnregistered(). */
StackAndTile::~StackAndTile()
{

}


/**
 * @brief Returns the magic identifier the desktop uses to find this listener.
 *
 * @return The kMagicSATIdentifier constant defined in StackAndTilePrivate.h.
 */
int32
StackAndTile::Identifier()
{
	return BPrivate::kMagicSATIdentifier;
}


/**
 * @brief DesktopListener hook called when the listener becomes active.
 *
 * Records the owning desktop and creates a SATWindow wrapper for every
 * window that already exists, so the listener starts in a consistent state.
 *
 * @param desktop The desktop hosting this listener.
 */
void
StackAndTile::ListenerRegistered(Desktop* desktop)
{
	fDesktop = desktop;

	WindowList& windows = desktop->AllWindows();
	for (Window *window = windows.FirstWindow(); window != NULL;
			window = window->NextWindow(kAllWindowList))
		WindowAdded(window);
}


/**
 * @brief DesktopListener hook called when the listener is being torn down.
 *
 * Deletes every SATWindow in the map and clears it so subsequent lookups
 * return NULL even if the desktop reuses the listener.
 */
void
StackAndTile::ListenerUnregistered()
{
	for (SATWindowMap::iterator it = fSATWindowMap.begin();
		it != fSATWindowMap.end(); it++) {
		SATWindow* satWindow = it->second;
		delete satWindow;
	}
	fSATWindowMap.clear();
}


/**
 * @brief Routes a SAT private-protocol message to the right handler.
 *
 * If @a sender is NULL the message is global (e.g. archive/restore all
 * groups) and is handled by _HandleMessage(); otherwise the corresponding
 * SATWindow is asked to handle it.
 *
 * @param sender Originating window, or NULL for global messages.
 * @param link   Reader supplying the payload.
 * @param reply  Writer for the response.
 * @return true if the message was understood and a reply was sent.
 */
bool
StackAndTile::HandleMessage(Window* sender, BPrivate::LinkReceiver& link,
	BPrivate::LinkSender& reply)
{
	if (sender == NULL)
		return _HandleMessage(link, reply);

	SATWindow* satWindow = GetSATWindow(sender);
	if (!satWindow)
		return false;

	return satWindow->HandleMessage(satWindow, link, reply);
}


/**
 * @brief DesktopListener hook called when a new server window appears.
 *
 * Allocates a SATWindow wrapper and stores it in the lookup map. Allocation
 * failures are silently ignored; the window simply becomes invisible to SAT.
 *
 * @param window The new server window.
 */
void
StackAndTile::WindowAdded(Window* window)
{
	SATWindow* satWindow = new (std::nothrow)SATWindow(this, window);
	if (!satWindow)
		return;

	ASSERT(fSATWindowMap.find(window) == fSATWindowMap.end());
	fSATWindowMap[window] = satWindow;
}


/**
 * @brief DesktopListener hook called when a server window is being removed.
 *
 * Deletes the matching SATWindow (which detaches it from its group) and
 * erases the map entry.
 *
 * @param window The window being removed.
 */
void
StackAndTile::WindowRemoved(Window* window)
{
	STRACE_SAT("StackAndTile::WindowRemoved %s\n", window->Title());

	SATWindowMap::iterator it = fSATWindowMap.find(window);
	if (it == fSATWindowMap.end())
		return;

	SATWindow* satWindow = it->second;
	// delete SATWindow
	delete satWindow;
	fSATWindowMap.erase(it);
}


/**
 * @brief Filters key events to drive SAT modifier state and shortcuts.
 *
 * Two responsibilities:
 *  1. Track whether the user is holding the SAT modifier (right Option, or
 *     the Option-only modifier mask) and start/stop snapping accordingly.
 *  2. While SAT is active, interpret arrow / Tab / PgUp / PgDn as group and
 *     stack navigation (cycle tabs, jump to previous/next group).
 *
 * @param what      Event kind (B_MODIFIERS_CHANGED, B_KEY_DOWN, ...).
 * @param key       Hardware key code.
 * @param modifiers Current modifier bitmask.
 * @return true if the event was consumed (caller should not propagate it).
 */
bool
StackAndTile::KeyPressed(uint32 what, int32 key, int32 modifiers)
{
	if (what == B_MODIFIERS_CHANGED
		|| (what == B_UNMAPPED_KEY_DOWN && key == kRightOptionKey)
		|| (what == B_UNMAPPED_KEY_UP && key == kRightOptionKey)) {
		// switch to and from stacking and snapping mode
		bool wasPressed = fSATKeyPressed;
		fSATKeyPressed = (what == B_MODIFIERS_CHANGED
				&& (modifiers & kModifiers) == B_OPTION_KEY)
			|| (what == B_UNMAPPED_KEY_DOWN && key == kRightOptionKey);
		if (wasPressed && !fSATKeyPressed)
			_StopSAT();
		if (!wasPressed && fSATKeyPressed)
			_StartSAT();
	}

	if (!SATKeyPressed() || what != B_KEY_DOWN)
		return false;

	SATWindow* frontWindow = GetSATWindow(fDesktop->FocusWindow());
	SATGroup* currentGroup = _GetSATGroup(frontWindow);

	switch (key) {
		case kLeftArrowKey:
		case kRightArrowKey:
		case kTabKey:
		{
			// go to previous or next window tab in current window group
			if (currentGroup == NULL)
				return false;

			int32 groupSize = currentGroup->CountItems();
			if (groupSize <= 1)
				return false;

			for (int32 i = 0; i < groupSize; i++) {
				SATWindow* targetWindow = currentGroup->WindowAt(i);
				if (targetWindow == frontWindow) {
					if (key == kLeftArrowKey
						|| (key == kTabKey && (modifiers & B_SHIFT_KEY) != 0)) {
						// Go to previous window tab (wrap around)
						int32 previousIndex = i > 0 ? i - 1 : groupSize - 1;
						targetWindow = currentGroup->WindowAt(previousIndex);
					} else {
						// Go to next window tab (wrap around)
						int32 nextIndex = i < groupSize - 1 ? i + 1 : 0;
						targetWindow = currentGroup->WindowAt(nextIndex);
					}

					_ActivateWindow(targetWindow);
					return true;
				}
			}
			break;
		}

		case kUpArrowKey:
		case kPageUpKey:
		{
			// go to previous window group
			GroupIterator groups(this, fDesktop);
			groups.SetCurrentGroup(currentGroup);
			SATGroup* backmostGroup = NULL;

			while (true) {
				SATGroup* group = groups.NextGroup();
				if (group == NULL || group == currentGroup)
					break;
				else if (group->CountItems() < 1)
					continue;

				if (currentGroup == NULL) {
					SATWindow* activeWindow = group->ActiveWindow();
					if (activeWindow != NULL)
						_ActivateWindow(activeWindow);
					else
						_ActivateWindow(group->WindowAt(0));

					return true;
				}
				backmostGroup = group;
			}
			if (backmostGroup != NULL && backmostGroup != currentGroup) {
				SATWindow* activeWindow = backmostGroup->ActiveWindow();
				if (activeWindow != NULL)
					_ActivateWindow(activeWindow);
				else
					_ActivateWindow(backmostGroup->WindowAt(0));

				return true;
			}

			break;
		}

		case kDownArrowKey:
		case kPageDownKey:
		{
			// go to next window group
			GroupIterator groups(this, fDesktop);
			groups.SetCurrentGroup(currentGroup);

			while (true) {
				SATGroup* group = groups.NextGroup();
				if (group == NULL || group == currentGroup)
					break;
				else if (group->CountItems() < 1)
					continue;

				SATWindow* activeWindow = group->ActiveWindow();
				if (activeWindow != NULL)
					_ActivateWindow(activeWindow);
				else
					_ActivateWindow(group->WindowAt(0));

				if (currentGroup != NULL && frontWindow != NULL) {
					Window* window = frontWindow->GetWindow();
					fDesktop->SendWindowBehind(window);
					WindowSentBehind(window, NULL);
				}
				return true;
			}
			break;
		}
	}

	return false;
}


/**
 * @brief DesktopListener hook called on every primary mouse-button press.
 *
 * Records the click target if it lands on a draggable decorator region (tab,
 * border, corner) and, when the SAT modifier is already held, triggers the
 * search for snapping candidates.
 *
 * @param window  Window under the cursor (may be unrelated to fCurrentSATWindow).
 * @param message Original mouse message.
 * @param where   Click point in screen coordinates.
 * @note Double clicks and secondary buttons are ignored, as is any click that
 *       arrives while another button is already being tracked.
 */
void
StackAndTile::MouseDown(Window* window, BMessage* message, const BPoint& where)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (!satWindow || !satWindow->GetDecorator())
		return;

	// fCurrentSATWindow is not zero if e.g. the secondary and the primary
	// mouse button are pressed at the same time
	if ((message->FindInt32("buttons") & B_PRIMARY_MOUSE_BUTTON) == 0 ||
		fCurrentSATWindow != NULL)
		return;

	// we are only interested in single clicks
	if (message->FindInt32("clicks") == 2)
		return;

	int32 tab;
	switch (satWindow->GetDecorator()->RegionAt(where, tab)) {
		case Decorator::REGION_TAB:
		case Decorator::REGION_LEFT_BORDER:
		case Decorator::REGION_RIGHT_BORDER:
		case Decorator::REGION_TOP_BORDER:
		case Decorator::REGION_BOTTOM_BORDER:
		case Decorator::REGION_LEFT_TOP_CORNER:
		case Decorator::REGION_LEFT_BOTTOM_CORNER:
		case Decorator::REGION_RIGHT_TOP_CORNER:
		case Decorator::REGION_RIGHT_BOTTOM_CORNER:
			break;

		default:
			return;
	}

	ASSERT(fCurrentSATWindow == NULL);
	fCurrentSATWindow = satWindow;

	if (!SATKeyPressed())
		return;

	_StartSAT();
}


/**
 * @brief DesktopListener hook called on mouse-button release.
 *
 * Commits any in-progress snapping gesture (when the SAT modifier is still
 * held the StopSAT logic still runs because the modifier may also be
 * released next) and clears the tracked window.
 *
 * @param window  Window the release was delivered to.
 * @param message Original mouse message.
 * @param where   Release point in screen coordinates.
 */
void
StackAndTile::MouseUp(Window* window, BMessage* message, const BPoint& where)
{
	if (fSATKeyPressed)
		_StopSAT();

	fCurrentSATWindow = NULL;
}


/**
 * @brief DesktopListener hook called whenever a window is moved.
 *
 * While SAT is active and the moved window is the one being dragged, looks
 * for snapping candidates so the highlight follows the cursor; otherwise
 * just re-solves the window's group layout.
 *
 * @param window The window that just moved.
 */
void
StackAndTile::WindowMoved(Window* window)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;

	if (SATKeyPressed() && fCurrentSATWindow)
		satWindow->FindSnappingCandidates();
	else
		satWindow->DoGroupLayout();
}


/**
 * @brief DesktopListener hook called whenever a window is resized.
 *
 * Mirrors WindowMoved() but also informs SATWindow::Resized() so the size
 * cache stays current with the externally-applied dimensions.
 *
 * @param window The window that just resized.
 */
void
StackAndTile::WindowResized(Window* window)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;
	satWindow->Resized();

	if (SATKeyPressed() && fCurrentSATWindow)
		satWindow->FindSnappingCandidates();
	else
		satWindow->DoGroupLayout();
}


/**
 * @brief DesktopListener hook called when a window becomes the focus window.
 *
 * Promotes the window's WindowArea to the top layer of its group and
 * activates the top window of every other area so the entire group surfaces
 * together.
 *
 * @param window The newly-active window.
 */
void
StackAndTile::WindowActivated(Window* window)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;

	_ActivateWindow(satWindow);
}


/**
 * @brief DesktopListener hook called when a window is sent behind another.
 *
 * Sends the top window of every other area in the same group behind
 * @a behindOf as well, so the cluster keeps its z-order coherent.
 *
 * @param window   The window being sent behind.
 * @param behindOf The window @a window should sit behind.
 */
void
StackAndTile::WindowSentBehind(Window* window, Window* behindOf)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;

	SATGroup* group = satWindow->GetGroup();
	if (group == NULL)
		return;

	Desktop* desktop = satWindow->GetWindow()->Desktop();
	if (desktop == NULL)
		return;

	const WindowAreaList& areaList = group->GetAreaList();
	for (int32 i = 0; i < areaList.CountItems(); i++) {
		WindowArea* area = areaList.ItemAt(i);
		SATWindow* topWindow = area->TopWindow();
		if (topWindow == NULL || topWindow == satWindow)
			continue;
		desktop->SendWindowBehind(topWindow->GetWindow(), behindOf);
	}
}


/**
 * @brief DesktopListener hook propagating workspace changes across a group.
 *
 * Whenever a member of a group moves to a different set of workspaces, copy
 * @a workspaces onto the top window of every other area so the whole group
 * shows up together on the new workspaces.
 *
 * @param window     The window whose workspace mask changed.
 * @param workspaces New workspace bitmask.
 */
void
StackAndTile::WindowWorkspacesChanged(Window* window, uint32 workspaces)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;

	SATGroup* group = satWindow->GetGroup();
	if (group == NULL)
		return;

	Desktop* desktop = satWindow->GetWindow()->Desktop();
	if (desktop == NULL)
		return;

	const WindowAreaList& areaList = group->GetAreaList();
	for (int32 i = 0; i < areaList.CountItems(); i++) {
		WindowArea* area = areaList.ItemAt(i);
		if (area->WindowList().HasItem(satWindow))
			continue;
		SATWindow* topWindow = area->TopWindow();
		desktop->SetWindowWorkspaces(topWindow->GetWindow(), workspaces);
	}
}


/**
 * @brief DesktopListener hook called when a window is hidden.
 *
 * Hidden-other-than-minimised windows are removed from their group so they
 * do not constrain the layout of the visible members.
 *
 * @param window       The window being hidden.
 * @param fromMinimize true if the hide was the result of minimisation.
 */
void
StackAndTile::WindowHidden(Window* window, bool fromMinimize)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;

	SATGroup* group = satWindow->GetGroup();
	if (group == NULL)
		return;

	if (fromMinimize == false && group->CountItems() > 1)
		group->RemoveWindow(satWindow, false);
}


/**
 * @brief DesktopListener hook propagating minimize state across a group.
 *
 * Notifies every other window in the group that it should follow @a minimize
 * so a tiled cluster minimises and restores as a whole.
 *
 * @param window   The window being (un)minimised.
 * @param minimize true to minimise, false to restore.
 */
void
StackAndTile::WindowMinimized(Window* window, bool minimize)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;

	SATGroup* group = satWindow->GetGroup();
	if (group == NULL)
		return;

	Desktop* desktop = satWindow->GetWindow()->Desktop();
	if (desktop == NULL)
		return;

	for (int i = 0; i < group->CountItems(); i++) {
		SATWindow* listWindow = group->WindowAt(i);
		if (listWindow != satWindow)
			listWindow->GetWindow()->ServerWindow()->NotifyMinimize(minimize);
	}
}


/**
 * @brief DesktopListener hook called when a tab is dragged along the title.
 *
 * Reserved for future use; currently a no-op.
 *
 * @param window     The window whose tab changed location.
 * @param location   New tab x-position.
 * @param isShifting Whether the location is still being adjusted.
 */
void
StackAndTile::WindowTabLocationChanged(Window* window, float location,
	bool isShifting)
{

}


/**
 * @brief DesktopListener hook called when a window's size limits change.
 *
 * Updates the SATWindow's pre-SAT cached limits and triggers a relayout via
 * WindowMoved() so the constraint solver respects the new bounds.
 *
 * @param window    The window whose limits changed.
 * @param minWidth  New minimum width.
 * @param maxWidth  New maximum width.
 * @param minHeight New minimum height.
 * @param maxHeight New maximum height.
 */
void
StackAndTile::SizeLimitsChanged(Window* window, int32 minWidth, int32 maxWidth,
	int32 minHeight, int32 maxHeight)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (!satWindow)
		return;
	satWindow->SetOriginalSizeLimits(minWidth, maxWidth, minHeight, maxHeight);

	// trigger a relayout
	WindowMoved(window);
}


/**
 * @brief DesktopListener hook called when a window's look changes.
 *
 * Forwards the new look to SATWindow::WindowLookChanged() which lets each
 * snapping behaviour decide whether the window may stay in its group.
 *
 * @param window The window whose look changed.
 * @param look   The new look value.
 */
void
StackAndTile::WindowLookChanged(Window* window, window_look look)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (!satWindow)
		return;
	satWindow->WindowLookChanged(look);
}


/**
 * @brief DesktopListener hook called when a window's feel changes.
 *
 * Only B_NORMAL_WINDOW_FEEL is compatible with multi-window groups; any
 * other feel evicts the window from a multi-member group.
 *
 * @param window The window whose feel changed.
 * @param feel   The new feel value.
 */
void
StackAndTile::WindowFeelChanged(Window* window, window_feel feel)
{
	// check if it is still a compatible feel
	if (feel == B_NORMAL_WINDOW_FEEL)
		return;
	SATWindow* satWindow = GetSATWindow(window);
	if (satWindow == NULL)
		return;

	SATGroup* group = satWindow->GetGroup();
	if (group == NULL)
		return;

	if (group->CountItems() > 1)
		group->RemoveWindow(satWindow, false);
}


/**
 * @brief DesktopListener hook applying decorator settings to a window.
 *
 * Forwards to SATWindow::SetSettings() which extracts the persistent id used
 * during group restoration.
 *
 * @param window   The window receiving the settings.
 * @param settings Settings archive.
 * @return true if the settings were applied.
 */
bool
StackAndTile::SetDecoratorSettings(Window* window, const BMessage& settings)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (!satWindow)
		return false;

	return satWindow->SetSettings(settings);
}


/**
 * @brief DesktopListener hook reading decorator settings from a window.
 *
 * Forwards to SATWindow::GetSettings() which writes the persistent id.
 *
 * @param window   The window producing the settings.
 * @param settings Output archive that receives the data.
 */
void
StackAndTile::GetDecoratorSettings(Window* window, BMessage& settings)
{
	SATWindow* satWindow = GetSATWindow(window);
	if (!satWindow)
		return;

	satWindow->GetSettings(settings);
}


/**
 * @brief Returns the SATWindow wrapper for @a window, or NULL.
 *
 * @param window The server window to look up; NULL is accepted and yields
 *               NULL.
 * @return The matching SATWindow if known, otherwise NULL.
 * @todo Fix the race with WindowAdded() noted in the source: this method is
 *       sometimes called before the wrapper has been registered.
 */
SATWindow*
StackAndTile::GetSATWindow(Window* window)
{
	if (window == NULL)
		return NULL;

	SATWindowMap::const_iterator it = fSATWindowMap.find(
		window);
	if (it != fSATWindowMap.end())
		return it->second;

	// TODO fix race condition with WindowAdded this method is called before
	// WindowAdded and a SATWindow is created twice!
	return NULL;

	// If we don't know this window, memory allocation might has been failed
	// previously. Try to add the window now.
	SATWindow* satWindow = new (std::nothrow)SATWindow(this, window);
	if (satWindow)
		fSATWindowMap[window] = satWindow;

	return satWindow;
}


/**
 * @brief Looks up a SATWindow by its persistent identifier.
 *
 * Used during group restoration to map archived ids back to live windows.
 *
 * @param id The 64-bit identifier produced by SATWindow::Id().
 * @return The matching SATWindow, or NULL if no window carries that id.
 */
SATWindow*
StackAndTile::FindSATWindow(uint64 id)
{
	for (SATWindowMap::const_iterator it = fSATWindowMap.begin();
		it != fSATWindowMap.end(); it++) {
		SATWindow* window = it->second;
		if (window->Id() == id)
			return window;
	}

	return NULL;
}


//	#pragma mark - StackAndTile private methods


/**
 * @brief Begins a snapping session for the currently-tracked window.
 *
 * Removes the dragged window from its group (so it can move freely while the
 * user looks for a target), brings it to the front (helpful in
 * focus-follows-mouse), and starts the candidate search.
 *
 * @note No-op when no window is currently being dragged.
 */
void
StackAndTile::_StartSAT()
{
	STRACE_SAT("StackAndTile::_StartSAT()\n");
	if (!fCurrentSATWindow)
		return;

	// Remove window from the group.
	SATGroup* group = fCurrentSATWindow->GetGroup();
	if (group == NULL)
		return;

	group->RemoveWindow(fCurrentSATWindow, false);
	// Bring window to the front. (in focus follow mouse this is not
	// automatically the case)
	_ActivateWindow(fCurrentSATWindow);

	fCurrentSATWindow->FindSnappingCandidates();
}


/**
 * @brief Ends a snapping session, committing the gesture if one was found.
 *
 * Asks the dragged window to JoinCandidates(); if it succeeded, re-activates
 * it so the freshly-merged group is in front.
 */
void
StackAndTile::_StopSAT()
{
	STRACE_SAT("StackAndTile::_StopSAT()\n");
	if (!fCurrentSATWindow)
		return;
	if (fCurrentSATWindow->JoinCandidates())
		_ActivateWindow(fCurrentSATWindow);
}


/**
 * @brief Brings @a satWindow's group to the front of the desktop z-order.
 *
 * Promotes the window inside its area, records it as the group's active
 * window, and asks the desktop to activate the top window of every other
 * area so the whole group surfaces together.
 *
 * @param satWindow The window to focus; NULL is accepted and ignored.
 */
void
StackAndTile::_ActivateWindow(SATWindow* satWindow)
{
	if (satWindow == NULL)
		return;

	SATGroup* group = satWindow->GetGroup();
	if (group == NULL)
		return;

	Desktop* desktop = satWindow->GetWindow()->Desktop();
	if (desktop == NULL)
		return;

	WindowArea* area = satWindow->GetWindowArea();
	if (area == NULL)
		return;

	area->MoveToTopLayer(satWindow);

	// save the active window of the current group
	SATWindow* frontWindow = GetSATWindow(fDesktop->FocusWindow());
	SATGroup* currentGroup = _GetSATGroup(frontWindow);
	if (currentGroup != NULL && currentGroup != group && frontWindow != NULL)
		currentGroup->SetActiveWindow(frontWindow);
	else
		group->SetActiveWindow(satWindow);

	const WindowAreaList& areas = group->GetAreaList();
	int32 areasCount = areas.CountItems();
	for (int32 i = 0; i < areasCount; i++) {
		WindowArea* currentArea = areas.ItemAt(i);
		if (currentArea == area)
			continue;

		desktop->ActivateWindow(currentArea->TopWindow()->GetWindow());
	}

	desktop->ActivateWindow(satWindow->GetWindow());
}


/**
 * @brief Handles SAT-global protocol messages (archive/restore all groups).
 *
 * Recognised opcodes:
 *  - kSaveAllGroups: walks every group, archives it via
 *    SATGroup::ArchiveGroup(), and replies with the flattened message.
 *  - kRestoreGroup: reads a flattened group message and reinstates it via
 *    SATGroup::RestoreGroup().
 *
 * @param link  Reader supplying the opcode and payload.
 * @param reply Writer for the response.
 * @return true if the opcode was recognised.
 */
bool
StackAndTile::_HandleMessage(BPrivate::LinkReceiver& link,
	BPrivate::LinkSender& reply)
{
	int32 what;
	link.Read<int32>(&what);

	switch (what) {
		case BPrivate::kSaveAllGroups:
		{
			BMessage allGroupsArchive;
			GroupIterator groups(this, fDesktop);
			while (true) {
				SATGroup* group = groups.NextGroup();
				if (group == NULL)
					break;
				if (group->CountItems() <= 1)
					continue;
				BMessage groupArchive;
				if (group->ArchiveGroup(groupArchive) != B_OK)
					continue;
				allGroupsArchive.AddMessage("group", &groupArchive);
			}
			int32 size = allGroupsArchive.FlattenedSize();
			char buffer[size];
			if (allGroupsArchive.Flatten(buffer, size) == B_OK) {
				reply.StartMessage(B_OK);
				reply.Attach<int32>(size);
				reply.Attach(buffer, size);
			} else
				reply.StartMessage(B_ERROR);
			reply.Flush();
			break;
		}

		case BPrivate::kRestoreGroup:
		{
			int32 size;
			if (link.Read<int32>(&size) == B_OK) {
				char buffer[size];
				BMessage group;
				if (link.Read(buffer, size) == B_OK
					&& group.Unflatten(buffer) == B_OK) {
					status_t status = SATGroup::RestoreGroup(group, this);
					reply.StartMessage(status);
					reply.Flush();
				}
			}
			break;
		}

		default:
			return false;
	}

	return true;
}


/**
 * @brief Returns @a window's group if it has a multi-window group, else NULL.
 *
 * Filters out lone-window groups so navigation logic can treat ungrouped
 * windows uniformly.
 *
 * @param window The candidate SATWindow; NULL yields NULL.
 * @return A non-empty SATGroup or NULL.
 */
SATGroup*
StackAndTile::_GetSATGroup(SATWindow* window)
{
	if (window == NULL)
		return NULL;

	SATGroup* group = window->GetGroup();
	if (group == NULL)
		return NULL;

	if (group->CountItems() < 1)
		return NULL;

	return group;
}


//	#pragma mark - GroupIterator


/**
 * @brief Constructs an iterator that visits every group on @a desktop.
 *
 * The iterator immediately rewinds to the front of the desktop's window list
 * so the first NextGroup() call yields the topmost group.
 *
 * @param sat     Owning StackAndTile listener (used to map windows to groups).
 * @param desktop Desktop whose window order is being walked.
 */
GroupIterator::GroupIterator(StackAndTile* sat, Desktop* desktop)
	:
	fStackAndTile(sat),
	fDesktop(desktop),
	fCurrentGroup(NULL)
{
	RewindToFront();
}


/**
 * @brief Resets iteration so the next NextGroup() returns the topmost group.
 */
void
GroupIterator::RewindToFront()
{
	fCurrentWindow = fDesktop->CurrentWindows().LastWindow();
}


/**
 * @brief Returns the next SATGroup in front-to-back order.
 *
 * Skips hidden windows, the Deskbar and Desktop windows, and groups already
 * yielded so each group is returned exactly once.
 *
 * @return The next group, or NULL when the iterator is exhausted.
 */
SATGroup*
GroupIterator::NextGroup()
{
	SATGroup* group = NULL;
	do {
		Window* window = fCurrentWindow;
		if (window == NULL) {
			group = NULL;
			break;
		}
		fCurrentWindow = fCurrentWindow->PreviousWindow(
			fCurrentWindow->CurrentWorkspace());
		if (window->IsHidden()
			|| strcmp(window->Title(), "Deskbar") == 0
			|| strcmp(window->Title(), "Desktop") == 0) {
			continue;
		}

		SATWindow* satWindow = fStackAndTile->GetSATWindow(window);
		group = satWindow->GetGroup();
	} while (group == NULL || fCurrentGroup == group);

	fCurrentGroup = group;
	return fCurrentGroup;
}


//	#pragma mark - WindowIterator


/**
 * @brief Constructs an iterator that walks every window in @a group.
 *
 * @param group             The group to traverse.
 * @param reverseLayerOrder When true, yields top-most windows first; when
 *                          false, yields bottom-up.
 */
WindowIterator::WindowIterator(SATGroup* group, bool reverseLayerOrder)
	:
	fGroup(group),
	fReverseLayerOrder(reverseLayerOrder)
{
	if (fReverseLayerOrder)
		_ReverseRewind();
	else
		Rewind();
}


/**
 * @brief Resets iteration to the first window of the first WindowArea.
 *
 * Forward direction; used by the bottom-up traversal.
 */
void
WindowIterator::Rewind()
{
	fAreaIndex = 0;
	fWindowIndex = 0;
	fCurrentArea = fGroup->GetAreaList().ItemAt(fAreaIndex);
}


/**
 * @brief Returns the next window in the group, in z-order within each area.
 *
 * Walks each area's LayerOrder() list bottom-up (or top-down via
 * _ReverseNextWindow()) and advances to the next area when the current one
 * is exhausted.
 *
 * @return The next SATWindow, or NULL when the iterator is exhausted.
 */
SATWindow*
WindowIterator::NextWindow()
{
	if (fReverseLayerOrder)
		return _ReverseNextWindow();

	if (fWindowIndex == fCurrentArea->LayerOrder().CountItems()) {
		fAreaIndex++;
		fWindowIndex = 0;
		fCurrentArea = fGroup->GetAreaList().ItemAt(fAreaIndex);
		if (!fCurrentArea)
			return NULL;
	}
	SATWindow* window = fCurrentArea->LayerOrder().ItemAt(fWindowIndex);
	fWindowIndex++;
	return window;
}


//	#pragma mark - WindowIterator private methods


/**
 * @brief Returns the next window when iterating top-down within each area.
 *
 * Mirror of NextWindow() walking the LayerOrder() list backwards; advances
 * to the next area when the current one is exhausted.
 *
 * @return The next SATWindow, or NULL when the iterator is exhausted.
 */
SATWindow*
WindowIterator::_ReverseNextWindow()
{
	if (fWindowIndex < 0) {
		fAreaIndex++;
		fCurrentArea = fGroup->GetAreaList().ItemAt(fAreaIndex);
		if (!fCurrentArea)
			return NULL;
		fWindowIndex = fCurrentArea->LayerOrder().CountItems() - 1;
	}
	SATWindow* window = fCurrentArea->LayerOrder().ItemAt(fWindowIndex);
	fWindowIndex--;
	return window;
}


/**
 * @brief Resets iteration to the last window of the first WindowArea.
 *
 * Used by the top-down traversal so the first _ReverseNextWindow() returns
 * the top-most window of the first area.
 */
void
WindowIterator::_ReverseRewind()
{
	Rewind();
	if (fCurrentArea)
		fWindowIndex = fCurrentArea->LayerOrder().CountItems() - 1;
}
