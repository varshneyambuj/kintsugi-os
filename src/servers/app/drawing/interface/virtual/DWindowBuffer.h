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
 * MIT License. Copyright 2001-2009, Haiku.
 * Original authors: Stephan Aßmus, Michael Lotz.
 */

/** @file DWindowBuffer.h
    @brief RenderingBuffer view onto a BDirectWindow's direct frame buffer. */

#ifndef D_WINDOW_BUFFER_H
#define D_WINDOW_BUFFER_H


#include "RenderingBuffer.h"

#include <Accelerant.h>
#include <DirectWindow.h>


/** @brief Rendering buffer that adapts a BDirectWindow direct_buffer_info or
           accelerant frame_buffer_config into the RenderingBuffer interface,
           tracking window clipping for the test app_server's virtual HW. */
class DWindowBuffer : public RenderingBuffer {
public:
								DWindowBuffer();
	virtual						~DWindowBuffer();

	virtual	status_t			InitCheck() const;

	virtual	color_space			ColorSpace() const;
	virtual	void*				Bits() const;
	virtual	uint32				BytesPerRow() const;
	virtual	uint32				Width() const;
	virtual	uint32				Height() const;

			void				SetTo(direct_buffer_info* info);

			void				SetTo(frame_buffer_config* config,
									  uint32 x, uint32 y,
									  uint32 width, uint32 height,
									  color_space format);

			/** @brief Returns the screen-space clipping region tracked from
			           the latest direct_buffer_info update. */
			BRegion&			WindowClipping()
									{ return fWindowClipping; }
private:
			uint8*				fBits;
			uint32				fWidth;
			uint32				fHeight;
			uint32				fBytesPerRow;
			color_space			fFormat;

			BRegion				fWindowClipping;
};

#endif // D_WINDOW_BUFFER_H
