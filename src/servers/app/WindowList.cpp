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
 *   Copyright (c) 2005-2008, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/** @file WindowList.cpp
 *  @brief Doubly-linked list managing server-side Window objects per workspace slot.
 */

#include "DesktopSettings.h"
#include "Window.h"


const BPoint kInvalidWindowPosition = BPoint(INFINITY, INFINITY);


/** @brief Default constructor for window_anchor.
 *
 *  Initialises the next and previous pointers to NULL and the saved position
 *  to the sentinel kInvalidWindowPosition value.
 */
window_anchor::window_anchor()
	 :
	 next(NULL),
	 previous(NULL),
	 position(kInvalidWindowPosition)
{
}


//	#pragma mark -


/** @brief Constructs a WindowList with the given index.
 *  @param index The workspace or list index this list represents.
 */
WindowList::WindowList(int32 index)
	:
	fIndex(index),
	fFirstWindow(NULL),
	fLastWindow(NULL)
{
}


/** @brief Destructor. Does not delete the Window objects it references. */
WindowList::~WindowList()
{
}


/** @brief Sets the list index.
 *  @param index The new workspace or list index.
 */
void
WindowList::SetIndex(int32 index)
{
	fIndex = index;
}


/*!
	Adds the \a window to the end of the list. If \a before is
	given, it will be inserted right before that window.
*/
/** @brief Inserts a Window into the list.
 *
 *  Appends \a window to the end of the list unless \a before is specified,
 *  in which case it is inserted immediately before that window.
 *  If the index is within the normal workspace range the window's workspace
 *  bitmask is updated accordingly.
 *
 *  @param window The window to insert. Must not be NULL.
 *  @param before Optional window before which \a window is inserted.
 *                If NULL the window is appended at the tail.
 */
void
WindowList::AddWindow(Window* window, Window* before)
{
	window_anchor& windowAnchor = window->Anchor(fIndex);

	if (before != NULL) {
		window_anchor& beforeAnchor = before->Anchor(fIndex);

		// add view before this one
		windowAnchor.next = before;
		windowAnchor.previous = beforeAnchor.previous;
		if (windowAnchor.previous != NULL)
			windowAnchor.previous->Anchor(fIndex).next = window;

		beforeAnchor.previous = window;
		if (fFirstWindow == before)
			fFirstWindow = window;
	} else {
		// add view to the end of the list
		if (fLastWindow != NULL) {
			fLastWindow->Anchor(fIndex).next = window;
			windowAnchor.previous = fLastWindow;
		} else {
			fFirstWindow = window;
			windowAnchor.previous = NULL;
		}

		windowAnchor.next = NULL;
		fLastWindow = window;
	}

	if (fIndex < kMaxWorkspaces)
		window->SetWorkspaces(window->Workspaces() | (1UL << fIndex));
}


/** @brief Removes a Window from the list.
 *
 *  Unlinks \a window from the doubly-linked list and, if the index is within
 *  the normal workspace range, clears the corresponding bit in the window's
 *  workspace bitmask.
 *
 *  @param window The window to remove. Must be a member of this list.
 */
void
WindowList::RemoveWindow(Window* window)
{
	window_anchor& windowAnchor = window->Anchor(fIndex);

	if (fFirstWindow == window) {
		// it's the first child
		fFirstWindow = windowAnchor.next;
	} else {
		// it must have a previous sibling, then
		windowAnchor.previous->Anchor(fIndex).next = windowAnchor.next;
	}

	if (fLastWindow == window) {
		// it's the last child
		fLastWindow = windowAnchor.previous;
	} else {
		// then it must have a next sibling
		windowAnchor.next->Anchor(fIndex).previous = windowAnchor.previous;
	}

	if (fIndex < kMaxWorkspaces)
		window->SetWorkspaces(window->Workspaces() & ~(1UL << fIndex));

	windowAnchor.previous = NULL;
	windowAnchor.next = NULL;
}


/** @brief Checks whether a Window is a member of this list.
 *
 *  Uses the window's anchor links to determine membership without walking the
 *  full list.  The pointer \a window must remain valid for the duration of
 *  this call.
 *
 *  @param window The window to look for; may be NULL (returns false).
 *  @return true if \a window is in the list, false otherwise.
 */
bool
WindowList::HasWindow(Window* window) const
{
	if (window == NULL)
		return false;

	return window->Anchor(fIndex).next != NULL
		|| window->Anchor(fIndex).previous != NULL
		|| fFirstWindow == window
		|| fLastWindow == window;
}


/*!	Unlike HasWindow(), this will not reference the window pointer. You
	can use this method to check whether or not a window is still part
	of a list (when it's possible that the window is already gone).
*/
/** @brief Safely validates whether a window pointer is still in the list.
 *
 *  Unlike HasWindow(), this method performs a full linear scan and never
 *  dereferences anchor data through the supplied pointer, making it safe
 *  to call when the window object may have already been deleted elsewhere.
 *
 *  @param validateWindow The pointer to validate.
 *  @return true if the pointer is found in the list, false otherwise.
 */
bool
WindowList::ValidateWindow(Window* validateWindow) const
{
	for (Window *window = FirstWindow(); window != NULL;
			window = window->NextWindow(fIndex)) {
		if (window == validateWindow)
			return true;
	}

	return false;
}


/** @brief Returns the number of windows currently in the list.
 *  @return The count of windows (O(n) traversal).
 */
int32
WindowList::Count() const
{
	int32 count = 0;

	for (Window *window = FirstWindow(); window != NULL;
			window = window->NextWindow(fIndex)) {
		count++;
	}

	return count;
}
