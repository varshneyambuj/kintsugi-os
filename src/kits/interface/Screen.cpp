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
 *   Copyright 2003-2009 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (burton666@libero.it)
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file Screen.cpp
 * @brief Implementation of BScreen, the public screen information and control interface
 *
 * BScreen is a proxy class that provides access to display properties including
 * screen frame, color space, color map, and refresh rate. It delegates to
 * BPrivateScreen for actual app_server communication.
 *
 * @see BPrivateScreen, BWindow
 */


#include <Screen.h>

#include <Window.h>

#include <PrivateScreen.h>


using namespace BPrivate;


/**
 * @brief Constructs a BScreen object for the given screen_id.
 *
 * Acquires a reference to the corresponding BPrivateScreen via the static
 * factory.  Use IsValid() after construction to verify the ID was recognised.
 *
 * @param id The screen_id to represent; pass B_MAIN_SCREEN_ID for the primary display.
 *
 * @see IsValid(), BScreen(BWindow*)
 */
BScreen::BScreen(screen_id id)
{
	fScreen = BPrivateScreen::Get(id.id);
}


/**
 * @brief Constructs a BScreen object for the screen currently hosting @a window.
 *
 * Queries the app_server for the screen on which @a window resides.  If
 * @a window is NULL, or the query fails, the main screen is used.
 *
 * @param window The window whose screen should be represented, or NULL for
 *               the main screen.
 *
 * @see IsValid(), BScreen(screen_id)
 */
BScreen::BScreen(BWindow* window)
{
	fScreen = BPrivateScreen::Get(window);
}


/**
 * @brief Destroys the BScreen object and releases the reference to BPrivateScreen.
 *
 * Calls BPrivateScreen::Put() to decrement the reference count.  The underlying
 * BPrivateScreen is deleted only when all references are released (except for
 * the main screen, which is kept alive permanently).
 */
BScreen::~BScreen()
{
	BPrivateScreen::Put(fScreen);
}


/**
 * @brief Returns whether this BScreen object refers to a valid, active display.
 *
 * A BScreen becomes invalid if constructed with an unknown screen_id, or if
 * the monitor was disconnected after construction.
 *
 * @return True if fScreen is non-NULL and reports itself as valid.
 *
 * @see BPrivateScreen::IsValid()
 */
bool
BScreen::IsValid()
{
	return fScreen != NULL && fScreen->IsValid();
}


/**
 * @brief Advances this BScreen to represent the next screen in enumeration order.
 *
 * Swaps the internal BPrivateScreen reference for the successor screen returned
 * by BPrivateScreen::GetNext().  After the call, all subsequent method calls
 * reflect the new screen.
 *
 * @return B_OK if a next screen was found and the object was updated;
 *         B_ERROR if there is no next screen or this BScreen is invalid.
 *
 * @see BPrivateScreen::GetNext()
 */
status_t
BScreen::SetToNext()
{
	if (fScreen != NULL) {
		BPrivateScreen* screen = BPrivateScreen::GetNext(fScreen);
		if (screen != NULL) {
			fScreen = screen;
			return B_OK;
		}
	}
	return B_ERROR;
}


/**
 * @brief Returns the colour space of the screen's active display mode.
 *
 * @return The color_space value (e.g. B_RGB32), or B_NO_COLOR_SPACE if the
 *         BScreen is invalid.
 *
 * @see BPrivateScreen::ColorSpace()
 */
color_space
BScreen::ColorSpace()
{
	if (fScreen != NULL)
		return fScreen->ColorSpace();

	return B_NO_COLOR_SPACE;
}


/**
 * @brief Returns the bounding rectangle of the screen in screen coordinates.
 *
 * The result is refreshed from the server at most once every 10 ms.
 *
 * @return A BRect describing the screen's pixel area, or (0,0,0,0) if invalid.
 *
 * @see BPrivateScreen::Frame()
 */
