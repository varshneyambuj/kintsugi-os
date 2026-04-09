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
 *   Copyright 2020 Haiku, Inc. All rights reserved.
 *   Authors: Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file DirectWindow.h
 *  @brief BDirectWindow class for direct framebuffer access in game/fullscreen contexts.
 */

#ifndef	_DIRECT_WINDOW_H
#define	_DIRECT_WINDOW_H


#include <Region.h>
#include <Window.h>


/** @brief Flags describing the current state of a direct framebuffer connection. */
enum direct_buffer_state {
	B_DIRECT_MODE_MASK = 15,
	B_DIRECT_START = 0,
	B_DIRECT_STOP = 1,
	B_DIRECT_MODIFY = 2,
	B_CLIPPING_MODIFIED = 16,
	B_BUFFER_RESIZED = 32,
	B_BUFFER_MOVED = 64,
	B_BUFFER_RESET = 128
};


/** @brief Flags indicating changes in the underlying graphics driver state. */
enum direct_driver_state {
	B_DRIVER_CHANGED = 0x0001,
	B_MODE_CHANGED = 0x0002
};


/** @brief Describes the current framebuffer layout, clipping, and pixel format for direct rendering. */
typedef struct {
	direct_buffer_state	buffer_state;
	direct_driver_state	driver_state;
	void				*bits;
	addr_t				_reserved; // was pci_bits
	int32				bytes_per_row;
	uint32				bits_per_pixel;
	color_space			pixel_format;
	buffer_layout		layout;
	buffer_orientation	orientation;
	area_id				bits_area;
	uint32				_reserved1[10];
	uint32				clip_list_count;
	clipping_rect		window_bounds;
	clipping_rect		clip_bounds;
	clipping_rect		clip_list[1];
} direct_buffer_info;


/** @brief A window subclass that provides direct access to the framebuffer for high-performance rendering. */
class BDirectWindow : public BWindow {
public:
	/** @brief Constructs a BDirectWindow with the given frame, title, and window type.
	 *  @param frame     Initial window frame rectangle.
	 *  @param title     Window title string.
	 *  @param type      Window type.
	 *  @param flags     Window flags.
	 *  @param workspace Workspace mask; defaults to B_CURRENT_WORKSPACE.
	 */
								BDirectWindow(BRect frame, const char* title,
									window_type type, uint32 flags,
									uint32 workspace = B_CURRENT_WORKSPACE);

	/** @brief Constructs a BDirectWindow with explicit look and feel.
	 *  @param frame     Initial window frame rectangle.
	 *  @param title     Window title string.
	 *  @param look      Window decorator look.
	 *  @param feel      Window feel (e.g. normal, floating, modal).
	 *  @param flags     Window flags.
	 *  @param workspace Workspace mask; defaults to B_CURRENT_WORKSPACE.
	 */
								BDirectWindow(BRect frame, const char* title,
									window_look look, window_feel feel,
									uint32 flags,
									uint32 workspace = B_CURRENT_WORKSPACE);

	/** @brief Destroys the BDirectWindow and releases direct-buffer resources. */
	virtual						~BDirectWindow();

	/** @brief Instantiates a BDirectWindow from an archived BMessage.
	 *  @param data Archive message produced by Archive().
	 *  @return Newly allocated BArchivable, or NULL on failure.
	 */
	static	BArchivable*		Instantiate(BMessage* data);

	/** @brief Archives this window into a BMessage.
	 *  @param data  Destination message.
	 *  @param deep  If true, archive child objects as well.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Archive(BMessage* data,
									bool deep = true) const;

	/** @brief Quits the window, terminating the direct-buffer daemon. */
	virtual	void				Quit();

	/** @brief Dispatches an incoming message to the appropriate handler.
	 *  @param message The message to dispatch.
	 *  @param handler The target handler.
	 */
	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);

	/** @brief Handles messages not consumed by the default dispatch mechanism.
	 *  @param message The message to handle.
	 */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Called when the window has been moved.
	 *  @param newPosition New top-left corner of the window.
	 */
	virtual	void				FrameMoved(BPoint newPosition);

	/** @brief Called when the window has been resized.
	 *  @param newWidth  New width in pixels.
	 *  @param newHeight New height in pixels.
	 */
	virtual	void				FrameResized(float newWidth, float newHeight);

	/** @brief Called when the set of active workspaces changes.
	 *  @param oldWorkspaces Previous workspace mask.
	 *  @param newWorkspaces New workspace mask.
	 */
	virtual	void				WorkspacesChanged(uint32 oldWorkspaces,
									uint32 newWorkspaces);

	/** @brief Called when a specific workspace is activated or deactivated.
	 *  @param workspaceIndex Index of the workspace that changed.
	 *  @param state          True if activated, false if deactivated.
	 */
	virtual	void				WorkspaceActivated(int32 workspaceIndex,
									bool state);

	/** @brief Minimizes or restores the window.
	 *  @param minimize True to minimize, false to restore.
	 */
	virtual	void				Minimize(bool minimize);

	/** @brief Zooms the window to the recommended size or restores it.
	 *  @param recPosition Recommended position.
	 *  @param recWidth    Recommended width.
	 *  @param recHeight   Recommended height.
	 */
	virtual	void				Zoom(BPoint recPosition, float recWidth,
									float recHeight);

	/** @brief Called when the screen resolution or color depth changes.
	 *  @param screenFrame New screen frame.
	 *  @param depth       New color space.
	 */
	virtual	void				ScreenChanged(BRect screenFrame,
									color_space depth);

	/** @brief Called just before menu tracking begins. */
	virtual	void				MenusBeginning();

	/** @brief Called just after menu tracking ends. */
	virtual	void				MenusEnded();

	/** @brief Called when the window gains or loses focus.
	 *  @param state True if activated, false if deactivated.
	 */
	virtual	void				WindowActivated(bool state);

	/** @brief Makes the window visible and initiates a direct-buffer connection. */
	virtual	void				Show();

	/** @brief Hides the window and terminates the direct-buffer connection. */
	virtual	void				Hide();

	/** @brief Resolves a scripting specifier to a handler.
	 *  @param message   Incoming scripting message.
	 *  @param index     Specifier index.
	 *  @param specifier The specifier message.
	 *  @param what      Specifier constant.
	 *  @param property  Name of the property.
	 *  @return The resolved BHandler.
	 */
	virtual	BHandler*			ResolveSpecifier(BMessage* message,
									int32 index, BMessage* specifier,
									int32 what, const char* property);

	/** @brief Fills in the supported scripting suites.
	 *  @param data Destination message.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetSupportedSuites(BMessage* data);

	/** @brief Executes a perform code (ABI compatibility hook).
	 *  @param code Perform code.
	 *  @param arg  Arbitrary argument pointer.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Perform(perform_code code, void* arg);

	/** @brief Called by the system when the direct-buffer connection state changes.
	 *  @param info Pointer to the current direct_buffer_info; may be NULL when stopping.
	 */
	virtual	void				DirectConnected(direct_buffer_info* info);

	/** @brief Retrieves the current clipping region for direct rendering.
	 *  @param region Filled with the current clipping region.
	 *  @param origin Optional coordinate origin offset.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetClippingRegion(BRegion* region,
									BPoint* origin = NULL) const;

	/** @brief Enables or disables full-screen exclusive mode.
	 *  @param enable True to enter full screen, false to leave.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetFullScreen(bool enable);

	/** @brief Returns whether the window is currently in full-screen mode.
	 *  @return True if full screen, false otherwise.
	 */
			bool				IsFullScreen() const;

	/** @brief Queries whether the display driver supports windowed direct-buffer mode.
	 *  @param id Screen identifier; defaults to B_MAIN_SCREEN_ID.
	 *  @return True if window mode is supported, false otherwise.
	 */
	static	bool				SupportsWindowMode(
									screen_id id = B_MAIN_SCREEN_ID);
private:
								BDirectWindow();
								BDirectWindow(BDirectWindow& other);

			BDirectWindow&		operator=(BDirectWindow& other);

	typedef	BWindow				inherited;

	virtual	void				_ReservedDirectWindow1();
	virtual	void				_ReservedDirectWindow2();
	virtual	void				_ReservedDirectWindow3();
	virtual	void				_ReservedDirectWindow4();

	virtual	void				task_looper();
	virtual	BMessage*			ConvertToMessage(void* raw, int32 code);

	static	int32				_daemon_thread(void* arg);
			int32				_DirectDaemon();
			bool				_LockDirect() const;
			void				_UnlockDirect() const;
			void				_InitData();
			void				_DisposeData();
private:
			bool				fDaemonKiller;
			bool				fConnectionEnable;
			bool				fIsFullScreen;
			bool				_unused;
			bool				fInDirectConnected;
			int32				fDirectLock;
			sem_id				fDirectSem;
			uint32				fDirectLockCount;
			thread_id			fDirectLockOwner;
			char*				fDirectLockStack;
			sem_id				fDisableSem;
			sem_id				fDisableSemAck;

			uint32				fInitStatus;

			uint32				_reserved[2];

			area_id				fSourceDirectArea;
			area_id				fClonedDirectArea;
			area_id				fSourceBitsArea;
			thread_id			fDirectDaemonId;
			direct_buffer_info*	fBufferDesc;

			uint32				_more_reserved_[17];
};

#endif
