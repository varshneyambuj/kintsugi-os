/*
 * Copyright 2025, Kintsugi OS Contributors.
 *
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright (c) 2005-2008, Haiku, Inc.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file WindowList.h
 *  @brief Intrusive doubly-linked list of Window objects and associated anchor types. */

#ifndef WINDOW_LIST_H
#define WINDOW_LIST_H


#include <SupportDefs.h>
#include <Point.h>


class Window;


/** @brief Intrusive doubly-linked list for Window objects keyed by a list index. */
class WindowList {
public:
	/** @brief Constructs an empty WindowList associated with the given index.
	 *  @param index List index (e.g. kAllWindowList). */
					WindowList(int32 index = 0);
					~WindowList();

	/** @brief Sets the list index used to find the window_anchor in each Window.
	 *  @param index New list index. */
			void	SetIndex(int32 index);

	/** @brief Returns the list index.
	 *  @return Current list index. */
			int32	Index() const { return fIndex; }

	/** @brief Returns the first Window in the list.
	 *  @return Pointer to the first Window, or NULL if empty. */
			Window*	FirstWindow() const { return fFirstWindow; }

	/** @brief Returns the last Window in the list.
	 *  @return Pointer to the last Window, or NULL if empty. */
			Window*	LastWindow() const { return fLastWindow; }

	/** @brief Inserts a window into the list before the given window.
	 *  @param window Window to insert.
	 *  @param before Window to insert before, or NULL to append. */
			void	AddWindow(Window* window, Window* before = NULL);

	/** @brief Removes a window from the list.
	 *  @param window Window to remove. */
			void	RemoveWindow(Window* window);

	/** @brief Returns whether the given window is in this list.
	 *  @param window Window to search for.
	 *  @return true if found. */
			bool	HasWindow(Window* window) const;

	/** @brief Validates that the given window's list links are consistent.
	 *  @param window Window to validate.
	 *  @return true if the window's anchor is intact. */
			bool	ValidateWindow(Window* window) const;

	/** @brief Returns the number of windows in this list (O(n) traversal).
	 *  @return Window count. */
			int32	Count() const;
						// O(n)

private:
	int32			fIndex;
	Window*			fFirstWindow;
	Window*			fLastWindow;
};

/** @brief Symbolic indices for the different window lists maintained by the Desktop. */
enum window_lists {
	kAllWindowList = 32, /**< All windows across all workspaces. */
	kSubsetList,         /**< Windows in the modal/floating subset. */
	kFocusList,          /**< Windows ordered by focus history. */
	kWorkingList,        /**< Windows on the current workspace. */

	kListCount           /**< Total number of list indices. */
};

/** @brief Intrusive link node embedded in each Window for membership in a WindowList. */
struct window_anchor {
	/** @brief Default-constructs an anchor with no neighbours and an invalid position. */
	window_anchor();

	Window*	next;       /**< Next window in the list. */
	Window*	previous;   /**< Previous window in the list. */
	BPoint	position;   /**< Saved position for workspace switches. */
};

/** @brief Sentinel value indicating an invalid or unset window position. */
extern const BPoint kInvalidWindowPosition;

#endif	// WINDOW_LIST_H
