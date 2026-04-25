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
 *   Copyright 2006-2009, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file Overlay.cpp
 * @brief Implementation of the Overlay wrapper around an accelerant overlay channel.
 *
 * Each Overlay instance binds a ServerBitmap to a hardware overlay slot
 * acquired from the HWInterface. The class manages the per-overlay semaphore
 * used to lock the buffer while the client writes a new frame, computes the
 * color-key value for the chosen color space, and forwards configure / hide
 * requests to the accelerant.
 */


#include "Overlay.h"

#include <BitmapPrivate.h>

#include "HWInterface.h"
#include "ServerBitmap.h"


//#define TRACE_OVERLAY
#ifdef TRACE_OVERLAY
#	define TRACE(x...) ktrace_printf(x);
#else
#	define TRACE(x...) ;
#endif


/** @brief Maximum time (microseconds) to wait for the overlay lock before assuming a stuck client. */
const static bigtime_t kOverlayTimeout = 1000000LL;
	// after 1 second, the team holding the lock will be killed


/** @brief RAII helper that acquires a sem_id on construction and releases it on destruction. */
class SemaphoreLocker {
public:
	/** @brief Acquires @a semaphore, retrying through B_INTERRUPTED.
	 *  @param semaphore Semaphore to lock.
	 *  @param timeout   Relative timeout passed to acquire_sem_etc(). */
	SemaphoreLocker(sem_id semaphore, bigtime_t timeout = B_INFINITE_TIMEOUT)
		:
		fSemaphore(semaphore)
	{
		do {
			fStatus = acquire_sem_etc(fSemaphore, 1, B_RELATIVE_TIMEOUT,
				timeout);
		} while (fStatus == B_INTERRUPTED);
	}

	/** @brief Releases the semaphore if it was successfully acquired. */
	~SemaphoreLocker()
	{
		if (fStatus == B_OK)
			release_sem_etc(fSemaphore, 1, B_DO_NOT_RESCHEDULE);
	}

	/** @brief Returns the result of the acquire call (B_OK / B_TIMED_OUT / ...). */
	status_t LockStatus()
	{
		return fStatus;
	}

private:
	sem_id		fSemaphore;
	status_t	fStatus;
};


//	#pragma mark -


/**
 * @brief Constructs an overlay over @a bitmap on the supplied hardware interface.
 *
 * Creates the per-overlay semaphore, sets a default color-key, and asks the
 * HWInterface to allocate the back-end overlay buffer with the bitmap's
 * dimensions and color space.
 *
 * @param interface Hardware interface owning the overlay channel.
 * @param bitmap    Bitmap whose contents will be displayed via the overlay.
 * @param token     Channel token previously obtained from the HWInterface.
 */
Overlay::Overlay(HWInterface& interface, ServerBitmap* bitmap,
		overlay_token token)
	:
	fHWInterface(interface),
	fOverlayBuffer(NULL),
	fClientData(NULL),
	fOverlayToken(token)
{
	fSemaphore = create_sem(1, "overlay lock");
	fColor = (rgb_color){ 0, 80, 0, 0 };
		// TODO: whatever fine color we want to use here...

	fWindow.offset_top = 0;
	fWindow.offset_left = 0;
	fWindow.offset_right = 0;
	fWindow.offset_bottom = 0;

	fWindow.flags = B_OVERLAY_COLOR_KEY;

	_AllocateBuffer(bitmap);

	TRACE("overlay: created %p, bitmap %p\n", this, bitmap);
}


/**
 * @brief Releases the overlay channel, frees the buffer, and deletes the semaphore.
 */
Overlay::~Overlay()
{
	fHWInterface.ReleaseOverlayChannel(fOverlayToken);
	_FreeBuffer();

	delete_sem(fSemaphore);
	TRACE("overlay: deleted %p\n", this);
}


/**
 * @brief Reports whether the overlay was successfully initialised.
 *
 * @retval B_OK         Both the semaphore and the overlay buffer are valid.
 * @retval B_NO_MEMORY  The buffer could not be allocated.
 * @return Other        Negative semaphore status when create_sem() failed.
 */
