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
 *   Copyright 2002-2009, Haiku. All Rights Reserved.
 *   Copyright 2002-2005,
 *       Marcus Overhagen,
 *       Stefano Ceccherini (stefano.ceccherini@gmail.com),
 *       Carwyn Jones (turok2@currantbun.com)
 *       All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marcus Overhagen
 *       Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *       Carwyn Jones <turok2@currantbun.com>
 */

/** @file WindowScreen.cpp
 *  @brief Implements BWindowScreen, a BWindow subclass that provides
 *         fullscreen, direct hardware-accelerated framebuffer access.
 *
 *  BWindowScreen occupies the entire display in a dedicated workspace,
 *  maps the framebuffer directly into the application's address space via
 *  a cloned accelerant, and exposes pre-R5-compatible acceleration hooks
 *  (CardHookAt()) for fill-rectangle, blit, and sync operations.
 *
 *  Static file-scope function pointers (sFillRectHook, sBlitRectHook, etc.)
 *  and sEngineToken hold the current accelerant hooks.  The thin wrapper
 *  functions card_sync(), blit(), scaled_filtered_blit(), draw_rect_8/16/32()
 *  translate the old pre-R5 calling convention into the modern acquire-engine /
 *  hook / release-engine pattern.
 */


#include <WindowScreen.h>

#include <new>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <Screen.h>
#include <String.h>

#include <AppServerLink.h>
#include <input_globals.h>
#include <InputServerTypes.h>
#include <InterfacePrivate.h>
#include <ServerProtocol.h>
#include <WindowPrivate.h>


using BPrivate::AppServerLink;


//#define TRACE_WINDOWSCREEN 1
#if TRACE_WINDOWSCREEN
#	define CALLED() printf("%s\n", __PRETTY_FUNCTION__);
#else
#	define CALLED() ;
#endif


// Acceleration hooks pointers
/** @brief Hook retrieved via B_GET_FRAME_BUFFER_CONFIG; fills a
 *         frame_buffer_config struct with the current framebuffer layout. */
static get_frame_buffer_config sGetFrameBufferConfigHook;

/** @brief Hook for hardware-accelerated filled rectangle drawing. */
static fill_rectangle sFillRectHook;
/** @brief Hook for screen-to-screen blit operations. */
static screen_to_screen_blit sBlitRectHook;
/** @brief Hook for screen-to-screen transparent blit operations. */
static screen_to_screen_transparent_blit sTransparentBlitHook;
/** @brief Hook for screen-to-screen scaled and filtered blit operations. */
static screen_to_screen_scaled_filtered_blit sScaledFilteredBlitHook;
/** @brief Hook that blocks until the 2-D engine is idle. */
static wait_engine_idle sWaitIdleHook;
/** @brief Hook that acquires exclusive use of the 2-D acceleration engine. */
static acquire_engine sAcquireEngineHook;
/** @brief Hook that releases the 2-D acceleration engine after use. */
static release_engine sReleaseEngineHook;

/** @brief Engine token returned by sAcquireEngineHook and passed to all
 *         per-operation hooks. */
static engine_token *sEngineToken;


// Helper methods which translates the pre r5 graphics methods to r5 ones

/** @brief Waits for the 2-D acceleration engine to become idle.
 *
 *  Provides the pre-R5 card_sync() calling convention by delegating to
 *  sWaitIdleHook.
 *
 *  @return Always returns 0.
 */
static int32
card_sync()
{
	sWaitIdleHook();
	return 0;
}


/** @brief Copies a rectangular region on-screen from one location to another.
 *
 *  Provides the pre-R5 blit() calling convention.  Acquires the engine,
 *  invokes sBlitRectHook with a single blit_params record, then releases
 *  the engine.
 *
 *  @param sx     Source left coordinate (pixels).
 *  @param sy     Source top coordinate (pixels).
 *  @param dx     Destination left coordinate (pixels).
 *  @param dy     Destination top coordinate (pixels).
 *  @param width  Width of the region to copy (pixels).
 *  @param height Height of the region to copy (pixels).
 *  @return Always returns 0.
 */
static int32
blit(int32 sx, int32 sy, int32 dx, int32 dy, int32 width, int32 height)
{
	blit_params param;
	param.src_left = sx;
	param.src_top = sy;
	param.dest_left = dx;
	param.dest_top = dy;
	param.width = width;
	param.height = height;

	sAcquireEngineHook(B_2D_ACCELERATION, 0xff, NULL, &sEngineToken);
	sBlitRectHook(sEngineToken, &param, 1);
	sReleaseEngineHook(sEngineToken, NULL);
	return 0;
}


// TODO: This function seems not to be exported through CardHookAt().
// At least, nothing I've tried uses it.
#if 0
static int32
transparent_blit(int32 sx, int32 sy, int32 dx, int32 dy, int32 width,
	int32 height, uint32 transparent_color)
{
	blit_params param;
	param.src_left = sx;
	param.src_top = sy;
	param.dest_left = dx;
	param.dest_top = dy;
	param.width = width;
	param.height = height;

	sAcquireEngineHook(B_2D_ACCELERATION, 0xff, 0, &sEngineToken);
	sTransparentBlitHook(sEngineToken, transparent_color, &param, 1);
	sReleaseEngineHook(sEngineToken, 0);
	return 0;
}
#endif


/** @brief Performs a scaled, filtered blit from one screen region to another.
 *
 *  Provides the pre-R5 scaled_filtered_blit() calling convention.  Acquires
 *  the engine, invokes sScaledFilteredBlitHook with a single scaled_blit_params
 *  record, then releases the engine.
 *
 *  @param sx  Source left coordinate (pixels).
 *  @param sy  Source top coordinate (pixels).
 *  @param sw  Source width (pixels).
 *  @param sh  Source height (pixels).
 *  @param dx  Destination left coordinate (pixels).
 *  @param dy  Destination top coordinate (pixels).
 *  @param dw  Destination width (pixels).
 *  @param dh  Destination height (pixels).
 *  @return Always returns 0.
 */
