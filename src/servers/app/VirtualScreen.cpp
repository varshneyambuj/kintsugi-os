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
 *   Copyright 2005-2013, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */

/** @file VirtualScreen.cpp
 *  @brief Aggregates one or more physical screens into a unified virtual display surface.
 */

#include "VirtualScreen.h"

#include "HWInterface.h"
#include "Desktop.h"

#include <new>


/** @brief Constructs an empty VirtualScreen with no screens attached. */
VirtualScreen::VirtualScreen()
	:
	fScreenList(4),
	fDrawingEngine(NULL),
	fHWInterface(NULL)
{
}


/** @brief Destructor. Releases all acquired screens. */
VirtualScreen::~VirtualScreen()
{
	_Reset();
}


/** @brief Releases all screens and resets internal state to empty.
 *
 *  Releases every screen held in the screen list back to the global
 *  ScreenManager, empties the list, and resets the cached frame,
 *  drawing engine, and hardware interface pointers to NULL/zero.
 */
void
VirtualScreen::_Reset()
{
	ScreenList list;
	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		screen_item* item = fScreenList.ItemAt(i);

		list.AddItem(item->screen);
	}

	gScreenManager->ReleaseScreens(list);
	fScreenList.MakeEmpty();

	fFrame.Set(0, 0, 0, 0);
	fDrawingEngine = NULL;
	fHWInterface = NULL;
}


/** @brief Acquires screens from the ScreenManager and applies stored configurations.
 *
 *  Resets the current screen list, then asks the global ScreenManager for all
 *  screens appropriate for \a desktop.  Each acquired screen is added via
 *  AddScreen() which attempts to restore its saved display mode from
 *  \a configurations.  If \a _changedScreens is non-NULL it is set to a
 *  bitmask indicating which screen indices changed their display mode compared
 *  to the previous configuration.
 *
 *  @param desktop         The Desktop that will own the screens.
 *  @param configurations  Stored screen configurations to apply where possible.
 *  @param _changedScreens Optional output bitmask of changed screen indices.
 *  @return B_OK on success, or an error code if screen acquisition fails.
 */
status_t
VirtualScreen::SetConfiguration(Desktop& desktop,
	ScreenConfigurations& configurations, uint32* _changedScreens)
{
	// Remember previous screen modes

	typedef std::map<Screen*, display_mode> ScreenModeMap;
	ScreenModeMap previousModes;
	bool previousModesFailed = false;

	if (_changedScreens != NULL) {
		*_changedScreens = 0;

		try {
			for (int32 i = 0; i < fScreenList.CountItems(); i++) {
				Screen* screen = fScreenList.ItemAt(i)->screen;

				display_mode mode;
				screen->GetMode(mode);

				previousModes.insert(std::make_pair(screen, mode));
			}
		} catch (...) {
			previousModesFailed = true;
			*_changedScreens = ~0L;
		}
	}

	_Reset();

	ScreenList list;
	status_t status = gScreenManager->AcquireScreens(&desktop, NULL, 0,
		desktop.TargetScreen(), false, list);
	if (status != B_OK) {
		// TODO: we would try again here with force == true
		return status;
	}

	for (int32 i = 0; i < list.CountItems(); i++) {
		Screen* screen = list.ItemAt(i);

		AddScreen(screen, configurations);

		if (!previousModesFailed && _changedScreens != NULL) {
			// Figure out which screens have changed their mode
			display_mode mode;
			screen->GetMode(mode);

			ScreenModeMap::const_iterator found = previousModes.find(screen);
			if (found != previousModes.end()
				&& memcmp(&mode, &found->second, sizeof(display_mode)))
				*_changedScreens |= 1 << i;
		}
	}

	UpdateFrame();
	return B_OK;
}


/** @brief Adds a single screen to the virtual display and applies its display mode.
 *
 *  Attempts to restore the screen's display mode from \a configurations.  If
 *  no valid saved configuration is found the method falls back to the screen's
 *  preferred mode, then to 1024x768 and 800x600 at 32-bit colour.  On success
 *  the screen is appended to the internal list and the cached drawing engine
 *  and hardware interface pointers are updated (single-screen implementation).
 *
 *  @param screen          The Screen object to add.
 *  @param configurations  Stored screen configurations to try first.
 *  @return B_OK on success, or B_NO_MEMORY / a driver error code on failure.
 */
