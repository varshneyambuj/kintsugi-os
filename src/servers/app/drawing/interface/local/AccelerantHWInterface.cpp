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
 *   Copyright 2001-2016, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus, superstippi@gmx.de
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Axel Dörfler, axeld@pinc-software.de
 *       Michael Lotz, mmlr@mlotz.ch
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file AccelerantHWInterface.cpp
 * @brief Production HWInterface that drives a graphics card through its
 *        accelerant add-on.
 *
 * Initialize() walks /dev/graphics, opens the first device that has a
 * usable accelerant, dlopens the matching .so, clones it for this user,
 * and resolves the standard hook table (acquire/release engine, mode
 * count / list, frame-buffer config, set/get display mode, pixel-clock
 * limits, plus the optional cursor / DPMS / brightness / overlay hooks).
 *
 * SetMode() loads or refreshes the supported mode list, picks a real
 * device mode that matches the requested width/height/colour-space, and
 * uses the optional fill-rect / blit accelerator paths when present;
 * otherwise it falls back to the software base implementation. The
 * frame buffer is exposed through two AccelerantBuffer instances: the
 * front buffer is the visible region, the back buffer is either the
 * card-provided off-screen region or a malloced shadow when the card
 * does not provide one.
 *
 * The cursor implementation prefers the hardware-cursor hook, then the
 * shape hook, then the software cursor compositor in the base class.
 */


#include "AccelerantHWInterface.h"

#include <new>

#include <dirent.h>
#include <edid.h>
#include <driver_settings.h>
#include <graphic_driver.h>
#include <image.h>
#include <safemode_defs.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <syscalls.h>
#include <syslog.h>
#include <unistd.h>

#include <Accelerant.h>
#include <Cursor.h>
#include <Directory.h>
#include <FindDirectory.h>
#include <Path.h>
#include <PathFinder.h>
#include <String.h>
#include <StringList.h>

#include "AccelerantBuffer.h"
#include "MallocBuffer.h"
#include "Overlay.h"
#include "RGBColor.h"
#include "ServerConfig.h"
#include "ServerCursor.h"
#include "ServerProtocol.h"
#include "SystemPalette.h"


using std::nothrow;


#ifdef DEBUG_DRIVER_MODULE
#	include <stdio.h>
#	define ATRACE(x) printf x
#else
#	define ATRACE(x) ;
#endif


/** @brief Default capacity used when sizing the fill-rect / blit
           parameter buffers handed to the accelerant. */
const int32 kDefaultParamsCount = 64;


/**
 * @brief Compares two display_mode structures byte-for-byte.
 *
 * Used by the mode-list helpers; treats the structs as opaque blobs so
 * any newly added field is automatically considered.
 *
 * @param a  First mode.
 * @param b  Second mode.
 * @return   true when the two structs are bitwise equal.
 */
bool
operator==(const display_mode& a, const display_mode& b)
{
	return memcmp(&a, &b, sizeof(display_mode)) == 0;
}


/**
 * @brief Checks the safemode flag that forces the fall-back display mode.
 *
 * @return     true when the kernel safemode option
 *             B_SAFEMODE_FAIL_SAFE_VIDEO_MODE is set to a positive value.
 */
bool
use_fail_safe_video_mode()
{
	char buffer[B_FILE_NAME_LENGTH];
	size_t size = sizeof(buffer);

	status_t status = _kern_get_safemode_option(
		B_SAFEMODE_FAIL_SAFE_VIDEO_MODE, buffer, &size);
	if (status == B_OK) {
		if (!strncasecmp(buffer, "true", size)
			|| !strncasecmp(buffer, "yes", size)
			|| !strncasecmp(buffer, "on", size)
			|| !strncasecmp(buffer, "enabled", size)) {
			return true;
		}
	}

	return false;
}


//	#pragma mark - AccelerantHWInterface


/**
 * @brief Constructs the interface with empty hook tables and a default
 *        display mode.
 *
 * All accelerant hook pointers start NULL; they are populated by
 * Initialize() through _OpenAccelerant() and _SetupDefaultHooks(). The
 * front buffer wrapper is allocated up front; the back buffer is created
 * (or aliased to the card's off-screen region) by SetMode().
 */
AccelerantHWInterface::AccelerantHWInterface()
	:
	HWInterface(),
	fCardFD(-1),
	fAccelerantImage(-1),
	fAccelerantHook(NULL),
	fEngineToken(NULL),
	fSyncToken(),

	// required hooks
	fAccGetModeCount(NULL),
	fAccGetModeList(NULL),
	fAccGetFrameBufferConfig(NULL),
	fAccSetDisplayMode(NULL),
	fAccGetDisplayMode(NULL),
	fAccGetPixelClockLimits(NULL),

	// optional accelerant hooks
	fAccGetTimingConstraints(NULL),
	fAccProposeDisplayMode(NULL),
	fAccSetCursorShape(NULL),
	fAccSetCursorBitmap(NULL),
	fAccMoveCursor(NULL),
	fAccShowCursor(NULL),

	// dpms hooks
	fAccDPMSCapabilities(NULL),
	fAccDPMSMode(NULL),
	fAccSetDPMSMode(NULL),

	// brightness hooks
	fAccSetBrightness(NULL),
	fAccGetBrightness(NULL),

	// overlay hooks
	fAccOverlayCount(NULL),
	fAccOverlaySupportedSpaces(NULL),
	fAccOverlaySupportedFeatures(NULL),
	fAccAllocateOverlayBuffer(NULL),
	fAccReleaseOverlayBuffer(NULL),
	fAccGetOverlayConstraints(NULL),
	fAccAllocateOverlay(NULL),
	fAccReleaseOverlay(NULL),
	fAccConfigureOverlay(NULL),

	fModeCount(0),
	fModeList(NULL),

	fBackBuffer(NULL),
	fFrontBuffer(new (nothrow) AccelerantBuffer()),

	fInitialModeSwitch(true),

	fRetraceSemaphore(-1),

	fRectParams(new (nothrow) fill_rect_params[kDefaultParamsCount]),
	fRectParamsCount(kDefaultParamsCount),
	fBlitParams(new (nothrow) blit_params[kDefaultParamsCount]),
	fBlitParamsCount(kDefaultParamsCount)
{
	fDisplayMode.virtual_width = 0;
	fDisplayMode.virtual_height = 0;
	fDisplayMode.space = B_RGB32;

	// NOTE: I have no clue what I'm doing here.
	//fSyncToken.counter = 0;
	//fSyncToken.engine_id = 0;
	memset(&fSyncToken, 0, sizeof(sync_token));
}


/**
 * @brief Releases the per-call parameter buffers and the cached mode list.
 *
 * The accelerant itself is unhooked by Shutdown(); the destructor only
 * cleans up the heap arrays.
 */
AccelerantHWInterface::~AccelerantHWInterface()
{
	delete[] fRectParams;
	delete[] fBlitParams;

	delete[] fModeList;
}


/**
 * @brief Opens the first available graphics device and initialises it.
 *
 * Iterates over /dev/graphics entries and tries to clone the matching
 * accelerant for each one until a working pair is found.
 *
 * @return     B_OK on success or an appropriate error code on failure.
 * @retval B_OK             A graphics device and accelerant are ready.
 * @retval B_NO_MEMORY      The fill-rect / blit parameter buffers could
 *                          not be allocated.
 * @retval B_ENTRY_NOT_FOUND  No suitable graphics device exists.
 */
status_t
AccelerantHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();

	if (!fRectParams || !fBlitParams)
		return B_NO_MEMORY;

	if (ret >= B_OK) {
		for (int32 i = 0; fCardFD != B_ENTRY_NOT_FOUND; i++) {
			fCardFD = _OpenGraphicsDevice(i);
			if (fCardFD < 0) {
				ATRACE(("Failed to open graphics device\n"));
				continue;
			}

			if (_OpenAccelerant(fCardFD) == B_OK)
				break;

			close(fCardFD);
			// _OpenAccelerant() failed, try to open next graphics card
		}

		return fCardFD >= 0 ? B_OK : fCardFD;
	}
	return ret;
}


