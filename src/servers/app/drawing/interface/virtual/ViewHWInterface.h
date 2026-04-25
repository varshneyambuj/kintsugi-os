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
 * Original author: Stephan Aßmus.
 */

/** @file ViewHWInterface.h
    @brief HWInterface implementation that renders into a BView/BWindow for
           the test/debug app_server. */

#ifndef VIEW_GRAPHICS_CARD_H
#define VIEW_GRAPHICS_CARD_H


#include "HWInterface.h"

#include <AutoDeleter.h>

class BBitmap;
class BBitmapBuffer;
class CardWindow;


/** @brief HWInterface that uses a BBitmap-backed BView inside a regular
           BWindow as the "screen", allowing app_server to run nested in the
           host desktop without needing a real graphics device. */
class ViewHWInterface : public HWInterface {
public:
								ViewHWInterface();
	virtual						~ViewHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();

	virtual	status_t			SetMode(const display_mode& mode);
	virtual	void				GetMode(display_mode* mode);

	virtual status_t			GetDeviceInfo(accelerant_device_info* info);

	virtual status_t			GetModeList(display_mode** _modeList,
									uint32* _count);
	virtual status_t			GetPixelClockLimits(display_mode* mode,
									uint32* _low, uint32* _high);
	virtual status_t			GetTimingConstraints(display_timing_constraints*
									constraints);
	virtual status_t			ProposeMode(display_mode* candidate,
									const display_mode* low,
									const display_mode* high);

	virtual sem_id				RetraceSemaphore();
	virtual status_t			WaitForRetrace(
									bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual status_t			SetDPMSMode(uint32 state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();

	virtual status_t			SetBrightness(float);
	virtual status_t			GetBrightness(float*);

	// frame buffer access
	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

	virtual	status_t			Invalidate(const BRect& frame);
	virtual	status_t			CopyBackToFront(const BRect& frame);

private:
			ObjectDeleter<BBitmapBuffer>
								fBackBuffer;
			ObjectDeleter<BBitmapBuffer>
								fFrontBuffer;

			CardWindow*			fWindow;

			display_mode		fDisplayMode;
};

#endif // VIEW_GRAPHICS_CARD_H
