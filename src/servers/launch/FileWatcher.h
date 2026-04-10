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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2025, Jérôme Duval, jerome.duval@gmail.com.
 *   Distributed under the terms of the MIT License.
 */

/** @file FileWatcher.h
 *  @brief Node-monitor based watcher used by file_created event triggers. */

#ifndef FILE_WATCHER_H
#define FILE_WATCHER_H


#include <Handler.h>
#include <ObjectList.h>


/** @brief Callback interface for FileWatcher subscribers. */
class FileListener {
public:
	virtual						~FileListener();

	/** @brief Invoked when a file watched on the listener's behalf is created. */
	virtual	void				FileCreated(const char* path) = 0;
};


/** @brief BHandler that turns kernel node-monitor messages into FileListener callbacks. */
class FileWatcher : public BHandler {
public:
								FileWatcher();
	virtual						~FileWatcher();

	/** @brief Adds @p listener to the notification set. */
			void				AddListener(FileListener* listener);
	/** @brief Removes @p listener from the notification set. */
			void				RemoveListener(FileListener* listener);
	/** @brief Returns the number of subscribers. */
			int32				CountListeners() const;

	/** @brief Dispatches kernel node-monitor messages to listeners. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Begins watching @p path on @p listener's behalf. */
	static	status_t			Register(FileListener* listener, BPath& path);
	/** @brief Stops watching @p path on @p listener's behalf. */
	static	void				Unregister(FileListener* listener, BPath& path);

protected:
			BObjectList<FileListener>
								fListeners;  /**< Subscribed listeners (not owned). */
};


#endif // FILE_WATCHER_H