/**
 * @brief Walks @a directory recursively, skipping VESA / framebuffer
 *        fall-back nodes, until it finds the @a deviceNumber-th regular
 *        entry.
 *
 * @param directory     Filesystem directory to walk (initially
 *                      "/dev/graphics/").
 * @param deviceNumber  Zero-based index of the target device.
 * @param count         In/out counter of regular entries already seen.
 * @param _path         Output, filled with the resolved path on hit.
 * @return              true when a matching entry is written into
 *                      @a _path.
 */
bool
AccelerantHWInterface::_RecursiveScan(const char* directory, int deviceNumber, int &count,
	char *_path)
{
	ATRACE(("_RecursiveScan directory: %s\n", directory));

	BEntry entry;
	BDirectory dir(directory);
	while (dir.GetNextEntry(&entry) == B_OK) {
		BPath path;
		entry.GetPath(&path);
		if (!strcmp(path.Path(), "/dev/graphics/vesa")
			|| !strcmp(path.Path(), "/dev/graphics/framebuffer")) {
			continue;
		}

		if (entry.IsDirectory()) {
			if (_RecursiveScan(path.Path(), deviceNumber, count, _path))
				return true;
		} else {
			if (count == deviceNumber) {
				strlcpy(_path, path.Path(), PATH_MAX);
				return true;
			}
			count++;
		}
	}
	return false;
}


/**
 * @brief Opens a graphics device for read-write access.
 *
 * The @a deviceNumber is relative to the number of graphics devices that
 * open successfully (not the directory order). Graphics drivers must be
 * openable more than once, so the function ignores devices that fail to
 * open. Falls back to /dev/graphics/vesa or /dev/graphics/framebuffer
 * when no real card is found.
 *
 * @param deviceNumber  Number identifying which graphics card to open
 *                      (1 for the first card).
 * @return              File descriptor on success, B_ENTRY_NOT_FOUND if
 *                      no usable device exists.
 */
int
AccelerantHWInterface::_OpenGraphicsDevice(int deviceNumber)
{
	int device = -1;
	int count = 0;
	if (!use_fail_safe_video_mode()) {
		char path[PATH_MAX];
		if (_RecursiveScan("/dev/graphics/", deviceNumber, count, path))
			device = open(path, B_READ_WRITE);
	}

	// Open VESA or Framebuffer driver if we were not able to get a better one.
	if (device == -1 && count < deviceNumber) {
		device = open("/dev/graphics/vesa", B_READ_WRITE);
		if (device > 0) {
			// store the device, so that we can access the planar blitter
			fVGADevice = device;
		} else {
			device = open("/dev/graphics/framebuffer", B_READ_WRITE);
		}

		if (device < 0)
			return B_ENTRY_NOT_FOUND;
	}

	return device;
}


/**
 * @brief Loads the accelerant add-on for @a device and initialises it.
 *
 * Queries the accelerant signature via ioctl, finds a matching add-on
 * under any add-on directory, loads it, looks up B_ACCELERANT_ENTRY_POINT,
 * runs B_INIT_ACCELERANT, then resolves the standard hook table via
 * _SetupDefaultHooks(). On failure the partially loaded image is
 * unloaded.
 *
 * @param device  File descriptor of the graphics device.
 * @return        B_OK on success, B_ERROR on any failure.
 */
