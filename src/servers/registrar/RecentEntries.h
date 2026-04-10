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
   File Name:		RecentEntries.h
   Author:			Tyler Dauwalder (tyler@dauwalder.net)
   Description:	Recently used entries list
 */

/** @file RecentEntries.h
 *  @brief Maintains a bounded list of recently used document and folder entry_refs. */

#ifndef RECENT_ENTRIES_H
#define RECENT_ENTRIES_H

#include <Entry.h>
#include <SupportDefs.h>

#include <list>
#include <stdio.h>
#include <string>

class BMessage;
class TRoster;

struct recent_entry {
	recent_entry(const entry_ref *ref, const char *appSig, uint32 index);
	entry_ref ref;
	std::string sig;
	uint32 index;
private:
	recent_entry();
};

/** @brief Tracks recently used file and folder entries, filterable by MIME type and application. */
class RecentEntries {
public:
	RecentEntries();
	~RecentEntries();

	/** @brief Adds an entry_ref with its associated application signature to the list. */
	status_t Add(const entry_ref *ref, const char *appSig);
	/** @brief Retrieves matching recent entries filtered by type and application. */
	status_t Get(int32 maxCount, const char *fileTypes[], int32 fileTypesCount,
	             const char *appSig, BMessage *result);
	/** @brief Removes all entries from the recent entries list. */
	status_t Clear();
	/** @brief Prints the recent entries list to standard output for debugging. */
	status_t Print();
	/** @brief Writes the recent entries list to the given file. */
	status_t Save(FILE* file, const char *description, const char *tag);

private:
	static const int32 kMaxRecentEntries = 100;

private:
	friend class TRoster;

	static status_t GetTypeForRef(const entry_ref *ref, char *result);

	std::list<recent_entry*> fEntryList;	/**< Ordered list of recent_entry pointers. */
};

#endif	// RECENT_FOLDERS_H
