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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file RenderingBuffer.h
 *  @brief Abstract interface representing a pixel buffer used for rendering. */

#ifndef RENDERING_BUFFER_H
#define RENDERING_BUFFER_H

#include <GraphicsDefs.h>
#include "IntRect.h"

/** @brief Abstract base class providing a uniform interface to a raw pixel buffer. */
class RenderingBuffer {
 public:
								RenderingBuffer() {}
	virtual						~RenderingBuffer() {}

	/** @brief Checks whether the buffer has been successfully initialised.
	 *  @return B_OK if valid, an error code otherwise. */
	virtual	status_t			InitCheck() const = 0;

	/** @brief Returns the color space of the buffer.
	 *  @return The color_space constant describing the pixel format. */
	virtual	color_space			ColorSpace() const = 0;

	/** @brief Returns a pointer to the raw pixel data.
	 *  @return Pointer to the first byte of the buffer. */
	virtual	void*				Bits() const = 0;

	/** @brief Returns the number of bytes per horizontal scan line.
	 *  @return Bytes per row, including any padding. */
	virtual	uint32				BytesPerRow() const = 0;

	/** @brief Returns the number of pixels per horizontal scan line.
	 *  @return Pixel width of the buffer. */
	// the *count* of the pixels per line
	virtual	uint32				Width() const = 0;

	/** @brief Returns the number of scan lines in the buffer.
	 *  @return Pixel height of the buffer. */
	// the *count* of lines
	virtual	uint32				Height() const = 0;

	/** @brief Returns the total size in bytes of the buffer contents.
	 *  @return Height() * BytesPerRow(). */
	inline	uint32				BitsLength() const
									{ return Height() * BytesPerRow(); }

	/** @brief Returns the bounding rectangle of the buffer in pixel coordinates.
	 *  @return IntRect from (0,0) to (Width()-1, Height()-1). */
	inline	IntRect				Bounds() const
									{ return IntRect(0, 0, Width() - 1,
										Height() - 1); }
};

#endif // RENDERING_BUFFER_H
