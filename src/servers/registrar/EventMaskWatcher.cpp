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
   Copyright (c) 2001-2002, Haiku
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   File Name:		EventMaskWatcher.cpp
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	EventMaskWatcher is a Watcher extended by an event mask.
   EventMaskWatcherFilter filters EventMaskWatchers with
   respect to their event mask and a specific event.
 */

/** @file EventMaskWatcher.cpp
 *  @brief Implements event-mask-filtered watchers for the watching service. */
#include "EventMaskWatcher.h"

/**
 * @brief Creates a new EventMaskWatcher with a given target and event mask.
 *
 * Each set bit in the event mask specifies an event the watcher wants to
 * receive notifications about.
 *
 * @param target    The watcher's message target.
 * @param eventMask The watcher's event mask.
 */
EventMaskWatcher::EventMaskWatcher(const BMessenger &target, uint32 eventMask)
	: Watcher(target),
	  fEventMask(eventMask)
{
}

/**
 * @brief Returns the watcher's event mask.
 *
 * @return The bitmask of events this watcher is interested in.
 */
uint32
EventMaskWatcher::EventMask() const
{
	return fEventMask;
}


/**
 * @brief Creates a new EventMaskWatcherFilter for the specified event(s).
 *
 * The @a event parameter may contain multiple events as a bitmask. Only
 * watchers whose event mask overlaps with this value will pass the filter.
 *
 * @param event The event bitmask to filter on.
 */
EventMaskWatcherFilter::EventMaskWatcherFilter(uint32 event)
	: WatcherFilter(),
	  fEvent(event)
{
}

/**
 * @brief Tests whether the watcher's event mask includes this filter's event.
 *
 * Returns @c true if the watcher is an EventMaskWatcher and the bitwise AND
 * of its event mask and this filter's event is non-zero.
 *
 * @param watcher The watcher to test.
 * @param message The notification message (unused by this filter).
 * @return @c true if the watcher matches, @c false otherwise.
 */
bool
EventMaskWatcherFilter::Filter(Watcher *watcher, BMessage *message)
{
	EventMaskWatcher *emWatcher = dynamic_cast<EventMaskWatcher *>(watcher);
	return (emWatcher && (emWatcher->EventMask() & fEvent));
}

