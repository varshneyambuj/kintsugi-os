/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2001-2009, Haiku.
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Stephan Aßmus <superstippi@gmx.de>
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ServerCursor.h
 *  @brief Server-side cursor bitmap with hot-spot and team-ownership tracking. */

#ifndef SERVER_CURSOR_H
#define SERVER_CURSOR_H


#include "ServerBitmap.h"

#include <Point.h>
#include <String.h>


class CursorManager;


/** @brief A ServerBitmap subclass representing a hardware cursor with a hot-spot. */
class ServerCursor : public ServerBitmap {
public:
	/** @brief Constructs a cursor from explicit geometry and color space.
	 *  @param r           Bounding rectangle (width/height determine cursor size).
	 *  @param space       Color space of the cursor data.
	 *  @param flags       Bitmap creation flags.
	 *  @param hotspot     Hot-spot position relative to the cursor's top-left.
	 *  @param bytesPerRow Bytes per row (-1 for auto-calculation).
	 *  @param screen      Target screen ID. */
								ServerCursor(BRect r, color_space space,
									int32 flags, BPoint hotspot,
									int32 bytesPerRow = -1,
									screen_id screen = B_MAIN_SCREEN_ID);

	/** @brief Constructs a cursor from a legacy R5-format cursor data blob.
	 *  @param cursorDataFromR5 Pointer to the R5-format cursor data. */
								ServerCursor(const uint8* cursorDataFromR5);

	/** @brief Constructs a cursor from pre-padded raw pixel data.
	 *  @param alreadyPaddedData Pre-padded pixel data buffer.
	 *  @param width             Cursor width in pixels.
	 *  @param height            Cursor height in pixels.
	 *  @param format            Color space of the data. */
								ServerCursor(const uint8* alreadyPaddedData,
									uint32 width, uint32 height,
									color_space format);

	/** @brief Copy-constructs from another ServerCursor.
	 *  @param cursor Source cursor. */
								ServerCursor(const ServerCursor* cursor);

	virtual						~ServerCursor();

	//! Returns the cursor's hot spot
	/** @brief Sets the cursor's hot-spot (the active pixel within the cursor image).
	 *  @param pt New hot-spot position. */
			void				SetHotSpot(BPoint pt);

	/** @brief Returns the cursor's current hot-spot position.
	 *  @return Hot-spot as a BPoint. */
			BPoint				GetHotSpot() const
									{ return fHotSpot; }

	/** @brief Associates this cursor with a client team.
	 *  @param tid Team ID of the owning application. */
			void				SetOwningTeam(team_id tid)
									{ fOwningTeam = tid; }

	/** @brief Returns the team ID of the client that owns this cursor.
	 *  @return Owning team ID. */
			team_id				OwningTeam() const
									{ return fOwningTeam; }

	/** @brief Returns the unique token identifying this cursor.
	 *  @return Token value. */
			int32				Token() const
									{ return fToken; }

	/** @brief Notifies the cursor that it has been registered with a CursorManager.
	 *  @param manager The manager that now owns this cursor. */
			void				AttachedToManager(CursorManager* manager);

	/** @brief Returns a pointer to the raw R5-format cursor data, if available.
	 *  @return Pointer to the cursor data blob, or NULL. */
			const uint8*		CursorData() const
									{ return fCursorData; }

private:
	friend class CursorManager;

			BPoint				fHotSpot;
			team_id				fOwningTeam;
			uint8*				fCursorData;
			CursorManager*		fManager;
};


/** @brief Reference-counted handle to a ServerCursor. */
typedef BReference<ServerCursor> ServerCursorReference;


#endif	// SERVER_CURSOR_H
