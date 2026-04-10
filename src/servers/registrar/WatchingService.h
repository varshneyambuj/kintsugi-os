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
   File Name:		WatchingService.h
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	Features everything needed to provide a watching service.
 */

/** @file WatchingService.h
 *  @brief Service that manages a collection of Watchers and dispatches filtered notifications. */
#ifndef WATCHING_SERVICE_H
#define WATCHING_SERVICE_H

#include <map>

#include <Messenger.h>

class Watcher;
class WatcherFilter;

/** @brief Maintains a map of Watchers and notifies them with optional filtering. */
class WatchingService {
public:
	WatchingService();
	virtual ~WatchingService();

	/** @brief Registers a watcher (by object or target messenger) for notifications. */
	bool AddWatcher(Watcher *watcher);
	/** @brief Registers a watcher (by object or target messenger) for notifications. */
	bool AddWatcher(const BMessenger &target);
	/** @brief Unregisters a watcher, optionally deleting it. */
	bool RemoveWatcher(Watcher *watcher, bool deleteWatcher = true);
	/** @brief Unregisters a watcher, optionally deleting it. */
	bool RemoveWatcher(const BMessenger &target, bool deleteWatcher = true);

	/** @brief Sends a message to all watchers that pass the optional filter. */
	void NotifyWatchers(BMessage *message, WatcherFilter *filter = NULL);

private:
	typedef std::map<BMessenger,Watcher*> watcher_map;

private:
	watcher_map	fWatchers;
};

#endif	// WATCHING_SERVICE_H
