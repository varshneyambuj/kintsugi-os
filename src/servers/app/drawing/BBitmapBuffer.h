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

/** @file BBitmapBuffer.h
    @brief RenderingBuffer wrapping a client-side BBitmap. */

#ifndef B_BITMAP_BUFFER_H
#define B_BITMAP_BUFFER_H

#include "RenderingBuffer.h"

#include <AutoDeleter.h>

class BBitmap;


/** @brief Adapts a BBitmap (client-side bitmap class) to the RenderingBuffer interface.
 *
 * The bitmap is owned by the wrapper through an ObjectDeleter so the bits remain
 * valid for the lifetime of the buffer. All accessors delegate to the underlying
 * BBitmap once it has passed InitCheck().
 */
class BBitmapBuffer : public RenderingBuffer {
 public:
	/** @brief Takes ownership of @a bitmap and exposes it as a RenderingBuffer. */
								BBitmapBuffer(BBitmap* bitmap);
	/** @brief Frees the wrapped bitmap. */
	virtual						~BBitmapBuffer();

	/** @brief Reflects the wrapped BBitmap's InitCheck() status. */
	virtual	status_t			InitCheck() const;

	/** @brief Returns the wrapped bitmap's color space. */
	virtual	color_space			ColorSpace() const;
	/** @brief Returns the wrapped bitmap's pixel data pointer. */
	virtual	void*				Bits() const;
	/** @brief Returns the wrapped bitmap's row stride in bytes. */
	virtual	uint32				BytesPerRow() const;
	/** @brief Returns the wrapped bitmap's width in pixels. */
	virtual	uint32				Width() const;
	/** @brief Returns the wrapped bitmap's height in pixels. */
	virtual	uint32				Height() const;

								// BBitmapBuffer
	/** @brief Returns the wrapped BBitmap (read only). */
			const BBitmap*		Bitmap() const
									{ return fBitmap.Get(); }
 private:

			ObjectDeleter<BBitmap>
								fBitmap;
};

#endif // B_BITMAP_BUFFER_H
