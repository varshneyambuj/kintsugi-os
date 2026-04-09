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
 *   Copyright 2001-2013, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Adi Oanca <adioanca@myrealbox.com>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus, <superstippi@gmx.de>
 */

/** @file Screen.cpp
 *  @brief Represents a physical display screen and manages its display modes. */


#include "Screen.h"

#include "BitmapManager.h"
#include "DrawingEngine.h"
#include "HWInterface.h"

#include <Accelerant.h>
#include <Point.h>
#include <GraphicsDefs.h>

#include <stdlib.h>
#include <stdio.h>


/**
 * @brief Computes the refresh frequency of a display mode in Hz.
 * @param mode The display mode whose timing information is used.
 * @return The refresh frequency rounded to one decimal place, or 0.0 if timing
 *         product is zero.
 */
static float
get_mode_frequency(const display_mode& mode)
{
	// Taken from Screen preferences
	float timing = float(mode.timing.h_total * mode.timing.v_total);
	if (timing == 0.0f)
		return 0.0f;

	return rint(10 * float(mode.timing.pixel_clock * 1000)
		/ timing) / 10.0;
}


//	#pragma mark -


/**
 * @brief Constructs a Screen backed by the given hardware interface.
 * @param interface The HWInterface that drives this screen.
 * @param id Numeric identifier assigned to this screen.
 */
Screen::Screen(::HWInterface *interface, int32 id)
	:
	fID(id),
	fHWInterface(interface),
	fDriver(interface != NULL ? interface->CreateDrawingEngine() : NULL)
{
}


/**
 * @brief Constructs an uninitialized Screen with no hardware interface.
 */
Screen::Screen()
	:
	fID(-1)
{
}


/**
 * @brief Destroys the Screen, shutting down the underlying hardware interface.
 */
Screen::~Screen()
{
	Shutdown();
}


/**
 * @brief Initializes the screen by initializing the hardware interface.
 *
 * Finds the mode in the mode list that is closest to the mode specified.
 * As long as the mode list is not empty, this method will always succeed.
 *
 * @return B_OK on success, B_NO_INIT if no hardware interface is set, or
 *         another error code from the hardware interface.
 */
status_t
Screen::Initialize()
{
	status_t status = B_NO_INIT;

	if (fHWInterface.IsSet()) {
		// init the graphics hardware
		status = fHWInterface->Initialize();
	}

	return status;
}


/**
 * @brief Shuts down the hardware interface associated with this screen.
 */
void
Screen::Shutdown()
{
	if (fHWInterface.IsSet())
		fHWInterface->Shutdown();
}


/**
 * @brief Sets the display mode to the given mode structure.
 *
 * If the requested mode is identical to the current mode, the call is a no-op.
 * Overlay bitmaps are suspended during the mode switch.
 *
 * @param mode The desired display mode.
 * @return B_OK on success, or an error code from the hardware interface.
 */
status_t
Screen::SetMode(const display_mode& mode)
{
	display_mode current;
	GetMode(current);
	if (!memcmp(&mode, &current, sizeof(display_mode)))
		return B_OK;

	gBitmapManager->SuspendOverlays();

	status_t status = fHWInterface->SetMode(mode);
		// any attached DrawingEngines will be notified

	gBitmapManager->ResumeOverlays();

	return status;
}


/**
 * @brief Sets the display mode by individual parameters.
 * @param width  Desired virtual width in pixels.
 * @param height Desired virtual height in pixels.
 * @param colorSpace Desired color space identifier.
 * @param timing Display timing parameters.
 * @return B_OK on success, or an error code from the hardware interface.
 */
status_t
Screen::SetMode(uint16 width, uint16 height, uint32 colorSpace,
	const display_timing& timing)
{
	display_mode mode;
	mode.timing = timing;
	mode.space = colorSpace;
	mode.virtual_width = width;
	mode.virtual_height = height;
	mode.h_display_start = 0;
	mode.v_display_start = 0;
	mode.flags = 0;

	return SetMode(mode);
}


/**
 * @brief Selects and applies the best matching mode from the hardware mode list.
 *
 * When @a strict is false and no exact match is found, the first available mode
 * is used as a fallback. The pixel clock may be adjusted to match the requested
 * frequency.
 *
 * @param width      Desired display width in pixels (hard constraint).
 * @param height     Desired display height in pixels.
 * @param colorSpace Desired color space.
 * @param frequency  Desired refresh frequency in Hz.
 * @param strict     If true, fail when no matching mode is found.
 * @return B_OK on success, B_ERROR if no suitable mode is found in strict mode,
 *         or an error code from the hardware interface.
 */