status_t
AccelerantHWInterface::_OpenAccelerant(int device)
{
	char signature[1024];
	if (ioctl(device, B_GET_ACCELERANT_SIGNATURE,
			&signature, sizeof(signature)) != B_OK) {
		return B_ERROR;
	}

	ATRACE(("accelerant signature is: %s\n", signature));

	fAccelerantImage = -1;

	BString leafPath("/accelerants/");
	leafPath << signature;
	BStringList addOnPaths;
	BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, leafPath.String(),
		addOnPaths);
	int32 count = addOnPaths.CountStrings();
	for (int32 i = 0; i < count; i++) {
		const char* path = addOnPaths.StringAt(i).String();
		struct stat accelerantStat;
		if (stat(path, &accelerantStat) != 0)
			continue;

		ATRACE(("accelerant path is: %s\n", path));

		fAccelerantImage = load_add_on(path);
		if (fAccelerantImage >= 0) {
			if (get_image_symbol(fAccelerantImage, B_ACCELERANT_ENTRY_POINT,
					B_SYMBOL_TYPE_ANY, (void**)(&fAccelerantHook)) != B_OK) {
				ATRACE(("unable to get B_ACCELERANT_ENTRY_POINT\n"));
				unload_add_on(fAccelerantImage);
				fAccelerantImage = -1;
				return B_ERROR;
			}

			init_accelerant initAccelerant;
			initAccelerant = (init_accelerant)fAccelerantHook(
				B_INIT_ACCELERANT, NULL);
			if (!initAccelerant || initAccelerant(device) != B_OK) {
				ATRACE(("InitAccelerant unsuccessful\n"));
				unload_add_on(fAccelerantImage);
				fAccelerantImage = -1;
				return B_ERROR;
			}

			break;
		}
	}

	if (fAccelerantImage < B_OK)
		return B_ERROR;

	if (_SetupDefaultHooks() != B_OK) {
		syslog(LOG_ERR, "Accelerant %s does not export the required hooks.\n",
			signature);

		uninit_accelerant uninitAccelerant = (uninit_accelerant)
			fAccelerantHook(B_UNINIT_ACCELERANT, NULL);
		if (uninitAccelerant != NULL)
			uninitAccelerant();

		unload_add_on(fAccelerantImage);
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Resolves the required and optional accelerant hook pointers.
 *
 * Required hooks (mode count / list, frame-buffer config, set/get
 * display mode, pixel-clock limits) must all resolve or the function
 * fails. Optional hooks (timing constraints, propose mode, monitor info,
 * EDID, cursor, DPMS, brightness, overlay) are stored when present and
 * left NULL otherwise.
 *
 * @return     B_OK when every required hook is present, B_ERROR
 *             otherwise.
 */
status_t
AccelerantHWInterface::_SetupDefaultHooks()
{
	// required
	fAccGetModeCount
		= (accelerant_mode_count)fAccelerantHook(B_ACCELERANT_MODE_COUNT, NULL);
	fAccGetModeList = (get_mode_list)fAccelerantHook(B_GET_MODE_LIST, NULL);
	fAccGetFrameBufferConfig = (get_frame_buffer_config)fAccelerantHook(
		B_GET_FRAME_BUFFER_CONFIG, NULL);
	fAccSetDisplayMode
		= (set_display_mode)fAccelerantHook(B_SET_DISPLAY_MODE, NULL);
	fAccGetDisplayMode
		= (get_display_mode)fAccelerantHook(B_GET_DISPLAY_MODE, NULL);
	fAccGetPixelClockLimits = (get_pixel_clock_limits)fAccelerantHook(
		B_GET_PIXEL_CLOCK_LIMITS, NULL);

	if (!fAccGetFrameBufferConfig || !fAccGetModeCount || !fAccGetModeList
			|| !fAccSetDisplayMode || !fAccGetDisplayMode || !fAccGetPixelClockLimits) {
		return B_ERROR;
	}

	// optional
	fAccGetTimingConstraints = (get_timing_constraints)fAccelerantHook(
		B_GET_TIMING_CONSTRAINTS, NULL);
	fAccProposeDisplayMode = (propose_display_mode)fAccelerantHook(
		B_PROPOSE_DISPLAY_MODE, NULL);
	fAccGetPreferredDisplayMode = (get_preferred_display_mode)fAccelerantHook(
		B_GET_PREFERRED_DISPLAY_MODE, NULL);
	fAccGetMonitorInfo
		= (get_monitor_info)fAccelerantHook(B_GET_MONITOR_INFO, NULL);
	fAccGetEDIDInfo = (get_edid_info)fAccelerantHook(B_GET_EDID_INFO, NULL);

	// cursor
	fAccSetCursorShape
		= (set_cursor_shape)fAccelerantHook(B_SET_CURSOR_SHAPE, NULL);
	fAccSetCursorBitmap
		= (set_cursor_bitmap)fAccelerantHook(B_SET_CURSOR_BITMAP, NULL);
	fAccMoveCursor = (move_cursor)fAccelerantHook(B_MOVE_CURSOR, NULL);
	fAccShowCursor = (show_cursor)fAccelerantHook(B_SHOW_CURSOR, NULL);

	// dpms
	fAccDPMSCapabilities
		= (dpms_capabilities)fAccelerantHook(B_DPMS_CAPABILITIES, NULL);
	fAccDPMSMode = (dpms_mode)fAccelerantHook(B_DPMS_MODE, NULL);
	fAccSetDPMSMode = (set_dpms_mode)fAccelerantHook(B_SET_DPMS_MODE, NULL);

	// brightness
	fAccGetBrightness = (get_brightness)fAccelerantHook(B_GET_BRIGHTNESS, NULL);
	fAccSetBrightness = (set_brightness)fAccelerantHook(B_SET_BRIGHTNESS, NULL);

	return B_OK;
}


/**
 * @brief Re-resolves the overlay hook table after a mode change.
 *
 * Some accelerants change which overlay hooks are valid depending on the
 * current display mode, so the table is refreshed every time the mode
 * is set.
 */
void
AccelerantHWInterface::_UpdateHooksAfterModeChange()
{
	// overlay
	fAccOverlayCount = (overlay_count)fAccelerantHook(B_OVERLAY_COUNT, NULL);
	fAccOverlaySupportedSpaces = (overlay_supported_spaces)fAccelerantHook(
		B_OVERLAY_SUPPORTED_SPACES, NULL);
	fAccOverlaySupportedFeatures = (overlay_supported_features)fAccelerantHook(
		B_OVERLAY_SUPPORTED_FEATURES, NULL);
	fAccAllocateOverlayBuffer = (allocate_overlay_buffer)fAccelerantHook(
		B_ALLOCATE_OVERLAY_BUFFER, NULL);
	fAccReleaseOverlayBuffer = (release_overlay_buffer)fAccelerantHook(
		B_RELEASE_OVERLAY_BUFFER, NULL);
	fAccGetOverlayConstraints = (get_overlay_constraints)fAccelerantHook(
		B_GET_OVERLAY_CONSTRAINTS, NULL);
	fAccAllocateOverlay
		= (allocate_overlay)fAccelerantHook(B_ALLOCATE_OVERLAY, NULL);
	fAccReleaseOverlay
		= (release_overlay)fAccelerantHook(B_RELEASE_OVERLAY, NULL);
	fAccConfigureOverlay
		= (configure_overlay)fAccelerantHook(B_CONFIGURE_OVERLAY, NULL);
}


/**
 * @brief Uninitialises the accelerant, unloads its add-on, and closes
 *        the graphics device.
 *
 * @return     Always B_OK; partial failures are absorbed and logged via
 *             the accelerant's own UninitAccelerant().
 */
status_t
AccelerantHWInterface::Shutdown()
{
	if (fAccelerantHook != NULL) {
		uninit_accelerant uninitAccelerant
			= (uninit_accelerant)fAccelerantHook(B_UNINIT_ACCELERANT, NULL);
		if (uninitAccelerant != NULL)
			uninitAccelerant();

		fAccelerantHook = NULL;
	}

	if (fAccelerantImage >= 0) {
		unload_add_on(fAccelerantImage);
		fAccelerantImage = -1;
	}

	if (fCardFD >= 0) {
		close(fCardFD);
		fCardFD = -1;
	}

	return B_OK;
}


/**
 * @brief Finds the mode in the mode list closest to @a compareMode.
 *
 * Scores each candidate by a weighted blend of resolution, total pixel
 * count, pixel clock, aspect ratio, and colour space distance, and
 * returns the lowest-scoring entry.
 *
 * @param compareMode         Reference mode the caller wants to match.
 * @param compareAspectRatio  Reference aspect ratio (0 disables the
 *                            aspect-ratio term).
 * @param modeFound           Output, populated with the closest mode.
 * @param _diff               Optional output, set to the score of the
 *                            chosen mode.
 * @return                    B_OK on success, B_ERROR if the mode list
 *                            is empty.
 * @todo  Revisit the weighting heuristic.
 */
status_t
AccelerantHWInterface::_FindBestMode(const display_mode& compareMode,
	float compareAspectRatio, display_mode& modeFound, int32 *_diff) const
{
	int32 bestDiff = 0;
	int32 bestIndex = -1;
	for (int32 i = 0; i < fModeCount; i++) {
		display_mode& mode = fModeList[i];
		float aspectRatio = 0;

		if (compareAspectRatio != 0 && mode.timing.v_display != 0)
			aspectRatio = mode.timing.h_display / mode.timing.v_display;

		// compute some random equality score
		// TODO: check if these scores make sense
		int32 diff
			= 1000 * abs(mode.timing.h_display - compareMode.timing.h_display)
			+ 1000 * abs(mode.timing.v_display - compareMode.timing.v_display)
			+ abs(mode.timing.h_total * mode.timing.v_total
					- compareMode.timing.h_total * compareMode.timing.v_total)
				/ 100
			+ abs((int)(mode.timing.pixel_clock - compareMode.timing.pixel_clock))
				/ 100
			+ (int32)(500 * fabs(aspectRatio - compareAspectRatio))
			+ 100 * abs((int)(mode.space - compareMode.space));

		if (bestIndex == -1 || diff < bestDiff) {
			bestDiff = diff;
			bestIndex = i;
		}
	}

	if (bestIndex < 0)
		return B_ERROR;

	modeFound = fModeList[bestIndex];
	if (_diff != 0)
		*_diff = bestDiff;

	return B_OK;
}


/**
 * @brief Picks any mode the driver accepts, starting from the best match
 *        and walking the list until something works.
 *
 * Used only by the initial mode set, which must not fail. The mode list
 * must already have been populated.
 *
 * @param newMode  In: the desired mode (used for the closest-match search).
 *                 Out: the mode the driver actually accepted.
 * @return         B_OK if a mode was set, B_ERROR if every mode in the
 *                 list was rejected.
 */
status_t
AccelerantHWInterface::_SetFallbackMode(display_mode& newMode) const
{
	// At first, we search the closest display mode from the list of
	// supported modes - if that fails, we just take one

	if (_FindBestMode(newMode, 0, newMode) == B_OK
		&& fAccSetDisplayMode(&newMode) == B_OK) {
		return B_OK;
	}

	// That failed as well, this looks like a bug in the graphics
	// driver, but we have to try to be as forgiving as possible
	// here - just take the first mode that works!

	for (int32 i = 0; i < fModeCount; i++) {
		newMode = fModeList[i];
		if (fAccSetDisplayMode(&newMode) == B_OK)
			return B_OK;
	}

	// Well, we tried.
	return B_ERROR;
}


/**
 * @brief Switches the card to the requested display mode.
 *
 * Asks the driver to set @a mode directly; if it rejects and this is the
 * initial mode switch (or the safemode flag forces it), tries the
 * fall-back path. Refreshes the front-buffer descriptor and the kernel
 * KDL frame buffer, re-resolves the overlay hooks, and reallocates the
 * software back buffer if its dimensions no longer match. CMAP8 / GRAY8
 * modes also reload the system / grayscale palette.
 *
 * @param mode  Desired mode.
 * @return      B_OK on success, B_BAD_VALUE for invalid input,
 *              B_NO_INIT when the front buffer wrapper is missing,
 *              B_NO_MEMORY on back-buffer allocation failure, or the
 *              error returned by the driver.
 * @todo  Roll back partial changes on failure.
 */
status_t
AccelerantHWInterface::SetMode(const display_mode& mode)
{
	AutoWriteLocker _(this);
	// TODO: There are places this function can fail,
	// maybe it needs to roll back changes in case of an
	// error.

	// prevent from doing the unnecessary
	if (fModeCount > 0 && fFrontBuffer.IsSet() && fDisplayMode == mode) {
		// TODO: better comparison of display modes
		return B_OK;
	}

	// some safety checks
	// TODO: more of those!
	if (!_IsValidMode(mode))
		return B_BAD_VALUE;

	if (!fFrontBuffer.IsSet())
		return B_NO_INIT;

	// just try to set the mode - we let the graphics driver
	// approve or deny the request, as it should know best

	display_mode newMode = mode;

	status_t status = B_ERROR;
	if (!use_fail_safe_video_mode() || !fInitialModeSwitch)
		status = fAccSetDisplayMode(&newMode);
	if (status != B_OK) {
		ATRACE(("setting display mode failed\n"));
		if (!fInitialModeSwitch)
			return status;

		if (fModeList == NULL) {
			status = _UpdateModeList();
			if (status != B_OK)
				return status;
		}

		// If this is the initial mode switch, we try a number of fallback
		// modes first, before we have to fail

		status = use_fail_safe_video_mode()
			? B_ERROR : _SetFallbackMode(newMode);
		if (status != B_OK) {
			// The driver doesn't allow us the mode switch - this usually
			// means we have a driver that doesn't allow mode switches at
			// all.
			// All we can do now is to ask the driver which mode we can
			// use - this is always necessary for VESA mode, for example.
			if (fAccGetDisplayMode(&newMode) != B_OK)
				return B_ERROR;

			// TODO: check if the mode returned is valid!
			if (!_IsValidMode(newMode))
				return B_BAD_DATA;

			// TODO: if the mode switch before fails as well, we must forbid
			//	any uses of this class!
			status = B_OK;
		}
	}

	fDisplayMode = newMode;
	fInitialModeSwitch = false;

	// update frontbuffer
	fFrontBuffer->SetDisplayMode(fDisplayMode);
	if (_UpdateFrameBufferConfig() != B_OK) {
		// TODO: if this fails, we're basically toasted - we need to handle this
		//	differently to crashing later on!
		return B_ERROR;
	}

	// Update the frame buffer used by the on-screen KDL
#ifndef HAIKU_TARGET_PLATFORM_LIBBE_TEST
	uint32 depth = (fFrameBufferConfig.bytes_per_row
		/ fFrontBuffer->Width()) << 3;
	if (fDisplayMode.space == B_RGB15)
		depth = 15;

	_kern_frame_buffer_update((addr_t)fFrameBufferConfig.frame_buffer,
		fFrontBuffer->Width(), fFrontBuffer->Height(),
		depth, fFrameBufferConfig.bytes_per_row);
#endif

	_UpdateHooksAfterModeChange();

	// update backbuffer if neccessary
	if (!fBackBuffer.IsSet()
		|| fBackBuffer->Width() != fFrontBuffer->Width()
		|| fBackBuffer->Height() != fFrontBuffer->Height()
		|| (fFrontBuffer->ColorSpace() == B_RGB32 && fBackBuffer.IsSet())) {
		// NOTE: backbuffer is always B_RGBA32, this simplifies the
		// drawing backend implementation tremendously for the time
		// being. The color space conversion is handled in CopyBackToFront()

		fBackBuffer.Unset();

		fBackBuffer.SetTo(new(nothrow) MallocBuffer(
			fFrontBuffer->Width(), fFrontBuffer->Height()));

		status = fBackBuffer.IsSet()
			? fBackBuffer->InitCheck() : B_NO_MEMORY;
		if (status < B_OK) {
			fBackBuffer.Unset();
			return status;
		}
		// clear out backbuffer, alpha is 255 this way
		memset(fBackBuffer->Bits(), 255, fBackBuffer->BitsLength());
	}

	// update color palette configuration if necessary
	if (fDisplayMode.space == B_CMAP8)
		_SetSystemPalette();
	else if (fDisplayMode.space == B_GRAY8)
		_SetGrayscalePalette();

	// notify all listeners about the mode change
	_NotifyFrameBufferChanged();

	return status;
}


/**
 * @brief Copies the currently active display mode into @a mode.
 *
 * @param mode  Destination; may be NULL, in which case the call is a
 *              no-op.
 */
void
AccelerantHWInterface::GetMode(display_mode* mode)
{
	if (mode && LockParallelAccess()) {
		*mode = fDisplayMode;
		UnlockParallelAccess();
	}
}


/**
 * @brief Refreshes the cached mode list from the accelerant.
 *
 * @return     B_OK on success, B_ERROR if the count hook returned <= 0,
 *             B_NO_MEMORY on allocation failure.
 */
status_t
AccelerantHWInterface::_UpdateModeList()
{
	fModeCount = fAccGetModeCount();
	if (fModeCount <= 0)
		return B_ERROR;

	delete[] fModeList;
	fModeList = new(nothrow) display_mode[fModeCount];
	if (!fModeList)
		return B_NO_MEMORY;

	if (fAccGetModeList(fModeList) != B_OK) {
		ATRACE(("unable to get mode list\n"));
		return B_ERROR;
	}

	return B_OK;
}


/**
 * @brief Re-queries the accelerant frame buffer config and forwards it
 *        to the front-buffer wrapper.
 *
 * @return     B_OK on success, B_ERROR if the accelerant call failed.
 */
status_t
AccelerantHWInterface::_UpdateFrameBufferConfig()
{
	if (fAccGetFrameBufferConfig(&fFrameBufferConfig) != B_OK) {
		ATRACE(("unable to get frame buffer config\n"));
		return B_ERROR;
	}

	fFrontBuffer->SetFrameBufferConfig(fFrameBufferConfig);

	return B_OK;
}


/**
 * @brief Reports the accelerant's device-info block.
 *
 * @param info  Output, populated by the accelerant.
 * @return      B_OK on success, B_UNSUPPORTED if the hook is missing,
 *              or whatever the accelerant returns.
 */
status_t
AccelerantHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	get_accelerant_device_info GetAccelerantDeviceInfo
		= (get_accelerant_device_info)fAccelerantHook(
			B_GET_ACCELERANT_DEVICE_INFO, NULL);
	if (!GetAccelerantDeviceInfo) {
		ATRACE(("No B_GET_ACCELERANT_DEVICE_INFO hook found\n"));
		return B_UNSUPPORTED;
	}

	return GetAccelerantDeviceInfo(info);
}


