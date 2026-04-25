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
 * @file Stacking.cpp
 * @brief Tab-stacking gesture and stacking-protocol message handler.
 *
 * Implements the snapping behaviour that lets the user drop one window's tab
 * onto another's to merge them into a single stacked WindowStack, plus the
 * StackingEventHandler which serves the BPrivate stacking API messages
 * (add/remove/count/query stack members) sent by the BWindow client.
 *
 * @see SATWindow, SATGroup, WindowArea
 */


#include "Stacking.h"

#include <Debug.h>

#include "StackAndTilePrivate.h"

#include "Desktop.h"
#include "SATWindow.h"
#include "Window.h"


//#define DEBUG_STACKING

#ifdef DEBUG_STACKING
#	define STRACE_STACKING(x...) debug_printf("SAT Stacking: "x)
#else
#	define STRACE_STACKING(x...) ;
#endif


using namespace BPrivate;


/**
 * @brief Dispatches a single stacking-protocol message to the matching action.
 *
 * Reads the opcode and any payload from @a link, applies the operation to the
 * sender's WindowArea / SATGroup, and writes a status reply (and any required
 * payload) back through @a reply. Recognised opcodes include adding or
 * removing a window from a stack, counting stack members, and looking up a
 * stack entry by index or by client port.
 *
 * @param sender The SATWindow that received the message; its WindowArea is
 *               the target stack.
 * @param link   Reader supplying the remaining message payload.
 * @param reply  Writer used to send back B_OK plus per-opcode results, or an
 *               error code on failure.
 * @return true if the opcode was recognised (a reply has been sent or the
 *         caller should treat the connection as still healthy), false on a
 *         protocol or read error.
 */