BRect
BScreen::Frame()
{
	if (fScreen != NULL)
		return fScreen->Frame();

	return BRect(0, 0, 0, 0);
}


/**
 * @brief Returns the screen_id that identifies this screen to the system.
 *
 * @return The screen_id, or B_MAIN_SCREEN_ID if the BScreen is invalid.
 */
screen_id
BScreen::ID()
{
	if (fScreen != NULL) {
		screen_id id = { fScreen->ID() };
		return id;
	}

	return B_MAIN_SCREEN_ID;
}


/**
 * @brief Blocks indefinitely until the next vertical retrace.
 *
 * Convenience wrapper that calls WaitForRetrace(B_INFINITE_TIMEOUT).
 *
 * @return B_OK after a retrace, or an error code if retrace sync is unsupported.
 *
 * @see WaitForRetrace(bigtime_t), BPrivateScreen::WaitForRetrace()
 */
status_t
BScreen::WaitForRetrace()
{
	return WaitForRetrace(B_INFINITE_TIMEOUT);
}


/**
 * @brief Blocks until the next vertical retrace or until @a timeout elapses.
 *
 * @param timeout Maximum wait time in microseconds.
 *
 * @return B_OK on retrace, B_TIMED_OUT if the timeout elapsed, or an error
 *         code if retrace synchronisation is not supported or the screen is
 *         invalid.
 *
 * @see WaitForRetrace(), BPrivateScreen::WaitForRetrace()
 */
status_t
BScreen::WaitForRetrace(bigtime_t timeout)
{
	if (fScreen != NULL)
		return fScreen->WaitForRetrace(timeout);

	return B_ERROR;
}


/**
 * @brief Returns the palette index that best matches the given RGBA colour.
 *
 * Delegates to BPrivateScreen::IndexForColor().  Meaningful only when the
 * screen is operating in an indexed (8-bit) colour space.
 *
 * @param red   Red component (0–255).
 * @param green Green component (0–255).
 * @param blue  Blue component (0–255).
 * @param alpha Alpha component (0–255).
 *
 * @return The closest palette index, or 0 if the screen is invalid.
 *
 * @see ColorForIndex(), BPrivateScreen::IndexForColor()
 */
uint8
BScreen::IndexForColor(uint8 red, uint8 green, uint8 blue, uint8 alpha)
{
	if (fScreen != NULL)
		return fScreen->IndexForColor(red, green, blue, alpha);

	return 0;
}


/**
 * @brief Returns the rgb_color stored at @a index in the current colour palette.
 *
 * @param index An 8-bit palette index.
 *
 * @return The corresponding rgb_color, or a default-initialised rgb_color{} if
 *         the screen is invalid.
 *
 * @see IndexForColor(), BPrivateScreen::ColorForIndex()
 */
rgb_color
BScreen::ColorForIndex(const uint8 index)
{
	if (fScreen != NULL)
		return fScreen->ColorForIndex(index);

	return rgb_color();
}


/**
 * @brief Returns the palette index whose colour is the visual inverse of @a index.
 *
 * @param index The source palette index to invert.
 *
 * @return The inverted palette index, or 0 if the screen is invalid.
 *
 * @see BPrivateScreen::InvertIndex()
 */
uint8
BScreen::InvertIndex(uint8 index)
{
	if (fScreen != NULL)
		return fScreen->InvertIndex(index);

	return 0;
}


/**
 * @brief Returns a pointer to the system colour map in shared read-only memory.
 *
 * The pointer is valid for the lifetime of the application.  Returns NULL if
 * the screen is invalid or the server has not yet initialised.
 *
 * @return Const pointer to the color_map, or NULL on failure.
 *
 * @see BPrivateScreen::ColorMap()
 */
const color_map*
BScreen::ColorMap()
{
	if (fScreen != NULL)
		return fScreen->ColorMap();

	return NULL;
}


