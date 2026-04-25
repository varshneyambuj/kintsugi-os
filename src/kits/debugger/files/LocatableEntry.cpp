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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/** @file LocatableEntry.cpp
    @brief Common base implementation for locatable filesystem entries. */

#include "LocatableEntry.h"

#include "AutoLocker.h"

#include "LocatableDirectory.h"


// #pragma mark - LocatableEntryOwner


/** @brief Virtual destructor anchor for the LocatableEntryOwner interface. */
LocatableEntryOwner::~LocatableEntryOwner()
{
}


// #pragma mark - LocatableEntry


/**
 * @brief Construct an unlocated entry under @a owner inside @a parent.
 *
 * Acquires a reference on @a parent so the parent outlives the child.
 *
 * @param owner   Domain that mediates locking and lifecycle.
 * @param parent  Parent directory, or NULL for the root.
 */
LocatableEntry::LocatableEntry(LocatableEntryOwner* owner,
	LocatableDirectory* parent)
	:
	fOwner(owner),
	fParent(parent),
	fState(LOCATABLE_ENTRY_UNLOCATED)
{
	if (fParent != NULL)
		fParent->AcquireReference();
}


/** @brief Release the reference on the parent directory. */
LocatableEntry::~LocatableEntry()
{
	if (fParent != NULL)
		fParent->ReleaseReference();
}


/**
 * @brief BReferenceable hook invoked when the last reference is released.
 *
 * Notifies the owning domain that the entry is unused and self-deletes.
 */
void
LocatableEntry::LastReferenceReleased()
{
	fOwner->LocatableEntryUnused(this);
	delete this;
}
