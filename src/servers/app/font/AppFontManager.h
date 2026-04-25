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
 * MIT License. Copyright 2001-2009, Haiku.
 * Original authors: DarkWyrm, Axel Dörfler.
 */

/** @file AppFontManager.h
    @brief Per-application font catalog overlaying user-registered fonts on the global set. */

#ifndef APP_FONT_MANAGER_H
#define APP_FONT_MANAGER_H


#include "FontManager.h"

#include <Locker.h>


#include <ft2build.h>
#include FT_FREETYPE_H

struct node_ref;


/** @brief Soft cap on the per-application font area total size, in bytes. */
// font areas should be less than 20MB
#define MAX_FONT_DATA_SIZE_BYTES	20 * 1024 * 1024
/** @brief Maximum number of user fonts a single application may register. */
#define MAX_USER_FONTS				128

/**
 * @brief Per-application font manager backing BApplication's add-fonts API.
 *
 * Subclasses FontManager to provide an isolated catalog of fonts that an
 * application registers at runtime (from a path or from memory). IDs are
 * allocated downwards from UINT16_MAX so they cannot collide with the
 * GlobalFontManager's upward-allocated IDs, which guarantees the user
 * font wins when both are queried.
 */
class AppFontManager : public FontManager, BLocker {
public:
								AppFontManager();

			/** @brief Acquires the per-instance BLocker. */
			bool				Lock() { return BLocker::Lock(); }
			/** @brief Releases the per-instance BLocker. */
			void				Unlock() { BLocker::Unlock(); }
			/** @brief Returns true when the calling thread holds the lock. */
			bool				IsLocked() const { return BLocker::IsLocked(); }

			status_t			AddUserFontFromFile(const char* path, uint16 index, uint16 instance,
									uint16& familyID, uint16& styleID);
			status_t			AddUserFontFromMemory(const FT_Byte* fontAddress, size_t size,
									uint16 index, uint16 instance,
									uint16& familyID, uint16& styleID);
			status_t			RemoveUserFont(uint16 familyID, uint16 styleID);

private:
			uint16				_NextID();

private:
			uint16				fNextID;
};


#endif	/* APP_FONT_MANAGER_H */
