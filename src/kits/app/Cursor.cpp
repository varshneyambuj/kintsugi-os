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
 *   Copyright 2001-2006, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Frans van Nispen (xlr8@tref.nl)
 *       Gabe Yoder (gyoder@stny.rr.com)
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file Cursor.cpp
 *  @brief Implementation of BCursor, a class representing a mouse cursor.
 *
 *  BCursor describes a view-wide or application-wide cursor. Cursor data
 *  is managed server-side by the app_server, and BCursor communicates
 *  with it to create, clone, and delete cursor resources.
 */


#include <AppDefs.h>
#include <Bitmap.h>
#include <Cursor.h>

#include <AppServerLink.h>
#include <ServerProtocol.h>


const BCursor *B_CURSOR_SYSTEM_DEFAULT;
const BCursor *B_CURSOR_I_BEAM;
	// these are initialized in BApplication::InitData()

/** @brief Constructs a cursor from legacy 68-byte cursor data.
 *
 *  The cursor data must be a 68-byte block with a 16x16 pixel, 1-bit
 *  depth cursor format (size byte = 16, depth byte = 1), or one of the
 *  predefined constants B_HAND_CURSOR or B_I_BEAM_CURSOR.
 *
 *  @param cursorData Pointer to the raw cursor data, or a predefined cursor constant.
 */
BCursor::BCursor(const void *cursorData)
	:
	fServerToken(-1),
	fNeedToFree(false)
{
	const uint8 *data = (const uint8 *)cursorData;

	if (data == B_HAND_CURSOR || data == B_I_BEAM_CURSOR) {
		// just use the default cursors from the app_server
		fServerToken = data == B_HAND_CURSOR ?
			B_CURSOR_ID_SYSTEM_DEFAULT : B_CURSOR_ID_I_BEAM;
		return;
	}

	// Create a new cursor in the app_server

	if (data == NULL
		|| data[0] != 16	// size
		|| data[1] != 1		// depth
		|| data[2] >= 16 || data[3] >= 16)	// hot-spot
		return;

	// Send data directly to server
	BPrivate::AppServerLink link;
	link.StartMessage(AS_CREATE_CURSOR);
	link.Attach(cursorData, 68);

	status_t status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<int32>(&fServerToken);
		fNeedToFree = true;
	}
}


/** @brief Constructs a cursor from a predefined system cursor ID.
 *  @param id The system cursor identifier (e.g., B_CURSOR_ID_SYSTEM_DEFAULT).
 */
BCursor::BCursor(BCursorID id)
	:
	fServerToken(id),
	fNeedToFree(false)
{
}


/** @brief Copy constructor. Clones the cursor from another BCursor.
 *  @param other The BCursor to copy.
 */
BCursor::BCursor(const BCursor& other)
	:
	fServerToken(-1),
	fNeedToFree(false)
{
	*this = other;
}


/** @brief Constructs a cursor from an archived BMessage.
 *
 *  This constructor is currently a stub and does not restore cursor
 *  data from the archive (undefined behavior on BeOS).
 *
 *  @param data The archived message (not used).
 */
BCursor::BCursor(BMessage *data)
{
	// undefined on BeOS
	fServerToken = -1;
	fNeedToFree = false;
}


/** @brief Constructs a cursor from a bitmap and a hotspot position.
 *
 *  Sends the bitmap data to the app_server to create a new cursor
 *  resource. The bitmap can be of any supported color space and size.
 *
 *  @param bitmap  The bitmap to use as the cursor image.
 *  @param hotspot The point within the bitmap that represents the cursor tip.
 */
BCursor::BCursor(const BBitmap* bitmap, const BPoint& hotspot)
	:
	fServerToken(-1),
	fNeedToFree(false)
{
	if (bitmap == NULL)
		return;

	BRect bounds = bitmap->Bounds();
	color_space colorspace = bitmap->ColorSpace();
	void* bits = bitmap->Bits();
	int32 size = bitmap->BitsLength();
	if (bits == NULL || size <= 0)
		return;

	// Send data directly to server
	BPrivate::AppServerLink link;
	link.StartMessage(AS_CREATE_CURSOR_BITMAP);
	link.Attach<BRect>(bounds);
	link.Attach<BPoint>(hotspot);
	link.Attach<color_space>(colorspace);
	link.Attach<int32>(bitmap->BytesPerRow());
	link.Attach<int32>(size);
	link.Attach(bits, size);

	status_t status;
	if (link.FlushWithReply(status) == B_OK) {
		if (status == B_OK) {
			link.Read<int32>(&fServerToken);
			fNeedToFree = true;
		} else
			fServerToken = status;
	}
}


