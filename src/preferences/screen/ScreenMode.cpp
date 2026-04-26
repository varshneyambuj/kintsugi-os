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
 *   Copyright 2005-2011, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Doerfler, axeld@pinc-software.de
 */


/**
 * @file ScreenMode.cpp
 * @brief Implementation of the ScreenMode helper used by ScreenWindow.
 *
 * Talks to app_server through @c BScreen to enumerate display modes,
 * apply new modes (per-workspace or globally), and revert to the original
 * mode the app started with. Also resolves EDID monitor info and the PNP-ID
 * vendor lookup table generated in Vendors.h.
 */


#include "ScreenMode.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <InterfaceDefs.h>
#include <String.h>

#include <compute_display_timing.h>


/* Note, this headers defines a *private* interface to the Radeon accelerant.
 * It's a solution that works with the current BeOS interface that Haiku
 * adopted.
 * However, it's not a nice and clean solution. Don't use this header in any
 * application if you can avoid it. No other driver is using this, or should
 * be using this.
 * It will be replaced as soon as we introduce an updated accelerant interface
 * which may even happen before R1 hits the streets.
 */

#include "multimon.h"	// the usual: DANGER WILL, ROBINSON!



// Vendors.h is generated using these commands (plus a bit of manual editing):
/*
 * wget https://uefi.org/uefi-pnp-export -O Vendors.h.tmp
 * if [ $? -eq 0 ]; then
 *	sed -E -e 's:<thead>::g' -e 's:<tr.*="(.+)"><td>:{ ":g' -e 's:<td>[[:digit:]]{2}/[[:digit:]]{2}/[[:digit:]]{4}</td>::g' -e 's: *(<\/td><td>):\", \":g' -e 's: *<\/td>.<\/tr>:\" },:g' -e 's/"(.*?)", "(.*?)"/\"\2\", \"\1\"/' -e 's/&amp;/\&/' -e "s/&#039;/'/" Vendors.h.tmp | grep -v '<' | sort > Vendors.h
 * fi
 * rm Vendors.h.tmp
 */

/**
 * @brief Pair of a three-letter EDID PNP-ID and its manufacturer name.
 *
 * Equality is case-insensitive on the @c id field; the function-call
 * operator provides a strict ordering used by @c std::find.
 */
struct pnp_id {
	const char* id;
	const char* manufacturer;
	bool operator==(const pnp_id& a) const {
		return ::strcasecmp(a.id, id) == 0;
	};
	bool operator()(const pnp_id& a, const pnp_id& b) const {
		return ::strcasecmp(a.id, b.id) < 0;
	};
};

/** @brief Static table of EDID PNP-IDs to vendor names; generated upstream. */
static const struct pnp_id kPNPIDs[] = {
#include "Vendors.h"
};


/**
 * @brief Decode the combine mode from the flags and dimensions of @a mode.
 *
 * @param mode display_mode reported by app_server.
 * @return     The corresponding @c combine_mode value.
 */
static combine_mode
get_combine_mode(const display_mode& mode)
{
	if ((mode.flags & B_SCROLL) == 0)
		return kCombineDisable;

	if (mode.virtual_width == mode.timing.h_display * 2)
		return kCombineHorizontally;

	if (mode.virtual_height == mode.timing.v_display * 2)
		return kCombineVertically;

	return kCombineDisable;
}


/**
 * @brief Compute the displayable refresh rate for @a mode.
 *
 * Rounds the result to one decimal digit because the underlying pixel
 * clock value cannot be tuned exactly and is subject to driver rounding.
 *
 * @param mode display_mode to inspect.
 * @return     Refresh rate in Hz, rounded to 0.1 Hz.
 */
static float
get_refresh_rate(const display_mode& mode)
{
	// we have to be catious as refresh rate cannot be controlled directly,
	// so it suffers under rounding errors and hardware restrictions
	return rint(10 * float(mode.timing.pixel_clock * 1000)
		/ float(mode.timing.h_total * mode.timing.v_total)) / 10.0;
}


