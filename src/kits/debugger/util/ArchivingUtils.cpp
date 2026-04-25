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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */


/** @file ArchivingUtils.cpp
    @brief Helpers for embedding child BArchivable objects inside a parent BMessage. */


#include "ArchivingUtils.h"

#include <Message.h>


/**
 * @brief Archive @a object into a sub-message of @a parentArchive under @a fieldName.
 *
 * @param object         Object to archive (deep). Must not be NULL.
 * @param parentArchive  Parent message that receives the nested archive.
 * @param fieldName      Field name under which the child archive is stored.
 * @retval B_OK         Child archive added to the parent.
 * @retval B_BAD_VALUE  @a object is NULL.
 * @return Otherwise the status from BArchivable::Archive() or BMessage::AddMessage().
 */
/*static*/ status_t
ArchivingUtils::ArchiveChild(BArchivable* object, BMessage& parentArchive,
	const char* fieldName)
{
	if (object == NULL)
		return B_BAD_VALUE;

	BMessage archive;
	status_t error = object->Archive(&archive, true);
	if (error != B_OK)
		return error;

	return parentArchive.AddMessage(fieldName, &archive);
}


/**
 * @brief Look up a nested archive in @a parentArchive and instantiate it.
 *
 * @param parentArchive  Parent message containing the nested archive.
 * @param fieldName      Field name under which the child archive is stored.
 * @param index          Zero-based index when several archives share @a fieldName.
 * @return The newly instantiated BArchivable, or NULL if no matching archive is
 *         found or instantiation fails. Ownership is transferred to the caller.
 */
/*static*/ BArchivable*
ArchivingUtils::UnarchiveChild(const BMessage& parentArchive,
	const char* fieldName, int32 index)
{
	BMessage archive;
	if (parentArchive.FindMessage(fieldName, index, &archive) != B_OK)
		return NULL;

	return instantiate_object(&archive);
}
