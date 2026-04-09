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
 *   Copyright 2008-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file DirectWindowInfo.cpp
 *  @brief Manages BDirectWindow buffer state and synchronization with the client. */


#include "DirectWindowInfo.h"

#include <stdio.h>
#include <string.h>
#include <syslog.h>

#include <Autolock.h>
#include <kernel.h>

#include "RenderingBuffer.h"
#include "clipping.h"


/**
 * @brief Constructs a DirectWindowInfo, creating the shared buffer area and semaphores.
 *
 * The shared area is initialized with B_DIRECT_STOP state. The synchronization
 * semaphores are created for use between the server and the BDirectWindow client.
 */
DirectWindowInfo::DirectWindowInfo()
	:
	fBufferInfo(NULL),
	fSem(-1),
	fAcknowledgeSem(-1),
	fBufferArea(-1),
	fOriginalFeel(B_NORMAL_WINDOW_FEEL),
	fFullScreen(false)
{
	fBufferArea = create_area("direct area", (void**)&fBufferInfo,
		B_ANY_ADDRESS, DIRECT_BUFFER_INFO_AREA_SIZE,
		B_NO_LOCK, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);

	memset(fBufferInfo, 0, DIRECT_BUFFER_INFO_AREA_SIZE);
	fBufferInfo->buffer_state = B_DIRECT_STOP;
	fBufferInfo->bits_area = -1;

	fSem = create_sem(0, "direct sem");
	fAcknowledgeSem = create_sem(0, "direct sem ack");
}


/**
 * @brief Destroys the DirectWindowInfo, invalidating the client buffer and releasing resources.
 *
 * Sets buffer bits and bytes_per_row to zero so that a still-running client
 * will notice the window has been destroyed.
 */
DirectWindowInfo::~DirectWindowInfo()
{
	// this should make the client die in case it's still running
	fBufferInfo->bits = NULL;
	fBufferInfo->bytes_per_row = 0;

	delete_area(fBufferArea);
	delete_sem(fSem);
	delete_sem(fAcknowledgeSem);
}


/**
 * @brief Checks whether the object was constructed successfully.
 * @return B_OK if the area and both semaphores are valid, or an error code.
 */
status_t
DirectWindowInfo::InitCheck() const
{
	if (fBufferArea < B_OK)
		return fBufferArea;
	if (fSem < B_OK)
		return fSem;
	if (fAcknowledgeSem < B_OK)
		return fAcknowledgeSem;

	return B_OK;
}


/**
 * @brief Fills @a data with the IPC identifiers needed for client synchronization.
 * @param data Output structure that receives the buffer area and semaphore IDs.
 * @return B_OK always.
 */
status_t
DirectWindowInfo::GetSyncData(direct_window_sync_data& data) const
{
	data.area = fBufferArea;
	data.disable_sem = fSem;
	data.disable_sem_ack = fAcknowledgeSem;

	return B_OK;
}


/**
 * @brief Updates the direct buffer state and synchronizes with the client.
 *
 * When @a bufferState includes B_BUFFER_RESET or the bits area is not yet set,
 * the full buffer description (bits, row stride, pixel format, color space,
 * layout, orientation) is updated from @a buffer. The clip list and window
 * bounds are updated for any non-stop state. A synchronization round-trip with
 * the client is performed at the end.
 *
 * @param bufferState  New direct buffer state flags.
 * @param driverState  New driver state (-1 to leave unchanged).
 * @param buffer       The rendering buffer supplying geometry information.
 * @param windowFrame  Window frame in screen coordinates.
 * @param clipRegion   Current visible clip region of the window.
 * @return B_OK on success, or an error from the synchronization semaphores.
 */