status_t
VirtualScreen::AddScreen(Screen* screen, ScreenConfigurations& configurations)
{
	screen_item* item = new(std::nothrow) screen_item;
	if (item == NULL)
		return B_NO_MEMORY;

	item->screen = screen;

	status_t status = B_ERROR;
	display_mode mode;
	if (_GetMode(screen, configurations, mode) == B_OK) {
		// we found settings for this screen, and try to apply them now
		status = screen->SetMode(mode);
	}

	if (status != B_OK) {
		// We found no configuration or it wasn't valid, try to fallback to
		// sane values
		status = screen->SetPreferredMode();
		if (status == B_OK) {
			monitor_info info;
			bool hasInfo = screen->GetMonitorInfo(info) == B_OK;
			screen->GetMode(mode);
			configurations.Set(screen->ID(), hasInfo ? &info : NULL, screen->Frame(), mode);
		}
		if (status != B_OK)
			status = screen->SetBestMode(1024, 768, B_RGB32, 60.f);
		if (status != B_OK)
			status = screen->SetBestMode(800, 600, B_RGB32, 60.f, false);
		if (status != B_OK) {
			debug_printf("app_server: Failed to set mode: %s\n",
				strerror(status));

			delete item;
			return status;
		}
	}

	// TODO: this works only for single screen configurations
	fDrawingEngine = screen->GetDrawingEngine();
	fHWInterface = screen->HWInterface();
	fFrame = screen->Frame();
	item->frame = fFrame;

	fScreenList.AddItem(item);

	return B_OK;
}


/** @brief Removes a screen from the virtual display (not yet implemented).
 *  @param screen The screen to remove.
 *  @return Always returns B_ERROR until dynamic hot-plug support is implemented.
 */
status_t
VirtualScreen::RemoveScreen(Screen* screen)
{
	// not implemented yet (config changes when running)
	return B_ERROR;
}


/** @brief Recalculates the bounding frame that spans all attached screens.
 *
 *  Iterates over every screen and sums their widths horizontally while
 *  tracking the maximum height.  The resulting frame is stored in fFrame.
 *  Note: the current implementation does not account for screen position
 *  offsets (suitable for single or side-by-side layouts only).
 */
void
VirtualScreen::UpdateFrame()
{
	int32 virtualWidth = 0, virtualHeight = 0;

	for (int32 i = 0; i < fScreenList.CountItems(); i++) {
		Screen* screen = fScreenList.ItemAt(i)->screen;

		uint16 width, height;
		uint32 colorSpace;
		float frequency;
		screen->GetMode(width, height, colorSpace, frequency);

		// TODO: compute virtual size depending on the actual screen position!
		virtualWidth += width;
		virtualHeight = max_c(virtualHeight, height);
	}

	fFrame.Set(0, 0, virtualWidth - 1, virtualHeight - 1);
}


/*!	Returns the smallest frame that spans over all screens
*/
/** @brief Returns the bounding rectangle that encompasses all screens.
 *  @return The virtual frame in screen coordinates (origin at 0,0).
 */
BRect
VirtualScreen::Frame() const
{
	return fFrame;
}


/** @brief Returns the Screen at the given list index.
 *  @param index Zero-based index into the screen list.
 *  @return Pointer to the Screen, or NULL if \a index is out of range.
 */
Screen*
VirtualScreen::ScreenAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->screen;

	return NULL;
}


/** @brief Finds a Screen by its identifier.
 *
 *  Iterates the screen list and returns the first screen whose ID matches
 *  \a id.  The special value B_MAIN_SCREEN_ID.id always matches the last
 *  screen in the list.
 *
 *  @param id The screen identifier to search for.
 *  @return Pointer to the matching Screen, or NULL if not found.
 */
Screen*
VirtualScreen::ScreenByID(int32 id) const
{
	for (int32 i = fScreenList.CountItems(); i-- > 0;) {
		screen_item* item = fScreenList.ItemAt(i);

		if (item->screen->ID() == id || id == B_MAIN_SCREEN_ID.id)
			return item->screen;
	}

	return NULL;
}


/** @brief Returns the frame rectangle of the screen at the given list index.
 *  @param index Zero-based index into the screen list.
 *  @return The screen's frame, or BRect(0,0,0,0) if \a index is out of range.
 */
BRect
VirtualScreen::ScreenFrameAt(int32 index) const
{
	screen_item* item = fScreenList.ItemAt(index);
	if (item != NULL)
		return item->frame;

	return BRect(0, 0, 0, 0);
}


/** @brief Returns the number of screens currently managed by this VirtualScreen.
 *  @return The screen count.
 */
int32
VirtualScreen::CountScreens() const
{
	return fScreenList.CountItems();
}


/** @brief Looks up the best matching stored display mode for \a screen.
 *
 *  Queries \a configurations for a configuration that best fits the given
 *  screen's identity and monitor information.  If a match is found the mode
 *  is written into \a mode and the configuration is marked as current.
 *
 *  @param screen         The screen whose mode should be retrieved.
 *  @param configurations The pool of stored screen configurations to search.
 *  @param mode           Output parameter filled with the found display mode.
 *  @return B_OK if a configuration was found, B_NAME_NOT_FOUND otherwise.
 */
status_t
VirtualScreen::_GetMode(Screen* screen, ScreenConfigurations& configurations,
	display_mode& mode) const
{
	monitor_info info;
	bool hasInfo = screen->GetMonitorInfo(info) == B_OK;

	screen_configuration* configuration = configurations.BestFit(screen->ID(),
		hasInfo ? &info : NULL);
	if (configuration == NULL)
		return B_NAME_NOT_FOUND;

	mode = configuration->mode;
	configuration->is_current = true;

	return B_OK;
}
