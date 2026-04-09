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
 *   Copyright 2001-2024, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 */

/** @file CursorManager.cpp
 *  @brief Handles the system's cursor infrastructure, including all standard cursors. */


#include "CursorManager.h"

#include "ServerCursor.h"
#include "ServerConfig.h"
#include "ServerTokenSpace.h"

#include <Autolock.h>
#include <Directory.h>
#include <String.h>
#include <IconUtils.h>

#include <agg_pixfmt_rgba.h>
#include <agg_blur.h>

#include <new>
#include <stdio.h>

#include "CursorData.cpp"


/**
 * @brief Constructs the CursorManager and initializes its lock.
 */
CursorManager::CursorManager()
	:
	BLocker("CursorManager")
{
}


/**
 * @brief Destroys the CursorManager, releasing all registered cursors.
 */
CursorManager::~CursorManager()
{
	ReleaseCursors();
}


/**
 * @brief Initializes all standard system cursors at the given display scale.
 *
 * Renders each cursor from its HVIF vector data at a size proportional to
 * @a scale times 22 pixels. A scale below 1.0 is clamped to 1.0.
 *
 * @param scale Display scale factor (1.0 = standard 22 px cursors).
 */
void
CursorManager::InitializeCursors(float scale)
{
	if (scale < 1.0)
		scale = 1.0;

	const BPoint kHandHotspot(1, 1);
	const BPoint kResizeHotspot(8, 8);

	struct StandardCursor {
		ServerCursor*& member;
		BCursorID id;
		const uint8* data;
		uint32 dataLength;
		BPoint hotspot;

#define C(IDNAME, NAME, HOTSPOT) {fCursor##NAME, B_CURSOR_ID_##IDNAME, \
		kCursor##NAME, B_COUNT_OF(kCursor##NAME), HOTSPOT}
	} standardCursors[] = {
		{fCursorNoCursor, B_CURSOR_ID_NO_CURSOR, NULL, 0, BPoint(0, 0)},

		C(SYSTEM_DEFAULT,	SystemDefault,	kHandHotspot),
		C(CONTEXT_MENU,		ContextMenu,	kHandHotspot),
		C(COPY,				Copy,			kHandHotspot),
		C(CREATE_LINK,		CreateLink,		kHandHotspot),
		C(CROSS_HAIR,		CrossHair,		BPoint(10, 10)),
		C(FOLLOW_LINK,		FollowLink,		BPoint(5, 0)),
		C(GRAB,				Grab,			kHandHotspot),
		C(GRABBING,			Grabbing,		kHandHotspot),
		C(HELP,				Help,			BPoint(0, 8)),
		C(I_BEAM,			IBeam,			BPoint(7, 9)),
		C(I_BEAM_HORIZONTAL, IBeamHorizontal, BPoint(8, 8)),
		C(MOVE,				Move,			kResizeHotspot),
		C(NOT_ALLOWED,		NotAllowed,		BPoint(8, 8)),
		C(PROGRESS,			Progress,		BPoint(7, 10)),
		C(RESIZE_EAST,		ResizeEast,		kResizeHotspot),
		C(RESIZE_EAST_WEST, ResizeEastWest, kResizeHotspot),
		C(RESIZE_NORTH,		ResizeNorth,	kResizeHotspot),
		C(RESIZE_NORTH_EAST, ResizeNorthEast, kResizeHotspot),
		C(RESIZE_NORTH_EAST_SOUTH_WEST, ResizeNorthEastSouthWest, kResizeHotspot),
		C(RESIZE_NORTH_SOUTH, ResizeNorthSouth, kResizeHotspot),
		C(RESIZE_NORTH_WEST, ResizeNorthWest, kResizeHotspot),
		C(RESIZE_NORTH_WEST_SOUTH_EAST, ResizeNorthWestSouthEast, kResizeHotspot),
		C(RESIZE_SOUTH,		ResizeSouth,	kResizeHotspot),
		C(RESIZE_SOUTH_EAST, ResizeSouthEast, kResizeHotspot),
		C(RESIZE_SOUTH_WEST, ResizeSouthWest, kResizeHotspot),
		C(RESIZE_WEST,		ResizeWest,		kResizeHotspot),
		C(ZOOM_IN,			ZoomIn,			BPoint(6, 6)),
		C(ZOOM_OUT,			ZoomOut,		BPoint(6, 6))
	};
#undef C

	for (size_t i = 0; i < B_COUNT_OF(standardCursors); i++) {
		const StandardCursor& info = standardCursors[i];
		_InitCursor(info.member, info.id, info.data, info.dataLength, info.hotspot, scale);
	}
}