static int32
scaled_filtered_blit(int32 sx, int32 sy, int32 sw, int32 sh, int32 dx, int32 dy,
	int32 dw, int32 dh)
{
	scaled_blit_params param;
	param.src_left = sx;
	param.src_top = sy;
	param.src_width = sw;
	param.src_height = sh;
	param.dest_left = dx;
	param.dest_top = dy;
	param.dest_width = dw;
	param.dest_height = dh;

	sAcquireEngineHook(B_2D_ACCELERATION, 0xff, NULL, &sEngineToken);
	sScaledFilteredBlitHook(sEngineToken, &param, 1);
	sReleaseEngineHook(sEngineToken, NULL);
	return 0;
}


/** @brief Fills a rectangle with an 8-bit colour index using hardware
 *         acceleration.
 *
 *  Provides the pre-R5 draw_rect_8() calling convention.  The rectangle
 *  is specified as (left, top, right, bottom) via the sx/sy/sw/sh parameters.
 *
 *  @param sx          Left edge of the rectangle (pixels).
 *  @param sy          Top edge of the rectangle (pixels).
 *  @param sw          Right edge of the rectangle (pixels).
 *  @param sh          Bottom edge of the rectangle (pixels).
 *  @param color_index 8-bit palette index to fill with.
 *  @return Always returns 0.
 */
static int32
draw_rect_8(int32 sx, int32 sy, int32 sw, int32 sh, uint8 color_index)
{
	fill_rect_params param;
	param.left = sx;
	param.top = sy;
	param.right = sw;
	param.bottom = sh;

	sAcquireEngineHook(B_2D_ACCELERATION, 0xff, NULL, &sEngineToken);
	sFillRectHook(sEngineToken, color_index, &param, 1);
	sReleaseEngineHook(sEngineToken, NULL);
	return 0;
}


/** @brief Fills a rectangle with a 16-bit colour value using hardware
 *         acceleration.
 *
 *  Provides the pre-R5 draw_rect_16() calling convention.  The rectangle
 *  is specified as (left, top, right, bottom) via the sx/sy/sw/sh parameters.
 *
 *  @param sx     Left edge of the rectangle (pixels).
 *  @param sy     Top edge of the rectangle (pixels).
 *  @param sw     Right edge of the rectangle (pixels).
 *  @param sh     Bottom edge of the rectangle (pixels).
 *  @param color  16-bit packed colour value to fill with.
 *  @return Always returns 0.
 */
static int32
draw_rect_16(int32 sx, int32 sy, int32 sw, int32 sh, uint16 color)
{
	fill_rect_params param;
	param.left = sx;
	param.top = sy;
	param.right = sw;
	param.bottom = sh;

	sAcquireEngineHook(B_2D_ACCELERATION, 0xff, NULL, &sEngineToken);
	sFillRectHook(sEngineToken, color, &param, 1);
	sReleaseEngineHook(sEngineToken, NULL);
	return 0;
}


/** @brief Fills a rectangle with a 32-bit colour value using hardware
 *         acceleration.
 *
 *  Provides the pre-R5 draw_rect_32() calling convention.  The rectangle
 *  is specified as (left, top, right, bottom) via the sx/sy/sw/sh parameters.
 *
 *  @param sx     Left edge of the rectangle (pixels).
 *  @param sy     Top edge of the rectangle (pixels).
 *  @param sw     Right edge of the rectangle (pixels).
 *  @param sh     Bottom edge of the rectangle (pixels).
 *  @param color  32-bit packed colour value to fill with.
 *  @return Always returns 0.
 */
static int32
draw_rect_32(int32 sx, int32 sy, int32 sw, int32 sh, uint32 color)
{
	fill_rect_params param;
	param.left = sx;
	param.top = sy;
	param.right = sw;
	param.bottom = sh;

	sAcquireEngineHook(B_2D_ACCELERATION, 0xff, NULL, &sEngineToken);
	sFillRectHook(sEngineToken, color, &param, 1);
	sReleaseEngineHook(sEngineToken, NULL);
	return 0;
}


//	#pragma mark - public API calls


/** @brief Moves the mouse cursor to the given screen coordinates.
 *
 *  Sends an IS_SET_MOUSE_POSITION command to the input server via
 *  _control_input_server_().  Useful for centring the cursor when
 *  entering fullscreen mode.
 *
 *  @param x  Desired horizontal cursor position in screen pixels.
 *  @param y  Desired vertical cursor position in screen pixels.
 */
void
set_mouse_position(int32 x, int32 y)
{
	BMessage command(IS_SET_MOUSE_POSITION);
	BMessage reply;

	command.AddPoint("where", BPoint(x, y));
	_control_input_server_(&command, &reply);
}


//	#pragma mark -


/** @brief Constructs a BWindowScreen with a legacy debug-enable flag.
 *
 *  Creates a borderless, fullscreen window on the current workspace and
 *  calls _InitData() to negotiate the display mode, clone the accelerant,
 *  and set up acceleration hooks.  Passes B_ENABLE_DEBUGGER in the
 *  attributes when @p debugEnable is true.
 *
 *  @param title        Window title (shown in the workspace tab).
 *  @param space        Desired colour space / resolution token (e.g.
 *                      B_32_BIT_800x600).
 *  @param error        If non-NULL, receives the initialisation status code.
 *  @param debugEnable  If true, enables the debug suspend/resume mechanism.
 */
BWindowScreen::BWindowScreen(const char *title, uint32 space, status_t *error,
		bool debugEnable)
	:
	BWindow(BScreen().Frame(), title, B_NO_BORDER_WINDOW_LOOK,
		kWindowScreenFeel, kWindowScreenFlag | B_NOT_MINIMIZABLE
			| B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MOVABLE | B_NOT_RESIZABLE,
		B_CURRENT_WORKSPACE)
{
	CALLED();
	uint32 attributes = 0;
	if (debugEnable)
		attributes |= B_ENABLE_DEBUGGER;

	status_t status = _InitData(space, attributes);
	if (error)
		*error = status;
}


