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
 * MIT License. Copyright 2005-2011, Haiku.
 * Original authors: Axel Doerfler (axeld@pinc-software.de).
 */

/** @file ScreenMode.h
    @brief High-level wrapper around BScreen for enumerating and applying display modes. */

#ifndef SCREEN_MODE_H
#define SCREEN_MODE_H


#include <Screen.h>


/** @brief How two physical screens are combined into one virtual desktop. */
typedef enum {
	kCombineDisable,
	kCombineHorizontally,
	kCombineVertically
} combine_mode;

/**
 * @brief Compact, app-friendly description of a display mode.
 *
 * Holds the corrected width/height (with the combine mode applied),
 * color space, refresh rate, plus the multi-monitor flags from the
 * Radeon settings tunnel. Built from a low-level @c display_mode via
 * @c SetTo() and compared with the equality operators.
 */
struct screen_mode {
	int32			width;		// these reflect the corrected width/height,
	int32			height;		// taking the combine mode into account
	color_space		space;
	float			refresh;
	combine_mode	combine;
	bool			swap_displays;
	bool			use_laptop_panel;
	uint32			tv_standard;

	/** @brief Populate this struct from an app_server display_mode. */
	void SetTo(const display_mode& mode);
	/** @brief Return the bit-depth (8/15/16/24/32) implied by @c space. */
	int32 BitsPerPixel() const;

	bool operator==(const screen_mode &otherMode) const;
	bool operator!=(const screen_mode &otherMode) const;
};


/**
 * @brief Helper that queries app_server for available modes and applies them.
 *
 * Wraps @c BScreen with extra bookkeeping: caches a sorted mode list,
 * remembers original modes per workspace so they can be reverted, and
 * exposes monitor / device info plus refresh-rate limits.
 */
class ScreenMode {
public:
								ScreenMode(BWindow* window);
								~ScreenMode();

			status_t			Set(const screen_mode& mode,
									int32 workspace = ~0);
			status_t			Get(screen_mode& mode,
									int32 workspace = ~0) const;
			status_t			GetOriginalMode(screen_mode &mode,
									int32 workspace = ~0) const;

			status_t			Set(const display_mode& mode,
									int32 workspace = ~0);
			status_t			Get(display_mode& mode,
									int32 workspace = ~0) const;

			status_t			Revert();
			void				UpdateOriginalModes();

			bool				SupportsColorSpace(const screen_mode& mode,
									color_space space);
			status_t			GetRefreshLimits(const screen_mode& mode,
									float& min, float& max);
			const char*			GetManufacturerFromID(const char* id) const;
			status_t			GetMonitorInfo(monitor_info& info,
									float* _diagonalInches = NULL);

			status_t			GetDeviceInfo(accelerant_device_info& info);

			screen_mode			ModeAt(int32 index);
			const display_mode&	DisplayModeAt(int32 index);
			int32				CountModes();

private:
			bool				_GetDisplayMode(const screen_mode& mode,
									display_mode& displayMode);

private:
			BWindow*			fWindow;
			display_mode*		fModeList;
			uint32				fModeCount;

			bool				fUpdatedModes;
			display_mode		fOriginalDisplayMode[32];
			screen_mode			fOriginal[32];
};


#endif	/* SCREEN_MODE_H */
