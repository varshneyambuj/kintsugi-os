/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2004-2009, Haiku, Inc. All rights reserved.
 * Original authors: Axel Dörfler, Marcus Overhagen.
 */

/** @file FormatManager.h
    @brief Singleton manager for registering and querying media formats. */

#ifndef _FORMAT_MANAGER_H
#define _FORMAT_MANAGER_H


#include <Locker.h>
#include <ObjectList.h>
#include <pthread.h>

#include "MetaFormat.h"


/** @brief Singleton that maintains the registry of all known media formats
           and assigns unique codec IDs to newly registered formats. */
class FormatManager {
public:
								~FormatManager();

			/** @brief Populates a reply message with all formats updated since lastUpdate.
			    @param lastUpdate Timestamp of the last successful query (microseconds).
			    @param reply BMessage to be filled with format data. */
			void				GetFormats(bigtime_t lastUpdate, BMessage& reply);

			/** @brief Registers or retrieves a media_format matching the given descriptions.
			    @param descriptions Array of media_format_description structs.
			    @param descriptionCount Number of elements in the descriptions array.
			    @param format In/out media_format; filled with the registered format on success.
			    @param flags Registration flags.
			    @param _reserved Reserved for future use; pass NULL.
			    @return B_OK on success, or an error code. */
			status_t			MakeFormatFor(
									const media_format_description* descriptions,
									int32 descriptionCount,
									media_format& format, uint32 flags,
									void* _reserved);

			/** @brief Removes a previously registered media format from the registry.
			    @param format The media_format to deregister. */
			void				RemoveFormat(const media_format& format);

			/** @brief Returns the singleton FormatManager instance, creating it if needed.
			    @return Pointer to the global FormatManager. */
			static FormatManager* GetInstance();

private:
								FormatManager();
			/** @brief One-time initialisation function used with pthread_once. */
			static void			CreateInstance();
private:
	typedef BPrivate::media::meta_format meta_format;

			BObjectList<meta_format> fList;
			BLocker				fLock;
			bigtime_t			fLastUpdate;
			int32				fNextCodecID;

			static FormatManager* sInstance;
			static pthread_once_t	sInitOnce;
};

#endif // _FORMAT_MANAGER_H
