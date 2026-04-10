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
   File Name:		Watcher.h
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	A Watcher represents a target of a watching service.
   A WatcherFilter represents a predicate on Watchers.
 */

/** @file Watcher.h
 *  @brief Base classes for notification targets and predicate-based watcher filtering. */
#ifndef WATCHER_H
#define WATCHER_H

#include <Messenger.h>

// Watcher
/** @brief Represents a single notification target identified by a BMessenger. */
class Watcher {
public:
	Watcher(const BMessenger &target);
	virtual ~Watcher();

	/** @brief Returns a reference to the watcher's target messenger. */
	const BMessenger &Target() const;

	/** @brief Delivers a notification message to this watcher's target. */
	virtual status_t SendMessage(BMessage *message);

private:
	BMessenger	fTarget;	/**< The BMessenger identifying where to send notifications. */
};

// WatcherFilter
/** @brief Predicate base class for filtering which watchers receive a notification. */
class WatcherFilter {
public:
	WatcherFilter();
	virtual ~WatcherFilter();

	/** @brief Returns whether the given watcher should receive the message. */
	virtual bool Filter(Watcher *watcher, BMessage *message);
};

#endif	// WATCHER_H
