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
 *   Copyright 2005, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file InputManager.cpp
 *  @brief Manages a pool of EventStream objects for the application server. */

// TODO: introduce means to define event stream features (like local vs. net)
// TODO: introduce the possibility to identify a stream by a unique name


#include "EventStream.h"
#include "InputManager.h"

#include <Autolock.h>


InputManager* gInputManager;
	// the global input manager will be created by the AppServer


/**
 * @brief Constructs an InputManager with empty free and used stream lists.
 */
InputManager::InputManager()
	: BLocker("input manager"),
	fFreeStreams(2),
	fUsedStreams(2)
{
}


/**
 * @brief Destroys the InputManager.
 */
InputManager::~InputManager()
{
}


/**
 * @brief Adds an EventStream to the pool of free (available) streams.
 * @param stream The EventStream to add.
 * @return true if the stream was added successfully, false otherwise.
 */
bool
InputManager::AddStream(EventStream* stream)
{
	BAutolock _(this);
	return fFreeStreams.AddItem(stream);
}


/**
 * @brief Removes an EventStream from the pool of free streams.
 * @param stream The EventStream to remove.
 */
void
InputManager::RemoveStream(EventStream* stream)
{
	BAutolock _(this);
	fFreeStreams.RemoveItem(stream);
}


/**
 * @brief Retrieves and removes a valid EventStream from the free pool.
 *
 * Invalid streams encountered during the search are deleted. The returned
 * stream is moved to the used-streams list.
 *
 * @return A valid EventStream, or NULL if no valid stream is available.
 */
EventStream*
InputManager::GetStream()
{
	BAutolock _(this);

	EventStream* stream = NULL;
	do {
		delete stream;
			// this deletes the previous invalid stream

		stream = fFreeStreams.RemoveItemAt(0);
	} while (stream != NULL && !stream->IsValid());

	if (stream == NULL)
		return NULL;

	fUsedStreams.AddItem(stream);
	return stream;
}


/**
 * @brief Returns an EventStream to the pool.
 *
 * If the stream is still valid it is moved back to the free pool; otherwise
 * it is deleted.
 *
 * @param stream The EventStream to return (NULL is silently ignored).
 */
void
InputManager::PutStream(EventStream* stream)
{
	if (stream == NULL)
		return;

	BAutolock _(this);

	fUsedStreams.RemoveItem(stream, false);
	if (stream->IsValid())
		fFreeStreams.AddItem(stream);
	else
		delete stream;
}


/**
 * @brief Notifies all managed streams of updated screen bounds.
 * @param bounds The new screen bounding rectangle.
 */
void
InputManager::UpdateScreenBounds(BRect bounds)
{
	BAutolock _(this);

	for (int32 i = fUsedStreams.CountItems(); i-- > 0;) {
		fUsedStreams.ItemAt(i)->UpdateScreenBounds(bounds);
	}

	for (int32 i = fFreeStreams.CountItems(); i-- > 0;) {
		fFreeStreams.ItemAt(i)->UpdateScreenBounds(bounds);
	}
}
