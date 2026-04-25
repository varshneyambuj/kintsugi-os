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
 * Original authors: Michael Lotz, Stephan Aßmus.
 */

/** @file DWindowHWInterface.h
    @brief HWInterface implementation that draws into a BDirectWindow for
           the test/debug app_server. */

#ifndef D_WINDOW_HW_INTERACE_H
#define D_WINDOW_HW_INTERACE_H


#include "HWInterface.h"

#include <AutoDeleter.h>
#include <Accelerant.h>
#include <image.h>
#include <Region.h>
#include <String.h>


class DWindowBuffer;
class DWindow;


/** @brief HWInterface that hosts the app_server frame buffer inside a real
           BDirectWindow plus a graphics accelerant, used in
           "app_server -test" mode for development on top of a running
           desktop. */
class DWindowHWInterface : public HWInterface {
public:
								DWindowHWInterface();
	virtual						~DWindowHWInterface();

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

								// DWindowHWInterface
			void				SetOffset(int32 left, int32 top);

private:
			ObjectDeleter<DWindowBuffer>
								fFrontBuffer;

			DWindow*			fWindow;

			display_mode		fDisplayMode;
			int32				fXOffset;
			int32				fYOffset;

			// accelerant stuff
			int					_OpenGraphicsDevice(int deviceNumber);
			status_t			_OpenAccelerant(int device);
			status_t			_SetupDefaultHooks();
			status_t			_UpdateFrameBufferConfig();

private:
			int					fCardFD;
			image_id			fAccelerantImage;
			GetAccelerantHook	fAccelerantHook;

			// required hooks - guaranteed to be valid
			acquire_engine			fAccAcquireEngine;
			release_engine			fAccReleaseEngine;
			sync_to_token			fAccSyncToToken;
			accelerant_mode_count	fAccGetModeCount;
			get_mode_list			fAccGetModeList;
			get_frame_buffer_config	fAccGetFrameBufferConfig;
			set_display_mode		fAccSetDisplayMode;
			get_display_mode		fAccGetDisplayMode;
			get_pixel_clock_limits	fAccGetPixelClockLimits;

			// optional accelerant hooks
			get_timing_constraints	fAccGetTimingConstraints;
			propose_display_mode	fAccProposeDisplayMode;
			fill_rectangle			fAccFillRect;
			invert_rectangle		fAccInvertRect;
			screen_to_screen_blit	fAccScreenBlit;
			set_cursor_shape		fAccSetCursorShape;
			move_cursor				fAccMoveCursor;
			show_cursor				fAccShowCursor;

			frame_buffer_config	fFrameBufferConfig;

			BString				fCardNameInDevFS;
};

#endif // D_WINDOW_HW_INTERACE_H
