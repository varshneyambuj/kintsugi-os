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

/** @file MimeUpdateThread.h
 *  @brief Abstract thread base class for recursive MIME database update operations. */

#ifndef _MIME_UPDATE_THREAD_H
#define _MIME_UPDATE_THREAD_H

#include <Entry.h>
#include <SupportDefs.h>

#include <list>
#include <utility>

#include "RegistrarThread.h"

struct entry_ref;
class BMessage;

namespace BPrivate {
namespace Storage {
namespace Mime {

class Database;

/** @brief Base thread that walks a directory tree and applies MIME updates to each entry. */
class MimeUpdateThread : public RegistrarThread {
public:
	MimeUpdateThread(const char *name, int32 priority, Database *database,
		BMessenger managerMessenger, const entry_ref *root, bool recursive,
		int32 force, BMessage *replyee);
	virtual ~MimeUpdateThread();
	
	/** @brief Returns the initialization status of the thread. */
	virtual status_t InitCheck();	
	
protected:
	virtual status_t ThreadFunction();
	/** @brief Pure virtual hook called for each entry to perform the MIME update. */
	virtual status_t DoMimeUpdate(const entry_ref *entry, bool *entryIsDir) = 0;

	Database* fDatabase;
	const entry_ref fRoot;
	const bool fRecursive;
	const int32 fForce;
	BMessage *fReplyee;
	
	/** @brief Checks whether a device volume supports file attributes. */
	bool DeviceSupportsAttributes(dev_t device);

private:
	std::list< std::pair<dev_t, bool> > fAttributeSupportList;

	status_t UpdateEntry(const entry_ref *ref);
	
	status_t fStatus;
};

}	// namespace Mime
}	// namespace Storage
}	// namespace BPrivate

#endif	// _MIME_UPDATE_THREAD_H
