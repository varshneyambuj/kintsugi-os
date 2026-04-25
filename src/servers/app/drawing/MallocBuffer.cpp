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
 * @file MallocBuffer.cpp
 * @brief RenderingBuffer backed by a malloc()'d B_RGBA32 pixel block.
 *
 * MallocBuffer is the simplest concrete RenderingBuffer: a fixed-size,
 * 4-byte-per-pixel array allocated up front. It is used as a scratch target
 * by code that needs an off-screen 32-bit buffer without the overhead of a
 * full ServerBitmap.
 */


#include <malloc.h>

#include "MallocBuffer.h"

// TODO: maybe this class could be more flexible by taking
// a color_space argument in the constructor
// the hardcoded width * 4 (because that's how it's used now anyways)
// could be avoided, but I'm in a hurry... :-)


/**
 * @brief Allocates a contiguous B_RGBA32 buffer of the requested dimensions.
 *
 * No allocation is performed when either dimension is zero, leaving the
 * buffer in an uninitialised state that InitCheck() will report.
 *
 * @param width  Width in pixels.
 * @param height Height in pixels.
 */
MallocBuffer::MallocBuffer(uint32 width,
						   uint32 height)
	: fBuffer(NULL),
	  fWidth(width),
	  fHeight(height)
{
	if (fWidth > 0 && fHeight > 0) {
		fBuffer = malloc((fWidth * 4) * fHeight);
	}
}


/**
 * @brief Frees the malloc()'d pixel buffer if one was allocated.
 */
MallocBuffer::~MallocBuffer()
{
	if (fBuffer)
		free(fBuffer);
}


/**
 * @brief Reports whether the buffer was successfully allocated.
 *
 * @retval B_OK         The buffer is allocated and ready to use.
 * @retval B_NO_MEMORY  Allocation failed (or zero dimensions were requested).
 */
status_t
MallocBuffer::InitCheck() const
{
	return fBuffer ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Returns the (fixed) color space of the buffer.
 * @return Always B_RGBA32.
 */
color_space
MallocBuffer::ColorSpace() const
{
	return B_RGBA32;
}


/**
 * @brief Returns a pointer to the raw pixel data.
 * @return The pixel pointer when InitCheck() succeeded, NULL otherwise.
 */
void*
MallocBuffer::Bits() const
{
	if (InitCheck() >= B_OK)
		return fBuffer;
	return NULL;
}


/**
 * @brief Returns the row stride in bytes.
 * @return Width times four when valid, zero otherwise.
 */
uint32
MallocBuffer::BytesPerRow() const
{
	if (InitCheck() >= B_OK)
		return fWidth * 4;
	return 0;
}


/**
 * @brief Returns the buffer width in pixels.
 * @return Width when valid, zero otherwise.
 */
uint32
MallocBuffer::Width() const
{
	if (InitCheck() >= B_OK)
		return fWidth;
	return 0;
}


/**
 * @brief Returns the buffer height in pixels.
 * @return Height when valid, zero otherwise.
 */
uint32
MallocBuffer::Height() const
{
	if (InitCheck() >= B_OK)
		return fHeight;
	return 0;
}