status_t
Screen::SetBestMode(uint16 width, uint16 height, uint32 colorSpace,
	float frequency, bool strict)
{
	// search for a matching mode
	display_mode* modes = NULL;
	uint32 count;
	status_t status = fHWInterface->GetModeList(&modes, &count);
	if (status < B_OK)
		return status;
	if (count <= 0)
		return B_ERROR;

	int32 index = _FindBestMode(modes, count, width, height, colorSpace,
		frequency);
	if (index < 0) {
		debug_printf("app_server: Finding best mode for %ux%u (%" B_PRIu32
			", %g Hz%s) failed\n", width, height, colorSpace, frequency,
			strict ? ", strict" : "");

		if (strict) {
			delete[] modes;
			return B_ERROR;
		} else {
			index = 0;
			// Just use the first mode in the list
			debug_printf("app_server: Use %ux%u (%" B_PRIu32 ") instead.\n",
				modes[0].timing.h_total, modes[0].timing.v_total, modes[0].space);
		}
	}

	display_mode mode = modes[index];
	delete[] modes;

	float modeFrequency = get_mode_frequency(mode);
	display_mode originalMode = mode;
	bool adjusted = false;

	if (modeFrequency != frequency) {
		// adjust timing to fit the requested frequency if needed
		// (taken from Screen preferences application)
		mode.timing.pixel_clock = ((uint32)mode.timing.h_total
			* mode.timing.v_total / 10 * int32(frequency * 10)) / 1000;
		adjusted = true;
	}
	status = SetMode(mode);
	if (status != B_OK && adjusted) {
		// try again with the unchanged mode
		status = SetMode(originalMode);
	}

	return status;
}


/**
 * @brief Sets the screen to the hardware's preferred mode.
 * @return B_OK on success, or an error code if the preferred mode is unavailable.
 */
status_t
Screen::SetPreferredMode()
{
	display_mode mode;
	status_t status = fHWInterface->GetPreferredMode(&mode);
	if (status != B_OK)
		return status;

	return SetMode(mode);
}


/**
 * @brief Retrieves the current display mode as a display_mode structure.
 * @param mode Output parameter filled with the current display mode.
 */
void
Screen::GetMode(display_mode& mode) const
{
	fHWInterface->GetMode(&mode);
}


/**
 * @brief Retrieves the current display mode as individual parameters.
 * @param width      Receives the current virtual width in pixels.
 * @param height     Receives the current virtual height in pixels.
 * @param colorspace Receives the current color space.
 * @param frequency  Receives the current refresh frequency in Hz.
 */
void
Screen::GetMode(uint16 &width, uint16 &height, uint32 &colorspace,
	float &frequency) const
{
	display_mode mode;
	fHWInterface->GetMode(&mode);

	width = mode.virtual_width;
	height = mode.virtual_height;
	colorspace = mode.space;
	frequency = get_mode_frequency(mode);
}


/**
 * @brief Retrieves monitor information from the hardware interface.
 * @param info Output parameter filled with the monitor information.
 * @return B_OK on success, or an error code if the information is unavailable.
 */
status_t
Screen::GetMonitorInfo(monitor_info& info) const
{
	return fHWInterface->GetMonitorInfo(&info);
}


/**
 * @brief Sets the frame rectangle of this screen (multi-monitor placeholder).
 * @param rect The desired frame rectangle (currently unused).
 */
void
Screen::SetFrame(const BRect& rect)
{
	// TODO: multi-monitor support...
}


/**
 * @brief Returns the frame rectangle of this screen in screen coordinates.
 * @return A BRect covering the full virtual display area, origin at (0, 0).
 */
BRect
Screen::Frame() const
{
	display_mode mode;
	fHWInterface->GetMode(&mode);

	return BRect(0, 0, mode.virtual_width - 1, mode.virtual_height - 1);
}


/**
 * @brief Returns the current color space of the screen.
 * @return The color_space value corresponding to the active display mode.
 */
color_space
Screen::ColorSpace() const
{
	display_mode mode;
	fHWInterface->GetMode(&mode);

	return (color_space)mode.space;
}


/**
 * @brief Returns the mode that matches the given criteria best.
 *
 * The "width" argument is the only hard argument; the rest will be adapted
 * as needed. A composite score is computed for each candidate mode and the
 * mode with the lowest score is selected.
 *
 * @param modes      Array of available display modes.
 * @param count      Number of entries in @a modes.
 * @param width      Required display width (hard constraint).
 * @param height     Preferred display height.
 * @param colorSpace Preferred color space.
 * @param frequency  Preferred refresh frequency in Hz.
 * @return Index into @a modes of the best match, or -1 if no mode has the
 *         required width.
 */
int32
Screen::_FindBestMode(const display_mode* modes, uint32 count,
	uint16 width, uint16 height, uint32 colorSpace, float frequency) const
{
	int32 bestDiff = 0;
	int32 bestIndex = -1;
	for (uint32 i = 0; i < count; i++) {
		const display_mode& mode = modes[i];
		if (mode.virtual_width != width)
			continue;

		// compute some random equality score
		// TODO: check if these scores make sense
		int32 diff = 1000 * abs(mode.timing.v_display - height)
			+ int32(fabs(get_mode_frequency(mode) - frequency) * 10)
			+ 100 * abs((int)(mode.space - colorSpace));

		if (bestIndex == -1 || diff < bestDiff) {
			bestDiff = diff;
			bestIndex = i;
		}
	}

	return bestIndex;
}
