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
   File Name:		RecentApps.h
   Author:			Tyler Dauwalder (tyler@dauwalder.net)
   Description:	Recently launched apps list
 */

/** @file RecentApps.h
 *  @brief Maintains a bounded list of recently launched application signatures. */

#ifndef RECENT_APPS_H
#define RECENT_APPS_H

#include <SupportDefs.h>

#include <list>
#include <stdio.h>
#include <string>

class BMessage;
class TRoster;
struct entry_ref;


/** @brief Tracks recently launched applications by signature and supports saving to disk. */
class RecentApps {
public:
	RecentApps();
	~RecentApps();

	/** @brief Records an application signature or entry_ref as recently launched. */
	status_t Add(const char *appSig, int32 appFlags = kQualifyingAppFlags);
	/** @brief Records an application signature or entry_ref as recently launched. */
	status_t Add(const entry_ref *ref, int32 appFlags);
	/** @brief Retrieves up to maxCount recent application entries into a BMessage. */
	status_t Get(int32 maxCount, BMessage *list);
	/** @brief Removes all entries from the recent apps list. */
	status_t Clear();
	/** @brief Prints the recent apps list to standard output for debugging. */
	status_t Print();
	/** @brief Writes the recent apps list to the given file. */
	status_t Save(FILE* file);

private:
	static const int32 kMaxRecentApps = 100;
	static const int32 kQualifyingAppFlags = 0;

private:
	friend class TRoster;
		// For loading from disk

	static status_t GetRefForApp(const char *appSig, entry_ref *result);

	std::list<std::string> fAppList;
};

#endif	// RECENT_APPS_H
