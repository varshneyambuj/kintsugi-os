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
 * Copyright 2005-2009, Haiku.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file VirtualScreen.h
 *  @brief Aggregates multiple physical screens into a single virtual desktop surface. */

#ifndef VIRTUAL_SCREEN_H
#define VIRTUAL_SCREEN_H


#include "ScreenConfigurations.h"
#include "ScreenManager.h"

#include <Message.h>


class Desktop;
class DrawingEngine;
class HWInterface;


/** @brief Combines all physical screens into a unified frame and provides layout services. */
class VirtualScreen {
public:
								VirtualScreen();
								~VirtualScreen();

	/** @brief Returns the shared DrawingEngine used across all screens.
	 *  @return Pointer to the DrawingEngine. */
			::DrawingEngine*	DrawingEngine() const
									{ return fDrawingEngine; }

			// TODO: can we have a multiplexing HWInterface as well?
			//	If not, this would need to be hidden, and only made
			//	available for the Screen class
	/** @brief Returns the primary HWInterface for this virtual screen.
	 *  @return Pointer to the HWInterface. */
			::HWInterface*		HWInterface() const
									{ return fHWInterface; }

	/** @brief Applies a new screen layout configuration to the virtual screen.
	 *  @param desktop         Desktop whose screens are being configured.
	 *  @param configurations  Stored screen configurations to apply.
	 *  @param _changedScreens Optional bitmask of screens whose mode changed.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			SetConfiguration(Desktop& desktop,
									ScreenConfigurations& configurations,
									uint32* _changedScreens = NULL);

	/** @brief Adds a physical screen to the virtual screen.
	 *  @param screen          Screen to add.
	 *  @param configurations  Configurations used to determine the screen's frame.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			AddScreen(Screen* screen,
									ScreenConfigurations& configurations);

	/** @brief Removes a physical screen from the virtual screen.
	 *  @param screen Screen to remove.
	 *  @return B_OK on success, an error code otherwise. */
			status_t			RemoveScreen(Screen* screen);

	/** @brief Recomputes the combined frame of all screens. */
			void				UpdateFrame();

	/** @brief Returns the bounding rectangle of the entire virtual screen.
	 *  @return Combined frame of all screens. */
			BRect				Frame() const;

			// TODO: we need to play with a real multi-screen configuration to
			//	figure out the specifics here
	/** @brief Sets the frame of an individual screen within the virtual desktop.
	 *  @param index Zero-based screen index.
	 *  @param frame New frame rectangle. */
			void				SetScreenFrame(int32 index, BRect frame);

	/** @brief Returns the Screen at the given index.
	 *  @param index Zero-based screen index.
	 *  @return Pointer to the Screen, or NULL if out of range. */
			Screen*				ScreenAt(int32 index) const;

	/** @brief Finds the Screen with the given ID.
	 *  @param id Screen identifier.
	 *  @return Pointer to the Screen, or NULL if not found. */
			Screen*				ScreenByID(int32 id) const;

	/** @brief Returns the frame of the Screen at the given index.
	 *  @param index Zero-based screen index.
	 *  @return Frame rectangle of the specified screen. */
			BRect				ScreenFrameAt(int32 index) const;

	/** @brief Returns the total number of physical screens in this virtual screen.
	 *  @return Screen count. */
			int32				CountScreens() const;

private:
			status_t			_GetMode(Screen* screen,
									ScreenConfigurations& configurations,
									display_mode& mode) const;
			void				_Reset();

	/** @brief Internal record associating a Screen with its virtual frame. */
	struct screen_item {
		Screen*	screen;
		BRect	frame;
		// TODO: do we want to have a different color per screen as well?
	};

			BRect				fFrame;
			BObjectList<screen_item, true> fScreenList;
			::DrawingEngine*	fDrawingEngine;
			::HWInterface*		fHWInterface;
};

#endif	/* VIRTUAL_SCREEN_H */
