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
 *   Copyright 2001-2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 */

/** @file CursorSet.cpp
 *  @brief Container for a named set of cursor bitmaps associated with system cursor IDs. */


#include "CursorSet.h"
#include "ServerCursor.h"

#include <AppServerLink.h>
#include <AutoDeleter.h>
#include <PortLink.h>
#include <ServerProtocol.h>

#include <OS.h>
#include <String.h>
#include <File.h>

#include <new>


/**
 * @brief Constructs a CursorSet with the given name.
 * @param name Name of the cursor set. "Untitled" is used when @a name is NULL.
 */
CursorSet::CursorSet(const char *name)
	: BMessage()
{
	AddString("name", name != NULL ? name : "Untitled");
}


/**
 * @brief Saves the cursor set data to a file.
 * @param path      Path of the file to save to.
 * @param saveFlags BFile open flags (B_READ_WRITE is always added).
 * @return B_OK on success, B_BAD_VALUE if @a path is NULL, or a BFile/BMessage error code.
 */
status_t
CursorSet::Save(const char *path, int32 saveFlags)
{
	if (path == NULL)
		return B_BAD_VALUE;

	BFile file;
	status_t status = file.SetTo(path, B_READ_WRITE | saveFlags);
	if (status != B_OK)
		return status;

	return Flatten(&file);
}


/**
 * @brief Loads cursor set data from a file.
 * @param path Path of the file to load from.
 * @return B_OK on success, B_BAD_VALUE if @a path is NULL, or a BFile/BMessage error code.
 */
status_t
CursorSet::Load(const char *path)
{
	if (path == NULL)
		return B_BAD_VALUE;

	BFile file;
	status_t status = file.SetTo(path, B_READ_ONLY);
	if (status != B_OK)
		return status;

	return Unflatten(&file);
}


/**
 * @brief Adds a cursor bitmap to the set, replacing any existing entry for @a which.
 * @param which   System cursor specifier (BCursorID).
 * @param cursor  BBitmap representing the cursor (must not be NULL, max 48x48).
 * @param hotspot The cursor hotspot position within the bitmap.
 * @return B_OK on success, B_BAD_VALUE if @a cursor is NULL, or a BMessage error code.
 */
status_t
CursorSet::AddCursor(BCursorID which, const BBitmap *cursor,
	const BPoint &hotspot)
{
	if (cursor == NULL)
		return B_BAD_VALUE;

	// Remove the data if it exists already
	RemoveData(_CursorWhichToString(which));

	// Actually add the data to our set
	BMessage message((int32)which);

	message.AddString("class", "bitmap");
	message.AddRect("_frame", cursor->Bounds());
	message.AddInt32("_cspace", cursor->ColorSpace());

	message.AddInt32("_bmflags", 0);
	message.AddInt32("_rowbytes", cursor->BytesPerRow());
	message.AddPoint("hotspot", hotspot);
	message.AddData("_data", B_RAW_TYPE, cursor->Bits(), cursor->BitsLength());

	return AddMessage(_CursorWhichToString(which), &message);
}


/**
 * @brief Adds a cursor to the set from R5 68-byte cursor data.
 *
 * The R5 cursor data is converted to a BBitmap internally. Prefer the BBitmap
 * overload of AddCursor() when possible.
 *
 * @param which System cursor specifier (BCursorID).
 * @param data  Pointer to 68-byte R5 cursor data (must not be NULL).
 * @return B_OK on success, B_BAD_VALUE if @a data is NULL.
 */
status_t
CursorSet::AddCursor(BCursorID which, uint8 *data)
{
	// Convert cursor data to a bitmap because all cursors are internally stored
	// as bitmaps
	if (data == NULL)
		return B_BAD_VALUE;

	ObjectDeleter<BBitmap> bitmap(_CursorDataToBitmap(data));
	BPoint hotspot(data[2], data[3]);

	status_t result = AddCursor(which, bitmap.Get(), hotspot);

	return result;
}


/**
 * @brief Removes the cursor associated with @a which from this set.
 * @param which System cursor specifier (BCursorID).
 */
void
CursorSet::RemoveCursor(BCursorID which)
{
	RemoveData(_CursorWhichToString(which));
}