/**
 * @brief @c qsort callback that orders display_modes by resolution then refresh.
 *
 * Combine modes are normalized (halved) before comparison so the visible
 * width and height drive the ordering rather than the raw virtual size.
 *
 * @param _mode1 Pointer to the first @c display_mode (cast from @c void*).
 * @param _mode2 Pointer to the second @c display_mode.
 * @return Negative, zero, or positive, following @c qsort conventions.
 */
static int
compare_mode(const void* _mode1, const void* _mode2)
{
	display_mode *mode1 = (display_mode *)_mode1;
	display_mode *mode2 = (display_mode *)_mode2;
	combine_mode combine1, combine2;
	uint16 width1, width2, height1, height2;

	combine1 = get_combine_mode(*mode1);
	combine2 = get_combine_mode(*mode2);

	width1 = mode1->virtual_width;
	height1 = mode1->virtual_height;
	width2 = mode2->virtual_width;
	height2 = mode2->virtual_height;

	if (combine1 == kCombineHorizontally)
		width1 /= 2;
	if (combine1 == kCombineVertically)
		height1 /= 2;
	if (combine2 == kCombineHorizontally)
		width2 /= 2;
	if (combine2 == kCombineVertically)
		height2 /= 2;

	if (width1 != width2)
		return width1 - width2;

	if (height1 != height2)
		return height1 - height2;

	return (int)(10 * get_refresh_rate(*mode1)
		-  10 * get_refresh_rate(*mode2));
}


//	#pragma mark -


/**
 * @brief Return the bits-per-pixel implied by the @c space field.
 *
 * @return 8/15/16/24/32 for the recognized color spaces, 0 otherwise.
 */
int32
screen_mode::BitsPerPixel() const
{
	switch (space) {
		case B_RGB32:	return 32;
		case B_RGB24:	return 24;
		case B_RGB16:	return 16;
		case B_RGB15:	return 15;
		case B_CMAP8:	return 8;
		default:		return 0;
	}
}


/** @brief Equality, defined as the negation of operator!=(). */
bool
screen_mode::operator==(const screen_mode &other) const
{
	return !(*this != other);
}


/**
 * @brief Field-wise inequality covering all visible mode parameters.
 *
 * @param other Mode to compare against.
 * @return      True when any of width/height/space/refresh/combine or
 *              the multi-monitor flags differ.
 */
bool
screen_mode::operator!=(const screen_mode &other) const
{
	return width != other.width || height != other.height
		|| space != other.space || refresh != other.refresh
		|| combine != other.combine
		|| swap_displays != other.swap_displays
		|| use_laptop_panel != other.use_laptop_panel
		|| tv_standard != other.tv_standard;
}


/**
 * @brief Populate this struct from an app_server @c display_mode.
 *
 * Extracts width / height / color space / combine mode / refresh rate
 * and leaves the multi-monitor flags zeroed; the caller is expected to
 * fill them in via @c GetSwapDisplays() etc.
 *
 * @param mode Source display_mode to derive the fields from.
 */
void
screen_mode::SetTo(const display_mode& mode)
{
	width = mode.virtual_width;
	height = mode.virtual_height;
	space = (color_space)mode.space;
	combine = get_combine_mode(mode);
	refresh = get_refresh_rate(mode);

	if (combine == kCombineHorizontally)
		width /= 2;
	else if (combine == kCombineVertically)
		height /= 2;

	swap_displays = false;
	use_laptop_panel = false;
	tv_standard = 0;
}


//	#pragma mark -


/**
 * @brief Construct a ScreenMode bound to @a window.
 *
 * Pre-fetches the supported mode list from app_server and sorts it by
 * resolution and refresh rate so menus look orderly.
 *
 * @param window BWindow used to obtain the corresponding @c BScreen.
 */
