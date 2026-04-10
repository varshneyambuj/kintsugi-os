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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MimeEntryProcessor.cpp
 * @brief Base class for operations that process filesystem entries against the MIME database.
 *
 * MimeEntryProcessor provides shared infrastructure — a Database reference, a
 * DatabaseLocker, and a force flag — used by concrete subclasses such as
 * AppMetaMimeCreator and MimeInfoUpdater.  The DoRecursively() helper allows a
 * single top-level call to descend into directories, invoking Do() on every
 * entry encountered.
 *
 * @see AppMetaMimeCreator
 * @see MimeInfoUpdater
 */


#include <mime/AppMetaMimeCreator.h>

#include <Directory.h>
#include <Entry.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


// #pragma mark - DatabaseLocker


/**
 * @brief Destroys the DatabaseLocker base interface.
 */
MimeEntryProcessor::DatabaseLocker::~DatabaseLocker()
{
}


// #pragma mark - MimeEntryProcessor


/**
 * @brief Constructs a MimeEntryProcessor.
 *
 * @param database       Pointer to the MIME Database instance used by subclasses.
 * @param databaseLocker Pointer to the locker that guards the database.
 * @param force          Force-update bitmask passed to subclass Do() implementations.
 */
MimeEntryProcessor::MimeEntryProcessor(Database* database,
	DatabaseLocker* databaseLocker, int32 force)
	:
	fDatabase(database),
	fDatabaseLocker(databaseLocker),
	fForce(force)
{
}


/**
 * @brief Destroys the MimeEntryProcessor.
 */
MimeEntryProcessor::~MimeEntryProcessor()
{
}


/**
 * @brief Processes an entry and, if it is a directory, recursively processes its children.
 *
 * Calls Do() on \a entry first.  If Do() indicates the entry is a directory,
 * opens it as a BDirectory and calls DoRecursively() on each child entry in turn.
 * Errors returned by child entries are ignored so that processing continues for
 * the remaining siblings.
 *
 * @param entry Reference to the filesystem entry to process.
 * @return B_OK on success, or the error code returned by the initial Do() call.
 */
status_t
MimeEntryProcessor::DoRecursively(const entry_ref& entry)
{
	bool entryIsDir = false;
	status_t error = Do(entry, &entryIsDir);
	if (error != B_OK)
		return error;

	if (entryIsDir) {
		BDirectory directory;
		error = directory.SetTo(&entry);
		if (error != B_OK)
			return error;

		entry_ref childEntry;
		while (directory.GetNextRef(&childEntry) == B_OK)
			DoRecursively(childEntry);
	}

	return B_OK;
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