/** @brief Constructs a BWindowScreen with an explicit attributes bitmask.
 *
 *  Creates a borderless, fullscreen window on the current workspace and
 *  calls _InitData() to negotiate the display mode, clone the accelerant,
 *  and set up acceleration hooks.
 *
 *  @param title       Window title (shown in the workspace tab).
 *  @param space       Desired colour space / resolution token.
 *  @param attributes  Attribute flags (e.g. B_ENABLE_DEBUGGER).
 *  @param error       If non-NULL, receives the initialisation status code.
 */
BWindowScreen::BWindowScreen(const char *title, uint32 space,
		uint32 attributes, status_t *error)
	:
	BWindow(BScreen().Frame(), title, B_NO_BORDER_WINDOW_LOOK,
		kWindowScreenFeel, kWindowScreenFlag | B_NOT_MINIMIZABLE
			| B_NOT_CLOSABLE | B_NOT_ZOOMABLE | B_NOT_MOVABLE | B_NOT_RESIZABLE,
		B_CURRENT_WORKSPACE)
{
	CALLED();
	status_t status = _InitData(space, attributes);
	if (error)
		*error = status;
}


/** @brief Destroys the BWindowScreen and releases all resources.
 *
 *  Calls _DisposeData() which disconnects from the screen, unloads the
 *  accelerant add-on, deletes the debug semaphore, and restores the
 *  original display mode.
 */
BWindowScreen::~BWindowScreen()
{
	CALLED();
	_DisposeData();
}


/** @brief Disconnects from the screen and quits the window.
 *
 *  Calls Disconnect() to deactivate direct screen access and restore
 *  the cursor before delegating to BWindow::Quit().
 */
void
BWindowScreen::Quit(void)
{
	CALLED();
	Disconnect();
	BWindow::Quit();
}


/** @brief Called when the screen connection becomes active or inactive.
 *
 *  Subclasses override this method to initialise or tear down per-frame
 *  rendering state.  The default implementation is a no-op.
 *
 *  @param active  true when the window has acquired direct screen access,
 *                 false when it is releasing it.
 */
void
BWindowScreen::ScreenConnected(bool active)
{
	// Implemented in subclasses
}


/** @brief Voluntarily disconnects from the direct screen connection.
 *
 *  If the screen is currently active (fLockState == 1), calls _Deactivate()
 *  to stop direct access.  Resets the debug-first flag and shows the
 *  system cursor via be_app->ShowCursor().
 */
void
BWindowScreen::Disconnect()
{
	CALLED();
	if (fLockState == 1) {
		if (fDebugState)
			fDebugFirst = true;
		_Deactivate();
	}

	be_app->ShowCursor();
}


/** @brief Called when the window gains or loses keyboard focus.
 *
 *  Activates direct screen access when the window becomes active and all
 *  preconditions are met (fLockState == 0 and fWorkState is true).
 *
 *  @param active  true if the window is now the active window.
 */
void
BWindowScreen::WindowActivated(bool active)
{
	CALLED();
	fWindowState = active;
	if (active && fLockState == 0 && fWorkState)
		_Activate();
}


/** @brief Called when the containing workspace is shown or hidden.
 *
 *  Activates the screen when the workspace becomes active (and the window
 *  is focused and unlocked), or deactivates it when the workspace is
 *  switched away from.
 *
 *  @param workspace  Index of the workspace that changed state.
 *  @param state      true if the workspace became active.
 */
void
BWindowScreen::WorkspaceActivated(int32 workspace, bool state)
{
	CALLED();
	fWorkState = state;

	if (state) {
		if (fLockState == 0 && fWindowState) {
			_Activate();
			if (!IsHidden()) {
				Activate(true);
				WindowActivated(true);
			}
		}
	} else if (fLockState)
		_Deactivate();
}


/** @brief Called when the screen resolution or colour depth changes.
 *
 *  Subclasses may override to update rendering parameters.  The default
 *  implementation is a no-op.
 *
 *  @param screenFrame  New frame of the screen in screen coordinates.
 *  @param depth        New colour space of the screen.
 */
void
BWindowScreen::ScreenChanged(BRect screenFrame, color_space depth)
{
	// Implemented in subclasses
}


/** @brief Hides the window and disconnects from the screen.
 *
 *  Calls Disconnect() before delegating to BWindow::Hide() so that
 *  direct screen access is properly released before the window disappears.
 */
void
BWindowScreen::Hide()
{
	CALLED();

	Disconnect();
	BWindow::Hide();
}


/** @brief Makes the window visible.
 *
 *  Delegates to BWindow::Show().
 */
void
BWindowScreen::Show()
{
	CALLED();

	BWindow::Show();
}


/** @brief Sets a range of entries in the 8-bit colour palette.
 *
 *  Updates fPalette[] for the given index range.  If the screen is
 *  currently active, the hardware colour table is also updated through
 *  the B_SET_INDEXED_COLORS accelerant hook and a retrace is awaited.
 *
 *  @param list        Array of rgb_color values to write beginning at
 *                     fPalette[firstIndex].
 *  @param firstIndex  First palette entry to update (0–255).
 *  @param lastIndex   Last palette entry to update (0–255, inclusive).
 */
void
BWindowScreen::SetColorList(rgb_color *list, int32 firstIndex, int32 lastIndex)
{
	CALLED();
	if (firstIndex < 0 || lastIndex > 255 || firstIndex > lastIndex)
		return;

	if (!Lock())
		return;

	if (!fActivateState) {
		// If we aren't active, we just change our local palette
		for (int32 x = firstIndex; x <= lastIndex; x++) {
			fPalette[x] = list[x - firstIndex];
		}
	} else {
		uint8 colors[3 * 256];
			// the color table has 3 bytes per color
		int32 j = 0;

		for (int32 x = firstIndex; x <= lastIndex; x++) {
			fPalette[x] = list[x - firstIndex];
				// update our local palette as well

			colors[j++] = fPalette[x].red;
			colors[j++] = fPalette[x].green;
			colors[j++] = fPalette[x].blue;
		}

		if (fAddonImage >= 0) {
			set_indexed_colors setIndexedColors
				= (set_indexed_colors)fGetAccelerantHook(B_SET_INDEXED_COLORS,
					NULL);
			if (setIndexedColors != NULL) {
				setIndexedColors(255, 0,
					colors, 0);
			}
		}

		// TODO: Tell the app_server about our changes

		BScreen screen(this);
		screen.WaitForRetrace();
	}

	Unlock();
}