/**
 * @brief Releases all cursors registered with this manager.
 *
 * Each cursor's manager pointer is cleared, its token is removed from the
 * token space, and its reference is released.
 */
void
CursorManager::ReleaseCursors()
{
	for (int32 i = 0; i < fCursorList.CountItems(); i++) {
		ServerCursor* cursor = ((ServerCursor*)fCursorList.ItemAtFast(i));
		cursor->fManager = NULL;
		fTokenSpace.RemoveToken(cursor->Token());
		cursor->ReleaseReference();
	}
	fCursorList.MakeEmpty();
}


/**
 * @brief Creates or reuses a cursor for the given team from raw cursor data.
 *
 * If a matching cursor (same team and data) already exists it is reused.
 *
 * @param clientTeam The owning application team ID.
 * @param cursorData Pointer to 68-byte R5-format cursor data.
 * @return A new (or existing) ServerCursor, or NULL on failure.
 */
ServerCursor*
CursorManager::CreateCursor(team_id clientTeam, const uint8* cursorData)
{
	if (!Lock())
		return NULL;

	ServerCursorReference cursor(_FindCursor(clientTeam, cursorData), false);

	if (!cursor) {
		cursor.SetTo(new (std::nothrow) ServerCursor(cursorData), true);
		if (cursor) {
			cursor->SetOwningTeam(clientTeam);
			if (AddCursor(cursor) < B_OK)
				cursor = NULL;
		}
	}

	Unlock();

	return cursor.Detach();
}


/**
 * @brief Creates a cursor from a bitmap rectangle and format.
 * @param clientTeam  The owning application team ID.
 * @param r           Bounding rectangle of the cursor bitmap.
 * @param format      Color space of the cursor bitmap.
 * @param flags       Cursor flags.
 * @param hotspot     Hotspot position within the cursor bitmap.
 * @param bytesPerRow Row stride of the cursor bitmap in bytes.
 * @return A newly allocated ServerCursor, or NULL on failure.
 */
ServerCursor*
CursorManager::CreateCursor(team_id clientTeam, BRect r, color_space format,
	int32 flags, BPoint hotspot, int32 bytesPerRow)
{
	if (!Lock())
		return NULL;

	ServerCursor* cursor = new (std::nothrow) ServerCursor(r, format, flags,
		hotspot, bytesPerRow);
	if (cursor != NULL) {
		cursor->SetOwningTeam(clientTeam);
		if (AddCursor(cursor) < B_OK) {
			delete cursor;
			cursor = NULL;
		}
	}

	Unlock();

	return cursor;
}


/**
 * @brief Registers a cursor with the manager and assigns it a token.
 *
 * If @a token is -1 a new token is allocated; otherwise the provided token
 * is used (replacing any existing cursor at that token).
 *
 * @param cursor The ServerCursor to register (must not be NULL).
 * @param token  Desired token, or -1 to allocate automatically.
 * @return The assigned token on success, B_BAD_VALUE or B_ERROR on failure.
 */
