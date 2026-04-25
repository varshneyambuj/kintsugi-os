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
 * @file BBitmapBuffer.cpp
 * @brief Adapter that exposes a (client-side) BBitmap as a RenderingBuffer.
 *
 * Used in places that need to feed a BBitmap to code expecting the abstract
 * RenderingBuffer interface; the wrapper takes ownership of the bitmap so
 * the pixel storage outlives the wrapper itself.
 */


#include <Bitmap.h>

#include "BBitmapBuffer.h"


/**
 * @brief Constructs the adapter and assumes ownership of @a bitmap.
 *
 * @param bitmap BBitmap to wrap. Ownership transfers to the adapter; the
 *               caller must not delete it.
 */
BBitmapBuffer::BBitmapBuffer(BBitmap* bitmap)
	: fBitmap(bitmap)
{
}


/**
 * @brief Destructor; the wrapped BBitmap is freed by ObjectDeleter.
 */
BBitmapBuffer::~BBitmapBuffer()
{
}


/**
 * @brief Reflects the wrapped bitmap's initialisation state.
 *
 * @retval B_OK       BBitmap is set and reports B_OK from InitCheck().
 * @retval B_NO_INIT  No bitmap was supplied at construction time.
 * @return Other      Whatever BBitmap::InitCheck() returns.
 */
status_t
BBitmapBuffer::InitCheck() const
{
	status_t ret = B_NO_INIT;
	if (fBitmap.IsSet())
		ret = fBitmap->InitCheck();
	return ret;
}


/**
 * @brief Returns the wrapped bitmap's color space.
 * @return The bitmap's color space, or B_NO_COLOR_SPACE if unset / invalid.
 */
color_space
BBitmapBuffer::ColorSpace() const
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
BBitmapBuffer::Bits() const
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
BBitmapBuffer::BytesPerRow() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->BytesPerRow();
	return 0;
}


/**
 * @brief Returns the wrapped bitmap's width in pixels.
 * @return Width in pixels (Bounds().IntegerWidth() + 1), or 0 when invalid.
 */
uint32
BBitmapBuffer::Width() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->Bounds().IntegerWidth() + 1;
	return 0;
}


/**
 * @brief Returns the wrapped bitmap's height in pixels.
 * @return Height in pixels (Bounds().IntegerHeight() + 1), or 0 when invalid.
 */
uint32
BBitmapBuffer::Height() const
{
	if (InitCheck() >= B_OK)
		return fBitmap->Bounds().IntegerHeight() + 1;
	return 0;
}

