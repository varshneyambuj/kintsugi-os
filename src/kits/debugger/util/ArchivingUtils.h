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
 * MIT License. Copyright 2009, Haiku.
 * Original authors: Ingo Weinhold.
 */

/** @file ArchivingUtils.h
    @brief Static helpers for archiving and unarchiving nested BArchivable objects. */

#ifndef ARCHIVABLE_UTILS_H
#define ARCHIVABLE_UTILS_H


#include <Archivable.h>


/** @brief Convenience helpers for embedding child BArchivable objects in a parent BMessage. */
class ArchivingUtils {
public:
	template<typename ObjectType>
	static	ObjectType*			CastOrDelete(BArchivable* archivable);

	template<typename ObjectType>
	static	ObjectType*			Unarchive(const BMessage& archive);

	static	status_t			ArchiveChild(BArchivable* object,
									BMessage& parentArchive,
									const char* fieldName);
	static	BArchivable*		UnarchiveChild(const BMessage& parentArchive,
									const char* fieldName, int32 index = 0);

	template<typename ObjectType>
	static	ObjectType*			UnarchiveChild(const BMessage& archive,
									const char* fieldName, int32 index = 0);
};


template<typename ObjectType>
/*static*/ ObjectType*
ArchivingUtils::CastOrDelete(BArchivable* archivable)
{
	if (archivable == NULL)
		return NULL;

	ObjectType* object = dynamic_cast<ObjectType*>(archivable);
	if (object == NULL)
		delete archivable;

	return object;
}


template<typename ObjectType>
/*static*/ ObjectType*
ArchivingUtils::Unarchive(const BMessage& archive)
{
	return CastOrDelete<ObjectType>(instantiate_object(
		const_cast<BMessage*>(&archive)));
}


template<typename ObjectType>
/*static*/ ObjectType*
ArchivingUtils::UnarchiveChild(const BMessage& archive, const char* fieldName,
	int32 index)
{
	return CastOrDelete<ObjectType>(UnarchiveChild(archive, fieldName, index));
}



#endif	// ARCHIVABLE_UTILS_H
