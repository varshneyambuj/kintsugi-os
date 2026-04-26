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
 *   Copyright (c) 2002, Thomas Kurschel
 *
 *   Part of Radeon driver - Multi-Monitor Settings interface.
 */


/**
 * @file multimon.cpp
 * @brief Settings tunnel for the Radeon multi-monitor accelerant interface.
 *
 * The Radeon accelerant exposes a private settings interface by piggy-backing
 * on the standard @c BScreen::ProposeMode() RPC: it recognizes a magic
 * combination of @c display_mode fields as "settings tunnel" requests and
 * returns the requested setting in @c timing.flags. This file packages those
 * conventions as ergonomic getter/setter helpers used by ScreenWindow.
 */


#include "multimon.h"
#include "accelerant_ext.h"

#include <OS.h>
#include <Screen.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/**
 * @brief Initialize a triplet of display_modes to be recognized as a tunnel.
 *
 * Sets distinctive "magic" pixel-clock and virtual-resolution sentinel
 * values in @a low and @a high so the accelerant identifies the call as
 * a tunneled setting access rather than a real mode-set proposal.
 *
 * @param mode Working mode to initialize.
 * @param low  Low bound of the proposal triplet.
 * @param high High bound of the proposal triplet.
 */
static void
PrepareTunnel(display_mode *mode, display_mode *low, display_mode *high)
{
	memset(mode, 0, sizeof(*mode));

	// mark modes as settings tunnel
	mode->space = low->space = high->space = 0;
	low->virtual_width = 0xffff;
	low->virtual_height = 0xffff;
	high->virtual_width = 0;
	high->virtual_height = 0;
	mode->timing.pixel_clock = 0;
	low->timing.pixel_clock = 'TKTK';
	high->timing.pixel_clock = 'KTKT';
}


/**
 * @brief Retrieve the current value of a tunneled accelerant setting.
 *
 * @param screen   Screen whose accelerant exposes the setting.
 * @param code     Setting identifier (e.g. @c ms_swap, @c ms_use_laptop_panel).
 * @param setting  Out: receives the current setting value.
 * @return         @c B_OK on success; otherwise an error from the accelerant.
 */
static status_t
GetSetting(BScreen *screen, uint16 code, uint32 *setting)
{
	display_mode mode, low, high;
	status_t result;

	result = TestMultiMonSupport(screen);
	if (result != B_OK)
		return result;

	PrepareTunnel(&mode, &low, &high);

	mode.h_display_start = code;
	mode.v_display_start = 0;

	result = screen->ProposeMode(&mode, &low, &high);
	if (result != B_OK)
		return result;

	*setting = mode.timing.flags;

	return B_OK;
}


/**
 * @brief Write a new value to a tunneled accelerant setting.
 *
 * @param screen   Screen whose accelerant exposes the setting.
 * @param code     Setting identifier.
 * @param value    New value to apply.
 * @return         @c B_OK on success; otherwise an error from the accelerant.
 */
static status_t
SetSetting(BScreen *screen, uint16 code, uint32 value)
{
	display_mode mode, low, high;
	status_t result;

	result = TestMultiMonSupport(screen);
	if (result != B_OK)
		return result;

	PrepareTunnel(&mode, &low, &high);

	mode.h_display_start = code;
	mode.v_display_start = 1;
	mode.timing.flags = value;

	return screen->ProposeMode(&mode, &low, &high);
}


/**
 * @brief Enumerate the @a idx-th supported value of a tunneled setting.
 *
 * Used to populate menus of supported TV standards or other enumerated
 * accelerant options.
 *
 * @param screen   Screen to query.
 * @param code     Setting identifier.
 * @param idx      Zero-based index of the value to retrieve.
 * @param setting  Out: receives the @a idx-th supported value.
 * @return         @c B_OK on success; @c B_BAD_INDEX or other error
 *                 once @a idx exceeds the supported range.
 */
static status_t
GetNthSupportedSetting(BScreen *screen, uint16 code, int32 idx,
	uint32 *setting)
{
	display_mode mode, low, high;
	status_t result;

	result = TestMultiMonSupport(screen);
	if (result != B_OK)
		return result;

	PrepareTunnel(&mode, &low, &high);

	mode.h_display_start = code;
	mode.v_display_start = 2;
	mode.timing.flags = idx;

	result = screen->ProposeMode(&mode, &low, &high);
	if (result != B_OK)
		return result;

	*setting = mode.timing.flags;

	return B_OK;
}