bool
StackingEventHandler::HandleMessage(SATWindow* sender,
	BPrivate::LinkReceiver& link, BPrivate::LinkSender& reply)
{
	Desktop* desktop = sender->GetDesktop();
	StackAndTile* stackAndTile = sender->GetStackAndTile();

	int32 what;
	link.Read<int32>(&what);

	switch (what) {
		case kAddWindowToStack:
		{
			port_id port;
			int32 token;
			team_id team;
			link.Read<port_id>(&port);
			link.Read<int32>(&token);
			link.Read<team_id>(&team);
			int32 position;
			if (link.Read<int32>(&position) != B_OK)
				return false;

			WindowArea* area = sender->GetWindowArea();
			if (!area)
				return false;
			if (position < 0)
				position = area->WindowList().CountItems() - 1;

			SATWindow* parent = area->WindowList().ItemAt(position);
			Window* window = desktop->WindowForClientLooperPort(port);
			if (!parent || !window) {
				reply.StartMessage(B_BAD_VALUE);
				reply.Flush();
				break;
			}

			SATWindow* candidate = stackAndTile->GetSATWindow(window);
			if (!candidate)
				return false;

			// Is that window already part of the stack?
			if (area->WindowList().HasItem(candidate)) {
				reply.StartMessage(B_MISMATCHED_VALUES);
				reply.Flush();
				break;
			}

			if (!parent->StackWindow(candidate))
				return false;

			reply.StartMessage(B_OK);
			reply.Flush();
			break;
		}
		case kRemoveWindowFromStack:
		{
			port_id port;
			int32 token;
			team_id team;
			link.Read<port_id>(&port);
			link.Read<int32>(&token);
			if (link.Read<team_id>(&team) != B_OK)
				return false;

			SATGroup* group = sender->GetGroup();
			if (!group)
				return false;

			Window* window = desktop->WindowForClientLooperPort(port);
			if (!window) {
				reply.StartMessage(B_BAD_VALUE);
				reply.Flush();
				break;
			}
			SATWindow* candidate = stackAndTile->GetSATWindow(window);
			if (!candidate)
				return false;
			if (!group->RemoveWindow(candidate, false))
				return false;
			break;
		}
		case kRemoveWindowFromStackAt:
		{
			int32 position;
			if (link.Read<int32>(&position) != B_OK)
				return false;
			SATGroup* group = sender->GetGroup();
			WindowArea* area = sender->GetWindowArea();
			if (!area || !group)
				return false;
			SATWindow* removeWindow = area->WindowList().ItemAt(position);
			if (!removeWindow) {
				reply.StartMessage(B_BAD_VALUE);
				reply.Flush();
				break;
			}

			if (!group->RemoveWindow(removeWindow, false))
				return false;

			ServerWindow* window = removeWindow->GetWindow()->ServerWindow();
			reply.StartMessage(B_OK);
			reply.Attach<port_id>(window->ClientLooperPort());
			reply.Attach<int32>(window->ClientToken());
			reply.Attach<team_id>(window->ClientTeam());
			reply.Flush();
			break;
		}
		case kCountWindowsOnStack:
		{
			WindowArea* area = sender->GetWindowArea();
			if (!area)
				return false;
			reply.StartMessage(B_OK);
			reply.Attach<int32>(area->WindowList().CountItems());
			reply.Flush();
			break;
		}
		case kWindowOnStackAt:
		{
			int32 position;
			if (link.Read<int32>(&position) != B_OK)
				return false;
			WindowArea* area = sender->GetWindowArea();
			if (!area)
				return false;
			SATWindow* satWindow = area->WindowList().ItemAt(position);
			if (!satWindow) {
				reply.StartMessage(B_BAD_VALUE);
				reply.Flush();
				break;
			}

			ServerWindow* window = satWindow->GetWindow()->ServerWindow();
			reply.StartMessage(B_OK);
			reply.Attach<port_id>(window->ClientLooperPort());
			reply.Attach<int32>(window->ClientToken());
			reply.Attach<team_id>(window->ClientTeam());
			reply.Flush();
			break;
		}
		case kStackHasWindow:
		{
			port_id port;
			int32 token;
			team_id team;
			link.Read<port_id>(&port);
			link.Read<int32>(&token);
			if (link.Read<team_id>(&team) != B_OK)
				return false;

			Window* window = desktop->WindowForClientLooperPort(port);
			if (!window) {
				reply.StartMessage(B_BAD_VALUE);
				reply.Flush();
				break;
			}
			SATWindow* candidate = stackAndTile->GetSATWindow(window);
			if (!candidate)
				return false;

			WindowArea* area = sender->GetWindowArea();
			if (!area)
				return false;
			reply.StartMessage(B_OK);
			reply.Attach<bool>(area->WindowList().HasItem(candidate));
			reply.Flush();
			break;
		}
		default:
			return false;
	}
	return true;
}


/**
 * @brief Constructs the stacking behaviour bound to one SATWindow.
 *
 * The window starts with no pending stacking parent; FindSnappingCandidates()
 * will populate fStackingParent only while a drag matches a target tab.
 *
 * @param window The window this behaviour represents during a SAT drag.
 */
SATStacking::SATStacking(SATWindow* window)
	:
	fSATWindow(window),
	fStackingParent(NULL)
{

}


/** @brief Destructor; no resources owned. */
SATStacking::~SATStacking()
{

}


/**
 * @brief Searches @a group for a tab whose rectangle is under the mouse.
 *
 * Walks every window in the group and tests whether the cursor sits inside
 * its current decorator tab. If so, the candidate is remembered as the
 * stacking parent and both the parent and this window's tabs are highlighted
 * to preview the upcoming merge.
 *
 * @param group The candidate SATGroup currently being probed.
 * @return true if a stacking parent has been found and highlighted, false
 *         otherwise.
 * @note Both windows must satisfy _IsStackableWindow() (titled or document
 *       look). Any earlier highlight from a previous call is cleared first.
 */
