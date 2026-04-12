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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file MimeTypeList.cpp
 * @brief Asynchronous cache of installed MIME types used by Tracker.
 *
 * ShortMimeInfo stores the internal name and short description of a single
 * MIME type.  MimeTypeList builds two sorted lists — all types and common
 * types only — on a background thread at construction time, locking until
 * the build is complete.
 *
 * @see OpenWithWindow, FindPanel
 */

#include <Mime.h>

#include <strings.h>

#include "AutoLock.h"
#include "MimeTypeList.h"
#include "Thread.h"


/**
 * @brief Construct a ShortMimeInfo from a BMimeType, filtering out apps and unnamed types.
 *
 * A type is considered "common" (visible in open-with menus) only if it has a
 * short description and its preferred handler is not itself (i.e., it is not
 * an application type).
 *
 * @param mimeType  The BMimeType to inspect.
 */
ShortMimeInfo::ShortMimeInfo(const BMimeType& mimeType)
	:
	fCommonMimeType(true)
{
	fPrivateName = mimeType.Type();

	char buffer[B_MIME_TYPE_LENGTH];

	// weed out apps - their preferred handler is themselves
	if (mimeType.GetPreferredApp(buffer) == B_OK
		&& fPrivateName.ICompare(buffer) == 0) {
		fCommonMimeType = false;
	}

	// weed out metamimes without a short description
	if (mimeType.GetShortDescription(buffer) != B_OK || buffer[0] == 0)
		fCommonMimeType = false;
	else
		fShortDescription = buffer;
}


/**
 * @brief Construct a ShortMimeInfo directly from a short-description string.
 *
 * Used as a temporary search key for binary searches on the common-type list.
 *
 * @param shortDescription  The human-readable short description string.
 */
ShortMimeInfo::ShortMimeInfo(const char* shortDescription)
	:
	fShortDescription(shortDescription),
	fCommonMimeType(true)
{
}


/**
 * @brief Return the internal (MIME type string) name.
 *
 * @return Pointer to the null-terminated type string, e.g. "text/plain".
 */
const char*
ShortMimeInfo::InternalName() const
{
	return fPrivateName.String();
}


/**
 * @brief Return the human-readable short description for this MIME type.
 *
 * @return Pointer to the null-terminated short description string.
 */
const char*
ShortMimeInfo::ShortDescription() const
{
	return fShortDescription.String();
}


/**
 * @brief Case-insensitive comparison by short description, for sorting.
 *
 * @param a  First ShortMimeInfo.
 * @param b  Second ShortMimeInfo.
 * @return Negative, zero, or positive per strcasecmp() semantics.
 */
int
ShortMimeInfo::CompareShortDescription(const ShortMimeInfo* a,
	const ShortMimeInfo* b)
{
	return a->fShortDescription.ICompare(b->fShortDescription);
}


/**
 * @brief Return true if this type should appear in the common open-with list.
 *
 * @return True when the type has a short description and is not an application.
 */
bool
ShortMimeInfo::IsCommonMimeType() const
{
	return fCommonMimeType;
}


//	#pragma mark - MimeTypeList


/**
 * @brief Construct the MimeTypeList and start the background Build() thread.
 *
 * The lock is taken immediately; it is released when Build() finishes so
 * callers that acquire the lock will block until the list is ready.
 */
MimeTypeList::MimeTypeList()
	:
	fMimeList(100),
	fCommonMimeList(30),
	fLock("mimeListLock")
{
	fLock.Lock();
	Thread::Launch(NewMemberFunctionObject(&MimeTypeList::Build, this),
		B_NORMAL_PRIORITY);
}


/**
 * @brief Comparison function for binary search by short description.
 *
 * @param a  Key ShortMimeInfo.
 * @param b  List element ShortMimeInfo.
 * @return Result of strcasecmp on the two short descriptions.
 */
static int
MatchOneShortDescription(const ShortMimeInfo* a, const ShortMimeInfo* b)
{
	return strcasecmp(a->ShortDescription(), b->ShortDescription());
}


/**
 * @brief Find a MIME type in the common list by its short description.
 *
 * Performs a case-insensitive binary search on the sorted common-type list.
 *
 * @param shortDescription  The human-readable description to search for.
 * @return Pointer to the matching ShortMimeInfo, or NULL if not found.
 */
const ShortMimeInfo*
MimeTypeList::FindMimeType(const char* shortDescription) const
{
	ShortMimeInfo tmp(shortDescription);
	const ShortMimeInfo* result = fCommonMimeList.BinarySearch(tmp,
		&MatchOneShortDescription);

	return result;
}


/**
 * @brief Iterate over every common MIME type, calling a callback for each.
 *
 * Acquires the list lock before iterating so callers do not need to worry
 * about the background build thread.  Stops early if the callback returns
 * true.
 *
 * @param func   Callback; return true to stop iteration.
 * @param state  Opaque data passed through to @p func.
 * @return The first ShortMimeInfo for which @p func returned true, or NULL.
 */
const ShortMimeInfo*
MimeTypeList::EachCommonType(bool (*func)(const ShortMimeInfo*, void*),
	void* state) const
{
	AutoLock<Benaphore> locker(fLock);
	int32 count = fCommonMimeList.CountItems();
	for (int32 index = 0; index < count; index++) {
		if ((func)(fCommonMimeList.ItemAt(index), state))
			return fCommonMimeList.ItemAt(index);
	}

	return NULL;
}


/**
 * @brief Background thread body: enumerate installed MIME types and populate both lists.
 *
 * Calls BMimeType::GetInstalledTypes(), creates a ShortMimeInfo for each,
 * adds common types to fCommonMimeList, sorts that list by short description,
 * then unlocks fLock to signal that the lists are ready.
 */
void
MimeTypeList::Build()
{
	ASSERT(fLock.IsLocked());

	BMessage message;
	BMimeType::GetInstalledTypes(&message);

	int32 count;
	uint32 type;
	message.GetInfo("types", &type, &count);

	for (int32 index = 0; index < count; index++) {
		const char* str;
		if (message.FindString("types", index, &str) != B_OK)
			continue;

		BMimeType mimetype(str);
		if (mimetype.InitCheck() != B_OK)
			continue;

		ShortMimeInfo* mimeInfo = new ShortMimeInfo(mimetype);
		fMimeList.AddItem(mimeInfo);
		if (mimeInfo->IsCommonMimeType())
			fCommonMimeList.AddItem(mimeInfo);
	}

	fCommonMimeList.SortItems(&ShortMimeInfo::CompareShortDescription);
	fLock.Unlock();
}