/** @brief Destroys the cursor and frees server-side cursor resources.
 */
BCursor::~BCursor()
{
	_FreeCursorData();
}


/** @brief Returns the initialization status of the cursor.
 *  @return B_OK if the cursor was created successfully, or an error code.
 */
status_t
BCursor::InitCheck() const
{
	return fServerToken >= 0 ? B_OK : fServerToken;
}


/** @brief Archives the cursor into a BMessage.
 *
 *  This is currently a stub and does not archive any cursor data
 *  (not implemented on BeOS).
 *
 *  @param into The message to archive into.
 *  @param deep Whether to perform a deep archive.
 *  @return B_OK.
 */
status_t
BCursor::Archive(BMessage *into, bool deep) const
{
	// not implemented on BeOS
	return B_OK;
}


/** @brief Creates a new BCursor from an archived message.
 *
 *  This is currently a stub and always returns NULL (not implemented on BeOS).
 *
 *  @param data The archived message.
 *  @return Always NULL.
 */
BArchivable	*
BCursor::Instantiate(BMessage *data)
{
	// not implemented on BeOS
	return NULL;
}


/** @brief Assigns another cursor to this one by cloning its server-side data.
 *
 *  If the other cursor owns server-side data, a clone request is sent
 *  to the app_server. Self-assignment and assignment of an equal cursor
 *  are handled as no-ops.
 *
 *  @param other The cursor to copy from.
 *  @return A reference to this cursor.
 */
BCursor&
BCursor::operator=(const BCursor& other)
{
	if (&other != this && other != *this) {
		_FreeCursorData();

		fServerToken = other.fServerToken;

		if (other.fNeedToFree) {
			BPrivate::AppServerLink link;
			link.StartMessage(AS_CLONE_CURSOR);
			link.Attach<int32>(other.fServerToken);

			status_t status;
			if (link.FlushWithReply(status) == B_OK) {
				if (status == B_OK) {
					link.Read<int32>(&fServerToken);
					fNeedToFree = true;
				} else
					fServerToken = status;
			}
		}
	}
	return *this;
}


/** @brief Tests whether two cursors refer to the same server-side cursor.
 *  @param other The cursor to compare with.
 *  @return true if both cursors have the same server token.
 */
bool
BCursor::operator==(const BCursor& other) const
{
	return fServerToken == other.fServerToken;
}


/** @brief Tests whether two cursors refer to different server-side cursors.
 *  @param other The cursor to compare with.
 *  @return true if the cursors have different server tokens.
 */
bool
BCursor::operator!=(const BCursor& other) const
{
	return fServerToken != other.fServerToken;
}


/** @brief Performs an action identified by a perform_code.
 *
 *  Reserved for future use by the system. Currently a no-op.
 *
 *  @param d   The perform code identifying the action.
 *  @param arg Argument data for the action.
 *  @return B_OK.
 */
status_t
BCursor::Perform(perform_code d, void *arg)
{
	return B_OK;
}


void BCursor::_ReservedCursor1() {}
void BCursor::_ReservedCursor2() {}
void BCursor::_ReservedCursor3() {}
void BCursor::_ReservedCursor4() {}


/** @brief Releases server-side cursor resources.
 *
 *  Sends a delete message to the app_server to deallocate the cursor
 *  data associated with this object's server token. Only sends the
 *  message if this cursor owns the server-side resource.
 */
void
BCursor::_FreeCursorData()
{
	// Notify server to deallocate server-side objects for this cursor
	if (fNeedToFree) {
		BPrivate::AppServerLink link;
		link.StartMessage(AS_DELETE_CURSOR);
		link.Attach<int32>(fServerToken);
		link.Flush();
	}
}
