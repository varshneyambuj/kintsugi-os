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
 * Copyright 2005-2013, Haiku.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file Workspace.h
 *  @brief Short-lived RAII accessor for iterating windows in a single workspace. */

#ifndef WORKSPACE_H
#define WORKSPACE_H


#include <InterfaceDefs.h>


class Desktop;
class Window;


/*!	Workspace objects are intended to be short-lived. You create them while
	already holding a lock to the Desktop read-write lock and then you can use
	them to query information, and then you destroy them again, for example by
	letting them go out of scope.
*/
/** @brief Provides read/write access to one workspace's window list and background color. */
class Workspace {
public:
	/** @brief Constructs a Workspace accessor for the given Desktop and index.
	 *  @param desktop   Desktop that owns the workspace.
	 *  @param index     Zero-based workspace index.
	 *  @param readOnly  If true, inhibit color writes. */
								Workspace(Desktop& desktop, int32 index,
									bool readOnly = false);
								~Workspace();

	/** @brief Returns the background color of this workspace.
	 *  @return Reference to the background rgb_color. */
			const rgb_color&	Color() const;

	/** @brief Sets the background color of this workspace.
	 *  @param color       New background color.
	 *  @param makeDefault If true, persist the color as the default for this workspace. */
			void				SetColor(const rgb_color& color,
									bool makeDefault);

	/** @brief Returns whether this workspace is the currently active one.
	 *  @return true if current. */
			bool				IsCurrent() const
									{ return fCurrentWorkspace; }

	/** @brief Advances the iterator to the next window in the workspace.
	 *  @param _window   Receives the next Window pointer.
	 *  @param _leftTop  Receives the window's stored position.
	 *  @return B_OK while more windows remain, B_ERROR at end of list. */
			status_t			GetNextWindow(Window*& _window,
									BPoint& _leftTop);

	/** @brief Moves the iterator to the previous window in the workspace.
	 *  @param _window   Receives the previous Window pointer.
	 *  @param _leftTop  Receives the window's stored position.
	 *  @return B_OK while more windows remain, B_ERROR at beginning of list. */
			status_t			GetPreviousWindow(Window*& _window,
									BPoint& _leftTop);

	/** @brief Resets the window iterator to the beginning of the workspace list. */
			void				RewindWindows();

	class Private;

private:
			Workspace::Private&	fWorkspace;
			Desktop&			fDesktop;
			Window*				fCurrent;
			bool				fCurrentWorkspace;
};


#endif	/* WORKSPACE_H */
