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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2006-2009, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */
#ifndef _CURSOR_H
#define _CURSOR_H

/**
 * @file Cursor.h
 * @brief Defines the BCursor class and BCursorID enumeration for mouse cursor management.
 */

#include <Archivable.h>
#include <InterfaceDefs.h>


/**
 * @enum BCursorID
 * @brief Identifies the system-defined mouse cursor shapes.
 *
 * These constants are used with the BCursor(BCursorID) constructor to
 * create a cursor that uses one of the standard system cursor images.
 */
enum BCursorID {
	B_CURSOR_ID_SYSTEM_DEFAULT					= 1,	/**< Default system pointer cursor. */

	B_CURSOR_ID_CONTEXT_MENU					= 3,	/**< Context menu cursor (arrow with menu). */
	B_CURSOR_ID_COPY							= 4,	/**< Copy operation cursor (arrow with plus). */
	B_CURSOR_ID_CREATE_LINK						= 29,	/**< Create link cursor (arrow with link icon). */
	B_CURSOR_ID_CROSS_HAIR						= 5,	/**< Cross-hair precision cursor. */
	B_CURSOR_ID_FOLLOW_LINK						= 6,	/**< Follow hyperlink cursor (pointing hand). */
	B_CURSOR_ID_GRAB							= 7,	/**< Open hand grab cursor. */
	B_CURSOR_ID_GRABBING						= 8,	/**< Closed hand grabbing cursor. */
	B_CURSOR_ID_HELP							= 9,	/**< Help cursor (arrow with question mark). */
	B_CURSOR_ID_I_BEAM							= 2,	/**< I-beam text selection cursor. */
	B_CURSOR_ID_I_BEAM_HORIZONTAL				= 10,	/**< Horizontal I-beam cursor for vertical text. */
	B_CURSOR_ID_MOVE							= 11,	/**< Move cursor (four-directional arrows). */
	B_CURSOR_ID_NO_CURSOR						= 12,	/**< Hidden cursor (no visible cursor). */
	B_CURSOR_ID_NOT_ALLOWED						= 13,	/**< Not-allowed cursor (circle with line). */
	B_CURSOR_ID_PROGRESS						= 14,	/**< Progress cursor (arrow with spinner). */
	B_CURSOR_ID_RESIZE_NORTH					= 15,	/**< Resize north (up) cursor. */
	B_CURSOR_ID_RESIZE_EAST						= 16,	/**< Resize east (right) cursor. */
	B_CURSOR_ID_RESIZE_SOUTH					= 17,	/**< Resize south (down) cursor. */
	B_CURSOR_ID_RESIZE_WEST						= 18,	/**< Resize west (left) cursor. */
	B_CURSOR_ID_RESIZE_NORTH_EAST				= 19,	/**< Resize north-east (diagonal) cursor. */
	B_CURSOR_ID_RESIZE_NORTH_WEST				= 20,	/**< Resize north-west (diagonal) cursor. */
	B_CURSOR_ID_RESIZE_SOUTH_EAST				= 21,	/**< Resize south-east (diagonal) cursor. */
	B_CURSOR_ID_RESIZE_SOUTH_WEST				= 22,	/**< Resize south-west (diagonal) cursor. */
	B_CURSOR_ID_RESIZE_NORTH_SOUTH				= 23,	/**< Resize north-south (vertical) cursor. */
	B_CURSOR_ID_RESIZE_EAST_WEST				= 24,	/**< Resize east-west (horizontal) cursor. */
	B_CURSOR_ID_RESIZE_NORTH_EAST_SOUTH_WEST	= 25,	/**< Resize diagonal (NE-SW) cursor. */
	B_CURSOR_ID_RESIZE_NORTH_WEST_SOUTH_EAST	= 26,	/**< Resize diagonal (NW-SE) cursor. */
	B_CURSOR_ID_ZOOM_IN							= 27,	/**< Zoom in cursor (magnifier with plus). */
	B_CURSOR_ID_ZOOM_OUT						= 28	/**< Zoom out cursor (magnifier with minus). */
};