/** @brief Changes the screen resolution and colour depth.
 *
 *  Looks up the display_mode that matches @p space in the cached mode list,
 *  then calls _AssertDisplayMode() to switch the hardware if needed.
 *
 *  @param space  Resolution/colour-depth token (e.g. B_32_BIT_1024x768).
 *  @return B_OK on success, or an error code if the mode is unsupported or
 *          the switch fails.
 */
status_t
BWindowScreen::SetSpace(uint32 space)
{
	CALLED();

	display_mode mode;
	status_t status = _GetModeFromSpace(space, &mode);
	if (status == B_OK)
		status = _AssertDisplayMode(&mode);

	return status;
}


/** @brief Reports whether the hardware supports virtual framebuffer control.
 *
 *  Checks the B_FRAME_BUFFER_CONTROL flag in fCardInfo.flags, which is set
 *  when the current display mode advertises B_SCROLL support.
 *
 *  @return true if SetFrameBuffer() and MoveDisplayArea() are supported.
 */
bool
BWindowScreen::CanControlFrameBuffer()
{
	return (fCardInfo.flags & B_FRAME_BUFFER_CONTROL) != 0;
}


/** @brief Attempts to configure the virtual framebuffer dimensions.
 *
 *  Proposes a display_mode with the requested virtual width and height
 *  (B_SCROLL flag set) via BScreen::ProposeMode(), then applies it with
 *  _AssertDisplayMode().
 *
 *  @param width   Desired virtual framebuffer width in pixels.
 *  @param height  Desired virtual framebuffer height in pixels.
 *  @return B_OK on success, or an error if the mode cannot be set.
 */
status_t
BWindowScreen::SetFrameBuffer(int32 width, int32 height)
{
	CALLED();
	display_mode highMode = *fDisplayMode;
	highMode.flags |= B_SCROLL;

	highMode.virtual_height = (int16)height;
	highMode.virtual_width = (int16)width;

	display_mode lowMode = highMode;
	display_mode mode = highMode;

	BScreen screen(this);
	status_t status = screen.ProposeMode(&mode, &lowMode, &highMode);
	if (status == B_OK)
		status = _AssertDisplayMode(&mode);

	return status;
}


/** @brief Scrolls the visible display area within the virtual framebuffer.
 *
 *  Calls the B_MOVE_DISPLAY accelerant hook and, on success, updates
 *  fFrameBufferInfo.display_x/y and fDisplayMode->h/v_display_start.
 *
 *  @param x  New horizontal start of the visible area (pixels from left).
 *  @param y  New vertical start of the visible area (pixels from top).
 *  @return B_OK if the hardware accepted the new position, B_ERROR otherwise.
 */
status_t
BWindowScreen::MoveDisplayArea(int32 x, int32 y)
{
	CALLED();
	move_display_area moveDisplayArea
		= (move_display_area)fGetAccelerantHook(B_MOVE_DISPLAY, NULL);
	if (moveDisplayArea && moveDisplayArea((int16)x, (int16)y) == B_OK) {
		fFrameBufferInfo.display_x = x;
		fFrameBufferInfo.display_y = y;
		fDisplayMode->h_display_start = x;
		fDisplayMode->v_display_start = y;
		return B_OK;
	}
	return B_ERROR;
}


#if 0
void *
BWindowScreen::IOBase()
{
	// Not supported
	return NULL;
}
#endif


/** @brief Returns the current 256-entry colour palette.
 *
 *  The returned pointer addresses fPalette[], which is kept in sync with
 *  the hardware palette by SetColorList().
 *
 *  @return Pointer to the internal array of 256 rgb_color entries.
 */
rgb_color *
BWindowScreen::ColorList()
{
	CALLED();
	return fPalette;
}


/** @brief Returns a pointer to the current frame-buffer layout descriptor.
 *
 *  The descriptor is updated each time _AssertDisplayMode() is called and
 *  reflects the live hardware configuration.
 *
 *  @return Pointer to the internal frame_buffer_info structure.
 */
frame_buffer_info *
BWindowScreen::FrameBufferInfo()
{
	CALLED();
	return &fFrameBufferInfo;
}


/** @brief Returns the pre-R5 acceleration hook for the given index.
 *
 *  Maps numeric hook indices (as used by the original BeOS Game Kit) to
 *  the thin wrapper functions that call into the modern accelerant API:
 *   - Index  5: 8-bit fill rectangle  (draw_rect_8)
 *   - Index  6: 32-bit fill rectangle (draw_rect_32)
 *   - Index  7: screen-to-screen blit (blit)
 *   - Index  8: scaled filtered blit  (scaled_filtered_blit)
 *   - Index 10: engine idle / sync    (card_sync)
 *   - Index 13: 16-bit fill rectangle (draw_rect_16)
 *
 *  Returns NULL if the accelerant is not loaded or if the underlying hook
 *  pointer is NULL.
 *
 *  @param index  Pre-R5 hook index.
 *  @return Function pointer cast to graphics_card_hook, or NULL.
 */