/**
 * @brief Captures the screen (or a sub-region) into a newly allocated BBitmap.
 *
 * The caller takes ownership of the BBitmap returned via @a _bitmap.
 *
 * @param _bitmap    On success, receives a pointer to the newly allocated BBitmap.
 * @param drawCursor If true, the cursor is composited into the capture.
 * @param bounds     The region to capture in screen coordinates, or NULL for the full screen.
 *
 * @return B_OK on success, or an error code if the screen is invalid or allocation fails.
 *
 * @see ReadBitmap(), BPrivateScreen::GetBitmap()
 */
status_t
BScreen::GetBitmap(BBitmap** _bitmap, bool drawCursor, BRect* bounds)
{
	if (fScreen != NULL)
		return fScreen->GetBitmap(_bitmap, drawCursor, bounds);

	return B_ERROR;
}


/**
 * @brief Reads screen pixels into a caller-supplied BBitmap.
 *
 * Unlike GetBitmap(), this method does not allocate a bitmap; @a bitmap
 * must already be initialised with the desired dimensions and colour space.
 *
 * @param bitmap     The destination BBitmap (must be non-NULL).
 * @param drawCursor If true, the cursor is composited into the result.
 * @param bounds     The capture region in screen coordinates, or NULL for the full screen.
 *
 * @return B_OK on success, or an error code if the screen is invalid or the server fails.
 *
 * @see GetBitmap(), BPrivateScreen::ReadBitmap()
 */
status_t
BScreen::ReadBitmap(BBitmap* bitmap, bool drawCursor, BRect* bounds)
{
	if (fScreen != NULL)
		return fScreen->ReadBitmap(bitmap, drawCursor, bounds);

	return B_ERROR;
}


/**
 * @brief Returns the desktop background colour for the current workspace.
 *
 * @return The desktop rgb_color, or a default-initialised rgb_color{} if invalid.
 *
 * @see SetDesktopColor(), DesktopColor(uint32)
 */
rgb_color
BScreen::DesktopColor()
{
	if (fScreen != NULL)
		return fScreen->DesktopColor(B_CURRENT_WORKSPACE_INDEX);

	return rgb_color();
}


/**
 * @brief Returns the desktop background colour for @a workspace.
 *
 * @param workspace The workspace index.
 *
 * @return The desktop rgb_color, or a default-initialised rgb_color{} if invalid.
 *
 * @see SetDesktopColor(rgb_color, uint32, bool)
 */
rgb_color
BScreen::DesktopColor(uint32 workspace)
{
	if (fScreen != NULL)
		return fScreen->DesktopColor(workspace);

	return rgb_color();
}


/**
 * @brief Sets the desktop background colour for the current workspace.
 *
 * @param color The new background colour.
 * @param stick If true, the colour is persisted as the workspace default.
 *
 * @see DesktopColor(), SetDesktopColor(rgb_color, uint32, bool)
 */
void
BScreen::SetDesktopColor(rgb_color color, bool stick)
{
	if (fScreen != NULL)
		fScreen->SetDesktopColor(color, B_CURRENT_WORKSPACE_INDEX, stick);
}


/**
 * @brief Sets the desktop background colour for @a workspace.
 *
 * @param color     The new background colour.
 * @param workspace The workspace index.
 * @param stick     If true, the colour is persisted as the workspace default.
 *
 * @see DesktopColor(uint32)
 */
void
BScreen::SetDesktopColor(rgb_color color, uint32 workspace, bool stick)
{
	if (fScreen != NULL)
		fScreen->SetDesktopColor(color, workspace, stick);
}


/**
 * @brief Asks the app_server to propose a display mode near @a target within limits.
 *
 * On success @a target is overwritten with the best available mode within the
 * range [@a low, @a high].  Returns B_BAD_VALUE (not B_ERROR) when the
 * proposed mode lies outside the requested limits.
 *
 * @param target On entry, the desired mode; on success, the closest available mode.
 * @param low    Lower bound constraints.
 * @param high   Upper bound constraints.
 *
 * @return B_OK if the proposed mode is within limits, B_BAD_VALUE if the best
 *         available mode is outside limits, or B_ERROR on failure.
 *
 * @see GetModeList(), SetMode(), BPrivateScreen::ProposeMode()
 */
