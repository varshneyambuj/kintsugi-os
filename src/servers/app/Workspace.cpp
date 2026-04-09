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
 *   Copyright 2005-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/** @file Workspace.cpp
 *  @brief Workspace colour, screen-configuration management, and window iteration.
 */

#include "Workspace.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include <Debug.h>

#include "Desktop.h"
#include "WorkspacePrivate.h"
#include "Window.h"


static rgb_color kDefaultColor = (rgb_color){ 51, 102, 152, 255 };


/** @brief Default constructor. Initialises the workspace private data to defaults. */
Workspace::Private::Private()
{
	_SetDefaults();
}


/** @brief Destructor. */
Workspace::Private::~Private()
{
}


/** @brief Populates screen configuration data from the active desktop screens.
 *  @param desktop Pointer to the Desktop from which screen information is read.
 */
void
Workspace::Private::SetDisplaysFromDesktop(Desktop* desktop)
{
}


/** @brief Sets the background colour of this workspace.
 *  @param color The new background colour.
 */
void
Workspace::Private::SetColor(const rgb_color& color)
{
	fColor = color;
}


/** @brief Restores workspace configuration from a serialised BMessage.
 *
 *  Reads the background colour and stored/current screen configurations from
 *  \a settings if the corresponding fields are present.
 *
 *  @param settings The BMessage containing previously stored workspace data.
 */
void
Workspace::Private::RestoreConfiguration(const BMessage& settings)
{
	rgb_color color;
	if (settings.FindInt32("color", (int32 *)&color) == B_OK)
		fColor = color;

	fStoredScreenConfiguration.Restore(settings);
	fCurrentScreenConfiguration.Restore(settings);
}


/*!	\brief Store the workspace configuration in a message
*/
/** @brief Serialises the workspace configuration into a BMessage.
 *
 *  Writes the stored screen configuration and background colour into
 *  \a settings so they can be persisted across sessions.
 *
 *  @param settings The BMessage to write workspace data into.
 */
void
Workspace::Private::StoreConfiguration(BMessage& settings)
{
	fStoredScreenConfiguration.Store(settings);
	settings.AddInt32("color", *(int32 *)&fColor);
}


/** @brief Resets internal state to default values (colour, etc.). */
void
Workspace::Private::_SetDefaults()
{
	fColor = kDefaultColor;
}


//	#pragma mark -


/** @brief Constructs a Workspace iterator for the workspace at \a index.
 *
 *  Acquires either a read or write lock on the desktop's window locker
 *  (enforced by the ASSERT); the \a readOnly flag controls which lock is
 *  required.  Rewinds the internal window cursor to the beginning of the
 *  workspace's window list.
 *
 *  @param desktop  The Desktop that owns this workspace.
 *  @param index    The zero-based workspace index to operate on.
 *  @param readOnly If true a read lock is sufficient; if false a write lock
 *                  must be held by the caller.
 */
Workspace::Workspace(Desktop& desktop, int32 index, bool readOnly)
	:
	fWorkspace(desktop.WorkspaceAt(index)),
	fDesktop(desktop),
	fCurrentWorkspace(index == desktop.CurrentWorkspace())
{
	ASSERT(desktop.WindowLocker().IsWriteLocked()
		|| ( readOnly && desktop.WindowLocker().IsReadLocked()));
	RewindWindows();
}


/** @brief Destructor. */
Workspace::~Workspace()
{
}


/** @brief Returns the current background colour of this workspace.
 *  @return A const reference to the workspace's background colour.
 */
const rgb_color&
Workspace::Color() const
{
	return fWorkspace.Color();
}


/** @brief Changes the background colour of this workspace.
 *
 *  If \a color differs from the current colour the desktop background is
 *  redrawn.  When \a makeDefault is true the new colour is also persisted to
 *  the workspace configuration store.
 *
 *  @param color       The new background colour.
 *  @param makeDefault If true, persist the colour as the default for this workspace.
 */
void
Workspace::SetColor(const rgb_color& color, bool makeDefault)
{
	if (color == Color())
		return;

	fWorkspace.SetColor(color);
	fDesktop.RedrawBackground();
	if (makeDefault)
		fDesktop.StoreWorkspaceConfiguration(fWorkspace.Index());
}


/** @brief Advances the window cursor and returns the next window.
 *
 *  On first call the cursor is positioned at the first window in the list.
 *  Subsequent calls advance to the next window.  The position returned in
 *  \a _leftTop reflects the window's actual on-screen position for the
 *  current workspace, or its saved anchor position for inactive workspaces.
 *
 *  @param _window   Set to the next Window on success.
 *  @param _leftTop  Set to the top-left position of the window in screen coordinates.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND when the list is exhausted.
 */
status_t
Workspace::GetNextWindow(Window*& _window, BPoint& _leftTop)
{
	if (fCurrent == NULL)
		fCurrent = fWorkspace.Windows().FirstWindow();
	else
		fCurrent = fCurrent->NextWindow(fWorkspace.Index());

	if (fCurrent == NULL)
		return B_ENTRY_NOT_FOUND;

	_window = fCurrent;

	if (fCurrentWorkspace)
		_leftTop = fCurrent->Frame().LeftTop();
	else
		_leftTop = fCurrent->Anchor(fWorkspace.Index()).position;

	return B_OK;
}


/** @brief Retreats the window cursor and returns the previous window.
 *
 *  On first call the cursor is positioned at the last window in the list.
 *  Subsequent calls move toward the front of the list.  The position returned
 *  in \a _leftTop follows the same active/inactive workspace convention as
 *  GetNextWindow().
 *
 *  @param _window   Set to the previous Window on success.
 *  @param _leftTop  Set to the top-left position of the window in screen coordinates.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND when the list is exhausted.
 */
status_t
Workspace::GetPreviousWindow(Window*& _window, BPoint& _leftTop)
{
	if (fCurrent == NULL)
		fCurrent = fWorkspace.Windows().LastWindow();
	else
		fCurrent = fCurrent->PreviousWindow(fWorkspace.Index());

	if (fCurrent == NULL)
		return B_ENTRY_NOT_FOUND;

	_window = fCurrent;

	if (fCurrentWorkspace)
		_leftTop = fCurrent->Frame().LeftTop();
	else
		_leftTop = fCurrent->Anchor(fWorkspace.Index()).position;

	return B_OK;
}


/** @brief Resets the window iteration cursor to the initial (before-first) position.
 *
 *  After calling this method the next call to GetNextWindow() will return the
 *  first window, and the next call to GetPreviousWindow() will return the last.
 */
void
Workspace::RewindWindows()
{
	fCurrent = NULL;
}