int32
CursorManager::AddCursor(ServerCursor* cursor, int32 token)
{
	if (cursor == NULL)
		return B_BAD_VALUE;
	if (!Lock())
		return B_ERROR;

	ServerCursor* oldCursor = FindCursor(token);
	if (oldCursor != NULL) {
		fCursorList.ReplaceItem(fCursorList.IndexOf(oldCursor), cursor);
		oldCursor->ReleaseReference();
	} else {
		if (!fCursorList.AddItem(cursor)) {
			Unlock();
			return B_NO_MEMORY;
		}
	}

	if (token == -1)
		token = fTokenSpace.NewToken(kCursorToken, cursor);
	else
		fTokenSpace.SetToken(token, kCursorToken, cursor);

	cursor->fToken = token;
	cursor->AttachedToManager(this);

	Unlock();
	return token;
}


/**
 * @brief Removes a cursor from the manager.
 *
 * If this was the last reference to the cursor, it will be deleted.
 *
 * @param cursor The ServerCursor to remove.
 */
void
CursorManager::RemoveCursor(ServerCursor* cursor)
{
	if (!Lock())
		return;

	_RemoveCursor(cursor);
	cursor->ReleaseReference();

	Unlock();
}


/**
 * @brief Removes and deletes all cursors belonging to the given team.
 * @param team The team ID whose cursors should be deleted.
 */
void
CursorManager::DeleteCursors(team_id team)
{
	if (!Lock())
		return;

	for (int32 index = fCursorList.CountItems(); index-- > 0;) {
		ServerCursor* cursor = (ServerCursor*)fCursorList.ItemAtFast(index);
		if (cursor->OwningTeam() != team)
			continue;

		_RemoveCursor(cursor);
		cursor->ReleaseReference();
	}

	Unlock();
}


/**
 * @brief Applies all cursors from the specified CursorSet file.
 *
 * All cursors defined in the set are assigned. If the set does not specify a
 * cursor for a particular system cursor ID, it remains unchanged. The function
 * fails silently if @a path is NULL, invalid, or not a CursorSet file.
 *
 * @param path Path to the cursor set file.
 */
void
CursorManager::SetCursorSet(const char* path)
{
	BAutolock locker (this);

	CursorSet cursorSet(NULL);

	if (!path || cursorSet.Load(path) != B_OK)
		return;

	_LoadCursor(fCursorSystemDefault, cursorSet, B_CURSOR_ID_SYSTEM_DEFAULT);
	_LoadCursor(fCursorContextMenu, cursorSet, B_CURSOR_ID_CONTEXT_MENU);
	_LoadCursor(fCursorCopy, cursorSet, B_CURSOR_ID_COPY);
	_LoadCursor(fCursorCreateLink, cursorSet, B_CURSOR_ID_CREATE_LINK);
	_LoadCursor(fCursorCrossHair, cursorSet, B_CURSOR_ID_CROSS_HAIR);
	_LoadCursor(fCursorFollowLink, cursorSet, B_CURSOR_ID_FOLLOW_LINK);
	_LoadCursor(fCursorGrab, cursorSet, B_CURSOR_ID_GRAB);
	_LoadCursor(fCursorGrabbing, cursorSet, B_CURSOR_ID_GRABBING);
	_LoadCursor(fCursorHelp, cursorSet, B_CURSOR_ID_HELP);
	_LoadCursor(fCursorIBeam, cursorSet, B_CURSOR_ID_I_BEAM);
	_LoadCursor(fCursorIBeamHorizontal, cursorSet,
		B_CURSOR_ID_I_BEAM_HORIZONTAL);
	_LoadCursor(fCursorMove, cursorSet, B_CURSOR_ID_MOVE);
	_LoadCursor(fCursorNotAllowed, cursorSet, B_CURSOR_ID_NOT_ALLOWED);
	_LoadCursor(fCursorProgress, cursorSet, B_CURSOR_ID_PROGRESS);
	_LoadCursor(fCursorResizeEast, cursorSet, B_CURSOR_ID_RESIZE_EAST);
	_LoadCursor(fCursorResizeEastWest, cursorSet,
		B_CURSOR_ID_RESIZE_EAST_WEST);
	_LoadCursor(fCursorResizeNorth, cursorSet, B_CURSOR_ID_RESIZE_NORTH);
	_LoadCursor(fCursorResizeNorthEast, cursorSet,
		B_CURSOR_ID_RESIZE_NORTH_EAST);
	_LoadCursor(fCursorResizeNorthEastSouthWest, cursorSet,
		B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST);
	_LoadCursor(fCursorResizeNorthSouth, cursorSet,
		B_CURSOR_ID_RESIZE_NORTH_SOUTH);
	_LoadCursor(fCursorResizeNorthWest, cursorSet,
		B_CURSOR_ID_RESIZE_NORTH_WEST);
	_LoadCursor(fCursorResizeNorthWestSouthEast, cursorSet,
		B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST);
	_LoadCursor(fCursorResizeSouth, cursorSet, B_CURSOR_ID_RESIZE_SOUTH);
	_LoadCursor(fCursorResizeSouthEast, cursorSet,
		B_CURSOR_ID_RESIZE_SOUTH_EAST);
	_LoadCursor(fCursorResizeSouthWest, cursorSet,
		B_CURSOR_ID_RESIZE_SOUTH_WEST);
	_LoadCursor(fCursorResizeWest, cursorSet, B_CURSOR_ID_RESIZE_WEST);
	_LoadCursor(fCursorZoomIn, cursorSet, B_CURSOR_ID_ZOOM_IN);
	_LoadCursor(fCursorZoomOut, cursorSet, B_CURSOR_ID_ZOOM_OUT);
}


