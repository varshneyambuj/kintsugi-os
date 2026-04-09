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
 * MIT License. Copyright 2001-2010, Haiku.
 * Original author: DarkWyrm <bpmagic@columbus.rr.com>.
 */

/** @file CursorManager.h
    @brief System-wide cursor registry with token-based lookup and per-team lifetime management. */

#ifndef CURSOR_MANAGER_H
#define CURSOR_MANAGER_H


#include <List.h>
#include <Locker.h>

#include <TokenSpace.h>

#include "CursorSet.h"

using BPrivate::BTokenSpace;
class ServerCursor;


/** @brief Manages all system cursors, assigns tokens to BCursor objects, and
           releases cursors automatically when an application exits. */
class CursorManager : public BLocker {
public:
								CursorManager();
								~CursorManager();

			/** @brief Loads and scales all built-in system cursors.
			    @param scale Display scale factor (e.g. 1.0 for standard density). */
			void				InitializeCursors(float scale);

			/** @brief Releases all system cursor resources. */
			void				ReleaseCursors();

			/** @brief Creates a cursor from the legacy 68-byte cursor data format.
			    @param clientTeam Team ID of the owning application.
			    @param cursorData Pointer to 68 bytes of cursor data.
			    @return Pointer to the new ServerCursor, or NULL on failure. */
			ServerCursor*		CreateCursor(team_id clientTeam,
									 const uint8* cursorData);

			/** @brief Creates a cursor from a bitmap description.
			    @param clientTeam Team ID of the owning application.
			    @param r Dimensions of the cursor bitmap.
			    @param format Pixel color space of the bitmap.
			    @param flags Cursor creation flags.
			    @param hotspot Hot-spot position within the bitmap.
			    @param bytesPerRow Row stride in bytes, or -1 to compute automatically.
			    @return Pointer to the new ServerCursor, or NULL on failure. */
			ServerCursor*		CreateCursor(team_id clientTeam,
									BRect r, color_space format, int32 flags,
									BPoint hotspot, int32 bytesPerRow = -1);

			/** @brief Registers a cursor and returns its integer token.
			    @param cursor The ServerCursor to register.
			    @param token Desired token, or -1 to auto-assign.
			    @return The assigned token. */
			int32				AddCursor(ServerCursor* cursor,
									int32 token = -1);

			/** @brief Removes and frees all cursors belonging to a given team.
			    @param team Team ID whose cursors should be deleted. */
			void				DeleteCursors(team_id team);

			/** @brief Removes a single cursor from the manager without deleting the object.
			    @param cursor The ServerCursor to remove. */
			void				RemoveCursor(ServerCursor* cursor);

			/** @brief Loads a named cursor theme set from a file path.
			    @param path Filesystem path to the cursor set file. */
			void				SetCursorSet(const char* path);

			/** @brief Returns the system cursor for a given BCursorID.
			    @param which The standard cursor identifier.
			    @return Pointer to the ServerCursor, or NULL if not found. */
			ServerCursor*		GetCursor(BCursorID which);

			/** @brief Looks up a cursor by its token.
			    @param token Token assigned when the cursor was added.
			    @return Pointer to the ServerCursor, or NULL if not found. */
			ServerCursor*		FindCursor(int32 token);

private:
			BBitmap				_RenderVectorCursor(uint32 size, const uint8* vector,
									uint32 vectorSize, float shadowStrength);
			void				_InitCursor(ServerCursor*& cursorMember, BCursorID id,
									const uint8* vector, uint32 vectorSize,
									const BPoint& hotSpot, float scale);
			void				_LoadCursor(ServerCursor*& cursorMember,
									const CursorSet& set, BCursorID id);
			ServerCursor*		_FindCursor(team_id cientTeam,
									const uint8* cursorData);
			void				_RemoveCursor(ServerCursor* cursor);

private:
			BList				fCursorList;
			BTokenSpace			fTokenSpace;

			// System cursor members
			ServerCursor*		fCursorSystemDefault;

			ServerCursor*		fCursorContextMenu;
			ServerCursor*		fCursorCopy;
			ServerCursor*		fCursorCreateLink;
			ServerCursor*		fCursorCrossHair;
			ServerCursor*		fCursorFollowLink;
			ServerCursor*		fCursorGrab;
			ServerCursor*		fCursorGrabbing;
			ServerCursor*		fCursorHelp;
			ServerCursor*		fCursorIBeam;
			ServerCursor*		fCursorIBeamHorizontal;
			ServerCursor*		fCursorMove;
			ServerCursor*		fCursorNoCursor;
			ServerCursor*		fCursorNotAllowed;
			ServerCursor*		fCursorProgress;
			ServerCursor*		fCursorResizeEast;
			ServerCursor*		fCursorResizeEastWest;
			ServerCursor*		fCursorResizeNorth;
			ServerCursor*		fCursorResizeNorthEast;
			ServerCursor*		fCursorResizeNorthEastSouthWest;
			ServerCursor*		fCursorResizeNorthSouth;
			ServerCursor*		fCursorResizeNorthWest;
			ServerCursor*		fCursorResizeNorthWestSouthEast;
			ServerCursor*		fCursorResizeSouth;
			ServerCursor*		fCursorResizeSouthEast;
			ServerCursor*		fCursorResizeSouthWest;
			ServerCursor*		fCursorResizeWest;
			ServerCursor*		fCursorZoomIn;
			ServerCursor*		fCursorZoomOut;
};

#endif	// CURSOR_MANAGER_H