ScreenMode::ScreenMode(BWindow* window)
	:
	fWindow(window),
	fUpdatedModes(false)
{
	BScreen screen(window);
	if (screen.GetModeList(&fModeList, &fModeCount) == B_OK) {
		// sort modes by resolution and refresh to make
		// the resolution and refresh menu look nicer
		qsort(fModeList, fModeCount, sizeof(display_mode), compare_mode);
	} else {
		fModeList = NULL;
		fModeCount = 0;
	}
}


/** @brief Free the cached display-mode list. */
ScreenMode::~ScreenMode()
{
	free(fModeList);
}


/**
 * @brief Apply @a mode to the given workspace, recording the original first.
 *
 * Captures the pre-change modes for later @c Revert() the first time it is
 * called, then writes the multi-monitor settings (swap, panel, TV) and asks
 * app_server to set the resolved display_mode.
 *
 * @param mode      Desired screen mode.
 * @param workspace Target workspace, or @c ~0 for the current one.
 * @retval B_OK              On success.
 * @retval B_ENTRY_NOT_FOUND When no matching display_mode could be resolved.
 */
status_t
ScreenMode::Set(const screen_mode& mode, int32 workspace)
{
	if (!fUpdatedModes)
		UpdateOriginalModes();

	BScreen screen(fWindow);

	if (workspace == ~0)
		workspace = current_workspace();

	// TODO: our app_server doesn't fully support workspaces, yet
	SetSwapDisplays(&screen, mode.swap_displays);
	SetUseLaptopPanel(&screen, mode.use_laptop_panel);
	SetTVStandard(&screen, mode.tv_standard);

	display_mode displayMode;
	if (!_GetDisplayMode(mode, displayMode))
		return B_ENTRY_NOT_FOUND;

	return screen.SetMode(workspace, &displayMode, true);
}


/**
 * @brief Retrieve the current screen mode of @a workspace.
 *
 * Reads both the display_mode and the multi-monitor accelerant flags and
 * fills @a mode with their combined view.
 *
 * @param mode      Out: the populated screen_mode.
 * @param workspace Workspace to query, or @c ~0 for the current one.
 * @retval B_OK     On success.
 * @retval B_ERROR  When the display_mode could not be queried.
 */
status_t
ScreenMode::Get(screen_mode& mode, int32 workspace) const
{
	display_mode displayMode;
	BScreen screen(fWindow);

	if (workspace == ~0)
		workspace = current_workspace();

	if (screen.GetMode(workspace, &displayMode) != B_OK)
		return B_ERROR;

	mode.SetTo(displayMode);

	// TODO: our app_server doesn't fully support workspaces, yet
	if (GetSwapDisplays(&screen, &mode.swap_displays) != B_OK)
		mode.swap_displays = false;
	if (GetUseLaptopPanel(&screen, &mode.use_laptop_panel) != B_OK)
		mode.use_laptop_panel = false;
	if (GetTVStandard(&screen, &mode.tv_standard) != B_OK)
		mode.tv_standard = 0;

	return B_OK;
}


/**
 * @brief Return the originally-active screen mode of @a workspace.
 *
 * Useful for the Revert button: callers can compare the live mode against
 * the original snapshot taken when the app started.
 *
 * @param mode      Out: original mode for the given workspace.
 * @param workspace Workspace index, or @c ~0 for the current workspace.
 * @retval B_OK         On success.
 * @retval B_BAD_INDEX  If @a workspace is out of range.
 */
status_t
ScreenMode::GetOriginalMode(screen_mode& mode, int32 workspace) const
{
	if (workspace == ~0)
		workspace = current_workspace();
		// TODO this should use kMaxWorkspaces
	else if (workspace > 31)
		return B_BAD_INDEX;

	mode = fOriginal[workspace];

	return B_OK;
}


/**
 * @brief Apply a raw @c display_mode, bypassing the @c screen_mode wrapper.
 *
 * Used by callers who already have a fully-populated display_mode and want
 * to force exactly that mode without re-resolving width/height/refresh.
 *
 * @param mode      The display_mode to apply.
 * @param workspace Target workspace, or @c ~0 for the current one.
 * @return          @c B_OK on success; otherwise an error from app_server.
 */