/**
 * @brief Retrieves a cursor bitmap and hotspot from the set.
 *
 * The returned BBitmap is heap-allocated and the caller takes ownership.
 *
 * @param which     System cursor specifier (BCursorID).
 * @param _cursor   Output pointer to a newly allocated BBitmap.
 * @param _hotspot  Output hotspot position.
 * @return B_OK on success, B_BAD_VALUE if either output pointer is NULL,
 *         B_NAME_NOT_FOUND if the cursor is not in the set, B_ERROR on
 *         internal data inconsistency.
 */
status_t
CursorSet::FindCursor(BCursorID which, BBitmap **_cursor, BPoint *_hotspot)
{
	if (_cursor == NULL || _hotspot == NULL)
		return B_BAD_VALUE;

	BMessage message;
	if (FindMessage(_CursorWhichToString(which), &message) != B_OK)
		return B_NAME_NOT_FOUND;

	const char *className;
	if (message.FindString("class", &className) != B_OK
		|| strcmp(className, "cursor") != 0) {
		return B_ERROR;
	}

	BPoint hotspot;
	if (message.FindPoint("hotspot", &hotspot) != B_OK)
		return B_ERROR;

	const void *buffer;
	int32 bufferLength;
	if (message.FindData("_data", B_RAW_TYPE, (const void **)&buffer,
			(ssize_t *)&bufferLength) != B_OK) {
		return B_ERROR;
	}

	BBitmap *bitmap = new(std::nothrow) BBitmap(message.FindRect("_frame"),
		(color_space)message.FindInt32("_cspace"), true);
	if (bitmap == NULL)
		return B_NO_MEMORY;

	memcpy(bitmap->Bits(), buffer,
		min_c(bufferLength, bitmap->BitsLength()));

	*_cursor = bitmap;
	*_hotspot = hotspot;
	return B_OK;
}


/**
 * @brief Retrieves a cursor as a ServerCursor from the set.
 *
 * The returned ServerCursor is heap-allocated and the caller takes ownership.
 *
 * @param which   System cursor specifier (BCursorID).
 * @param _cursor Output pointer to a newly allocated ServerCursor.
 * @return B_OK on success, B_BAD_VALUE if @a _cursor is NULL,
 *         B_NAME_NOT_FOUND if the cursor is not in the set, B_ERROR on
 *         internal data inconsistency.
 */
status_t
CursorSet::FindCursor(BCursorID which, ServerCursor **_cursor) const
{
	if (_cursor == NULL)
		return B_BAD_VALUE;

	BMessage message;
	if (FindMessage(_CursorWhichToString(which), &message) != B_OK)
		return B_NAME_NOT_FOUND;

	const char *className;
	if (message.FindString("class", &className) != B_OK
		|| strcmp(className, "cursor") != 0) {
		return B_ERROR;
	}

	BPoint hotspot;
	if (message.FindPoint("hotspot", &hotspot) != B_OK)
		return B_ERROR;

	const void *buffer;
	int32 bufferLength;
	if (message.FindData("_data", B_RAW_TYPE, (const void **)&buffer,
			(ssize_t *)&bufferLength) != B_OK) {
		return B_ERROR;
	}

	ServerCursor *cursor = new(std::nothrow) ServerCursor(
		message.FindRect("_frame"), (color_space)message.FindInt32("_cspace"),
		0, hotspot);
	if (cursor == NULL)
		return B_NO_MEMORY;

	memcpy(cursor->Bits(), buffer,
		min_c(bufferLength, (ssize_t)cursor->BitsLength()));

	*_cursor = cursor;
	return B_OK;
}


/**
 * @brief Returns the name of this cursor set.
 * @return A pointer to the name string, or NULL if the name cannot be found.
 */
const char *
CursorSet::GetName()
{
	const char *name;
	if (FindString("name", &name) == B_OK)
		return name;

	return NULL;
}


/**
 * @brief Renames the cursor set.
 *
 * The function silently ignores a NULL @a name.
 *
 * @param name The new name for the set.
 */
void
CursorSet::SetName(const char *name)
{
	if (name == NULL)
		return;

	RemoveData("name");
	AddString("name", name);
}


/**
 * @brief Maps a BCursorID to its string key used in the internal BMessage.
 * @param which The system cursor specifier to convert.
 * @return A string literal naming the cursor, or "Invalid" for unknown IDs.
 */
