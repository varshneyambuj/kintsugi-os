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
/*!
	\file UpdateMimeInfoThread.h
	UpdateMimeInfoThread implementation
*/


#include "UpdateMimeInfoThread.h"


namespace BPrivate {
namespace Storage {
namespace Mime {


//! Creates a new UpdateMimeInfoThread object
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


/*! \brief Performs an update_mime_info() update on the given entry

	If the entry has no \c BEOS:TYPE attribute, or if \c fForce is true, the
	entry is sniffed and its \c BEOS:TYPE attribute is set accordingly.
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