graphics_card_hook
BWindowScreen::CardHookAt(int32 index)
{
	CALLED();
	if (fAddonImage < 0)
		return NULL;

	graphics_card_hook hook = NULL;

	switch (index) {
		case 5: // 8 bit fill rect
			if (sFillRectHook)
				hook = (graphics_card_hook)draw_rect_8;
			break;
		case 6: // 32 bit fill rect
			if (sFillRectHook)
				hook = (graphics_card_hook)draw_rect_32;
			break;
		case 7: // screen to screen blit
			if (sBlitRectHook)
				hook = (graphics_card_hook)blit;
			break;
		case 8: // screen to screen scaled filtered blit
			if (sScaledFilteredBlitHook)
				hook = (graphics_card_hook)scaled_filtered_blit;
			break;
		case 10: // sync aka wait for graphics card idle
			if (sWaitIdleHook)
				hook = (graphics_card_hook)card_sync;
			break;
		case 13: // 16 bit fill rect
			if (sFillRectHook)
				hook = (graphics_card_hook)draw_rect_16;
			break;
		default:
			break;
	}

	return hook;
}


/** @brief Returns a pointer to the current graphics card descriptor.
 *
 *  The descriptor is refreshed by _GetCardInfo() each time the display
 *  mode changes.
 *
 *  @return Pointer to the internal graphics_card_info structure.
 */
graphics_card_info *
BWindowScreen::CardInfo()
{
	CALLED();
	return &fCardInfo;
}


/** @brief Registers a thread to be suspended during debug breakpoints.
 *
 *  Appends @p thread to the fDebugThreads array (protected by fDebugSem).
 *  All registered threads are suspended by _Suspend() when the debugger
 *  is triggered and resumed by _Resume() afterwards.
 *
 *  @param thread  Thread ID of the rendering thread to register.
 */
void
BWindowScreen::RegisterThread(thread_id thread)
{
	CALLED();

	status_t status;
	do {
		status = acquire_sem(fDebugSem);
	} while (status == B_INTERRUPTED);

	if (status < B_OK)
		return;

	void *newDebugList = realloc(fDebugThreads,
		(fDebugThreadCount + 1) * sizeof(thread_id));
	if (newDebugList != NULL) {
		fDebugThreads = (thread_id *)newDebugList;
		fDebugThreads[fDebugThreadCount] = thread;
		fDebugThreadCount++;
	}
	release_sem(fDebugSem);
}


/** @brief Called when the debug-suspend state changes.
 *
 *  Subclasses may override this to save and restore rendering state around
 *  debugger invocations.  The default implementation is a no-op.
 *
 *  @param active  true when rendering is about to be suspended,
 *                 false when it is about to resume.
 */
void
BWindowScreen::SuspensionHook(bool active)
{
	// Implemented in subclasses
}


/** @brief Suspends execution and switches to the debug workspace.
 *
 *  Prints a prompt to stderr, unlocks the window if necessary, activates
 *  the debug workspace, and calls suspend_thread() on the calling thread.
 *  Execution resumes (via _Resume()) when the user switches back to the
 *  window's workspace.  Only active when fDebugState is true.
 *
 *  @param label  Human-readable label printed in the debugger prompt.
 */
void
BWindowScreen::Suspend(char* label)
{
	CALLED();
	if (fDebugState) {
		fprintf(stderr, "## Debugger(\"%s\").", label);
		fprintf(stderr, " Press Alt-F%" B_PRId32 " or Cmd-F%" B_PRId32 " to resume.\n",
			fWorkspaceIndex + 1, fWorkspaceIndex + 1);

		if (IsLocked())
			Unlock();

		activate_workspace(fDebugWorkspace);

		// Suspend ourself
		suspend_thread(find_thread(NULL));

		Lock();
	}
}


/** @brief Executes a private perform_code action.
 *
 *  Delegates to BWindow::Perform().
 *
 *  @param d    Perform code identifying the requested action.
 *  @param arg  Opaque argument whose meaning depends on @p d.
 *  @return Result of the underlying BWindow::Perform() call.
 */
status_t
BWindowScreen::Perform(perform_code d, void* arg)
{
	return inherited::Perform(d, arg);
}


// Reserved for future binary compatibility
/** @brief Reserved virtual for future binary-compatible extensions. */
void BWindowScreen::_ReservedWindowScreen1() {}
/** @brief Reserved virtual for future binary-compatible extensions. */
void BWindowScreen::_ReservedWindowScreen2() {}
/** @brief Reserved virtual for future binary-compatible extensions. */
void BWindowScreen::_ReservedWindowScreen3() {}
/** @brief Reserved virtual for future binary-compatible extensions. */
void BWindowScreen::_ReservedWindowScreen4() {}


/** @brief Initialises all BWindowScreen state and resources.
 *
 *  Performs the following sequence:
 *   -# Snapshots the current display mode as fOriginalDisplayMode and
 *      enumerates available modes into fModeList.
 *   -# Resolves the requested @p space token to a display_mode via
 *      _GetModeFromSpace() and stores it in fDisplayMode.
 *   -# Creates fDebugSem (a counting semaphore protecting fDebugThreads).
 *   -# Copies the screen's colour map into fPalette.
 *
 *  Returns B_OK on success.  Any failure causes _DisposeData() to be
 *  called to release partial resources.
 *
 *  @param space       Resolution/colour-depth token for the desired mode.
 *  @param attributes  Attribute flags (e.g. B_ENABLE_DEBUGGER).
 *  @return B_OK on success, B_NO_MEMORY if allocation fails, or another
 *          error code if a system call fails.
 */
status_t
BWindowScreen::_InitData(uint32 space, uint32 attributes)
{
	CALLED();

	fDebugState = attributes & B_ENABLE_DEBUGGER;
	fDebugThreadCount = 0;
	fDebugThreads = NULL;
	fDebugFirst = true;

	fAttributes = attributes;
		// TODO: not really used right now, but should probably be known by
		// the app_server

	fWorkspaceIndex = fDebugWorkspace = current_workspace();
	fLockState = 0;
	fAddonImage = -1;
	fWindowState = 0;
	fOriginalDisplayMode = NULL;
	fDisplayMode = NULL;
	fModeList = NULL;
	fModeCount = 0;
	fDebugSem = -1;
	fActivateState = false;
	fWorkState = false;

	status_t status = B_ERROR;
	try {
		fOriginalDisplayMode = new display_mode;
		fDisplayMode = new display_mode;

		BScreen screen(this);
		status = screen.GetMode(fOriginalDisplayMode);
		if (status < B_OK)
			throw status;

		status = screen.GetModeList(&fModeList, &fModeCount);
		if (status < B_OK)
			throw status;

		status = _GetModeFromSpace(space, fDisplayMode);
		if (status < B_OK)
			throw status;

		fDebugSem = create_sem(1, "WindowScreen debug sem");
		if (fDebugSem < B_OK)
			throw (status_t)fDebugSem;

		memcpy((void*)fPalette, screen.ColorMap()->color_list, sizeof(fPalette));
		fActivateState = false;
		fWorkState = true;

		status = B_OK;
	} catch (std::bad_alloc&) {
		status = B_NO_MEMORY;
	} catch (status_t error) {
		status = error;
	} catch (...) {
		status = B_ERROR;
	}

	if (status != B_OK)
		_DisposeData();

	return status;
}