/**
 * @brief Returns the cursor used for a particular system cursor ID.
 * @param which The BCursorID identifying which system cursor to retrieve.
 * @return Pointer to the ServerCursor, or NULL if @a which is invalid or unset.
 */
ServerCursor*
CursorManager::GetCursor(BCursorID which)
{
	BAutolock locker(this);

	switch (which) {
		case B_CURSOR_ID_SYSTEM_DEFAULT:
			return fCursorSystemDefault;
		case B_CURSOR_ID_CONTEXT_MENU:
			return fCursorContextMenu;
		case B_CURSOR_ID_COPY:
			return fCursorCopy;
		case B_CURSOR_ID_CREATE_LINK:
			return fCursorCreateLink;
		case B_CURSOR_ID_CROSS_HAIR:
			return fCursorCrossHair;
		case B_CURSOR_ID_FOLLOW_LINK:
			return fCursorFollowLink;
		case B_CURSOR_ID_GRAB:
			return fCursorGrab;
		case B_CURSOR_ID_GRABBING:
			return fCursorGrabbing;
		case B_CURSOR_ID_HELP:
			return fCursorHelp;
		case B_CURSOR_ID_I_BEAM:
			return fCursorIBeam;
		case B_CURSOR_ID_I_BEAM_HORIZONTAL:
			return fCursorIBeamHorizontal;
		case B_CURSOR_ID_MOVE:
			return fCursorMove;
		case B_CURSOR_ID_NO_CURSOR:
			return fCursorNoCursor;
		case B_CURSOR_ID_NOT_ALLOWED:
			return fCursorNotAllowed;
		case B_CURSOR_ID_PROGRESS:
			return fCursorProgress;
		case B_CURSOR_ID_RESIZE_EAST:
			return fCursorResizeEast;
		case B_CURSOR_ID_RESIZE_EAST_WEST:
			return fCursorResizeEastWest;
		case B_CURSOR_ID_RESIZE_NORTH:
			return fCursorResizeNorth;
		case B_CURSOR_ID_RESIZE_NORTH_EAST:
			return fCursorResizeNorthEast;
		case B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST:
			return fCursorResizeNorthEastSouthWest;
		case B_CURSOR_ID_RESIZE_NORTH_SOUTH:
			return fCursorResizeNorthSouth;
		case B_CURSOR_ID_RESIZE_NORTH_WEST:
			return fCursorResizeNorthWest;
		case B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST:
			return fCursorResizeNorthWestSouthEast;
		case B_CURSOR_ID_RESIZE_SOUTH:
			return fCursorResizeSouth;
		case B_CURSOR_ID_RESIZE_SOUTH_EAST:
			return fCursorResizeSouthEast;
		case B_CURSOR_ID_RESIZE_SOUTH_WEST:
			return fCursorResizeSouthWest;
		case B_CURSOR_ID_RESIZE_WEST:
			return fCursorResizeWest;
		case B_CURSOR_ID_ZOOM_IN:
			return fCursorZoomIn;
		case B_CURSOR_ID_ZOOM_OUT:
			return fCursorZoomOut;

		default:
			return NULL;
	}
}


