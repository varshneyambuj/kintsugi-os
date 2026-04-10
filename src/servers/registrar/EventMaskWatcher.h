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
   File Name:		EventMaskWatcher.h
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	EventMaskWatcher is a Watcher extended by an event mask.
   EventMaskWatcherFilter filters EventMaskWatchers with
   respect to their event mask and a specific event.
 */

/** @file EventMaskWatcher.h
 *  @brief Watcher subclass filtered by an event mask, plus a filter class for mask-based selection. */
#ifndef EVENT_MASK_WATCHER_H
#define EVENT_MASK_WATCHER_H

#include "Watcher.h"

// EventMaskWatcher
/** @brief A Watcher that carries an event mask for selective notification delivery. */
class EventMaskWatcher : public Watcher {
public:
	EventMaskWatcher(const BMessenger &target, uint32 eventMask);

	/** @brief Returns the bitmask of events this watcher is interested in. */
	uint32 EventMask() const;

private:
	uint32	fEventMask;	/**< Bitmask specifying which events this watcher subscribes to. */
};

// EventMaskWatcherFilter
/** @brief A WatcherFilter that accepts only watchers whose mask includes a given event. */
class EventMaskWatcherFilter : public WatcherFilter {
public:
	EventMaskWatcherFilter(uint32 event);

	/** @brief Returns true if the watcher's event mask includes the filter's event. */
	virtual bool Filter(Watcher *watcher, BMessage *message);

private:
	uint32	fEvent;	/**< The specific event type this filter matches against. */
};

#endif	// EVENT_MASK_WATCHER_H