/** @brief Releases all resources allocated by _InitData() and _Activate().
 *
 *  Disconnects the screen connection, unloads the accelerant add-on image,
 *  deletes fDebugSem, restores the original display mode (when debugging),
 *  and frees fDisplayMode, fOriginalDisplayMode, and fModeList.
 */
void
BWindowScreen::_DisposeData()
{
	CALLED();
	Disconnect();
	if (fAddonImage >= 0) {
		unload_add_on(fAddonImage);
		fAddonImage = -1;
	}

	delete_sem(fDebugSem);
	fDebugSem = -1;

	if (fDebugState)
		activate_workspace(fDebugWorkspace);

	delete fDisplayMode;
	fDisplayMode = NULL;
	delete fOriginalDisplayMode;
	fOriginalDisplayMode = NULL;
	free(fModeList);
	fModeList = NULL;
	fModeCount = 0;

	fLockState = 0;
}


/** @brief Locks or unlocks the framebuffer for exclusive access.
 *
 *  Sends AS_DIRECT_SCREEN_LOCK with a boolean @p lock argument to the
 *  app_server and updates fActivateState on success.  Returns B_OK
 *  immediately if the requested state already matches fActivateState.
 *
 *  @param lock  true to acquire the lock, false to release it.
 *  @return B_OK on success, or an error code from the app_server.
 */
status_t
BWindowScreen::_LockScreen(bool lock)
{
	if (fActivateState == lock)
		return B_OK;

	// TODO: the BWindowScreen should use the same mechanism as BDirectWindows!
	BPrivate::AppServerLink link;

	link.StartMessage(AS_DIRECT_SCREEN_LOCK);
	link.Attach<bool>(lock);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		fActivateState = lock;

	return status;
}


/** @brief Activates direct screen access for this window.
 *
 *  Calls _SetupAccelerantHooks() to initialise (or reinitialise) all hook
 *  pointers, asserts the desired display mode, acquires the framebuffer
 *  lock via _LockScreen(true), hides the cursor, and notifies the subclass
 *  by calling either SuspensionHook(true)/_Resume() (debug resume path)
 *  or ScreenConnected(true) (normal path).
 *
 *  @return B_OK on success, or an error code if any step fails.
 */
status_t
BWindowScreen::_Activate()
{
	CALLED();

	status_t status = _SetupAccelerantHooks();
	if (status < B_OK)
		return status;

	status = _AssertDisplayMode(fDisplayMode);
	if (status < B_OK)
		return status;

	if (!fActivateState) {
		status = _LockScreen(true);
		if (status != B_OK)
			return status;
	}

	be_app->HideCursor();

	SetColorList(fPalette);
	if (fDebugState && !fDebugFirst) {
		SuspensionHook(true);
		_Resume();
	} else {
		fDebugFirst = true;
		ScreenConnected(true);
	}

	return B_OK;
}


/** @brief Deactivates direct screen access for this window.
 *
 *  Notifies the subclass via SuspensionHook(false)/_Suspend() (debug path)
 *  or ScreenConnected(false) (normal path), releases the framebuffer lock,
 *  restores the original colour palette, reverts the display mode to
 *  fOriginalDisplayMode, resets all accelerant hooks, and shows the cursor.
 *
 *  @return B_OK on success, or an error code if _LockScreen() fails.
 */
status_t
BWindowScreen::_Deactivate()
{
	CALLED();

	if (fDebugState && !fDebugFirst) {
		_Suspend();
		SuspensionHook(false);
	} else
		ScreenConnected(false);

	if (fActivateState) {
		status_t status = _LockScreen(false);
		if (status != B_OK)
			return status;

		BScreen screen(this);
		SetColorList((rgb_color *)screen.ColorMap()->color_list);
	}

	_AssertDisplayMode(fOriginalDisplayMode);
	_ResetAccelerantHooks();

	be_app->ShowCursor();

	return B_OK;
}


/** @brief Resolves all accelerant hook pointers needed for direct access.
 *
 *  If the accelerant add-on has not been loaded yet, calls _InitClone() to
 *  load and clone it.  Otherwise calls _ResetAccelerantHooks() to clear
 *  stale pointers before re-querying.  Populates all static hook globals
 *  (sGetFrameBufferConfigHook, sWaitIdleHook, etc.) and waits for the
 *  engine to become idle before marking fLockState = 1.
 *
 *  @return B_OK on success, or an error code if the accelerant cannot be
 *          loaded or if any required hook is unavailable.
 */
