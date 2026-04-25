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
 *   Copyright 2002-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file BitmapHWInterface.cpp
 * @brief HWInterface implementation that fakes a graphics card backed by a ServerBitmap.
 *
 * Mode setting, cursor handling, DPMS, and brightness all return
 * B_UNSUPPORTED. Only frame buffer access is meaningful: the supplied
 * ServerBitmap is exposed as the front buffer, and a 32-bit BBitmap-backed
 * back buffer is allocated transparently when the front buffer's color
 * space is not 32-bit (because Painter currently only renders in 32-bit).
 */


#include <new>
#include <stdio.h>
#include <string.h>

#include "Bitmap.h"
#include "BitmapBuffer.h"
#include "BBitmapBuffer.h"

#include "BitmapHWInterface.h"

using std::nothrow;


/**
 * @brief Wraps the supplied @a bitmap as the front buffer of the fake HWInterface.
 *
 * @param bitmap ServerBitmap to drive; the caller retains ownership.
 */
BitmapHWInterface::BitmapHWInterface(ServerBitmap* bitmap)
	:
	HWInterface(),
	fBackBuffer(NULL),
	fFrontBuffer(new(nothrow) BitmapBuffer(bitmap))
{
}


/**
 * @brief Destructor; the buffer wrappers are freed by their ObjectDeleters.
 */
BitmapHWInterface::~BitmapHWInterface()
{
}


/**
 * @brief Initialises the interface, optionally allocating a 32-bit back buffer.
 *
 * If the front buffer is not B_RGB32 / B_RGBA32 a 32-bit BBitmap-backed back
 * buffer is allocated and primed with the front buffer's current contents
 * so Painter (which only renders to 32-bit targets today) can still draw
 * into a non-32-bit ServerBitmap.
 *
 * @retval B_OK         Initialisation succeeded.
 * @return Other        Error from HWInterface::Initialize() or BitmapBuffer::InitCheck().
 */
status_t
BitmapHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();
	if (ret < B_OK)
		return ret;

	ret = fFrontBuffer->InitCheck();
	if (ret < B_OK)
		return ret;

// TODO: Remove once unnecessary...
	// fall back to double buffered mode until Painter knows how
	// to draw onto non 32-bit surfaces...
	if (fFrontBuffer->ColorSpace() != B_RGB32
		&& fFrontBuffer->ColorSpace() != B_RGBA32) {
		BBitmap* backBitmap = new BBitmap(fFrontBuffer->Bounds(),
			B_BITMAP_NO_SERVER_LINK, B_RGBA32);
		fBackBuffer.SetTo(new BBitmapBuffer(backBitmap));

		ret = fBackBuffer->InitCheck();
		if (ret < B_OK) {
			fBackBuffer.Unset();
		} else {
			// import the current contents of the bitmap
			// into the back bitmap
			backBitmap->ImportBits(fFrontBuffer->Bits(),
				fFrontBuffer->BitsLength(), fFrontBuffer->BytesPerRow(), 0,
				fFrontBuffer->ColorSpace());
		}
	}

	return ret;
}


/**
 * @brief No-op shutdown; resources are released by destructors.
 *
 * @return Always B_OK.
 */
status_t
BitmapHWInterface::Shutdown()
{
	return B_OK;
}


/**
 * @brief Mode setting is not meaningful for a bitmap target.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::SetMode(const display_mode& mode)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Returns a zero-initialised display_mode.
 *
 * @param mode Output mode; cleared to all zeros when non-NULL.
 */
void
BitmapHWInterface::GetMode(display_mode* mode)
{
	if (mode != NULL)
		memset(mode, 0, sizeof(display_mode));
}


/**
 * @brief Bitmap targets have no underlying accelerant.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets have no display modes to report.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::GetModeList(display_mode** modes, uint32 *count)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets have no pixel clock to report.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::GetPixelClockLimits(display_mode* mode, uint32* low,
	uint32* high)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets have no display timing constraints.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets cannot propose modes.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::ProposeMode(display_mode* candidate, const display_mode* low,
	const display_mode* high)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets have no retrace semaphore.
 *
 * @return Always -1.
 */
sem_id
BitmapHWInterface::RetraceSemaphore()
{
	return -1;
}


/**
 * @brief Bitmap targets do not retrace.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::WaitForRetrace(bigtime_t timeout)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets do not implement DPMS.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::SetDPMSMode(uint32 state)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets have no DPMS state.
 *
 * @return Always 0.
 */
uint32
BitmapHWInterface::DPMSMode()
{
	return 0;
}


/**
 * @brief Bitmap targets advertise no DPMS capabilities.
 *
 * @return Always 0.
 */
uint32
BitmapHWInterface::DPMSCapabilities()
{
	return 0;
}


/**
 * @brief Bitmap targets do not have a display brightness control.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::SetBrightness(float)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Bitmap targets do not have a display brightness control.
 *
 * @retval B_UNSUPPORTED Always.
 */
status_t
BitmapHWInterface::GetBrightness(float*)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Returns the BitmapBuffer wrapping the supplied ServerBitmap.
 *
 * @return Pointer to the front buffer; lifetime tied to this interface.
 */
RenderingBuffer*
BitmapHWInterface::FrontBuffer() const
{
	return fFrontBuffer.Get();
}


/**
 * @brief Returns the optional 32-bit back buffer.
 *
 * @return Back buffer pointer when one was allocated, NULL otherwise.
 */
RenderingBuffer*
BitmapHWInterface::BackBuffer() const
{
	return fBackBuffer.Get();
}


/**
 * @brief Reports whether the interface ended up double-buffered.
 *
 * The interface is double-buffered iff a 32-bit back buffer was allocated
 * during Initialize() to convert away from a non-32-bit front buffer.
 *
 * @return True when a back buffer is in use.
 */
bool
BitmapHWInterface::IsDoubleBuffered() const
{
	if (fFrontBuffer.IsSet())
		return fBackBuffer.IsSet();

	return false;
}
