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
 * MIT License. Copyright 2009, Haiku, Inc.
 * Original author: Michael Lotz.
 */

/** @file RemoteMessage.h
    @brief Wire-format encoder and decoder for the remote-display
           RP_* protocol used between app_server and the network viewer. */

#ifndef REMOTE_MESSAGE_H
#define REMOTE_MESSAGE_H

#ifndef CLIENT_COMPILE
#	include "PatternHandler.h"
#	include <ViewPrivate.h>
#endif

#include "StreamingRingBuffer.h"

#include <AffineTransform.h>
#include <GraphicsDefs.h>
#include <Region.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class BBitmap;
class BFont;
class BGradient;
class BView;
class DrawState;
class Pattern;
class RemotePainter;
class ServerBitmap;
class ServerCursor;
class ServerFont;
struct ViewLineArrayInfo;

/** @brief RP_* opcodes carried in the leading uint16 of every
           RemoteMessage frame. Value ranges are blocked out by category
           (connection / state / draw / cursor / input / gradient) to make
           extensions easy and routing fast. */
enum {
	RP_INIT_CONNECTION = 1,
	RP_UPDATE_DISPLAY_MODE,
	RP_CLOSE_CONNECTION,
	RP_GET_SYSTEM_PALETTE,
	RP_GET_SYSTEM_PALETTE_RESULT,

	RP_CREATE_STATE = 20,
	RP_DELETE_STATE,
	RP_ENABLE_SYNC_DRAWING,
	RP_DISABLE_SYNC_DRAWING,
	RP_INVALIDATE_RECT,
	RP_INVALIDATE_REGION,

	RP_SET_OFFSETS = 40,
	RP_SET_HIGH_COLOR,
	RP_SET_LOW_COLOR,
	RP_SET_PEN_SIZE,
	RP_SET_STROKE_MODE,
	RP_SET_BLENDING_MODE,
	RP_SET_PATTERN,
	RP_SET_DRAWING_MODE,
	RP_SET_FONT,
	RP_SET_TRANSFORM,

	RP_CONSTRAIN_CLIPPING_REGION = 60,
	RP_COPY_RECT_NO_CLIPPING,
	RP_INVERT_RECT,
	RP_DRAW_BITMAP,
	RP_DRAW_BITMAP_RECTS,

	RP_STROKE_ARC = 80,
	RP_STROKE_BEZIER,
	RP_STROKE_ELLIPSE,
	RP_STROKE_POLYGON,
	RP_STROKE_RECT,
	RP_STROKE_ROUND_RECT,
	RP_STROKE_SHAPE,
	RP_STROKE_TRIANGLE,
	RP_STROKE_LINE,
	RP_STROKE_LINE_ARRAY,

	RP_FILL_ARC = 100,
	RP_FILL_BEZIER,
	RP_FILL_ELLIPSE,
	RP_FILL_POLYGON,
	RP_FILL_RECT,
	RP_FILL_ROUND_RECT,
	RP_FILL_SHAPE,
	RP_FILL_TRIANGLE,
	RP_FILL_REGION,

	RP_FILL_ARC_GRADIENT = 120,
	RP_FILL_BEZIER_GRADIENT,
	RP_FILL_ELLIPSE_GRADIENT,
	RP_FILL_POLYGON_GRADIENT,
	RP_FILL_RECT_GRADIENT,
	RP_FILL_ROUND_RECT_GRADIENT,
	RP_FILL_SHAPE_GRADIENT,
	RP_FILL_TRIANGLE_GRADIENT,
	RP_FILL_REGION_GRADIENT,

	RP_STROKE_POINT_COLOR = 140,
	RP_STROKE_LINE_1PX_COLOR,
	RP_STROKE_RECT_1PX_COLOR,

	RP_FILL_RECT_COLOR = 160,
	RP_FILL_REGION_COLOR_NO_CLIPPING,

	RP_DRAW_STRING = 180,
	RP_DRAW_STRING_WITH_OFFSETS,
	RP_DRAW_STRING_RESULT,
	RP_STRING_WIDTH,
	RP_STRING_WIDTH_RESULT,
	RP_READ_BITMAP,
	RP_READ_BITMAP_RESULT,

	RP_SET_CURSOR = 200,
	RP_SET_CURSOR_VISIBLE,
	RP_MOVE_CURSOR_TO,

	RP_MOUSE_MOVED = 220,
	RP_MOUSE_DOWN,
	RP_MOUSE_UP,
	RP_MOUSE_WHEEL_CHANGED,

	RP_KEY_DOWN = 240,
	RP_KEY_UP,
	RP_UNMAPPED_KEY_DOWN,
	RP_UNMAPPED_KEY_UP,
	RP_MODIFIERS_CHANGED,