status_t
BWindowScreen::_SetupAccelerantHooks()
{
	CALLED();

	status_t status = B_OK;
	if (fAddonImage < 0)
		status = _InitClone();
	else
		_ResetAccelerantHooks();

	if (status == B_OK) {
		sGetFrameBufferConfigHook = (get_frame_buffer_config)
			fGetAccelerantHook(B_GET_FRAME_BUFFER_CONFIG, NULL);
		sWaitIdleHook = fWaitEngineIdle = (wait_engine_idle)
			fGetAccelerantHook(B_WAIT_ENGINE_IDLE, NULL);
		sReleaseEngineHook
			= (release_engine)fGetAccelerantHook(B_RELEASE_ENGINE, NULL);
		sAcquireEngineHook
			= (acquire_engine)fGetAccelerantHook(B_ACQUIRE_ENGINE, NULL);
		sFillRectHook
			= (fill_rectangle)fGetAccelerantHook(B_FILL_RECTANGLE, NULL);
		sBlitRectHook = (screen_to_screen_blit)
			fGetAccelerantHook(B_SCREEN_TO_SCREEN_BLIT, NULL);
		sTransparentBlitHook = (screen_to_screen_transparent_blit)
			fGetAccelerantHook(B_SCREEN_TO_SCREEN_TRANSPARENT_BLIT, NULL);
		sScaledFilteredBlitHook = (screen_to_screen_scaled_filtered_blit)
			fGetAccelerantHook(B_SCREEN_TO_SCREEN_SCALED_FILTERED_BLIT, NULL);

		if (fWaitEngineIdle)
			fWaitEngineIdle();

		fLockState = 1;
	}

	return status;
}


/** @brief Clears all accelerant hook pointers and marks the lock state idle.
 *
 *  Waits for the engine to idle (via fWaitEngineIdle) before nulling all
 *  static hook globals and sEngineToken, then sets fLockState = 0.
 *  Called by _Deactivate() and at the start of _SetupAccelerantHooks()
 *  when the add-on is already loaded.
 */
void
BWindowScreen::_ResetAccelerantHooks()
{
	CALLED();
	if (fWaitEngineIdle)
		fWaitEngineIdle();

	sGetFrameBufferConfigHook = NULL;
	sFillRectHook = NULL;
	sBlitRectHook = NULL;
	sTransparentBlitHook = NULL;
	sScaledFilteredBlitHook = NULL;
	sWaitIdleHook = NULL;
	sEngineToken = NULL;
	sAcquireEngineHook = NULL;
	sReleaseEngineHook = NULL;

	fWaitEngineIdle = NULL;

	fLockState = 0;
}


/** @brief Queries the screen and accelerant for the current hardware
 *         configuration and populates fCardInfo.
 *
 *  Reads the current display_mode, derives bits_per_pixel and the RGBA
 *  channel order, maps B_SCROLL/B_PARALLEL_ACCESS mode flags to
 *  B_FRAME_BUFFER_CONTROL/B_PARALLEL_BUFFER_ACCESS card flags, and
 *  calls sGetFrameBufferConfigHook to obtain the frame_buffer pointer and
 *  bytes_per_row.
 *
 *  @return B_OK on success, or an error code if BScreen::GetMode() fails.
 */
status_t
BWindowScreen::_GetCardInfo()
{
	CALLED();

	BScreen screen(this);
	display_mode mode;
	status_t status = screen.GetMode(&mode);
	if (status < B_OK)
		return status;

	uint32 bitsPerPixel;
	switch(mode.space & 0x0fff) {
		case B_CMAP8:
			bitsPerPixel = 8;
			break;
		case B_RGB15:
			bitsPerPixel = 15;
			break;
		case B_RGB16:
			bitsPerPixel = 16;
			break;
		case B_RGB32:
			bitsPerPixel = 32;
			break;
		default:
			bitsPerPixel = 0;
			break;
	}

	fCardInfo.version = 2;
	fCardInfo.id = screen.ID().id;
	fCardInfo.bits_per_pixel = bitsPerPixel;
	fCardInfo.width = mode.virtual_width;
	fCardInfo.height = mode.virtual_height;

	if (mode.space & 0x10)
		memcpy(fCardInfo.rgba_order, "rgba", 4);
	else
		memcpy(fCardInfo.rgba_order, "bgra", 4);

	fCardInfo.flags = 0;
	if (mode.flags & B_SCROLL)
		fCardInfo.flags |= B_FRAME_BUFFER_CONTROL;
	if (mode.flags & B_PARALLEL_ACCESS)
		fCardInfo.flags |= B_PARALLEL_BUFFER_ACCESS;

	frame_buffer_config config;
	sGetFrameBufferConfigHook(&config);

	fCardInfo.frame_buffer = config.frame_buffer;
	fCardInfo.bytes_per_row = config.bytes_per_row;

	return B_OK;
}


/** @brief Suspends all registered rendering threads for a debug breakpoint.
 *
 *  Acquires fDebugSem, saves the framebuffer contents into a heap-allocated
 *  fDebugFrameBuffer, then calls suspend_thread() on each thread in
 *  fDebugThreads (with a 10 ms delay between suspensions to avoid races).
 */
void
BWindowScreen::_Suspend()
{
	CALLED();

	status_t status;
	do {
		status = acquire_sem(fDebugSem);
	} while (status == B_INTERRUPTED);

	if (status != B_OK)
		return;

	// Suspend all the registered threads
	for (int32 i = 0; i < fDebugThreadCount; i++) {
		snooze(10000);
		suspend_thread(fDebugThreads[i]);
	}

	graphics_card_info *info = CardInfo();
	size_t fbSize = info->bytes_per_row * info->height;

	// Save the content of the frame buffer into the local buffer
	fDebugFrameBuffer = (char *)malloc(fbSize);
	memcpy(fDebugFrameBuffer, info->frame_buffer, fbSize);
}


/** @brief Resumes all registered rendering threads after a debug breakpoint.
 *
 *  Copies the saved fDebugFrameBuffer back into the hardware framebuffer,
 *  frees the buffer, resumes all threads in fDebugThreads, and releases
 *  fDebugSem.
 */
void
BWindowScreen::_Resume()
{
	CALLED();
	graphics_card_info *info = CardInfo();

	// Copy the content of the debug_buffer back into the frame buffer.
	memcpy(info->frame_buffer, fDebugFrameBuffer,
		info->bytes_per_row * info->height);
	free(fDebugFrameBuffer);
	fDebugFrameBuffer = NULL;

	// Resume all the registered threads
	for (int32 i = 0; i < fDebugThreadCount; i++) {
		resume_thread(fDebugThreads[i]);
	}

	release_sem(fDebugSem);
}


/** @brief Resolves a resolution/colour-depth space token to a display_mode.
 *
 *  Calls BPrivate::get_mode_parameter() to decode @p space into width,
 *  height, and colour space, then performs a linear search through fModeList
 *  for an exact match on all three fields.
 *
 *  @param space   Resolution/colour-depth token to resolve.
 *  @param dmode   Output parameter that receives the matching display_mode.
 *  @return B_OK if a matching mode was found.
 *  @return B_BAD_VALUE if @p space cannot be decoded.
 *  @return B_ERROR if no matching mode exists in fModeList.
 */
status_t
BWindowScreen::_GetModeFromSpace(uint32 space, display_mode *dmode)
{
	CALLED();

	int32 width, height;
	uint32 colorSpace;
	if (!BPrivate::get_mode_parameter(space, width, height, colorSpace))
		return B_BAD_VALUE;

	for (uint32 i = 0; i < fModeCount; i++) {
		if (fModeList[i].space == colorSpace
			&& fModeList[i].virtual_width == width
			&& fModeList[i].virtual_height == height) {
			memcpy(dmode, &fModeList[i], sizeof(display_mode));
			return B_OK;
		}
	}

	return B_ERROR;
}


/** @brief Loads and clones the accelerant add-on for the current screen.
 *
 *  Queries the app_server for the accelerant and driver paths via
 *  AS_GET_ACCELERANT_PATH and AS_GET_DRIVER_PATH, loads the add-on with
 *  load_add_on(), resolves B_ACCELERANT_ENTRY_POINT, and invokes the
 *  B_CLONE_ACCELERANT hook with the driver path so that the window can
 *  share the accelerant context already initialised by the app_server.
 *
 *  Returns immediately with B_OK if fAddonImage is already valid.
 *
 *  @return B_OK on success.
 *  @return B_NOT_SUPPORTED if B_ACCELERANT_ENTRY_POINT or B_CLONE_ACCELERANT
 *          cannot be resolved.
 *  @return Any error returned by load_add_on() or the clone hook.
 */
status_t
BWindowScreen::_InitClone()
{
	CALLED();

	if (fAddonImage >= 0)
		return B_OK;

	BScreen screen(this);

	AppServerLink link;
	link.StartMessage(AS_GET_ACCELERANT_PATH);
	link.Attach<screen_id>(screen.ID());

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) < B_OK || status < B_OK)
		return status;

	BString accelerantPath;
	link.ReadString(accelerantPath);

	link.StartMessage(AS_GET_DRIVER_PATH);
	link.Attach<screen_id>(screen.ID());

	status = B_ERROR;
	if (link.FlushWithReply(status) < B_OK || status < B_OK)
		return status;

	BString driverPath;
	link.ReadString(driverPath);

	fAddonImage = load_add_on(accelerantPath.String());
	if (fAddonImage < B_OK) {
		fprintf(stderr, "InitClone: cannot load accelerant image\n");
		return fAddonImage;
	}

	status = get_image_symbol(fAddonImage, B_ACCELERANT_ENTRY_POINT,
		B_SYMBOL_TYPE_TEXT, (void**)&fGetAccelerantHook);
	if (status < B_OK) {
		fprintf(stderr, "InitClone: cannot get accelerant entry point\n");
		unload_add_on(fAddonImage);
		fAddonImage = -1;
		return B_NOT_SUPPORTED;
	}

	clone_accelerant cloneHook
		= (clone_accelerant)fGetAccelerantHook(B_CLONE_ACCELERANT, NULL);
	if (cloneHook == NULL) {
		fprintf(stderr, "InitClone: cannot get clone hook\n");
		unload_add_on(fAddonImage);
		fAddonImage = -1;
		return B_NOT_SUPPORTED;
	}

	status = cloneHook((void*)driverPath.String());
	if (status < B_OK) {
		fprintf(stderr, "InitClone: cannot clone accelerant\n");
		unload_add_on(fAddonImage);
		fAddonImage = -1;
	}

	return status;
}


/** @brief Ensures the hardware is running in the requested display mode.
 *
 *  Compares @p displayMode against the current mode returned by
 *  BScreen::GetMode().  If any field differs (virtual size, colour space,
 *  or flags), calls BScreen::SetMode() to switch hardware.  In all cases,
 *  calls _GetCardInfo() to refresh fCardInfo and then propagates the
 *  card info fields into fFrameBufferInfo (bits_per_pixel, bytes_per_row,
 *  width, height, display_width, display_height, display_x, display_y).
 *
 *  @param displayMode  Pointer to the desired display_mode.
 *  @return B_OK on success, or an error code if mode switching or card-info
 *          queries fail.
 */
status_t
BWindowScreen::_AssertDisplayMode(display_mode* displayMode)
{
	CALLED();

	BScreen screen(this);

	display_mode currentMode;
	status_t status = screen.GetMode(&currentMode);
	if (status != B_OK)
		return status;

	if (currentMode.virtual_height != displayMode->virtual_height
		|| currentMode.virtual_width != displayMode->virtual_width
		|| currentMode.space != displayMode->space
		|| currentMode.flags != displayMode->flags) {
		status = screen.SetMode(displayMode);
		if (status != B_OK) {
			fprintf(stderr, "AssertDisplayMode: Setting mode failed: %s\n",
				strerror(status));
			return status;
		}

		memcpy(fDisplayMode, displayMode, sizeof(display_mode));
	}

	status = _GetCardInfo();
	if (status != B_OK)
		return status;

	fFrameBufferInfo.bits_per_pixel = fCardInfo.bits_per_pixel;
	fFrameBufferInfo.bytes_per_row = fCardInfo.bytes_per_row;
	fFrameBufferInfo.width = fCardInfo.width;
	fFrameBufferInfo.height = fCardInfo.height;
	fFrameBufferInfo.display_width = fCardInfo.width;
	fFrameBufferInfo.display_height = fCardInfo.height;
	fFrameBufferInfo.display_x = 0;
	fFrameBufferInfo.display_y = 0;

	return B_OK;
}
