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
 * MIT License. Copyright 2005-2009, Haiku.
 * Original authors: Stephan Aßmus.
 */

/** @file BitmapHWInterface.h
    @brief HWInterface implementation that targets an off-screen ServerBitmap. */

#ifndef BITMAP_HW_INTERFACE_H
#define BITMAP_HW_INTERFACE_H


#include "HWInterface.h"

#include <AutoDeleter.h>

class BitmapBuffer;
class MallocBuffer;
class ServerBitmap;
class BBitmapBuffer;


/** @brief HWInterface that exposes a ServerBitmap as a fake graphics card.
 *
 * Mode-setting and cursor APIs all return B_UNSUPPORTED; only frame buffer
 * access is meaningful. When the underlying bitmap is not B_RGB32 / B_RGBA32
 * a 32-bit back buffer is allocated so Painter (which currently only renders
 * to 32-bit targets) can still draw into the wrapper.
 */
class BitmapHWInterface : public HWInterface {
public:
	/** @brief Wraps @a bitmap as the front buffer of this fake interface. */
								BitmapHWInterface(ServerBitmap* bitmap);
	virtual						~BitmapHWInterface();

	/** @brief Allocates a 32-bit back buffer if the front buffer is not 32-bit.
	 *  @return B_OK on success, error from base or buffer construction otherwise. */
	virtual	status_t			Initialize();
	/** @brief No-op shutdown that always returns B_OK. */
	virtual	status_t			Shutdown();

	// overwrite all the meaningless functions with empty code
	/** @brief Mode-setting is unsupported on a bitmap target.
	 *  @retval B_UNSUPPORTED Always. */
	virtual	status_t			SetMode(const display_mode& mode);
	/** @brief Returns a zero-initialised display_mode. */
	virtual	void				GetMode(display_mode* mode);

	/** @brief Returns B_UNSUPPORTED; bitmap targets have no accelerant. */
	virtual status_t			GetDeviceInfo(accelerant_device_info* info);

	/** @brief Returns B_UNSUPPORTED; bitmap targets have no mode list. */
	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	/** @brief Returns B_UNSUPPORTED; bitmap targets have no pixel clock. */
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	/** @brief Returns B_UNSUPPORTED; bitmap targets have no timing constraints. */
	virtual status_t			GetTimingConstraints(display_timing_constraints*
									constraints);
	/** @brief Returns B_UNSUPPORTED; bitmap targets cannot propose modes. */
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);

	/** @brief Returns -1; bitmap targets have no retrace semaphore. */
	virtual sem_id				RetraceSemaphore();
	/** @brief Returns B_UNSUPPORTED; bitmap targets do not retrace. */
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	/** @brief Returns B_UNSUPPORTED; bitmap targets do not support DPMS. */
	virtual status_t			SetDPMSMode(uint32 state);
	/** @brief Returns 0; bitmap targets have no DPMS state. */
	virtual uint32				DPMSMode();
	/** @brief Returns 0; bitmap targets advertise no DPMS capabilities. */
	virtual uint32				DPMSCapabilities();

	/** @brief Returns B_UNSUPPORTED; bitmap targets have no display brightness. */
	virtual status_t			SetBrightness(float);
	/** @brief Returns B_UNSUPPORTED; bitmap targets have no display brightness. */
	virtual status_t			GetBrightness(float*);

	// frame buffer access
	/** @brief Returns the BitmapBuffer wrapping the supplied ServerBitmap. */
	virtual	RenderingBuffer*	FrontBuffer() const;
	/** @brief Returns the optional 32-bit back buffer, or NULL when not double-buffered. */
	virtual	RenderingBuffer*	BackBuffer() const;
	/** @brief Returns true when a back buffer was allocated for color-space conversion. */
	virtual	bool				IsDoubleBuffered() const;

private:
			ObjectDeleter<BBitmapBuffer>
								fBackBuffer;
			ObjectDeleter<BitmapBuffer>
								fFrontBuffer;
};

#endif // BITMAP_HW_INTERFACE_H