status_t
ScreenMode::Set(const display_mode& mode, int32 workspace)
{
	if (!fUpdatedModes)
		UpdateOriginalModes();

	BScreen screen(fWindow);

	if (workspace == ~0)
		workspace = current_workspace();

	// BScreen::SetMode() needs a non-const display_mode
	display_mode nonConstMode;
	memcpy(&nonConstMode, &mode, sizeof(display_mode));
	return screen.SetMode(workspace, &nonConstMode, true);
}


/**
 * @brief Read the raw @c display_mode active in @a workspace.
 *
 * @param mode      Out: receives the active display_mode.
 * @param workspace Workspace to query, or @c ~0 for the current one.
 * @return          @c B_OK on success; otherwise an error from app_server.
 */
status_t
ScreenMode::Get(display_mode& mode, int32 workspace) const
{
	BScreen screen(fWindow);

	if (workspace == ~0)
		workspace = current_workspace();

	return screen.GetMode(workspace, &mode);
}


/*!	This method assumes that you already reverted to the correct number
	of workspaces.
*/
/**
 * @brief Restore every workspace to its original display mode.
 *
 * Iterates over all workspaces in count_workspaces() order and writes back
 * the modes captured by @c UpdateOriginalModes(). Multi-monitor flags are
 * only restored for the current workspace because the accelerant tunnel
 * does not support per-workspace flags.
 *
 * @retval B_OK     On success.
 * @retval B_ERROR  When @c UpdateOriginalModes() has never been called.
 * @note  Caller must first restore the original workspace count.
 */
status_t
ScreenMode::Revert()
{
	if (!fUpdatedModes)
		return B_ERROR;

	status_t result = B_OK;
	screen_mode current;
	for (int32 workspace = 0; workspace < count_workspaces(); workspace++) {
		if (Get(current, workspace) == B_OK && fOriginal[workspace] == current)
			continue;

		BScreen screen(fWindow);

		// TODO: our app_server doesn't fully support workspaces, yet
		if (workspace == current_workspace()) {
			SetSwapDisplays(&screen, fOriginal[workspace].swap_displays);
			SetUseLaptopPanel(&screen, fOriginal[workspace].use_laptop_panel);
			SetTVStandard(&screen, fOriginal[workspace].tv_standard);
		}

		result = screen.SetMode(workspace, &fOriginalDisplayMode[workspace],
			true);
		if (result != B_OK)
			break;
	}

	return result;
}


/**
 * @brief Snapshot the current per-workspace modes for later @c Revert().
 *
 * Idempotent: @c Revert() depends on this being called at least once,
 * usually right before the first @c Set().
 */
void
ScreenMode::UpdateOriginalModes()
{
	BScreen screen(fWindow);
	for (int32 workspace = 0; workspace < count_workspaces(); workspace++) {
		if (screen.GetMode(workspace, &fOriginalDisplayMode[workspace])
				== B_OK) {
			Get(fOriginal[workspace], workspace);
			fUpdatedModes = true;
		}
	}
}


/**
 * @brief Stub indicating universal color-space support.
 *
 * @param mode  Mode being queried (unused).
 * @param space Color space being queried (unused).
 * @return      Always true.
 * @note  Real filtering happens in ScreenWindow which iterates the
 *        available mode list and tests each entry directly.
 */
bool
ScreenMode::SupportsColorSpace(const screen_mode& mode, color_space space)
{
	return true;
}


/**
 * @brief Compute the lower and upper refresh-rate bounds for @a mode.
 *
 * Asks app_server for the pixel-clock limits of the equivalent
 * display_mode and converts them to Hz using the total horizontal
 * and vertical pixel counts.
 *
 * @param mode Mode whose limits should be retrieved.
 * @param min  Out: minimum supported refresh rate in Hz.
 * @param max  Out: maximum supported refresh rate in Hz.
 * @return     @c B_OK on success; otherwise @c B_ERROR.
 */