bool
SATStacking::FindSnappingCandidates(SATGroup* group)
{
	_ClearSearchResult();

	Window* window = fSATWindow->GetWindow();
	if (!window->Decorator())
		return false;

	BPoint mousePosition;
	int32 buttons;
	fSATWindow->GetDesktop()->GetLastMouseState(&mousePosition, &buttons);
	if (!window->Decorator()->TitleBarRect().Contains(mousePosition))
		return false;

	// use the upper edge of the candidate window to find the parent window
	mousePosition.y = window->Decorator()->TitleBarRect().top;

	for (int i = 0; i < group->CountItems(); i++) {
		SATWindow* satWindow = group->WindowAt(i);
		// search for stacking parent
		Window* parentWindow = satWindow->GetWindow();
		if (parentWindow == window || parentWindow->Decorator() == NULL)
			continue;
		if (_IsStackableWindow(parentWindow) == false
			|| _IsStackableWindow(window) == false)
			continue;
		Decorator::Tab* tab = parentWindow->Decorator()->TabAt(
			parentWindow->PositionInStack());
		if (tab == NULL)
			continue;
		if (tab->tabRect.Contains(mousePosition)) {
			// remember window as the parent for stacking
			fStackingParent = satWindow;
			_HighlightWindows(true);
			return true;
		}
	}

	return false;
}


/**
 * @brief Commits the pending stacking gesture by merging the two windows.
 *
 * Stacks fSATWindow into the parent's WindowArea, clearing the search state
 * regardless of outcome.
 *
 * @return true if the stacking parent accepted the new tab, false if there
 *         was no candidate or the merge failed.
 */
bool
SATStacking::JoinCandidates()
{
	if (!fStackingParent)
		return false;

	bool result = fStackingParent->StackWindow(fSATWindow);

	_ClearSearchResult();
	return result;
}


/**
 * @brief Re-runs the layout when this window's area loses a member.
 *
 * If any windows remain in @a area, asks the first one to re-solve the group
 * geometry so the remaining tabs reflow.
 *
 * @param area The WindowArea this window has just been removed from.
 */
void
SATStacking::RemovedFromArea(WindowArea* area)
{
	const SATWindowList& list = area->WindowList();
	if (list.CountItems() > 0)
		list.ItemAt(0)->DoGroupLayout();
}


/**
 * @brief Detaches this window from its stack if its look is no longer stackable.
 *
 * Called from SATWindow::WindowLookChanged(); when the window switches to a
 * look that cannot be tab-stacked (e.g. floating or modal) and is currently
 * part of a multi-window stack, removes it so the rest of the stack stays
 * consistent.
 *
 * @param look The new window_look reported by the server window.
 */
void
SATStacking::WindowLookChanged(window_look look)
{
	Window* window = fSATWindow->GetWindow();
	WindowStack* stack = window->GetWindowStack();
	if (stack == NULL)
		return;
	SATGroup* group = fSATWindow->GetGroup();
	if (group == NULL)
		return;
	if (stack->CountWindows() > 1 && _IsStackableWindow(window) == false)
		group->RemoveWindow(fSATWindow);
}


/**
 * @brief Returns whether @a window's look permits tab-stacking.
 *
 * Only document-style and titled windows can host or join a stack; every
 * other window_look is rejected.
 *
 * @param window The candidate window.
 * @return true for B_DOCUMENT_WINDOW_LOOK and B_TITLED_WINDOW_LOOK.
 */
bool
SATStacking::_IsStackableWindow(Window* window)
{
	if (window->Look() == B_DOCUMENT_WINDOW_LOOK)
		return true;
	if (window->Look() == B_TITLED_WINDOW_LOOK)
		return true;
	return false;
}


/**
 * @brief Clears any pending stacking parent and removes its tab highlight.
 *
 * Safe to call when no candidate has been recorded; in that case it is a
 * no-op.
 */
void
SATStacking::_ClearSearchResult()
{
	if (!fStackingParent)
		return;

	_HighlightWindows(false);
	fStackingParent = NULL;
}


/**
 * @brief Toggles the SAT highlight on the parent and the dragged window's tabs.
 *
 * @param highlight true to draw the highlight, false to remove it.
 * @note Bails out early when the dragged window is no longer attached to a
 *       desktop.
 */
void
SATStacking::_HighlightWindows(bool highlight)
{
	Desktop* desktop = fSATWindow->GetWindow()->Desktop();
	if (!desktop)
		return;
	fStackingParent->HighlightTab(highlight);
	fSATWindow->HighlightTab(highlight);
}
