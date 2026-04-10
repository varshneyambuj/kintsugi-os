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
   File Name:		AppInfoList.h
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	A helper class for TRoster. A list of RosterAppInfos.
 */

/** @file AppInfoList.h
 *  @brief Ordered collection of RosterAppInfo entries supporting lookup by signature, team, ref, and token. */
#ifndef APP_INFO_LIST_H
#define APP_INFO_LIST_H

#include <List.h>
#include <OS.h>

struct entry_ref;

struct RosterAppInfo;

// AppInfoList
/** @brief Maintains a searchable and sortable list of registered application info records. */
class AppInfoList {
public:
	class Iterator;

public:
	AppInfoList();
	virtual ~AppInfoList();

	/** @brief Adds a RosterAppInfo entry to the list. */
	bool AddInfo(RosterAppInfo *info);
	/** @brief Removes the given RosterAppInfo entry from the list. */
	bool RemoveInfo(RosterAppInfo *info);
	/** @brief Removes all entries, optionally deleting them. */
	void MakeEmpty(bool deleteInfos = false);

	/** @brief Looks up an app info entry by signature, team, or entry_ref. */
	RosterAppInfo *InfoFor(const char *signature) const;
	/** @brief Looks up an app info entry by signature, team, or entry_ref. */
	RosterAppInfo *InfoFor(team_id team) const;
	/** @brief Looks up an app info entry by signature, team, or entry_ref. */
	RosterAppInfo *InfoFor(const entry_ref *ref) const;
	/** @brief Looks up an app info entry by its unique pre-registration token. */
	RosterAppInfo *InfoForToken(uint32 token) const;

	bool IsEmpty() const		{ return (CountInfos() == 0); };
	/** @brief Returns the number of entries currently in the list. */
	int32 CountInfos() const;

	/** @brief Returns an iterator over the list entries. */
	Iterator It();

	/** @brief Sorts the list using a caller-supplied comparison function. */
	void Sort(bool (*lessFunc)(const RosterAppInfo *, const RosterAppInfo *));

private:
	/** @brief Removes the given RosterAppInfo entry from the list. */
	RosterAppInfo *RemoveInfo(int32 index);

	RosterAppInfo *InfoAt(int32 index) const;

	int32 IndexOf(RosterAppInfo *info) const;
	int32 IndexOf(const char *signature) const;
	int32 IndexOf(team_id team) const;
	int32 IndexOf(const entry_ref *ref) const;
	int32 IndexOfToken(uint32 token) const;

private:
	friend class Iterator;

private:
	BList	fInfos;
};

// AppInfoList::Iterator
/** @brief Maintains a searchable and sortable list of registered application info records. */
class AppInfoList::Iterator {
public:
	inline Iterator(const Iterator &it)
		: fList(it.fList),
		  fIndex(it.fIndex),
		  fCount(it.fCount)
	{
	}

	inline ~Iterator() {}

	inline bool IsValid() const
	{
		return (fIndex >= 0 && fIndex < fCount);
	}

	inline RosterAppInfo *Remove()
	{
		RosterAppInfo *info = fList->RemoveInfo(fIndex);
		if (info)
			fCount--;
		return info;
	}

	inline Iterator &operator=(const Iterator &it)
	{
		fList = it.fList;
		fIndex = it.fIndex;
		fCount = it.fCount;
		return *this;
	}

	inline Iterator &operator++()
	{
		fIndex++;
		return *this;
	}

	inline Iterator operator++(int)
	{
		return Iterator(fList, fIndex + 1);
	}

	inline Iterator &operator--()
	{
		fIndex--;
		return *this;
	}

	inline Iterator operator--(int)
	{
		return Iterator(fList, fIndex - 1);
	}

	inline bool operator==(const Iterator &it) const
	{
		return (fList == it.fList && fIndex == it.fIndex);
	}

	inline bool operator!=(const Iterator &it) const
	{
		return !(*this == it);
	}

	inline RosterAppInfo *operator*() const
	{
		return fList->InfoAt(fIndex);
	}

private:
	friend class AppInfoList;

private:
	inline Iterator(AppInfoList *list, int32 index = 0)
		: fList(list),
		  fIndex(index),
		  fCount(list->CountInfos())
	{
	}

private:
	AppInfoList	*fList;
	int32		fIndex;
	int32		fCount;
};

#endif	// APP_INFO_LIST_H
