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
   Clipboard.cpp
 */

/** @file Clipboard.cpp
 *  @brief Server-side clipboard representation managing shared data and watchers. */
#include <app/Clipboard.h>

#include "Clipboard.h"

/**
 * @brief Creates and initializes a server-side Clipboard.
 *
 * @param name The name of the clipboard.
 */
Clipboard::Clipboard(const char *name)
	: fName(name),
	  fData(B_SIMPLE_DATA),
	  fDataSource(),
	  fCount(0),
	  fWatchingService()
{
}

/** @brief Frees all resources associated with this Clipboard. */
Clipboard::~Clipboard()
{
}

/**
 * @brief Replaces the clipboard's data and notifies all watchers.
 *
 * Copies the supplied message, records the new data source, increments the
 * modification count, and sends a B_CLIPBOARD_CHANGED notification.
 *
 * @param data       The new clipboard data.
 * @param dataSource The messenger identifying the new data source.
 */
void
Clipboard::SetData(const BMessage *data, BMessenger dataSource)
{
	fData = *data;
	fDataSource = dataSource;
	fCount++;
	NotifyWatchers();
}

/**
 * @brief Returns a pointer to the clipboard's current data message.
 *
 * @return The clipboard's data.
 */
const BMessage *
Clipboard::Data() const
{
	return &fData;
}

/**
 * @brief Returns the messenger identifying the clipboard's data source.
 *
 * @return The data source messenger.
 */
BMessenger
Clipboard::DataSource() const
{
	return fDataSource;
}

/**
 * @brief Returns the number of times the clipboard data has been set.
 *
 * @return The clipboard's modification count.
 */
int32
Clipboard::Count() const
{
	return fCount;
}


/**
 * @brief Registers a new watcher for clipboard change notifications.
 *
 * @param watcher The messenger referring to the new watcher.
 * @return @c true if the watcher was added successfully, @c false otherwise.
 */
bool
Clipboard::AddWatcher(BMessenger watcher)
{
	return fWatchingService.AddWatcher(watcher);
}

/**
 * @brief Removes an existing watcher from this clipboard.
 *
 * @param watcher The watcher to be removed.
 * @return @c true if the watcher was found and removed, @c false if it was
 *         not watching this clipboard.
 */
bool
Clipboard::RemoveWatcher(BMessenger watcher)
{
	return fWatchingService.RemoveWatcher(watcher);
}

/**
 * @brief Sends a B_CLIPBOARD_CHANGED notification to all registered watchers.
 */
void
Clipboard::NotifyWatchers()
{
	BMessage message(B_CLIPBOARD_CHANGED);
	fWatchingService.NotifyWatchers(&message, NULL);
}