/**
 * @brief Returns a copy of the cached mode list.
 *
 * Refreshes the cache from the accelerant if it has not been built yet.
 *
 * @param _modes  Output, newly-allocated array; caller frees with delete[].
 * @param _count  Output, number of modes copied into @a _modes.
 * @return        B_OK on success, B_BAD_VALUE for NULL outputs,
 *                B_NO_MEMORY on allocation failure, otherwise the error
 *                returned by the accelerant.
 */
status_t
AccelerantHWInterface::GetModeList(display_mode** _modes, uint32* _count)
{
	AutoReadLocker _(this);

	if (_count == NULL || _modes == NULL)
		return B_BAD_VALUE;

	status_t status = B_OK;

	if (fModeList == NULL)
		status = _UpdateModeList();

	if (status >= B_OK) {
		*_modes = new(nothrow) display_mode[fModeCount];
		if (*_modes) {
			*_count = fModeCount;
			memcpy(*_modes, fModeList, sizeof(display_mode) * fModeCount);
		} else {
			*_count = 0;
			status = B_NO_MEMORY;
		}
	}
	return status;
}


/**
 * @brief Forwards a pixel-clock-limits query to the accelerant.
 *
 * @param mode   Mode whose limits are queried.
 * @param _low   Output low limit.
 * @param _high  Output high limit.
 * @return       B_OK on success, B_BAD_VALUE for NULL inputs, otherwise
 *               whatever the accelerant returns.
 */