status_t
BScreen::ProposeMode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	if (fScreen != NULL)
		return fScreen->ProposeMode(target, low, high);

	return B_ERROR;
}


/**
 * @brief Retrieves the list of all display modes supported by this screen.
 *
 * Allocates a malloc()-owned array of display_mode structs.  The caller must
 * call free() on @a *_modeList when done.
 *
 * @param _modeList On success, receives a pointer to the mode array.
 * @param _count    On success, receives the number of entries in @a _modeList.
 *
 * @return B_OK on success, or an error code on failure.
 *
 * @see ProposeMode(), SetMode(), BPrivateScreen::GetModeList()
 */
status_t
BScreen::GetModeList(display_mode** _modeList, uint32* _count)
{
	if (fScreen != NULL)
		return fScreen->GetModeList(_modeList, _count);

	return B_ERROR;
}


/**
 * @brief Retrieves the display mode currently active on the current workspace.
 *
 * @param mode Output parameter that receives the active display_mode.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 *
 * @see GetMode(uint32, display_mode*), SetMode(), BPrivateScreen::GetMode()
 */
status_t
BScreen::GetMode(display_mode* mode)
{
	if (fScreen != NULL)
		return fScreen->GetMode(B_CURRENT_WORKSPACE_INDEX, mode);

	return B_ERROR;
}


/**
 * @brief Retrieves the display mode active on @a workspace.
 *
 * @param workspace The workspace index.
 * @param mode      Output parameter that receives the active display_mode.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 *
 * @see GetMode(display_mode*), SetMode(uint32, display_mode*, bool)
 */
status_t
BScreen::GetMode(uint32 workspace, display_mode* mode)
{
	if (fScreen != NULL)
		return fScreen->GetMode(workspace, mode);

	return B_ERROR;
}


/**
 * @brief Sets the display mode for the current workspace.
 *
 * @param mode        The desired display_mode.
 * @param makeDefault If true, persist @a mode as the workspace default.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 *
 * @see SetMode(uint32, display_mode*, bool), GetMode(), ProposeMode()
 */
status_t
BScreen::SetMode(display_mode* mode, bool makeDefault)
{
	if (fScreen != NULL)
		return fScreen->SetMode(B_CURRENT_WORKSPACE_INDEX, mode, makeDefault);

	return B_ERROR;
}


/**
 * @brief Sets the display mode for @a workspace.
 *
 * @param workspace   The workspace index.
 * @param mode        The desired display_mode.
 * @param makeDefault If true, persist @a mode as the workspace default.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 *
 * @see SetMode(display_mode*, bool), GetModeList()
 */
status_t
BScreen::SetMode(uint32 workspace, display_mode* mode, bool makeDefault)
{
	if (fScreen != NULL)
		return fScreen->SetMode(workspace, mode, makeDefault);

	return B_ERROR;
}


/**
 * @brief Fills @a info with hardware accelerant information for this screen.
 *
 * @param info Output parameter; must be non-NULL.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 */
status_t
BScreen::GetDeviceInfo(accelerant_device_info* info)
{
	if (fScreen != NULL)
		return fScreen->GetDeviceInfo(info);

	return B_ERROR;
}


/**
 * @brief Fills @a info with physical monitor information for this screen.
 *
 * @param info Output parameter; must be non-NULL.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 */
status_t
BScreen::GetMonitorInfo(monitor_info* info)
{
	if (fScreen != NULL)
		return fScreen->GetMonitorInfo(info);

	return B_ERROR;
}


/**
 * @brief Retrieves the minimum and maximum pixel clock rates for @a mode.
 *
 * @param mode  The display mode for which clock limits are requested.
 * @param _low  On success, receives the minimum pixel clock in kHz.
 * @param _high On success, receives the maximum pixel clock in kHz.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 */
