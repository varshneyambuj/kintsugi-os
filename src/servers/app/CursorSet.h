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
 * MIT License. Copyright 2001-2006, Haiku.
 * Original author: DarkWyrm <bpmagic@columbus.rr.com>.
 */

/** @file CursorSet.h
    @brief Manages a named collection of system cursors stored in a BMessage archive. */

#ifndef CURSOR_SET_H
#define CURSOR_SET_H


#include <Bitmap.h>
#include <Cursor.h>
#include <Message.h>

#include <ServerProtocol.h>

class ServerCursor;


/** @brief A named set of system cursors persisted as a BMessage archive on disk. */
class CursorSet : public BMessage {
	public:
		/** @brief Constructs an empty cursor set with the given name.
		    @param name Display name for this cursor set. */
		CursorSet(const char *name);

		/** @brief Saves this cursor set to a file.
		    @param path Destination filesystem path.
		    @param saveflags BMessage save flags.
		    @return B_OK on success, or an error code. */
		status_t		Save(const char *path,int32 saveflags=0);

		/** @brief Loads a cursor set from a file, replacing the current contents.
		    @param path Source filesystem path.
		    @return B_OK on success, or an error code. */
		status_t		Load(const char *path);

		/** @brief Adds a bitmap-based cursor to this set for the given cursor ID.
		    @param which Standard cursor identifier.
		    @param cursor BBitmap containing the cursor image.
		    @param hotspot Hot-spot position within the bitmap.
		    @return B_OK on success, or an error code. */
		status_t		AddCursor(BCursorID which,const BBitmap *cursor, const BPoint &hotspot);

		/** @brief Adds a cursor from legacy 68-byte cursor data to this set.
		    @param which Standard cursor identifier.
		    @param data Pointer to 68 bytes of cursor data.
		    @return B_OK on success, or an error code. */
		status_t		AddCursor(BCursorID which, uint8 *data);

		/** @brief Removes the cursor for the given ID from this set.
		    @param which Standard cursor identifier to remove. */
		void			RemoveCursor(BCursorID which);

		/** @brief Retrieves the bitmap and hot-spot for a cursor in this set.
		    @param which Standard cursor identifier.
		    @param cursor Output pointer to the cursor BBitmap.
		    @param hotspot Output hot-spot position.
		    @return B_OK if found, or B_NAME_NOT_FOUND. */
		status_t		FindCursor(BCursorID which, BBitmap **cursor, BPoint *hotspot);

		/** @brief Retrieves a ServerCursor for the given ID from this set.
		    @param which Standard cursor identifier.
		    @param cursor Output pointer to the ServerCursor.
		    @return B_OK if found, or B_NAME_NOT_FOUND. */
		status_t		FindCursor(BCursorID which, ServerCursor **cursor) const;

		/** @brief Sets the display name of this cursor set.
		    @param name New name string. */
		void			SetName(const char *name);

		/** @brief Returns the display name of this cursor set.
		    @return Null-terminated name string. */
		const char		*GetName();

	private:
		const char *_CursorWhichToString(BCursorID which) const;
		BBitmap *_CursorDataToBitmap(uint8 *data);
};

#endif	// CURSOR_SET_H