status_t
AccelerantHWInterface::GetPixelClockLimits(display_mode *mode, uint32* _low,
	uint32* _high)
{
	if (mode == NULL || _low == NULL || _high == NULL)
		return B_BAD_VALUE;

	AutoReadLocker _(this);
	return fAccGetPixelClockLimits(mode, _low, _high);
}


/**
 * @brief Forwards a timing-constraints query to the accelerant when the
 *        hook is available.
 *
 * @param constraints  Output structure.
 * @return             B_OK on success, B_BAD_VALUE if @a constraints is
 *                     NULL, B_UNSUPPORTED when the hook is missing, or
 *                     the accelerant's error.
 */
status_t
AccelerantHWInterface::GetTimingConstraints(
	display_timing_constraints* constraints)
{
	if (constraints == NULL)
		return B_BAD_VALUE;

	AutoReadLocker _(this);

	if (fAccGetTimingConstraints)
		return fAccGetTimingConstraints(constraints);

	return B_UNSUPPORTED;
}


/**
 * @brief Asks the accelerant whether @a candidate is acceptable within
 *        the given [low, high] range, possibly adjusting it.
 *
 * @param candidate  In/out candidate mode; the accelerant may rewrite it
 *                   to a nearby legal mode.
 * @param _low       Lower bound.
 * @param _high      Upper bound.
 * @return           B_OK if the candidate (possibly adjusted) is legal,
 *                   B_BAD_VALUE for NULL inputs, B_UNSUPPORTED when the
 *                   hook is missing, otherwise the accelerant's error.
 */
status_t
AccelerantHWInterface::ProposeMode(display_mode* candidate,
	const display_mode* _low, const display_mode* _high)
{
	if (candidate == NULL || _low == NULL || _high == NULL)
		return B_BAD_VALUE;

	AutoReadLocker _(this);

	if (fAccProposeDisplayMode == NULL)
		return B_UNSUPPORTED;

	// avoid const issues
	display_mode high, low;
	high = *_high;
	low = *_low;

	return fAccProposeDisplayMode(candidate, &low, &high);
}


/**
 * @brief Asks the accelerant (or, failing that, the EDID block) for the
 *        monitor's preferred mode.
 *
 * Walks the EDID detailed timing descriptors, builds a candidate
 * display_mode from each, and uses _FindBestMode() to project the
 * preferred timing onto an actually supported mode. The mode with the
 * lowest score wins.
 *
 * @param preferredMode  Output, populated with the preferred mode.
 * @return               B_OK on success, B_NOT_SUPPORTED if neither hook
 *                       can answer, otherwise the underlying error.
 */
status_t
AccelerantHWInterface::GetPreferredMode(display_mode* preferredMode)
{
	status_t status = B_NOT_SUPPORTED;

	if (fAccGetPreferredDisplayMode != NULL) {
		status = fAccGetPreferredDisplayMode(preferredMode);
		if (status == B_OK)
			return B_OK;
	}

	if (fAccGetEDIDInfo != NULL) {
		edid1_info info;
		uint32 version;
		status = fAccGetEDIDInfo(&info, sizeof(info), &version);
		if (status < B_OK)
			return status;
		if (version != EDID_VERSION_1)
			return B_NOT_SUPPORTED;

		if (fModeList == NULL) {
			status = _UpdateModeList();
			if (status != B_OK)
				return status;
		}

		status = B_NOT_SUPPORTED;
		display_mode bestMode;
		int32 bestDiff = INT_MAX;

		// find preferred mode from EDID info
		for (uint32 i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; ++i) {
			if (info.detailed_monitor[i].monitor_desc_type
					!= EDID1_IS_DETAILED_TIMING)
				continue;

			// construct basic mode and find it in the mode list
			const edid1_detailed_timing& timing
				= info.detailed_monitor[i].data.detailed_timing;
			if (timing.h_active < 640 || timing.v_active < 350)
				continue;

			float aspectRatio = 0.0f;
			if (timing.h_size > 0 && timing.v_size > 0)
				aspectRatio = 1.0f * timing.h_size / timing.v_size;

			display_mode modeFound;
			display_mode mode;

			mode.timing.pixel_clock = timing.pixel_clock * 10;
			mode.timing.h_display = timing.h_active;
			mode.timing.h_sync_start = timing.h_active + timing.h_sync_off;
			mode.timing.h_sync_end = mode.timing.h_sync_start
				+ timing.h_sync_width;
			mode.timing.h_total = timing.h_active + timing.h_blank;
			mode.timing.v_display = timing.v_active;
			mode.timing.v_sync_start = timing.v_active + timing.v_sync_off;
			mode.timing.v_sync_end = mode.timing.v_sync_start
				+ timing.v_sync_width;
			mode.timing.v_total = timing.v_active + timing.v_blank;

			mode.space = B_RGB32;
			mode.virtual_width = mode.timing.h_display;
			mode.virtual_height = mode.timing.v_display;

			// TODO: eventually ignore detailed modes for the preferred one
			// if there are more than one usable?
			int32 diff;
			if (_FindBestMode(mode, aspectRatio, modeFound, &diff) == B_OK) {
				status = B_OK;
				if (diff < bestDiff) {
					bestMode = modeFound;
					bestDiff = diff;
				}
			}
		}

		if (status == B_OK)
			*preferredMode = bestMode;
	}

	return status;
}


/**
 * @brief Returns metadata about the connected monitor.
 *
 * Prefers the accelerant's monitor-info hook; falls back to parsing the
 * EDID block when the hook is unavailable, populating @a info with the
 * vendor / product / size / frequency-range fields.
 *
 * @param info  Output structure.
 * @return      B_OK on success, B_NOT_SUPPORTED when neither path works,
 *              otherwise the accelerant's error.
 */
status_t
AccelerantHWInterface::GetMonitorInfo(monitor_info* info)
{
	status_t status = B_NOT_SUPPORTED;

	if (fAccGetMonitorInfo != NULL) {
		status = fAccGetMonitorInfo(info);
		if (status == B_OK)
			return B_OK;
	}

	if (fAccGetEDIDInfo == NULL)
		return status;

	edid1_info edid;
	uint32 version;
	status = fAccGetEDIDInfo(&edid, sizeof(edid), &version);
	if (status < B_OK)
		return status;
	if (version != EDID_VERSION_1)
		return B_NOT_SUPPORTED;

	memset(info, 0, sizeof(monitor_info));
	strlcpy(info->vendor, edid.vendor.manufacturer, sizeof(info->vendor));
	if (edid.vendor.serial != 0) {
		snprintf(info->serial_number, sizeof(info->serial_number), "%" B_PRIu32,
			edid.vendor.serial);
	}
	info->product_id = edid.vendor.prod_id;
	info->produced.week = edid.vendor.week;
	info->produced.year = edid.vendor.year;
	info->width = edid.display.h_size;
	info->height = edid.display.v_size;

	for (uint32 i = 0; i < EDID1_NUM_DETAILED_MONITOR_DESC; ++i) {
		edid1_detailed_monitor *monitor = &edid.detailed_monitor[i];

		switch (monitor->monitor_desc_type) {
			case EDID1_SERIAL_NUMBER:
				strlcpy(info->serial_number, monitor->data.serial_number,
					sizeof(info->serial_number));
				break;

			case EDID1_MONITOR_NAME:
				// There can be several of these; in this case we'll just
				// overwrite the previous entries
				// TODO: we could append them as well
				strlcpy(info->name, monitor->data.monitor_name,
					sizeof(info->name));
				break;

			case EDID1_MONITOR_RANGES:
			{
				edid1_monitor_range& range = monitor->data.monitor_range;

				info->min_horizontal_frequency = range.min_h;
				info->max_horizontal_frequency = range.max_h;
				info->min_vertical_frequency = range.min_v;
				info->max_vertical_frequency = range.max_v;
				info->max_pixel_clock = range.max_clock * 10000;
				break;
			}

			case EDID1_IS_DETAILED_TIMING:
			{
				edid1_detailed_timing& timing = monitor->data.detailed_timing;
				info->width = timing.h_size / 10.0;
				info->height = timing.v_size / 10.0;
			}

			default:
				break;
		}
	}

	return B_OK;
}


