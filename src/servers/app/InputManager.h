/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2005, Haiku, Inc. All Rights Reserved.
 * Original author: Axel Dörfler, axeld@pinc-software.de.
 */

/** @file InputManager.h
    @brief Pool-based manager for EventStream objects used by the app server. */

#ifndef INPUT_MANAGER_H
#define INPUT_MANAGER_H


#include <Locker.h>
#include <ObjectList.h>


class EventStream;

/** @brief Manages a pool of EventStream objects, allowing streams to be checked
           out for use and returned when done, with automatic screen-bounds broadcasting. */
class InputManager : public BLocker {
	public:
		InputManager();
		virtual ~InputManager();

		/** @brief Propagates new screen bounds to all managed EventStreams.
		    @param bounds New screen bounding rectangle. */
		void UpdateScreenBounds(BRect bounds);

		/** @brief Adds an EventStream to the pool of available streams.
		    @param stream The EventStream to add; InputManager takes ownership.
		    @return true on success. */
		bool AddStream(EventStream* stream);

		/** @brief Removes and deletes an EventStream from the manager.
		    @param stream The EventStream to remove. */
		void RemoveStream(EventStream* stream);

		/** @brief Checks out an EventStream for exclusive use by the caller.
		    @return Pointer to a free EventStream, or NULL if none are available. */
		EventStream* GetStream();

		/** @brief Returns a previously checked-out EventStream to the free pool.
		    @param stream The EventStream to return. */
		void PutStream(EventStream* stream);

	private:
		BObjectList<EventStream, true> fFreeStreams;
		BObjectList<EventStream, true> fUsedStreams;
};

/** @brief Global InputManager instance used by the app server. */
extern InputManager* gInputManager;

#endif	/* INPUT_MANAGER_H */
