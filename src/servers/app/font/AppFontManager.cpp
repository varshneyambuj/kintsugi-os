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
 *   Copyright 2001-2016, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file AppFontManager.cpp
 * @brief Implementation of the per-application font manager.
 *
 * Backs BApplication's AddFont APIs by registering FreeType faces
 * loaded from a path or from an in-memory blob into a private catalog.
 * Uses BLocker for serialization (rather than the BLooper lock used by
 * the global manager) and allocates IDs descending from UINT16_MAX so
 * user fonts cannot collide with system fonts allocated by
 * GlobalFontManager.
 */


#include "AppFontManager.h"

#include <new>
#include <stdint.h>

#include <Debug.h>
#include <Entry.h>

#include "FontFamily.h"


//#define TRACE_FONT_MANAGER
#ifdef TRACE_FONT_MANAGER
#	define FTRACE(x) debug_printf x
#else
#	define FTRACE(x) ;
#endif


/** @brief Process-wide FreeType library handle owned by GlobalFontManager. */
extern FT_Library gFreeTypeLibrary;


//	#pragma mark -


/**
 * @brief Constructs the manager and primes the descending ID counter.
 *
 * Starting from UINT16_MAX guarantees the IDs handed out for
 * application-registered fonts never collide with the GlobalFontManager's
 * ascending IDs, so a user font wins the lookup race when both
 * managers know a family by the same name.
 */
AppFontManager::AppFontManager()
	: BLocker("AppFontManager")
{
	fNextID = UINT16_MAX;
}


/**
 * @brief Registers a font from disk under this app's catalog.
 *
 * Resolves @a path to a node_ref, opens the FT_Face for the requested
 * face/instance, and feeds it through FontManager::_AddFont() so the
 * usual family/style indexing applies.
 *
 * @param path      File path to the font.
 * @param index     Face index inside the font file.
 * @param instance  Variable-font instance index packed into the high
 *                  16 bits when calling FT_New_Face.
 * @param familyID  Output: assigned family ID.
 * @param styleID   Output: assigned style ID.
 * @retval B_OK            On success.
 * @retval B_ERROR         FreeType could not open the face.
 * @retval B_NAME_IN_USE   The (family, style) pair was already present.
 *
 * @note  Caller must hold the AppFontManager lock.
 */
status_t
AppFontManager::AddUserFontFromFile(const char* path, uint16 index, uint16 instance,
	uint16& familyID, uint16& styleID)
{
	ASSERT(IsLocked());

	BEntry entry;
	status_t status = entry.SetTo(path);
	if (status != B_OK)
		return status;

	node_ref nodeRef;
	status = entry.GetNodeRef(&nodeRef);
	if (status < B_OK)
		return status;

	FT_Face face;
	FT_Error error = FT_New_Face(gFreeTypeLibrary, path, index | (instance << 16), &face);
	if (error != 0)
		return B_ERROR;

	status = _AddFont(face, nodeRef, path, familyID, styleID);
	return status;
}


/**
 * @brief Registers a font from an in-memory buffer under this app's catalog.
 *
 * @param fontAddress  Pointer to the font bytes; must remain valid for the
 *                     lifetime of the registered FontStyle.
 * @param size         Length of @a fontAddress in bytes.
 * @param index        Face index inside the font blob.
 * @param instance     Variable-font instance index packed into high bits.
 * @param familyID     Output: assigned family ID.
 * @param styleID      Output: assigned style ID.
 * @retval B_OK            On success.
 * @retval B_ERROR         FreeType could not interpret the buffer.
 * @retval B_NAME_IN_USE   The (family, style) pair was already present.
 *
 * @note  Caller must hold the AppFontManager lock.
 */
status_t
AppFontManager::AddUserFontFromMemory(const FT_Byte* fontAddress, size_t size, uint16 index,
	uint16 instance, uint16& familyID, uint16& styleID)
{
	ASSERT(IsLocked());

	node_ref nodeRef;
	status_t status;

	FT_Face face;
	FT_Error error = FT_New_Memory_Face(gFreeTypeLibrary, fontAddress, size,
		index | (instance << 16), &face);
	if (error != 0)
		return B_ERROR;

	status = _AddFont(face, nodeRef, "", familyID, styleID);

	return status;
}


/**
 * @brief Drops a previously registered user font from the catalog.
 *
 * @param familyID  Family ID returned by Add* call.
 * @param styleID   Style ID returned by Add* call.
 * @retval B_OK         The font was found and removed.
 * @retval B_BAD_VALUE  No matching (familyID, styleID) entry exists.
 */
status_t
AppFontManager::RemoveUserFont(uint16 familyID, uint16 styleID)
{
	return _RemoveFont(familyID, styleID) != NULL ? B_OK : B_BAD_VALUE;
}


/**
 * @brief Returns the next descending ID, keeping app fonts above system fonts.
 *
 * @return  16-bit ID; counted down from UINT16_MAX.
 */
uint16
AppFontManager::_NextID()
{
	return fNextID--;
}