/**
 * @brief Read the current "swap displays" flag.
 *
 * @param screen Target screen.
 * @param swap   Out: true if displays are currently swapped.
 * @return       @c B_OK on success; otherwise an accelerant error.
 */
status_t
GetSwapDisplays(BScreen *screen, bool *swap)
{
	status_t result;
	uint32 tmp;

	result = GetSetting(screen, ms_swap, &tmp);
	if (result != B_OK)
		return result;

	*swap = tmp != 0;

	return B_OK;
}


/**
 * @brief Write the "swap displays" flag.
 *
 * @param screen Target screen.
 * @param swap   New flag value.
 * @return       @c B_OK on success; otherwise an accelerant error.
 */
status_t
SetSwapDisplays(BScreen *screen, bool swap)
{
	return SetSetting(screen, ms_swap, swap);
}


/**
 * @brief Read the current "use laptop panel" flag.
 *
 * @param screen Target screen.
 * @param use    Out: true if the laptop panel is forced on.
 * @return       @c B_OK on success; otherwise an accelerant error.
 */
status_t
GetUseLaptopPanel(BScreen *screen, bool *use)
{
	status_t result;
	uint32 tmp;

	result = GetSetting(screen, ms_use_laptop_panel, &tmp);
	if (result != B_OK)
		return result;

	*use = tmp != 0;
	return B_OK;
}


/**
 * @brief Write the "use laptop panel" flag.
 *
 * @param screen Target screen.
 * @param use    New flag value.
 * @return       @c B_OK on success; otherwise an accelerant error.
 */
status_t
SetUseLaptopPanel(BScreen *screen, bool use)
{
	return SetSetting(screen, ms_use_laptop_panel, use);
}


/**
 * @brief Enumerate the @a idx-th supported TV standard.
 *
 * @param screen   Target screen.
 * @param idx      Zero-based index of the standard to retrieve.
 * @param standard Out: receives the standard identifier.
 * @return         @c B_OK on success; otherwise an accelerant error.
 */
status_t
GetNthSupportedTVStandard(BScreen *screen, int idx, uint32 *standard)
{
	return GetNthSupportedSetting(
		screen, ms_tv_standard, (int32)idx, standard);
}


/**
 * @brief Read the currently active TV standard.
 *
 * @param screen   Target screen.
 * @param standard Out: receives the active standard identifier.
 * @return         @c B_OK on success; otherwise an accelerant error.
 */
status_t
GetTVStandard(BScreen *screen, uint32 *standard)
{
	return GetSetting(screen, ms_tv_standard, standard);
}


/**
 * @brief Apply a TV standard to the accelerant.
 *
 * @param screen   Target screen.
 * @param standard New TV standard identifier.
 * @return         @c B_OK on success; otherwise an accelerant error.
 */
status_t
SetTVStandard(BScreen *screen, uint32 standard)
{
	return SetSetting(screen, ms_tv_standard, standard);
}


/**
 * @brief Probe whether the accelerant exposes the multi-monitor settings tunnel.
 *
 * Sets a request bit in a real mode, calls @c ProposeMode, and inspects the
 * reply bits in @c timing.flags. Drivers without the tunnel either reject
 * the proposal or never set the reply bit.
 *
 * @param screen Target screen.
 * @return       @c B_OK if the tunnel is supported; otherwise an error.
 */
status_t
TestMultiMonSupport(BScreen *screen)
{
	display_mode *modeList = NULL;
	display_mode low, high;
	uint32 count;
	status_t result;

	// take any valid mode
	result = screen->GetModeList(&modeList, &count);
	if (result != B_OK)
		return result;

	if (count < 1)
		return B_ERROR;

	// set request bits
	modeList[0].timing.flags |= RADEON_MODE_MULTIMON_REQUEST;
	modeList[0].timing.flags &= ~RADEON_MODE_MULTIMON_REPLY;
	low = high = modeList[0];

	result = screen->ProposeMode(&modeList[0], &low, &high);
	if (result != B_OK)
		goto out;

	// check reply bits
	if ((modeList[0].timing.flags & RADEON_MODE_MULTIMON_REQUEST) == 0
		&& (modeList[0].timing.flags & RADEON_MODE_MULTIMON_REPLY) != 0)
		result = B_OK;
	else
		result = B_ERROR;

out:
	free(modeList);
	return result;
}