/**
 * @brief Lazily resolves and caches the accelerant's retrace semaphore.
 *
 * @return     The semaphore id, B_UNSUPPORTED when the hook is missing,
 *             or whatever the accelerant returns.
 */
sem_id
AccelerantHWInterface::RetraceSemaphore()
{
	AutoWriteLocker _(this);

	if (fRetraceSemaphore != -1)
		return fRetraceSemaphore;

	accelerant_retrace_semaphore AccelerantRetraceSemaphore =
		(accelerant_retrace_semaphore)fAccelerantHook(
			B_ACCELERANT_RETRACE_SEMAPHORE, NULL);
	if (!AccelerantRetraceSemaphore)
		fRetraceSemaphore = B_UNSUPPORTED;
	else
		fRetraceSemaphore = AccelerantRetraceSemaphore();

	return fRetraceSemaphore;
}


/**
 * @brief Blocks until the next vertical retrace, or @a timeout expires.
 *
 * @param timeout  Maximum wait in microseconds; defaults to infinite.
 * @return         B_OK on retrace, the semaphore error from
 *                 RetraceSemaphore() if the hook is missing, otherwise
 *                 the result of acquire_sem_etc().
 */
status_t
AccelerantHWInterface::WaitForRetrace(bigtime_t timeout)
{
	sem_id sem = RetraceSemaphore();
	if (sem < 0)
		return sem;

	return acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, timeout);
}


/**
 * @brief Sets the DPMS power state via the accelerant.
 *
 * @param state  Target DPMS state.
 * @return       Accelerant result, or B_UNSUPPORTED when the hook is
 *               missing.
 */
status_t
AccelerantHWInterface::SetDPMSMode(uint32 state)
{
	AutoWriteLocker _(this);

	if (!fAccSetDPMSMode)
		return B_UNSUPPORTED;

	return fAccSetDPMSMode(state);
}


/**
 * @brief Returns the current DPMS state from the accelerant.
 *
 * @return     DPMS state bitmask, or B_UNSUPPORTED when the hook is
 *             missing.
 */
uint32
AccelerantHWInterface::DPMSMode()
{
	AutoReadLocker _(this);

	if (!fAccDPMSMode)
		return B_UNSUPPORTED;

	return fAccDPMSMode();
}


/**
 * @brief Returns the DPMS capability bitmask from the accelerant.
 *
 * @return     Bitmask of supported DPMS modes, or B_UNSUPPORTED when the
 *             hook is missing.
 */
uint32
AccelerantHWInterface::DPMSCapabilities()
{
	AutoReadLocker _(this);

	if (!fAccDPMSCapabilities)
		return B_UNSUPPORTED;

	return fAccDPMSCapabilities();
}


/**
 * @brief Forwards a brightness change to the accelerant.
 *
 * @param brightness  Target brightness in [0.0, 1.0].
 * @return            Accelerant result, or B_UNSUPPORTED when the hook
 *                    is missing.
 */
status_t
AccelerantHWInterface::SetBrightness(float brightness)
{
	AutoReadLocker _(this);

	if (!fAccSetBrightness)
		return B_UNSUPPORTED;

	return fAccSetBrightness(brightness);
}


/**
 * @brief Reads the current brightness from the accelerant.
 *
 * @param brightness  Output, populated with the current brightness in
 *                    [0.0, 1.0].
 * @return            Accelerant result, or B_UNSUPPORTED when the hook
 *                    is missing.
 */
status_t
AccelerantHWInterface::GetBrightness(float* brightness)
{
	AutoReadLocker _(this);

	if (!fAccGetBrightness)
		return B_UNSUPPORTED;

	return fAccGetBrightness(brightness);
}


/**
 * @brief Returns the on-disk path of the loaded accelerant add-on.
 *
 * @param string  Output path.
 * @return        Result of get_image_info() for the accelerant image.
 */
status_t
AccelerantHWInterface::GetAccelerantPath(BString& string)
{
	image_info info;
	status_t status = get_image_info(fAccelerantImage, &info);
	if (status == B_OK)
		string = info.name;
	return status;
}


/**
 * @brief Returns the on-disk path of the underlying graphics driver.
 *
 * Uses the accelerant's clone-info hook, which by convention returns the
 * driver path used to clone it.
 *
 * @param string  Output path.
 * @return        B_OK on success, B_NOT_SUPPORTED when the hook is
 *                missing.
 */
status_t
AccelerantHWInterface::GetDriverPath(BString& string)
{
	// TODO: this currently assumes that the accelerant's clone info
	//	is always the path name of its driver (that's the case for
	//	all of our drivers)
	char path[B_PATH_NAME_LENGTH];
	get_accelerant_clone_info getCloneInfo;
	getCloneInfo = (get_accelerant_clone_info)fAccelerantHook(
		B_GET_ACCELERANT_CLONE_INFO, NULL);

	if (getCloneInfo == NULL)
		return B_NOT_SUPPORTED;

	getCloneInfo((void*)path);
	string.SetTo(path);
	return B_OK;
}


// #pragma mark - overlays


/**
 * @brief Allocates an overlay channel from the accelerant.
 *
 * @return     Opaque overlay token, or NULL if either the allocate or
 *             release hook is missing.
 * @note  The return is valid for the lifetime of the call to
 *        ConfigureOverlay() and must be paired with
 *        ReleaseOverlayChannel().
 */
overlay_token
AccelerantHWInterface::AcquireOverlayChannel()
{
	if (fAccAllocateOverlay == NULL
		|| fAccReleaseOverlay == NULL)
		return NULL;

	// The current display mode only matters at the time we're planning on
	// showing the overlay channel on screen - that's why we can't use
	// the B_OVERLAY_COUNT hook.
	// TODO: remove fAccOverlayCount if we're not going to need it at all.

	return fAccAllocateOverlay();
}


/**
 * @brief Releases an overlay channel previously returned by
 *        AcquireOverlayChannel().
 *
 * @param token  Token to release; NULL is silently ignored.
 */
void
AccelerantHWInterface::ReleaseOverlayChannel(overlay_token token)
{
	if (token == NULL)
		return;

	fAccReleaseOverlay(token);
}


/**
 * @brief Translates the accelerant's overlay constraints into the public
 *        overlay_restrictions layout.
 *
 * @param overlay       Overlay whose restrictions are queried.
 * @param restrictions  Output, populated on success.
 * @return              B_OK on success, B_BAD_VALUE for NULL inputs,
 *                      B_NOT_SUPPORTED when the constraints hook is
 *                      missing, otherwise the accelerant's error.
 */