status_t
ScreenMode::GetRefreshLimits(const screen_mode& mode, float& min, float& max)
{
	uint32 minClock, maxClock;
	display_mode displayMode;
	if (!_GetDisplayMode(mode, displayMode))
		return B_ERROR;

	BScreen screen(fWindow);
	if (screen.GetPixelClockLimits(&displayMode, &minClock, &maxClock) < B_OK)
		return B_ERROR;

	uint32 total = displayMode.timing.h_total * displayMode.timing.v_total;
	min = minClock * 1000.0 / total;
	max = maxClock * 1000.0 / total;

	return B_OK;
}


/**
 * @brief Look up the human-readable vendor name for a PNP-ID.
 *
 * Performs a case-insensitive binary search in the sorted @c kPNPIDs table
 * generated from the UEFI PNP-ID export.
 *
 * @param id Three-letter PNP-ID from an EDID block.
 * @return   Pointer to the vendor name (statically allocated), or NULL when
 *           the code is unknown.
 */
const char*
ScreenMode::GetManufacturerFromID(const char* id) const
{
	// We assume the array is sorted
	const size_t numElements = B_COUNT_OF(kPNPIDs);
	const struct pnp_id key = { id, "dummy" };
	const pnp_id* lastElement = kPNPIDs + numElements;
	const pnp_id* element = std::find(kPNPIDs, lastElement, key);
	if (element == lastElement) {
		// can't find the vendor code
		return NULL;
	}

	return element->manufacturer;
}


/**
 * @brief Fetch EDID monitor info, augmented with a vendor lookup and cleanup.
 *
 * Replaces the three-letter EDID vendor code with the human-readable
 * vendor name (when the code is known), strips redundant vendor strings
 * from @c info.name, and patches in a default vertical-frequency range
 * when the EDID block omits it (common on older CRTs).
 *
 * @param info             Out: receives the monitor info.
 * @param _diagonalInches  Optional out: diagonal screen size in inches.
 * @return                 @c B_OK on success; otherwise an error from
 *                         @c BScreen::GetMonitorInfo().
 */
status_t
ScreenMode::GetMonitorInfo(monitor_info& info, float* _diagonalInches)
{
	BScreen screen(fWindow);
	status_t status = screen.GetMonitorInfo(&info);
	if (status != B_OK)
		return status;

	if (_diagonalInches != NULL) {
		*_diagonalInches = round(sqrt(info.width * info.width
			+ info.height * info.height) / 0.254) / 10.0;
	}

	// Some older CRT monitors do not contain the monitor range information
	// (EDID1_MONITOR_RANGES) in their EDID info resulting in the min/max
	// horizontal/vertical frequencies being zero.  In this case, set the
	// vertical frequency range to 60..85 Hz.
	if (info.min_vertical_frequency == 0) {
		info.min_vertical_frequency = 60;
		info.max_vertical_frequency = 85;
	}

	char vendor[4];
	strlcpy(vendor, info.vendor, sizeof(vendor));
	const char* vendorString = GetManufacturerFromID(vendor);
	if (vendorString != NULL)
		strlcpy(info.vendor, vendorString, sizeof(info.vendor));

	// Remove extraneous vendor strings and whitespace

	BString name(info.name);
	name.IReplaceAll(info.vendor, "");
	name.Trim();

	strcpy(info.name, name.String());

	return B_OK;
}


/**
 * @brief Forward-fill @a info from the underlying accelerant.
 *
 * @param info Out: receives the accelerant device info.
 * @return     Whatever @c BScreen::GetDeviceInfo() returns.
 */
status_t
ScreenMode::GetDeviceInfo(accelerant_device_info& info)
{
	BScreen screen(fWindow);
	return screen.GetDeviceInfo(&info);
}


/**
 * @brief Return the @a index-th cached mode as a @c screen_mode.
 *
 * @param index Zero-based index into the cached mode list.
 * @return      A populated @c screen_mode; clamped to the bounds of the list.
 */