/**
 * @brief Finds a registered cursor by its token.
 * @param token The token of the cursor to look up.
 * @return Pointer to the matching ServerCursor, or NULL if not found.
 */
ServerCursor*
CursorManager::FindCursor(int32 token)
{
	if (!Lock())
		return NULL;

	ServerCursor* cursor;
	if (fTokenSpace.GetToken(token, kCursorToken, (void**)&cursor) != B_OK)
		cursor = NULL;

	Unlock();

	return cursor;
}


/**
 * @brief Renders a vector cursor icon into a composite RGBA32 bitmap with shadow.
 *
 * The icon is rendered at a slightly larger internal size and then cropped.
 * A drop shadow is composited beneath the cursor pixels.
 *
 * @param size           Side length of the output bitmap in pixels.
 * @param vector         HVIF vector data.
 * @param vectorSize     Byte length of @a vector.
 * @param shadowStrength Alpha multiplier for the shadow (0.0–1.0).
 * @return A BBitmap in B_RGBA32 format containing the composited cursor.
 */
BBitmap
CursorManager::_RenderVectorCursor(uint32 size, const uint8* vector,
	uint32 vectorSize, float shadowStrength)
{
	const uint32 flags = B_BITMAP_NO_SERVER_LINK;

	// The cursor HVIFs need to be rendered at an equivalent of 32x32,
	// with everything outside the 22x22 area discarded.
	const int32 renderRectSize = (int32)(size * (32.0f / 22.0f));

	BBitmap renderCursor(BRect(0, 0, renderRectSize - 1, renderRectSize - 1), flags, B_RGBA32);
	status_t status = BIconUtils::GetVectorIcon(vector, vectorSize, &renderCursor);
	if (status != B_OK) {
		BBitmap fallback(BRect(0, 0, 21, 21), B_BITMAP_NO_SERVER_LINK, B_RGBA32);
		fallback.SetBits(kCursorFallbackBits, 22 * 22 * 4, 0, B_RGBA32);
		return fallback;
	}

	const BRect rect(0, 0, size - 1, size - 1);
	BBitmap cursor(rect, flags, B_RGBA32);
	cursor.ImportBits(&renderCursor, B_ORIGIN, B_ORIGIN, rect.Size());

	BBitmap shadow(rect, flags, B_RGBA32);
	memset(shadow.Bits(), 0, shadow.BitsLength());

	{
		int32 offset = size / 32;
		if (offset == 0)
			offset = 1; // <32px cursors

		shadow.ImportBits(&cursor, BPoint(0, 0), BPoint(offset, offset),
			BSize(size - offset - 1, size - offset - 1));

		agg::rendering_buffer buffer((unsigned char*)shadow.Bits(),
			size, size, shadow.BytesPerRow());
		agg::pixfmt_rgba32 pixFmt(buffer);

		agg::recursive_blur<agg::rgba8, agg::recursive_blur_calc_rgba<> > blur;
		blur.blur(pixFmt, 1);

		for (int32 i = 0; i < shadow.BitsLength(); i += 4) {
			uint8* bits = (uint8*)shadow.Bits() + i;
			bits[0] = 0;
			bits[1] = 0;
			bits[2] = 0;
			bits[3] = (uint8)(bits[3] * shadowStrength);
		}
	}

	BBitmap composite(rect, flags, B_RGBA32);

	uint8* s = (uint8*)shadow.Bits();
	uint8* c = (uint8*)cursor.Bits();
	uint8* d = (uint8*)composite.Bits();
	for (uint32 y = 0; y < size; y++) {
		for (uint32 x = 0; x < size; x++) {
			uint8 a = (uint8)(c[3] + (255 - c[3]) * (s[3] / 255.0));
			d[3] = a;
			for (int32 i = 0; i < 3; ++i) {
				d[i] = ((s[i] * (255 - c[3]) + 255) >> 8) + c[i];

				// premultiply
				d[i] = (uint8)(d[i] * int32(a) / 255.0);
			}
			s += 4;
			c += 4;
			d += 4;
		}
	}

	return composite;
}