status_t
AccelerantHWInterface::GetOverlayRestrictions(const Overlay* overlay,
	overlay_restrictions* restrictions)
{
	if (overlay == NULL || restrictions == NULL)
		return B_BAD_VALUE;
	if (fAccGetOverlayConstraints == NULL)
		return B_NOT_SUPPORTED;

	overlay_constraints constraints;
	status_t status = fAccGetOverlayConstraints(&fDisplayMode,
		overlay->OverlayBuffer(), &constraints);
	if (status < B_OK)
		return status;

	memset(restrictions, 0, sizeof(overlay_restrictions));
	memcpy(&restrictions->source, &constraints.view, sizeof(overlay_limits));
	memcpy(&restrictions->destination, &constraints.window,
		sizeof(overlay_limits));
	restrictions->min_width_scale = constraints.h_scale.min;
	restrictions->max_width_scale = constraints.h_scale.max;
	restrictions->min_height_scale = constraints.v_scale.min;
	restrictions->max_height_scale = constraints.v_scale.max;

	return B_OK;
}


/**
 * @brief Quickly checks whether an overlay with the given dimensions and
 *        colour space is plausible.
 *
 * Only validates static information (size limits and the supported-spaces
 * list); the actual buffer allocation may still fail.
 *
 * @param width       Overlay width in pixels.
 * @param height      Overlay height in pixels.
 * @param colorSpace  Desired colour space.
 * @return            true when @a colorSpace appears in the supported
 *                    list and the dimensions are within the 16-bit limit.
 */
bool
AccelerantHWInterface::CheckOverlayRestrictions(int32 width, int32 height,
	color_space colorSpace)
{
	if (fAccOverlaySupportedSpaces == NULL
		|| fAccGetOverlayConstraints == NULL
		|| fAccAllocateOverlayBuffer == NULL
		|| fAccReleaseOverlayBuffer == NULL)
		return false;

	// Note: we can't really check the size of the overlay upfront - we
	// must assume fAccAllocateOverlayBuffer() will fail in that case.
	if (width < 0 || width > 65535 || height < 0 || height > 65535)
		return false;

	// check color space

	const uint32* spaces = fAccOverlaySupportedSpaces(&fDisplayMode);
	if (spaces == NULL)
		return false;

	for (int32 i = 0; spaces[i] != 0; i++) {
		if (spaces[i] == (uint32)colorSpace)
			return true;
	}

	return false;
}


/**
 * @brief Allocates an overlay buffer through the accelerant.
 *
 * @param width   Buffer width in pixels.
 * @param height  Buffer height in pixels.
 * @param space   Colour space.
 * @return        Pointer to the accelerant-managed buffer, or NULL when
 *                the hook is missing or the allocation fails.
 */
const overlay_buffer*
AccelerantHWInterface::AllocateOverlayBuffer(int32 width, int32 height,
	color_space space)
{
	if (fAccAllocateOverlayBuffer == NULL)
		return NULL;

	return fAccAllocateOverlayBuffer(space, width, height);
}


/**
 * @brief Releases an overlay buffer previously returned by
 *        AllocateOverlayBuffer().
 *
 * @param buffer  Buffer to release; NULL is silently ignored.
 */
void
AccelerantHWInterface::FreeOverlayBuffer(const overlay_buffer* buffer)
{
	if (buffer == NULL || fAccReleaseOverlayBuffer == NULL)
		return;

	fAccReleaseOverlayBuffer(buffer);
}


/**
 * @brief Pushes overlay configuration (window/view position, colour space)
 *        to the accelerant so the overlay starts (or keeps) being shown.
 *
 * @param overlay  Overlay descriptor.
 * @todo  Detect and skip redundant SetColorSpace() outside mode changes.
 */
void
AccelerantHWInterface::ConfigureOverlay(Overlay* overlay)
{
	// TODO: this only needs to be done on mode changes!
	overlay->SetColorSpace(fDisplayMode.space);

	fAccConfigureOverlay(overlay->OverlayToken(), overlay->OverlayBuffer(),
		overlay->OverlayWindow(), overlay->OverlayView());
}


/**
 * @brief Tells the accelerant to stop displaying @a overlay by passing
 *        NULL window / view rectangles.
 *
 * @param overlay  Overlay descriptor.
 */
void
AccelerantHWInterface::HideOverlay(Overlay* overlay)
{
	fAccConfigureOverlay(overlay->OverlayToken(), overlay->OverlayBuffer(),
		NULL, NULL);
}


// #pragma mark - cursor


/**
 * @brief Installs @a cursor either as a hardware cursor or, when no
 *        accelerant cursor hooks are available, as a software cursor.
 *
 * Tries the bitmap-cursor hook first, then the legacy 16x16 monochrome
 * shape hook (translating BCursor's mask layout into the bitmap the
 * accelerant expects), and finally falls back to the software cursor in
 * the base class.
 *
 * @param cursor  Cursor to install; ownership is taken by the base class.
 */
void
AccelerantHWInterface::SetCursor(ServerCursor* cursor)
{
	// cursor should never be NULL, but let us be safe!!
	if (cursor == NULL || LockExclusiveAccess() == false)
		return;

	bool cursorSet = false;

	if (fAccSetCursorBitmap != NULL) {
		// Bitmap cursor
		// TODO are x and y switched for this, too?
		uint16 xHotSpot = (uint16)cursor->GetHotSpot().x;
		uint16 yHotSpot = (uint16)cursor->GetHotSpot().y;

		uint16 width = (uint16)cursor->Width();
		uint16 height = (uint16)cursor->Height();

		// Time to talk to the accelerant!
		cursorSet = fAccSetCursorBitmap(width, height, xHotSpot,
			yHotSpot, cursor->ColorSpace(), (uint16)cursor->BytesPerRow(),
			cursor->Bits()) == B_OK;
	} else if (cursor->CursorData() != NULL && fAccSetCursorShape != NULL) {
		// BeOS BCursor, 16x16 monochrome
		uint8 size = cursor->CursorData()[0];
		// CursorData()[1] is color depth (always monochrome)
		// x and y are switched
		uint8 xHotSpot = cursor->CursorData()[3];
		uint8 yHotSpot = cursor->CursorData()[2];

		// Create pointers to the cursor and/xor bit arrays
		// for the BeOS BCursor there are two 32 byte, 16x16 bit arrays
		// in the first:  1 is black,  0 is white
		// in the second: 1 is opaque, 0 is transparent
		// 1st	2nd
		//  0	 0	 transparent
		//  0	 1	 white
		//  1	 0	 transparent
		//  1	 1	 black
		// for the HW cursor the first is ANDed and the second is XORed
		// AND	XOR
		//  0	 0	 white
		//  0	 1	 black
		//  1	 0	 transparent
		//  1	 1	 reverse
		// so, the first 32 bytes are the XOR mask
		const uint8* xorMask = cursor->CursorData() + 4;
		// the second 32 bytes *NOTed* are the AND mask
		// TODO maybe this should be NOTed when copied to the ServerCursor
		uint8 andMask[32];
		const uint8* transMask = cursor->CursorData() + 36;
		for (int32 i = 0; i < 32; i++)
			andMask[i] = ~transMask[i];

		// Time to talk to the accelerant!
		cursorSet = fAccSetCursorShape(size, size, xHotSpot,
			yHotSpot, andMask, xorMask) == B_OK;
	}

	if (cursorSet && !fHardwareCursorEnabled) {
		// we switched from SW to HW, so we need to erase the SW cursor
		if (fCursorVisible && fFloatingOverlaysLock.Lock()) {
			IntRect r = _CursorFrame();
			fCursorVisible = false;
				// so the Invalidate doesn't draw it again
			_RestoreCursorArea();
			Invalidate(r);
			fCursorVisible = true;
			fFloatingOverlaysLock.Unlock();
		}
		// and we need to update our position
		if (fAccMoveCursor != NULL)
			fAccMoveCursor((uint16)fCursorLocation.x,
				(uint16)fCursorLocation.y);
	}

	if (fAccShowCursor != NULL)
		fAccShowCursor(cursorSet);

	UnlockExclusiveAccess();

	fHardwareCursorEnabled = cursorSet;

	HWInterface::SetCursor(cursor);
		// HWInterface claims ownership of cursor.
}