screen_mode
ScreenMode::ModeAt(int32 index)
{
	screen_mode mode;
	mode.SetTo(DisplayModeAt(index));

	return mode;
}


/**
 * @brief Return the raw @c display_mode at @a index in the cached list.
 *
 * Out-of-range indices are clamped to the nearest valid entry.
 *
 * @param index Zero-based index.
 * @return      Reference to the cached display_mode.
 */
const display_mode&
ScreenMode::DisplayModeAt(int32 index)
{
	if (index < 0)
		index = 0;
	else if (index >= (int32)fModeCount)
		index = fModeCount - 1;

	return fModeList[index];
}


/** @brief Number of cached display modes available for selection. */
int32
ScreenMode::CountModes()
{
	return fModeCount;
}


/*!	Searches for a similar mode in the reported mode list, and if that does not
	find a matching mode, it will compute the mode manually using the GTF.
*/
/**
 * @brief Resolve a @c screen_mode to a concrete @c display_mode.
 *
 * First scans the driver-reported list for an exact resolution / color
 * space match within 0.6% of the requested refresh rate, tweaking the
 * pixel clock so the actual rate matches the request. Falls back to the
 * VESA Generalized Timing Formula when no driver mode is close enough.
 *
 * @param mode         The high-level mode to resolve.
 * @param displayMode  Out: receives the matching display_mode on success.
 * @return             True when a mode was resolved; false otherwise.
 */
bool
ScreenMode::_GetDisplayMode(const screen_mode& mode, display_mode& displayMode)
{
	uint16 virtualWidth, virtualHeight;
	int32 bestIndex = -1;
	float bestDiff = 999;

	virtualWidth = mode.combine == kCombineHorizontally
		? mode.width * 2 : mode.width;
	virtualHeight = mode.combine == kCombineVertically
		? mode.height * 2 : mode.height;

	// try to find mode in list provided by driver
	for (uint32 i = 0; i < fModeCount; i++) {
		if (fModeList[i].virtual_width != virtualWidth
			|| fModeList[i].virtual_height != virtualHeight
			|| (color_space)fModeList[i].space != mode.space)
			continue;

		// Accept the mode if the computed refresh rate of the mode is within
		// 0.6 percent of the refresh rate specified by the caller.  Note that
		// refresh rates computed from mode parameters is not exact; especially
		// some of the older modes such as 640x480, 800x600, and 1024x768.
		// The tolerance of 0.6% was obtained by examining the various possible
		// modes.

		float refreshDiff = fabs(get_refresh_rate(fModeList[i]) - mode.refresh);
		if (refreshDiff < 0.006 * mode.refresh) {
			// Accept this mode.
			displayMode = fModeList[i];
			displayMode.h_display_start = 0;
			displayMode.v_display_start = 0;

			// Since the computed refresh rate of the selected mode might differ
			// from selected refresh rate by a few tenths (e.g. 60.2 instead of
			// 60.0), tweak the pixel clock so the the refresh rate of the mode
			// matches the selected refresh rate.

			displayMode.timing.pixel_clock = uint32(((displayMode.timing.h_total
				* displayMode.timing.v_total * mode.refresh) / 1000.0) + 0.5);
			return true;
		}

		// Mode not acceptable.

		if (refreshDiff < bestDiff) {
			bestDiff = refreshDiff;
			bestIndex = i;
		}
	}

	// we didn't find the exact mode, but something very similar?
	if (bestIndex == -1)
		return false;

	displayMode = fModeList[bestIndex];
	displayMode.h_display_start = 0;
	displayMode.v_display_start = 0;

	// For the mode selected by the width, height, and refresh rate, compute
	// the video timing parameters for the mode by using the VESA Generalized
	// Timing Formula (GTF).
	compute_display_timing(mode.width, mode.height, mode.refresh, false,
		&displayMode.timing);

	return true;
}
