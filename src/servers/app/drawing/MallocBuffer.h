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
 * MIT License.
 */

/** @file MallocBuffer.h
    @brief RenderingBuffer backed by a plain malloc() allocation in B_RGBA32. */

#ifndef MALLOC_BUFFER_H
#define MALLOC_BUFFER_H

#include "RenderingBuffer.h"

class BBitmap;


/** @brief A simple in-memory RenderingBuffer used as a temporary 32-bit drawing target.
 *
 * The pixel format is fixed at B_RGBA32 (4 bytes per pixel, no padding) and the
 * memory is owned by the buffer; freeing it on destruction. Construction with
 * a width or height of zero leaves the buffer in an uninitialised state.
 */
class MallocBuffer : public RenderingBuffer {
 public:
	/** @brief Allocates a B_RGBA32 buffer of @a width x @a height pixels. */
								MallocBuffer(uint32 width,
											 uint32 height);
	/** @brief Frees the underlying allocation. */
	virtual						~MallocBuffer();

	/** @brief Returns B_OK when the allocation succeeded, B_NO_MEMORY otherwise. */
	virtual	status_t			InitCheck() const;

	/** @brief Always returns B_RGBA32 (the only supported color space). */
	virtual	color_space			ColorSpace() const;
	/** @brief Returns the raw pixel pointer or NULL if InitCheck() failed. */
	virtual	void*				Bits() const;
	/** @brief Returns the row stride in bytes (width * 4). */
	virtual	uint32				BytesPerRow() const;
	/** @brief Returns the buffer width in pixels. */
	virtual	uint32				Width() const;
	/** @brief Returns the buffer height in pixels. */
	virtual	uint32				Height() const;

 private:

			void*				fBuffer;
			uint32				fWidth;
			uint32				fHeight;
};

#endif // MALLOC_BUFFER_H
