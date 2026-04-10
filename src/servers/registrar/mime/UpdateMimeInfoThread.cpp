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
   Copyright 2002-2013, Haiku, Inc. All Rights Reserved.
   Distributed under the terms of the MIT License.

   Authors:
   Tyler Dauwalder
   Jonas Sundström, jonas@kirilla.com
   Michael Lotz, mmlr@mlotz.ch
   Ingo Weinhold, ingo_weinhold@gmx.de
 */

/** @file UpdateMimeInfoThread.cpp
 *  @brief Implements the directory-scanning thread for updating MIME type information. */

#include "UpdateMimeInfoThread.h"


namespace BPrivate {
namespace Storage {
namespace Mime {


/**
 * @brief Constructs a new thread for updating MIME type information.
 *
 * Initializes the base MimeUpdateThread and the internal MimeInfoUpdater
 * that will sniff and update each discovered entry.
 *
 * @param name              Thread name.
 * @param priority          Thread scheduling priority.
 * @param database          The MIME database to update.
 * @param databaseLocker    Locker used to serialize database access.
 * @param managerMessenger  Messenger for communicating with the thread manager.
 * @param root              Root entry_ref of the directory tree to scan.
 * @param recursive         Whether to recurse into subdirectories.
 * @param force             If non-zero, forces updates even for entries that already have types.
 * @param replyee           Optional message to reply to when the operation completes.
 */
UpdateMimeInfoThread::UpdateMimeInfoThread(const char* name, int32 priority,
	Database* database, MimeEntryProcessor::DatabaseLocker* databaseLocker,
	BMessenger managerMessenger, const entry_ref* root, bool recursive,
	int32 force, BMessage* replyee)
	:
	MimeUpdateThread(name, priority, database, managerMessenger, root,
		recursive, force, replyee),
	fUpdater(database, databaseLocker, force)
{
}


/**
 * @brief Performs a MIME type update on the given entry.
 *
 * If the entry has no BEOS:TYPE attribute, or if the force flag is set,
 * the entry is sniffed and its BEOS:TYPE attribute is set accordingly.
 *
 * @param entry       The entry to update.
 * @param _entryIsDir Output flag set to @c true if the entry is a directory.
 * @return @c B_OK on success, @c B_BAD_VALUE if @a entry is NULL, or another
 *         error code on failure.
 */
status_t
UpdateMimeInfoThread::DoMimeUpdate(const entry_ref* entry, bool* _entryIsDir)
{
	if (entry == NULL)
		return B_BAD_VALUE;

	return fUpdater.Do(*entry, _entryIsDir);
}


}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate

