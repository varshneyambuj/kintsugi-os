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
 * Copyright 2005, Haiku.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ScreenManager.h
 *  @brief Manages enumeration, acquisition, and release of physical screens. */

#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H


#include <AutoDeleter.h>
#include <Looper.h>
#include <ObjectList.h>


class BMessage;

class DrawingEngine;
class HWInterface;
class HWInterfaceListener;
class Screen;


/** @brief Alias for a list of Screen pointers. */
typedef BObjectList<Screen> ScreenList;


/** @brief Interface that receives screen lifecycle notifications from ScreenManager. */
class ScreenOwner {
	public:
		virtual ~ScreenOwner() {};

		/** @brief Called when a screen has been removed from the system.
		 *  @param screen The removed Screen. */
		virtual void	ScreenRemoved(Screen* screen) = 0;

		/** @brief Called when a new screen has been added to the system.
		 *  @param screen The newly added Screen. */
		virtual void	ScreenAdded(Screen* screen) = 0;

		/** @brief Called when a screen's configuration has changed.
		 *  @param screen The changed Screen. */
		virtual void	ScreenChanged(Screen* screen) = 0;

		/** @brief Requests the owner to release a previously acquired screen.
		 *  @param screen The Screen to release.
		 *  @return true if the screen was released successfully. */
		virtual bool	ReleaseScreen(Screen* screen) = 0;
};


/** @brief BLooper-based manager that scans hardware and dispatches screens to owners. */
class ScreenManager : public BLooper {
	public:
		ScreenManager();
		virtual ~ScreenManager();

		/** @brief Returns the Screen at the given index in the internal list.
		 *  @param index Zero-based index.
		 *  @return Pointer to the Screen, or NULL if out of range. */
		Screen*			ScreenAt(int32 index) const;

		/** @brief Returns the total number of detected screens.
		 *  @return Screen count. */
		int32			CountScreens() const;

		/** @brief Acquires a set of screens for the given owner.
		 *  @param owner     Object that will receive screen events.
		 *  @param wishList  Array of preferred screen IDs (may be NULL).
		 *  @param wishCount Number of entries in wishList.
		 *  @param target    Target name for display selection hints.
		 *  @param force     If true, steal screens from existing owners.
		 *  @param list      Receives the acquired Screen pointers.
		 *  @return B_OK on success, an error code otherwise. */
		status_t		AcquireScreens(ScreenOwner* owner, int32* wishList,
							int32 wishCount, const char* target, bool force,
							ScreenList& list);

		/** @brief Releases all screens in the given list back to the pool.
		 *  @param list List of Screen pointers to release. */
		void			ReleaseScreens(ScreenList& list);

		/** @brief Notifies the manager that a screen's configuration has changed.
		 *  @param screen The Screen whose configuration changed. */
		void			ScreenChanged(Screen* screen);

		/** @brief Handles asynchronous BMessages delivered to this looper.
		 *  @param message Incoming message. */
		virtual void	MessageReceived(BMessage* message);

	private:
		/** @brief Internal record pairing a Screen with its owner and listener. */
		struct screen_item {
			ObjectDeleter<Screen>	screen;
			ScreenOwner*			owner;
			ObjectDeleter<HWInterfaceListener>
									listener;
		};

		void			_ScanDrivers();
		screen_item*	_AddHWInterface(HWInterface* interface);

		BObjectList<screen_item, true>	fScreenList;
};

/** @brief Global singleton ScreenManager instance. */
extern ScreenManager *gScreenManager;

#endif	/* SCREEN_MANAGER_H */
