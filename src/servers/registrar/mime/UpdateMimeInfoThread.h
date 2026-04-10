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
   This software is part of the Haiku distribution and is covered
   by the MIT License.
 */

/** @file UpdateMimeInfoThread.h
 *  @brief Thread that scans files and updates their MIME type information in the database. */

#ifndef _MIME_UPDATE_MIME_INFO_THREAD_H
#define _MIME_UPDATE_MIME_INFO_THREAD_H


#include <mime/MimeInfoUpdater.h>

#include "MimeUpdateThread.h"


namespace BPrivate {
namespace Storage {
namespace Mime {


/** @brief Traverses a directory tree updating MIME info for each file entry. */
class UpdateMimeInfoThread : public MimeUpdateThread {
public:
								UpdateMimeInfoThread(const char* name,
									int32 priority, Database* database,
									MimeEntryProcessor::DatabaseLocker*
										databaseLocker,
									BMessenger managerMessenger,
									const entry_ref* root, bool recursive,
									int32 force, BMessage* replyee);

	/** @brief Processes a single entry to update its MIME type information. */
	virtual	status_t			DoMimeUpdate(const entry_ref* entry,
									bool* _entryIsDir);

private:
			MimeInfoUpdater		fUpdater;
};


}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate

#endif	// _MIME_UPDATE_MIME_INFO_THREAD_H