	RP_STROKE_ARC_GRADIENT = 260,
	RP_STROKE_BEZIER_GRADIENT,
	RP_STROKE_ELLIPSE_GRADIENT,
	RP_STROKE_POLYGON_GRADIENT,
	RP_STROKE_RECT_GRADIENT,
	RP_STROKE_ROUND_RECT_GRADIENT,
	RP_STROKE_SHAPE_GRADIENT,
	RP_STROKE_TRIANGLE_GRADIENT,
	RP_STROKE_LINE_GRADIENT,
};


/** @brief One end of the RP_* wire protocol. A RemoteMessage instance is
           either a writer (target ring set), a reader (source ring set),
           or both; every encode buffers into a small heap-grown work area
           that Flush() copies into the target ring as a single contiguous
           frame, while the decode side pulls bytes directly from the source
           ring. The leading uint16 carries the opcode and is followed by a
           uint32 length so receivers can size each frame. */
class RemoteMessage {
public:
								RemoteMessage(StreamingRingBuffer* source,
									StreamingRingBuffer *target);
								~RemoteMessage();

		void					Start(uint16 code);
		status_t				Flush();
		void					Cancel();

		status_t				NextMessage(uint16& code);
		/** @brief Returns the opcode of the message currently being
		           decoded; valid only after a successful NextMessage(). */
		uint16					Code() { return fCode; }
		/** @brief Returns the number of payload bytes remaining in the
		           current inbound frame. */
		uint32					DataLeft() { return fDataLeft; }

		template<typename T>
		void					Add(const T& value);

		void					AddString(const char* string, size_t length);
		void					AddRegion(const BRegion& region);
		void					AddGradient(const BGradient& gradient);
		void					AddTransform(const BAffineTransform& transform);

#ifndef CLIENT_COMPILE
		void					AddBitmap(const ServerBitmap& bitmap,
									bool minimal = false);
		void					AddFont(const ServerFont& font);
		void					AddPattern(const Pattern& pattern);
		void					AddDrawState(const DrawState& drawState);
		void					AddArrayLine(const ViewLineArrayInfo& line);
		void					AddCursor(const ServerCursor& cursor);
#else
		void					AddBitmap(const BBitmap& bitmap);
#endif

		template<typename T>
		void					AddList(const T* array, int32 count);

		template<typename T>
		status_t				Read(T& value);

		status_t				ReadRegion(BRegion& region);
		status_t				ReadFontState(BFont& font);
									// sets font state
		status_t				ReadViewState(BView& view, ::pattern& pattern);
									// sets viewstate and returns pattern

		status_t				ReadString(char** _string, size_t& length);
		status_t				ReadBitmap(BBitmap** _bitmap,
									bool minimal = false,
									color_space colorSpace = B_RGB32,
									uint32 flags = 0);
		status_t				ReadGradient(BGradient** _gradient);
		status_t				ReadTransform(BAffineTransform& transform);
		status_t				ReadArrayLine(BPoint& startPoint,
									BPoint& endPoint, rgb_color& color);

		template<typename T>
		status_t				ReadList(T* array, int32 count);

private:
		bool					_MakeSpace(size_t size);

		StreamingRingBuffer*	fSource;
		StreamingRingBuffer*	fTarget;

		uint8*					fBuffer;
		size_t					fAvailable;
		size_t					fWriteIndex;
		uint32					fDataLeft;
		uint16					fCode;
};


/**
 * @brief Constructs a message bound to the given inbound and/or outbound
 *        ring buffers.
 *
 * @param source  Ring to read inbound bytes from; may be NULL when the
 *                instance is used purely for encoding.
 * @param target  Ring to write encoded frames into; may be NULL when the
 *                instance is used purely for decoding.
 */
inline
RemoteMessage::RemoteMessage(StreamingRingBuffer* source,
	StreamingRingBuffer* target)
	:
	fSource(source),
	fTarget(target),
	fBuffer(NULL),
	fAvailable(0),
	fWriteIndex(0),
	fDataLeft(0)
{
}


/**
 * @brief Flushes any pending frame and frees the working buffer.
 */
inline
RemoteMessage::~RemoteMessage()
{
	if (fWriteIndex > 0)
		Flush();
	free(fBuffer);
}


/**
 * @brief Begins a new outbound frame with the given opcode.
 *
 * Writes the opcode and a placeholder length into the working buffer; the
 * length is back-patched when Flush() runs. Auto-flushes any previously
 * staged frame.
 *
 * @param code  RP_* opcode for the frame.
 */
inline void
RemoteMessage::Start(uint16 code)
{
	if (fWriteIndex > 0)
		Flush();

	Add(code);

	uint32 sizeDummy = 0;
	Add(sizeDummy);
}


/**
 * @brief Patches the frame length field and pushes the staged bytes into
 *        the target ring as a single Write().
 *
 * @return     B_OK on success, B_NO_INIT if nothing has been staged or the
 *             instance has no target ring, otherwise the error code from
 *             the underlying ring write.
 */