status_t
BScreen::GetPixelClockLimits(display_mode* mode, uint32* _low, uint32* _high)
{
	if (fScreen != NULL)
		return fScreen->GetPixelClockLimits(mode, _low, _high);

	return B_ERROR;
}


/**
 * @brief Retrieves the display timing constraints for the current hardware.
 *
 * @param constraints Output parameter; must be non-NULL.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 */
status_t
BScreen::GetTimingConstraints(display_timing_constraints* constraints)
{
	if (fScreen != NULL)
		return fScreen->GetTimingConstraints(constraints);

	return B_ERROR;
}


/**
 * @brief Sets the DPMS (power management) state of the monitor.
 *
 * @param dpmsState One of the B_DPMS_* constants defined in GraphicsDefs.h.
 *
 * @return B_OK on success, or an error code if the screen is invalid.
 */
status_t
BScreen::SetDPMS(uint32 dpmsState)
{
	if (fScreen != NULL)
		return fScreen->SetDPMS(dpmsState);

	return B_ERROR;
}


/**
 * @brief Returns the current DPMS power state of the monitor.
 *
 * @return One of the B_DPMS_* constants, or 0 if the screen is invalid.
 */
uint32
BScreen::DPMSState()
{
	if (fScreen != NULL)
		return fScreen->DPMSState();

	return 0;
}


/**
 * @brief Returns a bitmask of the DPMS states supported by the monitor.
 *
 * @return A combination of B_DPMS_* flags, or 0 if the screen is invalid.
 */
uint32
BScreen::DPMSCapabilites()
{
	if (fScreen != NULL)
		return fScreen->DPMSCapabilites();

	return 0;
}


/**
 * @brief Retrieves the current display brightness as a value in [0.0, 1.0].
 *
 * @param brightness On success, receives the brightness level (0.0 = off, 1.0 = full).
 *
 * @return B_OK on success, or B_ERROR if the screen is invalid.
 *
 * @see SetBrightness(), BPrivateScreen::GetBrightness()
 */
status_t
BScreen::GetBrightness(float* brightness)
{
	if (fScreen != NULL)
		return fScreen->GetBrightness(brightness);
	return B_ERROR;
}


/**
 * @brief Sets the display brightness.
 *
 * @param brightness Desired brightness in [0.0, 1.0].
 *
 * @return B_OK on success, or B_ERROR if the screen is invalid.
 *
 * @see GetBrightness(), BPrivateScreen::SetBrightness()
 */
status_t
BScreen::SetBrightness(float brightness)
{
	if (fScreen != NULL)
		return fScreen->SetBrightness(brightness);
	return B_ERROR;
}


//	#pragma mark - Deprecated methods


/**
 * @brief Returns the underlying BPrivateScreen pointer.
 *
 * @return The internal BPrivateScreen, or NULL if this BScreen is invalid.
 *
 * @note This method is deprecated and exists only for binary compatibility.
 */
BPrivate::BPrivateScreen*
BScreen::private_screen()
{
	return fScreen;
}


/**
 * @brief Deprecated alias for ProposeMode().
 *
 * @param target On entry, the desired mode; on success, the closest available mode.
 * @param low    Lower bound constraints.
 * @param high   Upper bound constraints.
 *
 * @return Forwarded from ProposeMode().
 *
 * @see ProposeMode()
 */
status_t
BScreen::ProposeDisplayMode(display_mode* target, const display_mode* low,
	const display_mode* high)
{
	return ProposeMode(target, low, high);
}


/**
 * @brief Deprecated; always returns NULL.
 *
 * Direct framebuffer access is no longer supported.
 *
 * @return NULL.
 */
void*
BScreen::BaseAddress()
{
	// deprecated
	return NULL;
}


/**
 * @brief Deprecated; always returns 0.
 *
 * Direct framebuffer access is no longer supported.
 *
 * @return 0.
 */
uint32
BScreen::BytesPerRow()
{
	// deprecated
	return 0;
}
