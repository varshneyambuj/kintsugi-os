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
 *   Copyright 2001-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file DWindowBuffer.cpp
 * @brief Implementation of DWindowBuffer, the RenderingBuffer view over a
 *        BDirectWindow's direct frame buffer or an accelerant frame buffer.
 *
 * Used by DWindowHWInterface (test/debug app_server mode) to expose the
 * direct-mode pixel memory of a BDirectWindow to the drawing engine while
 * tracking the window's current clipping region.
 */


#include <stdio.h>

#include <Accelerant.h>
#include <DirectWindow.h>

#include "DWindowBuffer.h"


/**
 * @brief Constructs an empty buffer with no backing storage.
 *
 * The buffer is unusable until SetTo() is called with a valid
 * direct_buffer_info or frame_buffer_config.
 */
DWindowBuffer::DWindowBuffer()
	: fBits(NULL),
	  fWidth(0),
	  fHeight(0),
	  fBytesPerRow(0),
	  fFormat(B_NO_COLOR_SPACE),
	  fWindowClipping()
{
}


/**
 * @brief Destroys the buffer; backing storage is owned elsewhere.
 */
DWindowBuffer::~DWindowBuffer()
{
}


/**
 * @brief Reports whether the buffer currently points at valid pixel memory.
 *
 * @return     B_OK when a backing buffer is bound, B_NO_INIT otherwise.
 * @retval B_OK       Bits() refers to live pixel memory.
 * @retval B_NO_INIT  No SetTo() call has bound a buffer yet.
 */
status_t
DWindowBuffer::InitCheck() const
{
	if (fBits)
		return B_OK;

	return B_NO_INIT;
}


/**
 * @brief Returns the colour space of the bound buffer.
 *
 * @return     The color_space recorded by the most recent SetTo() call, or
 *             B_NO_COLOR_SPACE if no buffer is bound.
 */
color_space
DWindowBuffer::ColorSpace() const
{
	return fFormat;
}


/**
 * @brief Returns the raw pixel pointer for the buffer.
 *
 * @return     Pointer to the top-left pixel, already offset for the window's
 *             screen position; NULL if no buffer is bound.
 */
void*
DWindowBuffer::Bits() const
{
	return (void*)fBits;
}


/**
 * @brief Returns the row stride of the bound buffer in bytes.
 *
 * @return     Number of bytes between successive scan lines.
 */
uint32
DWindowBuffer::BytesPerRow() const
{
	return fBytesPerRow;
}


/**
 * @brief Returns the buffer width in pixels.
 *
 * @return     Number of pixels per row.
 */
uint32
DWindowBuffer::Width() const
{
	return fWidth;
}


/**
 * @brief Returns the buffer height in scan lines.
 *
 * @return     Number of scan lines in the buffer.
 */
uint32
DWindowBuffer::Height() const
{
	return fHeight;
}


/**
 * @brief Binds the buffer to a BDirectWindow direct_buffer_info update.
 *
 * Rebuilds the window clipping region from the supplied clip list, offsets
 * the region into screen coordinates, and points fBits at the top-left of
 * the window's bounds within the direct frame buffer. Passing NULL detaches
 * the buffer (B_DIRECT_STOP semantics).
 *
 * @param info  Direct-mode buffer update, or NULL to detach.
 * @note  No bounds checking is performed against the actual frame buffer
 *        size; the caller must respect @a info's clip list when drawing.
 */
void
DWindowBuffer::SetTo(direct_buffer_info* info)
{
	fWindowClipping.MakeEmpty();

	if (info) {
		int32 xOffset = info->window_bounds.left;
		int32 yOffset = info->window_bounds.top;
		// Get clipping information
		for (uint32 i = 0; i < info->clip_list_count; i++) {
			fWindowClipping.Include(info->clip_list[i]);
		}
		fWindowClipping.OffsetBy(xOffset, yOffset);

		fBytesPerRow = info->bytes_per_row;
		fBits = (uint8*)info->bits;
		fFormat = info->pixel_format;
		fWidth = info->window_bounds.right - info->window_bounds.left + 1;
		fHeight = info->window_bounds.bottom - info->window_bounds.top + 1;
		// offset bits to left top corner of window
		fBits += xOffset * 4 + yOffset * fBytesPerRow;
	} else {
		fBits = NULL;
		fWidth = 0;
		fHeight = 0;
		fBytesPerRow = 0;
		fFormat = B_NO_COLOR_SPACE;
	}
}


/**
 * @brief Binds the buffer to an accelerant-managed frame buffer region.
 *
 * Used when the underlying surface is reported by the graphics accelerant
 * (frame_buffer_config) rather than by BDirectWindow. The buffer is offset
 * into the supplied (x, y) origin and sized by (width, height).
 *
 * @param config  Accelerant frame-buffer configuration; must be non-NULL.
 * @param x       Horizontal offset into the frame buffer (pixels).
 * @param y       Vertical offset into the frame buffer (pixels).
 * @param width   Width of the exposed region in pixels.
 * @param height  Height of the exposed region in pixels.
 * @param format  Colour space of the exposed region.
 */
void
DWindowBuffer::SetTo(frame_buffer_config* config,
					 uint32 x, uint32 y,
					 uint32 width, uint32 height,
					 color_space format)
{
	fBits = (uint8*)config->frame_buffer;
	fBytesPerRow = config->bytes_per_row;
	fBits += x * 4 + y * fBytesPerRow;
	fWidth = width;
	fHeight = height;
	fFormat = format;
}