/**
 * @brief Initializes a predefined system cursor member from HVIF data.
 *
 * This method must only be called during CursorManager construction.
 *
 * @param cursorMember Reference to the cursor pointer to initialize.
 * @param id           The BCursorID this cursor represents.
 * @param vector       HVIF vector data (NULL for the no-cursor placeholder).
 * @param vectorSize   Byte length of @a vector.
 * @param hotSpot      Hotspot position in unscaled cursor coordinates.
 * @param scale        Display scale factor.
 */
void
CursorManager::_InitCursor(ServerCursor*& cursorMember, BCursorID id,
	const uint8* vector, uint32 vectorSize, const BPoint& hotSpot, float scale)
{
	int32 cursorSize = (int32)(22 * scale);
	float shadow = 3 / 10.0;
	BPoint scaledHotspot((int32)(hotSpot.x * scale), (int32)(hotSpot.y * scale));

	if (vector != NULL) {
		BBitmap bitmap = _RenderVectorCursor(cursorSize, vector, vectorSize, shadow);
		cursorMember = new ServerCursor((uint8*)bitmap.Bits(), cursorSize,
			cursorSize, bitmap.ColorSpace());
	} else {
		const unsigned char noCursor[] = {0x00, 0x00, 0x00, 0x00};
		cursorMember = new ServerCursor(noCursor, 1, 1, B_RGBA32);
	}

	cursorMember->SetHotSpot(scaledHotspot);
	AddCursor(cursorMember, id);
}


/**
 * @brief Loads a cursor from a CursorSet and registers it under the given ID.
 * @param cursorMember Reference to the cursor pointer to update on success.
 * @param set          The CursorSet to search in.
 * @param id           The BCursorID to load.
 */
void
CursorManager::_LoadCursor(ServerCursor*& cursorMember, const CursorSet& set,
	BCursorID id)
{
	ServerCursor* cursor;
	if (set.FindCursor(id, &cursor) == B_OK) {
		AddCursor(cursor, id);
		cursorMember = cursor;
	}
}


/**
 * @brief Searches the cursor list for a cursor owned by @a clientTeam with
 *        matching raw cursor data.
 * @param clientTeam The owning team ID to match.
 * @param cursorData Pointer to 68-byte R5-format cursor data to compare.
 * @return Matching ServerCursor, or NULL if not found.
 */
ServerCursor*
CursorManager::_FindCursor(team_id clientTeam, const uint8* cursorData)
{
	int32 count = fCursorList.CountItems();
	for (int32 i = 0; i < count; i++) {
		ServerCursor* cursor = (ServerCursor*)fCursorList.ItemAtFast(i);
		if (cursor->OwningTeam() == clientTeam
			&& cursor->CursorData()
			&& memcmp(cursor->CursorData(), cursorData, 68) == 0) {
			return cursor;
		}
	}
	return NULL;
}


/**
 * @brief Internal helper that removes a cursor from the list and token space.
 *
 * Does not release the cursor's reference; callers are responsible for that.
 *
 * @param cursor The ServerCursor to remove.
 */
void
CursorManager::_RemoveCursor(ServerCursor* cursor)
{
	fCursorList.RemoveItem(cursor);
	fTokenSpace.RemoveToken(cursor->fToken);
	cursor->fToken = -1;
}