status_t
DirectWindowInfo::SetState(direct_buffer_state bufferState,
	direct_driver_state driverState, RenderingBuffer* buffer,
	const BRect& windowFrame, const BRegion& clipRegion)
{
	if ((fBufferInfo->buffer_state & B_DIRECT_MODE_MASK) == B_DIRECT_STOP
		&& (bufferState & B_DIRECT_MODE_MASK) != B_DIRECT_START)
		return B_OK;

	fBufferInfo->buffer_state = bufferState;

	if ((int)driverState != -1)
		fBufferInfo->driver_state = driverState;

	if ((bufferState & B_BUFFER_RESET) != 0 || fBufferInfo->bits_area < 0) {
		void* bits = buffer->Bits();
		if (IS_USER_ADDRESS(bits)) {
			fBufferInfo->bits = NULL;

			area_id area = area_for(bits);
			fBufferInfo->bits_area = area;

			// make sure the area is cloneable
			set_area_protection(area, B_READ_AREA | B_WRITE_AREA | B_CLONEABLE_AREA);
		} else {
			// framebuffer is in kernel address space
			// TODO: update all drivers and then drop this case!
			fBufferInfo->bits = bits;
		}
		fBufferInfo->bytes_per_row = buffer->BytesPerRow();

		switch (buffer->ColorSpace()) {
			case B_RGBA64:
			case B_RGBA64_BIG:
				fBufferInfo->bits_per_pixel = 64;
				break;
			case B_RGB48:
			case B_RGB48_BIG:
				fBufferInfo->bits_per_pixel = 48;
				break;
			case B_RGB32:
			case B_RGBA32:
			case B_RGB32_BIG:
			case B_RGBA32_BIG:
				fBufferInfo->bits_per_pixel = 32;
				break;
			case B_RGB24:
			case B_RGB24_BIG:
				fBufferInfo->bits_per_pixel = 24;
				break;
			case B_RGB16:
			case B_RGB16_BIG:
			case B_RGB15:
			case B_RGB15_BIG:
				fBufferInfo->bits_per_pixel = 16;
				break;
			case B_CMAP8:
			case B_GRAY8:
				fBufferInfo->bits_per_pixel = 8;
				break;
			default:
				syslog(LOG_ERR,
					"unknown colorspace in DirectWindowInfo::SetState()!\n");
				fBufferInfo->bits_per_pixel = 0;
				break;
		}

		fBufferInfo->pixel_format = buffer->ColorSpace();
		fBufferInfo->layout = B_BUFFER_NONINTERLEAVED;
		fBufferInfo->orientation = B_BUFFER_TOP_TO_BOTTOM;
			// TODO
	}

	if ((bufferState & B_DIRECT_MODE_MASK) != B_DIRECT_STOP) {
		fBufferInfo->window_bounds = to_clipping_rect(windowFrame);

		const int32 kMaxClipRectsCount = (DIRECT_BUFFER_INFO_AREA_SIZE
			- sizeof(direct_buffer_info)) / sizeof(clipping_rect);

		fBufferInfo->clip_list_count = min_c(clipRegion.CountRects(),
			kMaxClipRectsCount);
		fBufferInfo->clip_bounds = clipRegion.FrameInt();

		for (uint32 i = 0; i < fBufferInfo->clip_list_count; i++)
			fBufferInfo->clip_list[i] = clipRegion.RectAtInt(i);
	}

	return _SynchronizeWithClient();
}


/**
 * @brief Records the original frame and feel before entering full-screen mode.
 * @param frame The window frame to restore when leaving full screen.
 * @param feel  The window feel to restore when leaving full screen.
 */
void
DirectWindowInfo::EnableFullScreen(const BRect& frame, window_feel feel)
{
	fOriginalFrame = frame;
	fOriginalFeel = feel;
	fFullScreen = true;
}


/**
 * @brief Marks the window as no longer in full-screen mode.
 */
void
DirectWindowInfo::DisableFullScreen()
{
	fFullScreen = false;
}


/**
 * @brief Releases the synchronization semaphore and waits for the client to acknowledge.
 *
 * Releases fSem to trigger a BDirectWindow::DirectConnected() call in the
 * client, then acquires fAcknowledgeSem (with a 500 ms timeout) to wait for
 * the client to return from that callback.
 *
 * @return B_OK on success, or an error if the client does not respond in time.
 */
status_t
DirectWindowInfo::_SynchronizeWithClient()
{
	// Releasing this semaphore causes the client to call
	// BDirectWindow::DirectConnected()
	status_t status = release_sem(fSem);
	if (status != B_OK)
		return status;

	// Wait with a timeout of half a second until the client exits
	// from its DirectConnected() implementation
	do {
		status = acquire_sem_etc(fAcknowledgeSem, 1, B_TIMEOUT, 500000);
	} while (status == B_INTERRUPTED);

	return status;
}