inline status_t
RemoteMessage::Flush()
{
	if (fWriteIndex == 0 || fTarget == NULL)
		return B_NO_INIT;

	uint32 length = fWriteIndex;
	fAvailable += fWriteIndex;
	fWriteIndex = 0;

	memcpy(fBuffer + sizeof(uint16), &length, sizeof(uint32));
	return fTarget->Write(fBuffer, length);
}


/**
 * @brief Appends a POD value verbatim to the staged frame.
 *
 * @param value  Value to append; must be trivially copyable.
 * @note  Silently drops the byte on allocation failure of the working
 *        buffer; callers normally Flush() and inspect its return code to
 *        observe transport errors.
 */
template<typename T>
inline void
RemoteMessage::Add(const T& value)
{
	if (!_MakeSpace(sizeof(T)))
		return;

	memcpy(fBuffer + fWriteIndex, &value, sizeof(T));
	fWriteIndex += sizeof(T);
	fAvailable -= sizeof(T);
}


/**
 * @brief Appends a length-prefixed byte string.
 *
 * @param string  Source bytes; may be non-NUL-terminated.
 * @param length  Number of bytes to copy.
 */
inline void
RemoteMessage::AddString(const char* string, size_t length)
{
	Add((uint32)length);
	if (length > fAvailable && !_MakeSpace(length))
		return;

	memcpy(fBuffer + fWriteIndex, string, length);
	fWriteIndex += length;
	fAvailable -= length;
}


/**
 * @brief Appends a BRegion as a uint32 rectangle count followed by each
 *        BRect.
 *
 * @param region  Region to encode.
 */
inline void
RemoteMessage::AddRegion(const BRegion& region)
{
	int32 rectCount = region.CountRects();
	Add(rectCount);

	for (int32 i = 0; i < rectCount; i++)
		Add(region.RectAt(i));
}


/**
 * @brief Appends @a count POD values from @a array in order.
 *
 * @param array  Source array of length @a count.
 * @param count  Number of elements to append.
 */
template<typename T>
inline void
RemoteMessage::AddList(const T* array, int32 count)
{
	for (int32 i = 0; i < count; i++)
		Add(array[i]);
}


/**
 * @brief Reads one POD value from the inbound ring into @a value.
 *
 * @param value  Destination; populated only on success.
 * @return       B_OK on success, B_ERROR if the current frame has too few
 *               bytes left or the ring read short, B_NO_INIT if the
 *               instance has no source ring, or the negative ring error
 *               code.
 */
template<typename T>
inline status_t
RemoteMessage::Read(T& value)
{
	if (fDataLeft < sizeof(T))
		return B_ERROR;

	if (fSource == NULL)
		return B_NO_INIT;

	int32 readSize = fSource->Read(&value, sizeof(T));
	if (readSize < 0)
		return readSize;

	if (readSize != sizeof(T))
		return B_ERROR;

	fDataLeft -= sizeof(T);
	return B_OK;
}


/**
 * @brief Reads a region encoded by AddRegion() into @a region.
 *
 * The region is emptied first, then re-populated from the inbound
 * rectangles.
 *
 * @param region  Destination region.
 * @return        B_OK on success, B_ERROR or the underlying read error
 *                otherwise.
 */
inline status_t
RemoteMessage::ReadRegion(BRegion& region)
{
	region.MakeEmpty();

	int32 rectCount;
	status_t result = Read(rectCount);
	if (result != B_OK)
		return B_ERROR;

	for (int32 i = 0; i < rectCount; i++) {
		BRect rect;
		status_t result = Read(rect);
		if (result != B_OK)
			return result;

		region.Include(rect);
	}

	return B_OK;
}


/**
 * @brief Reads @a count POD values into @a array in order.
 *
 * @param array  Destination array of length @a count.
 * @param count  Number of elements to read.
 * @return       B_OK on success, otherwise the first error encountered.
 */
template<typename T>
inline status_t
RemoteMessage::ReadList(T* array, int32 count)
{
	for (int32 i = 0; i < count; i++) {
		status_t result = Read(array[i]);
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


/**
 * @brief Grows the working buffer so at least @a size bytes can be staged.
 *
 * Always reserves a small slack on top of the requested size to amortise
 * realloc cost.
 *
 * @param size  Minimum number of free bytes the caller needs.
 * @return      true on success, false if realloc() fails.
 */
inline bool
RemoteMessage::_MakeSpace(size_t size)
{
	if (fAvailable >= size)
		return true;

	size_t extraSize = size + 20;
	uint8 *newBuffer = (uint8*)realloc(fBuffer, fWriteIndex + extraSize);
	if (newBuffer == NULL)
		return false;

	fAvailable = extraSize;
	fBuffer = newBuffer;
	return true;
}

#endif // REMOTE_MESSAGE_H
