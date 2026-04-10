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
   Copyright 2002-2013, Haiku, Inc.
   Distributed under the terms of the MIT License.

   Authors:
   Tyler Dauwalder
   Axel Dörfler, axeld@pinc-software.de
   Ingo Weinhold, ingo_weinhold@gmx.de
 */

/** @file CreateAppMetaMimeThread.cpp
 *  @brief Implements the directory-scanning thread for creating application meta-MIME entries. */
#include "CreateAppMetaMimeThread.h"


namespace BPrivate {
namespace Storage {
namespace Mime {


CreateAppMetaMimeThread::CreateAppMetaMimeThread(const char* name,
	int32 priority, Database* database,
	MimeEntryProcessor::DatabaseLocker* databaseLocker,
	BMessenger managerMessenger, const entry_ref* root, bool recursive,
	int32 force, BMessage* replyee)
	:
	MimeUpdateThread(name, priority, database, managerMessenger, root,
		recursive, force, replyee),
	fCreator(database, databaseLocker, force)
{
}


status_t
CreateAppMetaMimeThread::DoMimeUpdate(const entry_ref* ref, bool* _entryIsDir)
{
	if (ref == NULL)
		return B_BAD_VALUE;

	return fCreator.Do(*ref, _entryIsDir);
}


}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate

