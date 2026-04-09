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
 * Incorporates work from the Haiku project, originally copyrighted:
 *   Copyright 2020, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file WindowScreen.h
 *  @brief BWindowScreen class providing exclusive full-screen hardware access for games.
 */

#ifndef	_WINDOW_SCREEN_H
#define	_WINDOW_SCREEN_H


#include <Accelerant.h>
#include <GraphicsCard.h>
#include <OS.h>
#include <SupportDefs.h>
#include <Window.h>
#include <kernel/image.h>


/** @brief Moves the hardware mouse cursor to the given screen coordinates.
 *  @param x Horizontal position in pixels.
 *  @param y Vertical position in pixels.
 */
void set_mouse_position(int32 x, int32 y);

/** @brief Attribute flags for BWindowScreen construction. */
enum {
	B_ENABLE_VIEW_DRAWING	= 0x0001, /**< Allow BView drawing while screen is connected. */
	B_ENABLE_DEBUGGER		= 0x0002  /**< Enable the built-in debug overlay. */
};

/** @brief A window that takes exclusive control of the screen for full-screen game rendering. */
class BWindowScreen : public BWindow {
public:
	/** @brief Constructs a BWindowScreen in the given video space with optional debug mode.
	 *  @param title     Window title string.
	 *  @param space     Video space constant (e.g. B_8_BIT_640x480).
	 *  @param _error    Set to B_OK on success or an error code on failure.
	 *  @param debugMode True to enable the debug overlay.
	 */
						BWindowScreen(const char* title, uint32 space,
							status_t* _error, bool debugMode = false);

	/** @brief Constructs a BWindowScreen with explicit attribute flags.
	 *  @param title      Window title string.
	 *  @param space      Video space constant.
	 *  @param attributes Attribute flags (e.g. B_ENABLE_VIEW_DRAWING).
	 *  @param _error     Set to B_OK on success or an error code on failure.
	 */
        				BWindowScreen(const char* title, uint32 space,
        					uint32 attributes, status_t* _error);

	/** @brief Destroys the BWindowScreen and releases exclusive screen access. */
	virtual				~BWindowScreen();

	/** @brief Quits the window and disconnects from the screen. */
	virtual	void		Quit();

	/** @brief Called when the screen connection state changes.
	 *  @param active True when the screen is connected and accessible.
	 */
	virtual	void		ScreenConnected(bool active);

	/** @brief Explicitly disconnects from the screen without quitting the window.  */
    		void		Disconnect();

	/** @brief Called when the window gains or loses activation.
	 *  @param active True if the window is now active.
	 */
	virtual	void		WindowActivated(bool active);

	/** @brief Called when a workspace is activated or deactivated.
	 *  @param workspace Index of the affected workspace.
	 *  @param active    True if the workspace became active.
	 */
	virtual	void		WorkspaceActivated(int32 workspace, bool active);

	/** @brief Called when the screen resolution or color depth changes.
	 *  @param screenSize New screen bounding rectangle.
	 *  @param depth      New color space.
	 */
	virtual	void		ScreenChanged(BRect screenSize, color_space depth);

	/** @brief Hides the window and releases exclusive screen access. */
	virtual	void		Hide();

	/** @brief Shows the window and reacquires exclusive screen access. */
	virtual	void		Show();

	/** @brief Sets palette entries for indexed (8-bit) color modes.
	 *  @param list       Array of rgb_color palette entries.
	 *  @param firstIndex First palette index to update; defaults to 0.
	 *  @param lastIndex  Last palette index to update; defaults to 255.
	 */
			void		SetColorList(rgb_color* list, int32 firstIndex = 0,
							int32 lastIndex = 255);

	/** @brief Switches the display to a different video space.
	 *  @param space Video space constant to switch to.
	 *  @return B_OK on success, or an error code.
	 */
			status_t	SetSpace(uint32 space);

	/** @brief Returns whether the graphics driver supports frame-buffer panning.
	 *  @return True if SetFrameBuffer() and MoveDisplayArea() are supported.
	 */
			bool		CanControlFrameBuffer();

	/** @brief Sets the logical frame-buffer dimensions for panning.
	 *  @param width  Logical width in pixels.
	 *  @param height Logical height in pixels.
	 *  @return B_OK on success, or an error code.
	 */
			status_t	SetFrameBuffer(int32 width, int32 height);

	/** @brief Pans the visible display area within a larger logical frame buffer.
	 *  @param x New horizontal display origin.
	 *  @param y New vertical display origin.
	 *  @return B_OK on success, or an error code.
	 */
			status_t	MoveDisplayArea(int32 x, int32 y);

	/** @brief Returns a pointer to the current 256-entry color palette.
	 *  @return Pointer to the internal rgb_color palette array.
	 */
	rgb_color*			ColorList();

	/** @brief Returns a pointer to the current frame-buffer descriptor.
	 *  @return Pointer to the frame_buffer_info structure.
	 */
	frame_buffer_info*	FrameBufferInfo();

	/** @brief Returns the accelerant hook function at the given index.
	 *  @param index Index into the accelerant hook table.
	 *  @return The requested graphics_card_hook function pointer.
	 */
	graphics_card_hook	CardHookAt(int32 index);

	/** @brief Returns a pointer to the graphics card information structure.
	 *  @return Pointer to the graphics_card_info structure.
	 */
	graphics_card_info*	CardInfo();

	/** @brief Registers a thread so it can be suspended during workspace switches.
	 *  @param thread Thread ID to register.
	 */
			void		RegisterThread(thread_id thread);

	/** @brief Called when the application is suspended or resumed by a workspace switch.
	 *  @param active True when the screen is being reactivated.
	 */
	virtual	void		SuspensionHook(bool active);

	/** @brief Suspends the window screen with a debug label.
	 *  @param label Human-readable label shown while suspended.
	 */
			void		Suspend(char* label);

private:
	virtual status_t	Perform(perform_code d, void* arg);
	virtual	void		_ReservedWindowScreen1();
	virtual	void		_ReservedWindowScreen2();
	virtual	void		_ReservedWindowScreen3();
	virtual	void		_ReservedWindowScreen4();
			status_t	_InitData(uint32 space, uint32 attributes);
			void		_DisposeData();
			status_t	_LockScreen(bool lock);
			status_t	_Activate();
			status_t	_Deactivate();
			status_t	_SetupAccelerantHooks();
			void		_ResetAccelerantHooks();
			status_t	_GetCardInfo();
			void		_Suspend();
			void		_Resume();
			status_t	_GetModeFromSpace(uint32 space, display_mode* mode);
			status_t	_InitClone();
			status_t	_AssertDisplayMode(display_mode* mode);

private:
			uint16		_reserved0;
			bool		_reserved1;
			bool		fWorkState;
			bool		fWindowState;
			bool		fActivateState;
			int32		fLockState;
			int32		fWorkspaceIndex;

	display_mode*		fOriginalDisplayMode;
	display_mode*		fDisplayMode;
			sem_id		fDebugSem;
			image_id	fAddonImage;
			uint32		fAttributes;

			rgb_color	fPalette[256];

	graphics_card_info	fCardInfo;
	frame_buffer_info	fFrameBufferInfo;

			char*		fDebugFrameBuffer;
			bool		fDebugState;
			bool		fDebugFirst;
			int32		fDebugWorkspace;
			int32		fDebugThreadCount;
			thread_id*	fDebugThreads;
			uint32		fModeCount;
	display_mode*		fModeList;

	GetAccelerantHook	fGetAccelerantHook;
	wait_engine_idle	fWaitEngineIdle;

			uint32		_reserved[163];
};

#endif
