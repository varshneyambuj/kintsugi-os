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
   Clipboard.h
 */

/** @file Clipboard.h
 *  @brief Server-side representation of a named clipboard with data and watcher management. */
#ifndef CLIPBOARD_H
#define CLIPBOARD_H

#include <String.h>
#include <Message.h>
#include <Messenger.h>
#include "WatchingService.h"

/** @brief Stores clipboard data with its source and notifies registered watchers on changes. */
class Clipboard {
public:
	Clipboard(const char *name);
	~Clipboard();

	/** @brief Replaces the clipboard data and records the data source. */
	void SetData(const BMessage *data, BMessenger dataSource);

	/** @brief Returns a pointer to the current clipboard data message. */
	const BMessage *Data() const;
	/** @brief Returns the messenger of the last data source. */
	BMessenger DataSource() const;
	/** @brief Returns the clipboard modification count. */
	int32 Count() const;

	/** @brief Registers a messenger to receive clipboard change notifications. */
	bool AddWatcher(BMessenger watcher);
	/** @brief Unregisters a watcher from clipboard change notifications. */
	bool RemoveWatcher(BMessenger watcher);
	/** @brief Sends change notifications to all registered watchers. */
	void NotifyWatchers();

private:
	BString			fName;	/**< Name identifying this clipboard instance. */
	BMessage		fData;	/**< Current clipboard content as a BMessage. */
	BMessenger		fDataSource;	/**< Messenger of the application that last set the data. */
	int32			fCount;	/**< Monotonically increasing modification counter. */
	WatchingService	fWatchingService;	/**< Service managing the set of clipboard watchers. */
};

#endif	// CLIPBOARD_H
