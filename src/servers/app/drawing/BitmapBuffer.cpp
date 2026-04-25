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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BitmapBuffer.cpp
 * @brief Adapter exposing a (server-side) ServerBitmap as a RenderingBuffer.
 *
 * Unlike BBitmapBuffer this wrapper does not own the underlying bitmap, since
 * ServerBitmap lifetimes are normally managed by the BitmapManager.
 */


#include "ServerBitmap.h"

#include "BitmapBuffer.h"

// TODO: It should be more or less guaranteed that this object
// is not used if InitCheck() returns an error, so the checks
// in all thos functions should probably be removed...


/**
 * @brief Constructs the adapter without taking ownership of @a bitmap.
 *
 * @param bitmap ServerBitmap to wrap. The caller is responsible for keeping
 *               it alive while the BitmapBuffer is in use.
 */
BitmapBuffer::BitmapBuffer(ServerBitmap* bitmap)
	: fBitmap(bitmap)
{
}


/**
 * @brief Destructor; the wrapped ServerBitmap is left untouched.
 */
BitmapBuffer::~BitmapBuffer()
{
	// We don't own the ServerBitmap
}


/**
 * @brief Reports whether the wrapped bitmap is set and valid.
 *
 * @retval B_OK       The bitmap reports IsValid().
 * @retval B_NO_INIT  No bitmap was supplied.
 * @retval B_ERROR    The bitmap is set but is not valid.
 */
status_t
BitmapBuffer::InitCheck() const
{
	status_t ret = B_NO_INIT;
	if (fBitmap)
		ret = fBitmap->IsValid() ? B_OK : B_ERROR;
	return ret;
}


/**
 * @brief Returns the wrapped bitmap's color space.
 * @return Color space, or B_NO_COLOR_SPACE when invalid.
 */
color_space
BitmapBuffer::ColorSpace() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->ColorSpace();
	return B_NO_COLOR_SPACE;
}


/**
 * @brief Returns the wrapped bitmap's pixel pointer.
 * @return Pointer to the first pixel, or NULL when invalid.
 */
void*
BitmapBuffer::Bits() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->Bits();
	return NULL;
}


/**
 * @brief Returns the wrapped bitmap's row stride.
 * @return Row stride in bytes, or 0 when invalid.
 */
uint32
BitmapBuffer::BytesPerRow() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->BytesPerRow();
	return 0;
}


/**
 * @brief Returns the wrapped bitmap's width in pixels.
 * @return Width in pixels, or 0 when invalid.
 */
uint32
BitmapBuffer::Width() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->Width();
	return 0;
}


/**
 * @brief Returns the wrapped bitmap's height in pixels.
 * @return Height in pixels, or 0 when invalid.
 */
uint32
BitmapBuffer::Height() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->Height();
	return 0;
}