const char *
CursorSet::_CursorWhichToString(BCursorID which) const
{
	switch (which) {
		case B_CURSOR_ID_SYSTEM_DEFAULT:
			return "System default";
		case B_CURSOR_ID_CONTEXT_MENU:
			return "Context menu";
		case B_CURSOR_ID_COPY:
			return "Copy";
		case B_CURSOR_ID_CROSS_HAIR:
			return "Cross hair";
		case B_CURSOR_ID_NO_CURSOR:
			return "No cursor";
		case B_CURSOR_ID_FOLLOW_LINK:
			return "Follow link";
		case B_CURSOR_ID_GRAB:
			return "Grab";
		case B_CURSOR_ID_GRABBING:
			return "Grabbing";
		case B_CURSOR_ID_HELP:
			return "Help";
		case B_CURSOR_ID_I_BEAM:
			return "I-beam";
		case B_CURSOR_ID_I_BEAM_HORIZONTAL:
			return "I-beam horizontal";
		case B_CURSOR_ID_MOVE:
			return "Move";
		case B_CURSOR_ID_NOT_ALLOWED:
			return "Not allowed";
		case B_CURSOR_ID_PROGRESS:
			return "Progress";
		case B_CURSOR_ID_RESIZE_NORTH:
			return "Resize North";
		case B_CURSOR_ID_RESIZE_EAST:
			return "Resize East";
		case B_CURSOR_ID_RESIZE_SOUTH:
			return "Resize South";
		case B_CURSOR_ID_RESIZE_WEST:
			return "Resize West";
		case B_CURSOR_ID_RESIZE_NORTH_EAST:
			return "Resize North East";
		case B_CURSOR_ID_RESIZE_NORTH_WEST:
			return "Resize North West";
		case B_CURSOR_ID_RESIZE_SOUTH_EAST:
			return "Resize South East";
		case B_CURSOR_ID_RESIZE_SOUTH_WEST:
			return "Resize South West";
		case B_CURSOR_ID_RESIZE_NORTH_SOUTH:
			return "Resize North South";
		case B_CURSOR_ID_RESIZE_EAST_WEST:
			return "Resize East West";
		case B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST:
			return "Resize North East South West";
		case B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST:
			return "Resize North West South East";
		case B_CURSOR_ID_ZOOM_IN:
			return "Zoom in";
		case B_CURSOR_ID_ZOOM_OUT:
			return "Zoom out";
		default:
			return "Invalid";
	}
}

/**
 * @brief Creates a new BBitmap from R5 68-byte cursor data in RGBA32 format.
 *
 * The 1-bit monochrome cursor and mask data are expanded to a 16x16 RGBA32
 * bitmap. Bytes are swapped to correct for little-endian byte order.
 *
 * @param data Pointer to 68-byte R5 cursor data (NULL returns NULL).
 * @return A heap-allocated BBitmap in B_RGBA32 format, or NULL on failure.
 */
BBitmap *
CursorSet::_CursorDataToBitmap(uint8 *data)
{
	// 68-byte array used in R5 for holding cursors.
	// This API has serious problems and should be deprecated (but supported)
	// in R2

	if (data == NULL)
		return NULL;

	// Now that we have all the setup, we're going to map (for now) the cursor
	// to RGBA32. Eventually, there will be support for 16 and 8-bit depths
	BBitmap *bitmap
		= new(std::nothrow) BBitmap(BRect(0,0,15,15), B_RGBA32, 0);
	if (bitmap == NULL)
		return NULL;

	const uint32 black = 0xff000000;
	const uint32 white = 0xffffffff;

	uint8 *buffer = (uint8 *)bitmap->Bits();
	uint16 *cursorPosition = (uint16 *)(data + 4);
	uint16 *maskPosition = (uint16 *)(data + 36);

	// for each row in the cursor data
	for (uint8 y = 0; y < 16; y++) {
		uint32 *bitmapPosition
			= (uint32 *)(buffer + y * bitmap->BytesPerRow());

		// TODO: use proper byteswap macros
		// On intel, our bytes end up swapped, so we must swap them back
		uint16 cursorFlip = (cursorPosition[y] & 0xff) << 8;
		cursorFlip |= (cursorPosition[y] & 0xff00) >> 8;

		uint16 maskFlip = (maskPosition[y] & 0xff) << 8;
		maskFlip |= (maskPosition[y] & 0xff00) >> 8;

		// for each column in each row of cursor data
		for (uint8 x = 0; x < 16; x++) {
			// Get the values and dump them to the bitmap
			uint16 bit = 1 << (15 - x);
			bitmapPosition[x] = ((cursorFlip & bit) != 0 ? black : white)
				& ((maskFlip & bit) != 0 ? 0xffffffff : 0x00ffffff);
		}
	}

	return bitmap;
}