/**
 * @brief Represents a mouse cursor image.
 *
 * BCursor encapsulates a mouse cursor, which can be created from raw cursor
 * data, a system cursor ID, a bitmap with a hotspot, or an archived message.
 * Cursors can be set as the active cursor using BApplication::SetCursor() or
 * BView::SetViewCursor().
 *
 * @see BApplication::SetCursor()
 * @see BView::SetViewCursor()
 * @see BCursorID
 */
class BCursor : BArchivable {
public:
	/**
	 * @brief Constructs a cursor from raw cursor data.
	 *
	 * The data must be in the legacy 68-byte cursor format: 2 bytes for
	 * dimensions, 2 bytes for hotspot, 32 bytes for image, 32 bytes for mask.
	 *
	 * @param cursorData  Pointer to the raw cursor data.
	 */
								BCursor(const void* cursorData);

	/**
	 * @brief Copy constructor.
	 *
	 * @param other  The BCursor to copy.
	 */
								BCursor(const BCursor& other);

	/**
	 * @brief Constructs a cursor from a system cursor ID.
	 *
	 * @param id  The BCursorID identifying the desired system cursor.
	 */
								BCursor(BCursorID id);

	/**
	 * @brief Constructs a cursor from an archived BMessage.
	 *
	 * @param data  The archived message containing cursor data.
	 */
								BCursor(BMessage* data);

	/**
	 * @brief Constructs a cursor from a bitmap and hotspot.
	 *
	 * @param bitmap   The bitmap image to use as the cursor.
	 * @param hotspot  The point within the bitmap that is the active click point.
	 */
								BCursor(const BBitmap* bitmap,
									const BPoint& hotspot);

	/**
	 * @brief Destructor.
	 *
	 * Releases the cursor's server-side resources.
	 */
	virtual	~BCursor();

	/**
	 * @brief Checks whether the cursor was initialized successfully.
	 *
	 * @return B_OK if the cursor is valid, or an error code otherwise.
	 */
			status_t			InitCheck() const;

	/**
	 * @brief Archives the cursor into a BMessage.
	 *
	 * @param archive  The message to archive into.
	 * @param deep     If true, archives child objects as well.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			Archive(BMessage* archive,
									bool deep = true) const;

	/**
	 * @brief Instantiates a BCursor from an archived BMessage.
	 *
	 * @param archive  The archived message to instantiate from.
	 * @return A pointer to the new BArchivable (BCursor), or NULL on failure.
	 */
	static	BArchivable*		Instantiate(BMessage* archive);

	/**
	 * @brief Assignment operator.
	 *
	 * @param other  The BCursor to assign from.
	 * @return A reference to this BCursor.
	 */
			BCursor&			operator=(const BCursor& other);

	/**
	 * @brief Equality operator.
	 *
	 * @param other  The BCursor to compare with.
	 * @return true if the cursors are equal, false otherwise.
	 */
			bool				operator==(const BCursor& other) const;

	/**
	 * @brief Inequality operator.
	 *
	 * @param other  The BCursor to compare with.
	 * @return true if the cursors are not equal, false otherwise.
	 */
			bool				operator!=(const BCursor& other) const;

private:
	virtual	status_t			Perform(perform_code d, void* arg);

	virtual	void				_ReservedCursor1();
	virtual	void				_ReservedCursor2();
	virtual	void				_ReservedCursor3();
	virtual	void				_ReservedCursor4();

			void				_FreeCursorData();

private:
	friend class BApplication;
	friend class BView;

			int32				fServerToken;
			bool				fNeedToFree;

			bool				_reservedWasPendingViewCursor;
				// Probably bogus because of padding.
			uint32				_reserved[6];
};

#endif	// _CURSOR_H