status_t
Overlay::InitCheck() const
{
	if (fSemaphore < B_OK)
		return fSemaphore;

	if (fOverlayBuffer == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Re-allocates the overlay buffer after a previous Suspend().
 *
 * Acquires the per-overlay lock (waiting up to @c kOverlayTimeout) so that
 * no client is mid-write when the buffer is recreated, then asks the
 * HWInterface for a fresh buffer matching the bitmap's geometry.
 *
 * @param bitmap Bitmap describing the new buffer dimensions and color space.
 * @return       B_OK on success, otherwise the failure status from buffer allocation.
 */
status_t
Overlay::Resume(ServerBitmap* bitmap)
{
	SemaphoreLocker locker(fSemaphore, kOverlayTimeout);
	if (locker.LockStatus() == B_TIMED_OUT) {
		// TODO: kill app!
	}

	TRACE("overlay: resume %p (lock status %ld)\n", this, locker.LockStatus());

	status_t status = _AllocateBuffer(bitmap);
	if (status < B_OK)
		return status;

	fClientData->buffer = (uint8*)fOverlayBuffer->buffer;
	return B_OK;
}


/**
 * @brief Releases the back-end overlay buffer while keeping the channel reservation.
 *
 * @param bitmap         Bitmap currently driving the overlay (unused, kept for symmetry with Resume()).
 * @param needTemporary  Reserved for future allocation of a placeholder buffer.
 * @return  Always B_OK.
 */
status_t
Overlay::Suspend(ServerBitmap* bitmap, bool needTemporary)
{
	SemaphoreLocker locker(fSemaphore, kOverlayTimeout);
	if (locker.LockStatus() == B_TIMED_OUT) {
		// TODO: kill app!
	}

	TRACE("overlay: suspend %p (lock status %ld)\n", this, locker.LockStatus());

	_FreeBuffer();
	fClientData->buffer = NULL;

	return B_OK;
}


/**
 * @brief Releases the overlay buffer back to the HWInterface and clears the cached pointer.
 */
void
Overlay::_FreeBuffer()
{
	fHWInterface.FreeOverlayBuffer(fOverlayBuffer);
	fOverlayBuffer = NULL;
}


/**
 * @brief Allocates a new overlay buffer matching @a bitmap's geometry.
 *
 * @param bitmap Bitmap whose width, height, and color space define the buffer.
 * @retval B_OK         Buffer allocated successfully.
 * @retval B_NO_MEMORY  The HWInterface refused to allocate.
 */
status_t
Overlay::_AllocateBuffer(ServerBitmap* bitmap)
{
	fOverlayBuffer = fHWInterface.AllocateOverlayBuffer(bitmap->Width(),
		bitmap->Height(), bitmap->ColorSpace());
	if (fOverlayBuffer == NULL)
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Records the client-shared overlay descriptor and publishes the lock and pixel pointer.
 *
 * @param clientData Client-visible structure shared with the application; the
 *                   semaphore and current buffer pointer are stored into it.
 */
void
Overlay::SetClientData(overlay_client_data* clientData)
{
	fClientData = clientData;
	fClientData->lock = fSemaphore;
	fClientData->buffer = (uint8*)fOverlayBuffer->buffer;
}


/**
 * @brief Translates B_OVERLAY_* user flags into the equivalent overlay window flags.
 *
 * @param flags Bitwise combination of B_OVERLAY_FILTER_HORIZONTAL,
 *              B_OVERLAY_FILTER_VERTICAL, and B_OVERLAY_MIRROR.
 */
void
Overlay::SetFlags(uint32 flags)
{
	if (flags & B_OVERLAY_FILTER_HORIZONTAL)
		fWindow.flags |= B_OVERLAY_HORIZONTAL_FILTERING;
	if (flags & B_OVERLAY_FILTER_VERTICAL)
		fWindow.flags |= B_OVERLAY_VERTICAL_FILTERING;
	if (flags & B_OVERLAY_MIRROR)
		fWindow.flags |= B_OVERLAY_HORIZONTAL_MIRRORING;
}


/**
 * @brief Adopts the channel token previously held by @a other.
 *
 * Used during overlay handoff (e.g. when an existing overlay is replaced
 * without releasing the underlying hardware channel).
 *
 * @param other Donor overlay; its token is transferred verbatim.
 */
void
Overlay::TakeOverToken(Overlay* other)
{
	overlay_token token = other->OverlayToken();
	if (token == NULL)
		return;

	fOverlayToken = token;
	//other->fOverlayToken = NULL;
}


/**
 * @brief Returns the accelerant overlay buffer descriptor.
 * @return Pointer to the overlay buffer descriptor, or NULL when not allocated.
 */
const overlay_buffer*
Overlay::OverlayBuffer() const
{
	return fOverlayBuffer;
}


/**
 * @brief Returns the client-shared data structure for this overlay.
 */
overlay_client_data*
Overlay::ClientData() const
{
	return fClientData;
}


/**
 * @brief Returns the accelerant token for this overlay's channel.
 */
overlay_token
Overlay::OverlayToken() const
{
	return fOverlayToken;
}


/**
 * @brief Hides the overlay; the next call to Configure() makes it visible again.
 *
 * @note Has no effect when the channel has already been released.
 */
void
Overlay::Hide()
{
	if (fOverlayToken == NULL)
		return;

	fHWInterface.HideOverlay(this);
	TRACE("overlay: hide %p\n", this);
}


/**
 * @brief Computes color-key values matching the supplied @a colorSpace.
 *
 * Adjusts the per-channel mask and value of @c fWindow so that the chosen
 * color key produces the same RGB color regardless of the framebuffer's bit
 * depth (B_RGB15 / B_RGB16 / 32-bit).
 *
 * @param colorSpace One of the supported screen color spaces.
 * @note  Has no effect when the overlay is not using B_OVERLAY_COLOR_KEY.
 */
void
Overlay::SetColorSpace(uint32 colorSpace)
{
	if ((fWindow.flags & B_OVERLAY_COLOR_KEY) == 0)
		return;

	uint8 colorShift = 0, greenShift = 0, alphaShift = 0;
	rgb_color colorKey = fColor;

	switch (colorSpace) {
		case B_RGB15:
			greenShift = colorShift = 3;
			alphaShift = 7;
			break;
		case B_RGB16:
			colorShift = 3;
			greenShift = 2;
			alphaShift = 8;
			break;
	}

	fWindow.red.value = colorKey.red >> colorShift;
	fWindow.green.value = colorKey.green >> greenShift;
	fWindow.blue.value = colorKey.blue >> colorShift;
	fWindow.alpha.value = colorKey.alpha >> alphaShift;
	fWindow.red.mask = 0xff >> colorShift;
	fWindow.green.mask = 0xff >> greenShift;
	fWindow.blue.mask = 0xff >> colorShift;
	fWindow.alpha.mask = 0xff >> alphaShift;
}


/**
 * @brief Pushes a new (source, destination) rectangle pair to the accelerant.
 *
 * Lazily acquires a channel token if this overlay does not own one yet, then
 * fills the view / window descriptors and asks the HWInterface to commit the
 * configuration to the hardware.
 *
 * @param source      Source rectangle in overlay buffer coordinates.
 * @param destination Destination rectangle in screen coordinates.
 */
void
Overlay::Configure(const BRect& source, const BRect& destination)
{
	if (fOverlayToken == NULL) {
		fOverlayToken = fHWInterface.AcquireOverlayChannel();
		if (fOverlayToken == NULL)
			return;
	}

	TRACE("overlay: configure %p\n", this);

	fView.h_start = (uint16)source.left;
	fView.v_start = (uint16)source.top;
	fView.width = (uint16)source.IntegerWidth() + 1;
	fView.height = (uint16)source.IntegerHeight() + 1;

	fWindow.h_start = (int16)destination.left;
	fWindow.v_start = (int16)destination.top;
	fWindow.width = (uint16)destination.IntegerWidth() + 1;
	fWindow.height = (uint16)destination.IntegerHeight() + 1;

	fHWInterface.ConfigureOverlay(this);
}

