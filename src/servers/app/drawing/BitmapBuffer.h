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

/** @file BitmapBuffer.h
    @brief RenderingBuffer adapter wrapping a server-side ServerBitmap. */

#ifndef BITMAP_BUFFER_H
#define BITMAP_BUFFER_H

#include "RenderingBuffer.h"

class ServerBitmap;


/** @brief Adapts a ServerBitmap (app_server-side bitmap) to the RenderingBuffer interface.
 *
 * Unlike BBitmapBuffer this class does not own the wrapped bitmap; the caller
 * is responsible for keeping it alive for the lifetime of the buffer.
 */
class BitmapBuffer : public RenderingBuffer {
 public:
	/** @brief Wraps @a bitmap (caller retains ownership). */
								BitmapBuffer(ServerBitmap* bitmap);
	/** @brief Destroys the wrapper without freeing the underlying ServerBitmap. */
	virtual						~BitmapBuffer();

	/** @brief Returns B_OK when the wrapped bitmap is set and valid. */
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

								// BitmapBuffer
	/** @brief Returns the wrapped ServerBitmap (read only). */
			const ServerBitmap*	Bitmap() const
									{ return fBitmap; }
 private:

			ServerBitmap*		fBitmap;
};

#endif // BITMAP_BUFFER_H
