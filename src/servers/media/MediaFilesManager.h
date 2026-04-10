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
 *   Copyright 2003, Jérôme Duval. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file MediaFilesManager.h
 *  @brief Manages named media file references (e.g. system sounds) and their persistence. */

#ifndef _MEDIA_FILES_MANAGER_H
#define _MEDIA_FILES_MANAGER_H


#include <map>

#include <Entry.h>
#include <File.h>
#include <Locker.h>
#include <MessageRunner.h>
#include <String.h>

#include "DataExchange.h"


#define MEDIA_FILES_MANAGER_SAVE_TIMER 'mmst'


/** @brief Manages named media file references such as system sound events. */
class MediaFilesManager : BLocker {
public:
	/** @brief Construct the manager and populate default sound entries. */
								MediaFilesManager();
	/** @brief Destroy the manager and release the save timer. */
								~MediaFilesManager();

	/** @brief Persist all media file references to disk. */
			status_t			SaveState();

	/** @brief Dump all registered types and items to stdout. */
			void				Dump();

	/** @brief Create a shared area containing all registered type names. */
			area_id				GetTypesArea(int32& count);
	/** @brief Create a shared area containing all item names for a given type. */
			area_id				GetItemsArea(const char* type, int32& count);

	/** @brief Retrieve the entry_ref for a named media item. */
			status_t			GetRefFor(const char* type, const char* item,
									entry_ref** _ref);
	/** @brief Retrieve the audio gain for a named media item. */
			status_t			GetAudioGainFor(const char* type,
									const char* item, float* _gain);
	/** @brief Set the entry_ref for a named media item. */
			status_t			SetRefFor(const char* type, const char* item,
									const entry_ref& ref);
	/** @brief Set the audio gain for a named media item. */
			status_t			SetAudioGainFor(const char* type,
									const char* item, float gain);
	/** @brief Clear the entry_ref of a named media item. */
			status_t			InvalidateItem(const char* type,
									const char* item);
	/** @brief Remove a named item from the registry. */
			status_t			RemoveItem(const char* type, const char* item);

	/** @brief Handle the deferred save timer expiration. */
			void				TimerMessage();

	/** @brief Register a new system beep event from a BMessage request. */
			void				HandleAddSystemBeepEvent(BMessage* message);

private:
			struct item_info {
				item_info() : gain(1.0f) {}

				entry_ref		ref;
				float			gain;
			};

			void				_LaunchTimer();
			status_t			_GetItem(const char* type, const char* item,
									item_info*& info);
			status_t			_SetItem(const char* type, const char* item,
									const entry_ref* ref = NULL,
									const float* gain = NULL);
			status_t			_OpenSettingsFile(BFile& file, int mode);
			status_t			_LoadState();

private:
			typedef std::map<BString, item_info> ItemMap;
			typedef std::map<BString, ItemMap> TypeMap;

			TypeMap				fMap;
			BMessageRunner*		fSaveTimerRunner;
};

#endif // _MEDIA_FILES_MANAGER_H