/**
 * @brief Toggles cursor visibility, preferring the hardware path when
 *        the accelerant supports it.
 *
 * @param visible  true to show the cursor, false to hide it.
 */
void
AccelerantHWInterface::SetCursorVisible(bool visible)
{
	HWInterface::SetCursorVisible(visible);

	if (fHardwareCursorEnabled && LockExclusiveAccess()) {
		if (fAccShowCursor != NULL)
				fAccShowCursor(visible);
		else
			fHardwareCursorEnabled = false;

		UnlockExclusiveAccess();
	}
}


/**
 * @brief Moves the cursor, preferring the hardware path when available.
 *
 * Falls back to the software cursor (and disables further hardware use)
 * if the accelerant does not provide a move hook.
 *
 * @param x  Target X in screen pixels.
 * @param y  Target Y in screen pixels.
 */
void
AccelerantHWInterface::MoveCursorTo(float x, float y)
{
	HWInterface::MoveCursorTo(x, y);

	if (fHardwareCursorEnabled && LockExclusiveAccess()) {
		if (fAccMoveCursor != NULL)
				fAccMoveCursor((uint16)x, (uint16)y);
		else {
			fHardwareCursorEnabled = false;
			if (fAccShowCursor != NULL)
				fAccShowCursor(false);
		}

		UnlockExclusiveAccess();
	}
}


// #pragma mark - buffer access


/**
 * @brief Returns the RenderingBuffer for the visible frame buffer.
 *
 * @return     Pointer to the front buffer wrapper.
 */
RenderingBuffer*
AccelerantHWInterface::FrontBuffer() const
{
	return fFrontBuffer.Get();
}


/**
 * @brief Returns the back buffer (typically a malloced shadow).
 *
 * @return     Pointer to the back buffer, or NULL if running unbuffered.
 */
RenderingBuffer*
AccelerantHWInterface::BackBuffer() const
{
	return fBackBuffer.Get();
}


/**
 * @brief Reports whether the interface currently has a back buffer.
 *
 * @return     true when a back buffer exists.
 */
bool
AccelerantHWInterface::IsDoubleBuffered() const
{
	return fBackBuffer.IsSet();
}


/**
 * @brief Copies the dirty region from the back buffer to the front
 *        buffer using the base implementation.
 *
 * @param region  Region to copy in screen coordinates.
 */
void
AccelerantHWInterface::_CopyBackToFront(/*const*/ BRegion& region)
{
	return HWInterface::_CopyBackToFront(region);
}


// #pragma mark -


/**
 * @brief Software-cursor draw hook; only runs when the hardware cursor
 *        is not in use.
 *
 * @param area  Area being repainted.
 */
void
AccelerantHWInterface::_DrawCursor(IntRect area) const
{
	if (!fHardwareCursorEnabled)
		HWInterface::_DrawCursor(area);
}


/**
 * @brief Materialises a BRegion into the accelerant's fill_rect_params
 *        array, growing the cache as needed.
 *
 * @param region  Source region.
 * @param count   In/out: capacity hint on entry, number of valid entries
 *                on exit (may be clamped down on allocation failure).
 * @todo  The cache mutation below is not currently locked.
 */
void
AccelerantHWInterface::_RegionToRectParams(/*const*/ BRegion* region,
	uint32* count) const
{
	*count = region->CountRects();
	// TODO: locking!!
	if (fRectParamsCount < *count) {
		fRectParamsCount = (*count / kDefaultParamsCount + 1)
			* kDefaultParamsCount;
		// NOTE: realloc() could be used instead...
		fill_rect_params* params
			= new (nothrow) fill_rect_params[fRectParamsCount];
		if (params) {
			delete[] fRectParams;
			fRectParams = params;
		} else {
			*count = fRectParamsCount;
		}
	}

	for (uint32 i = 0; i < *count; i++) {
		clipping_rect r = region->RectAtInt(i);
		fRectParams[i].left = (uint16)r.left;
		fRectParams[i].top = (uint16)r.top;
		fRectParams[i].right = (uint16)r.right;
		fRectParams[i].bottom = (uint16)r.bottom;
	}
}


/**
 * @brief Packs an rgb_color into the native pixel format of the active
 *        display mode.
 *
 * @param color  Source colour.
 * @return       Packed pixel value, or 0 when the colour space is not
 *               recognised.
 * @note  Assumes all targets share the same endianness; not strictly
 *        correct on big-endian hardware.
 */
uint32
AccelerantHWInterface::_NativeColor(const rgb_color& color) const
{
	// NOTE: This functions looks somehow suspicios to me.
	// It assumes that all graphics cards have the same native endianess, no?
	switch (fDisplayMode.space) {
		case B_CMAP8:
		case B_GRAY8:
			return RGBColor(color).GetColor8();

		case B_RGB15_BIG:
		case B_RGBA15_BIG:
		case B_RGB15_LITTLE:
		case B_RGBA15_LITTLE:
			return RGBColor(color).GetColor15();

		case B_RGB16_BIG:
		case B_RGB16_LITTLE:
			return RGBColor(color).GetColor16();

		case B_RGB32_BIG:
		case B_RGBA32_BIG:
		case B_RGB32_LITTLE:
		case B_RGBA32_LITTLE: {
			return (uint32)((color.alpha << 24) | (color.red << 16)
				| (color.green << 8) | color.blue);
		}
	}
	return 0;
}


/**
 * @brief Pushes the system 8-bit colour palette to the accelerant.
 *
 * Used after switching to B_CMAP8.
 */
void
AccelerantHWInterface::_SetSystemPalette()
{
	set_indexed_colors setIndexedColors = (set_indexed_colors)fAccelerantHook(
		B_SET_INDEXED_COLORS, NULL);
	if (setIndexedColors == NULL)
		return;

	const rgb_color* palette = SystemPalette();
	uint8 colors[3 * 256];
		// the color table is an array with 3 bytes per color
	uint32 j = 0;

	for (int32 i = 0; i < 256; i++) {
		colors[j++] = palette[i].red;
		colors[j++] = palette[i].green;
		colors[j++] = palette[i].blue;
	}

	setIndexedColors(256, 0, colors, 0);
}


/**
 * @brief Pushes a grayscale 8-bit palette to the accelerant.
 *
 * In planar VGA 16-colour mode (detected when Width() exceeds
 * BytesPerRow()) the palette repeats 16 grey levels through the index
 * range; otherwise it programs a straight 0..255 ramp.
 */
void
AccelerantHWInterface::_SetGrayscalePalette()
{
	set_indexed_colors setIndexedColors = (set_indexed_colors)fAccelerantHook(
		B_SET_INDEXED_COLORS, NULL);
	if (setIndexedColors == NULL)
		return;

	uint8 colors[3 * 256];
		// the color table is an array with 3 bytes per color
	uint32 j = 0;

	if (fFrontBuffer->Width() > fFrontBuffer->BytesPerRow()) {
		// VGA 16 color grayscale planar mode
		for (int32 i = 0; i < 256; i++) {
			colors[j++] = (i & 0xf) * 17;
			colors[j++] = (i & 0xf) * 17;
			colors[j++] = (i & 0xf) * 17;
		}

		setIndexedColors(256, 0, colors, 0);
	} else {
		for (int32 i = 0; i < 256; i++) {
			colors[j++] = i;
			colors[j++] = i;
			colors[j++] = i;
		}

		setIndexedColors(256, 0, colors, 0);
	}
}
