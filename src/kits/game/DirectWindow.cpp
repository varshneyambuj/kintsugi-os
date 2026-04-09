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
 *       Stefano Ceccherini <stefano.ceccherini@gmail.com>
 *       Carwyn Jones <turok2@currantbun.com>
 */

/** @file DirectWindow.cpp
 *  @brief Implements BDirectWindow, a BWindow subclass that grants
 *         applications direct, low-latency access to the framebuffer.
 *
 *  The implementation negotiates sync semaphores with the app_server via
 *  AS_DIRECT_WINDOW_GET_SYNC_DATA, clones the shared direct_buffer_info
 *  area, and runs a private daemon thread (_DirectDaemon) that waits on
 *  fDisableSem and dispatches DirectConnected() notifications.
 */


#include <DirectWindow.h>

#include <stdio.h>
#include <string.h>

#include <Screen.h>

#include <clipping.h>
#include <AppServerLink.h>
#include <ApplicationPrivate.h>
#include <DirectWindowPrivate.h>
#include <ServerProtocol.h>
#include <ServerMemoryAllocator.h>


//#define DEBUG		1
#define OUTPUT		printf
//#define OUTPUT	debug_printf


// We don't need this kind of locking, since the directDaemonFunc
// doesn't access critical shared data.
#define DW_NEEDS_LOCKING 0

/** @brief Bitmask flags tracking which sub-resources have been successfully
 *         initialised by _InitData().
 *
 *  Used so that _DisposeData() can tear down only the resources that were
 *  actually created, even when initialisation failed partway through.
 */
enum dw_status_bits {
	DW_STATUS_AREA_CLONED	 = 0x1,  ///< fClonedDirectArea has been cloned successfully.
	DW_STATUS_THREAD_STARTED = 0x2,  ///< The direct daemon thread has been started.
	DW_STATUS_SEM_CREATED	 = 0x4   ///< fDirectSem has been created (DW_NEEDS_LOCKING only).
};


#if DEBUG


/** @brief Prints the human-readable name of a direct_buffer_state value to
 *         the configured OUTPUT macro.
 *
 *  Only compiled when the DEBUG macro is non-zero.
 *
 *  @param state The direct_buffer_state value to decode and print.
 */
static void
print_direct_buffer_state(const direct_buffer_state &state)
{
	char string[128];
	int modeState = state & B_DIRECT_MODE_MASK;
	if (modeState == B_DIRECT_START)
		strcpy(string, "B_DIRECT_START");
	else if (modeState == B_DIRECT_MODIFY)
		strcpy(string, "B_DIRECT_MODIFY");
	else if (modeState == B_DIRECT_STOP)
		strcpy(string, "B_DIRECT_STOP");

	if (state & B_CLIPPING_MODIFIED)
		strcat(string, " | B_CLIPPING_MODIFIED");
	if (state & B_BUFFER_RESIZED)
		strcat(string, " | B_BUFFER_RESIZED");
	if (state & B_BUFFER_MOVED)
		strcat(string, " | B_BUFFER_MOVED");
	if (state & B_BUFFER_RESET)
		strcat(string, " | B_BUFFER_RESET");

	OUTPUT("direct_buffer_state: %s\n", string);
}


/** @brief Prints the human-readable name of a direct_driver_state value.
 *
 *  Only compiled when the DEBUG macro is non-zero.  Returns immediately
 *  if @p state is zero (no driver event to report).
 *
 *  @param state The direct_driver_state value to decode and print.
 */
static void
print_direct_driver_state(const direct_driver_state &state)
{
	if (state == 0)
		return;

	char string[64];
	if (state == B_DRIVER_CHANGED)
		strcpy(string, "B_DRIVER_CHANGED");
	else if (state == B_MODE_CHANGED)
		strcpy(string, "B_MODE_CHANGED");

	OUTPUT("direct_driver_state: %s\n", string);
}


#if DEBUG > 1


/** @brief Prints the human-readable name of a buffer_layout value.
 *
 *  Only compiled when DEBUG > 1.
 *
 *  @param layout The buffer_layout value to decode and print.
 */
static void
print_direct_buffer_layout(const buffer_layout &layout)
{
	char string[64];
	if (layout == B_BUFFER_NONINTERLEAVED)
		strcpy(string, "B_BUFFER_NONINTERLEAVED");
	else
		strcpy(string, "unknown");

	OUTPUT("layout: %s\n", string);
}


/** @brief Prints the human-readable name of a buffer_orientation value.
 *
 *  Only compiled when DEBUG > 1.
 *
 *  @param orientation The buffer_orientation value to decode and print.
 */
static void
print_direct_buffer_orientation(const buffer_orientation &orientation)
{
	char string[64];
	switch (orientation) {
		case B_BUFFER_TOP_TO_BOTTOM:
			strcpy(string, "B_BUFFER_TOP_TO_BOTTOM");
			break;
		case B_BUFFER_BOTTOM_TO_TOP:
			strcpy(string, "B_BUFFER_BOTTOM_TO_TOP");
			break;
		default:
			strcpy(string, "unknown");
			break;
	}

	OUTPUT("orientation: %s\n", string);
}


#endif	// DEBUG > 2


/** @brief Dumps a full direct_buffer_info structure to the debug output.
 *
 *  Calls print_direct_buffer_state() and print_direct_driver_state() at
 *  all debug levels, and additionally dumps the pixel format, layout,
 *  orientation, and clipping rectangles at higher debug levels.
 *
 *  Only compiled when the DEBUG macro is non-zero.
 *
 *  @param info The direct_buffer_info to print.
 */
static void
print_direct_buffer_info(const direct_buffer_info &info)
{
	print_direct_buffer_state(info.buffer_state);
	print_direct_driver_state(info.driver_state);

#	if DEBUG > 1
	OUTPUT("bits: %p\n", info.bits);
	OUTPUT("bytes_per_row: %ld\n", info.bytes_per_row);
	OUTPUT("bits_per_pixel: %lu\n", info.bits_per_pixel);
	OUTPUT("pixel_format: %d\n", info.pixel_format);
	print_direct_buffer_layout(info.layout);
	print_direct_buffer_orientation(info.orientation);

#		if DEBUG > 2
	// TODO: this won't work correctly with debug_printf()
	printf("CLIPPING INFO:\n");
	printf("clipping_rects count: %ld\n", info.clip_list_count);

	printf("- window_bounds:\n");
	BRegion region;
	region.Set(info.window_bounds);
	region.PrintToStream();

	region.MakeEmpty();
	for (uint32 i = 0; i < info.clip_list_count; i++)
		region.Include(info.clip_list[i]);

	printf("- clip_list:\n");
	region.PrintToStream();
#		endif
#	endif

	OUTPUT("\n");
}


#endif	// DEBUG


//	#pragma mark -


/** @brief Constructs a BDirectWindow using a window_type shorthand.
 *
 *  Delegates window creation to BWindow and then calls _InitData() to
 *  negotiate sync data with the app_server and start the daemon thread.
 *
 *  @param frame      Initial frame rectangle for the window.
 *  @param title      Window title string.
 *  @param type       Shorthand window type (e.g. B_TITLED_WINDOW).
 *  @param flags      Window behaviour flags.
 *  @param workspace  Bitmask of workspaces in which the window appears.
 */
BDirectWindow::BDirectWindow(BRect frame, const char* title, window_type type,
		uint32 flags, uint32 workspace)
	:
	BWindow(frame, title, type, flags, workspace)
{
	_InitData();
}


/** @brief Constructs a BDirectWindow using explicit window_look and
 *         window_feel values.
 *
 *  Delegates window creation to BWindow and then calls _InitData() to
 *  negotiate sync data with the app_server and start the daemon thread.
 *
 *  @param frame      Initial frame rectangle for the window.
 *  @param title      Window title string.
 *  @param look       Decorative appearance of the window.
 *  @param feel       Behavioural feel of the window.
 *  @param flags      Window behaviour flags.
 *  @param workspace  Bitmask of workspaces in which the window appears.
 */
BDirectWindow::BDirectWindow(BRect frame, const char* title, window_look look,
		window_feel feel, uint32 flags, uint32 workspace)
	:
	BWindow(frame, title, look, feel, flags, workspace)
{
	_InitData();
}


/** @brief Destroys the BDirectWindow and releases all direct-access
 *         resources.
 *
 *  Calls _DisposeData() which waits for any active DirectConnected()
 *  session to terminate, signals the daemon thread to exit, and frees
 *  the cloned shared-memory area.
 */
BDirectWindow::~BDirectWindow()
{
	_DisposeData();
}


//	#pragma mark - BWindow API implementation


/** @brief Factory method for BArchivable — not supported by BDirectWindow.
 *
 *  @param data  Archive message (unused).
 *  @return Always returns NULL.
 */
BArchivable*
BDirectWindow::Instantiate(BMessage* data)
{
	return NULL;
}


/** @brief Archives the window into a BMessage.
 *
 *  Delegates entirely to BWindow::Archive().
 *
 *  @param data  Destination archive message.
 *  @param deep  If true, child views are also archived.
 *  @return B_OK on success, or an error code.
 */
status_t
BDirectWindow::Archive(BMessage* data, bool deep) const
{
	return inherited::Archive(data, deep);
}


/** @brief Quits the window.
 *
 *  Delegates to BWindow::Quit().
 */
void
BDirectWindow::Quit()
{
	inherited::Quit();
}


/** @brief Dispatches an incoming BMessage to the appropriate handler.
 *
 *  Delegates to BWindow::DispatchMessage().
 *
 *  @param message  The message to dispatch.
 *  @param handler  The handler that should receive the message.
 */
void
BDirectWindow::DispatchMessage(BMessage* message, BHandler* handler)
{
	inherited::DispatchMessage(message, handler);
}


/** @brief Handles messages not dealt with by the standard dispatch path.
 *
 *  Delegates to BWindow::MessageReceived().
 *
 *  @param message  The unhandled message.
 */
void
BDirectWindow::MessageReceived(BMessage* message)
{
	inherited::MessageReceived(message);
}


/** @brief Called when the window's origin changes.
 *
 *  Delegates to BWindow::FrameMoved().
 *
 *  @param newPosition  The window's new top-left screen coordinate.
 */
void
BDirectWindow::FrameMoved(BPoint newPosition)
{
	inherited::FrameMoved(newPosition);
}


/** @brief Called when the set of active workspaces changes.
 *
 *  Delegates to BWindow::WorkspacesChanged().
 *
 *  @param oldWorkspaces  Bitmask of previously active workspaces.
 *  @param newWorkspaces  Bitmask of newly active workspaces.
 */
void
BDirectWindow::WorkspacesChanged(uint32 oldWorkspaces, uint32 newWorkspaces)
{
	inherited::WorkspacesChanged(oldWorkspaces, newWorkspaces);
}


/** @brief Called when the current workspace is activated or deactivated.
 *
 *  Delegates to BWindow::WorkspaceActivated().
 *
 *  @param index  Zero-based index of the workspace that changed.
 *  @param state  true if the workspace became active, false otherwise.
 */
void
BDirectWindow::WorkspaceActivated(int32 index, bool state)
{
	inherited::WorkspaceActivated(index, state);
}


/** @brief Called when the window is resized.
 *
 *  Delegates to BWindow::FrameResized().
 *
 *  @param newWidth   New width of the window content area.
 *  @param newHeight  New height of the window content area.
 */
void
BDirectWindow::FrameResized(float newWidth, float newHeight)
{
	inherited::FrameResized(newWidth, newHeight);
}


/** @brief Minimises or restores the window.
 *
 *  Delegates to BWindow::Minimize().
 *
 *  @param minimize  true to minimise, false to restore.
 */
void
BDirectWindow::Minimize(bool minimize)
{
	inherited::Minimize(minimize);
}


/** @brief Zooms the window to its ideal size or back to the user size.
 *
 *  Delegates to BWindow::Zoom().
 *
 *  @param recPosition  Recommended position after zooming.
 *  @param recWidth     Recommended width after zooming.
 *  @param recHeight    Recommended height after zooming.
 */
void
BDirectWindow::Zoom(BPoint recPosition, float recWidth, float recHeight)
{
	inherited::Zoom(recPosition, recWidth, recHeight);
}


/** @brief Called when the display resolution or colour depth changes.
 *
 *  Delegates to BWindow::ScreenChanged().
 *
 *  @param screenFrame  New frame of the screen.
 *  @param depth        New colour space of the screen.
 */
void
BDirectWindow::ScreenChanged(BRect screenFrame, color_space depth)
{
	inherited::ScreenChanged(screenFrame, depth);
}


/** @brief Called just before a menu attached to this window is shown.
 *
 *  Delegates to BWindow::MenusBeginning().
 */
void
BDirectWindow::MenusBeginning()
{
	inherited::MenusBeginning();
}


/** @brief Called just after the last menu attached to this window closes.
 *
 *  Delegates to BWindow::MenusEnded().
 */
void
BDirectWindow::MenusEnded()
{
	inherited::MenusEnded();
}


/** @brief Called when the window gains or loses keyboard focus.
 *
 *  Delegates to BWindow::WindowActivated().
 *
 *  @param state  true if the window became active, false if it lost focus.
 */
void
BDirectWindow::WindowActivated(bool state)
{
	inherited::WindowActivated(state);
}


/** @brief Makes the window visible on screen.
 *
 *  Delegates to BWindow::Show().
 */
void
BDirectWindow::Show()
{
	inherited::Show();
}


/** @brief Hides the window from screen.
 *
 *  Delegates to BWindow::Hide().
 */
void
BDirectWindow::Hide()
{
	inherited::Hide();
}


/** @brief Resolves a scripting specifier to the appropriate BHandler.
 *
 *  Delegates to BWindow::ResolveSpecifier().
 *
 *  @param message    The scripting message.
 *  @param index      Index of the specifier within the message.
 *  @param specifier  The specifier sub-message.
 *  @param what       The specifier type constant.
 *  @param property   Name of the property being scripted.
 *  @return The BHandler that should handle the scripting request.
 */
BHandler*
BDirectWindow::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	return inherited::ResolveSpecifier(message, index, specifier, what,
		property);
}


/** @brief Fills @p data with the scripting suites supported by this window.
 *
 *  Delegates to BWindow::GetSupportedSuites().
 *
 *  @param data  Message into which suite information is written.
 *  @return B_OK on success, or an error code.
 */
status_t
BDirectWindow::GetSupportedSuites(BMessage* data)
{
	return inherited::GetSupportedSuites(data);
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
BDirectWindow::Perform(perform_code d, void* arg)
{
	return inherited::Perform(d, arg);
}


/** @brief Runs the window's main message loop.
 *
 *  Delegates to BWindow::task_looper().
 */
void
BDirectWindow::task_looper()
{
	inherited::task_looper();
}


/** @brief Converts a raw message buffer into a BMessage.
 *
 *  Delegates to BWindow::ConvertToMessage().
 *
 *  @param raw   Pointer to the raw message buffer.
 *  @param code  Protocol opcode identifying the message type.
 *  @return Newly allocated BMessage, or NULL on failure.
 */
BMessage*
BDirectWindow::ConvertToMessage(void* raw, int32 code)
{
	return inherited::ConvertToMessage(raw, code);
}


//	#pragma mark - BDirectWindow specific API


/** @brief Called by the daemon thread each time the app_server signals a
 *         direct-buffer connection event.
 *
 *  Subclasses override this method to react to framebuffer start, modify,
 *  and stop notifications.  The default implementation is a no-op.
 *
 *  @param info  Pointer to the shared direct_buffer_info describing the
 *               current framebuffer state and clipping region.
 */
void
BDirectWindow::DirectConnected(direct_buffer_info* info)
{
	// implemented in subclasses
}


/** @brief Copies the current window clipping region into @p region.
 *
 *  Must only be called from within DirectConnected().  Uses BRegion's
 *  private _SetSize() / fData API to bulk-copy clip_list rectangles from
 *  the shared direct_buffer_info without individually calling Include().
 *  If @p origin is non-NULL, the resulting region is offset by the
 *  negated origin coordinates (integer-truncated from float).
 *
 *  @param region  Output BRegion to populate.  Must not be NULL.
 *  @param origin  Optional offset applied to the clip region; pass NULL
 *                 to leave the region in screen coordinates.
 *  @return B_OK on success.
 *  @return B_BAD_VALUE if @p region is NULL.
 *  @return B_ERROR if the window is already locked by the calling thread,
 *          if _LockDirect() fails, or if the call is made outside of a
 *          DirectConnected() context.
 *  @return B_NO_MEMORY if the BRegion cannot be resized to hold all
 *          clipping rectangles.
 */
status_t
BDirectWindow::GetClippingRegion(BRegion* region, BPoint* origin) const
{
	if (region == NULL)
		return B_BAD_VALUE;

	if (IsLocked() || !_LockDirect())
		return B_ERROR;

	if (!fInDirectConnected) {
		_UnlockDirect();
		return B_ERROR;
	}

	// BPoint's coordinates are floats. We can only work
	// with integers._DaemonStarter
	int32 originX, originY;
	if (origin == NULL) {
		originX = 0;
		originY = 0;
	} else {
		originX = (int32)origin->x;
		originY = (int32)origin->y;
	}

#ifndef HAIKU_TARGET_PLATFORM_DANO
	// Since we are friend of BRegion, we can access its private members.
	// Otherwise, we would need to call BRegion::Include(clipping_rect)
	// for every clipping_rect in our clip_list, and that would be much
	// more overkill than this (tested ).
	if (!region->_SetSize(fBufferDesc->clip_list_count)) {
		_UnlockDirect();
		return B_NO_MEMORY;
	}
	region->fCount = fBufferDesc->clip_list_count;
	region->fBounds = region->_ConvertToInternal(fBufferDesc->clip_bounds);
	for (uint32 c = 0; c < fBufferDesc->clip_list_count; c++) {
		region->fData[c] = region->_ConvertToInternal(
			fBufferDesc->clip_list[c]);
	}

	// adjust bounds by the given origin point
	region->OffsetBy(-originX, -originY);
#endif

	_UnlockDirect();

	return B_OK;

}


/** @brief Requests that the window enter or leave fullscreen mode.
 *
 *  Sends AS_DIRECT_WINDOW_SET_FULLSCREEN to the app_server and updates
 *  fIsFullScreen only when the server confirms success.  Returns B_OK
 *  immediately if the window is already in the requested state.
 *
 *  @param enable  true to enable fullscreen mode, false to disable it.
 *  @return B_OK on success, or an error code from the app_server.
 */
status_t
BDirectWindow::SetFullScreen(bool enable)
{
	if (fIsFullScreen == enable)
		return B_OK;

	status_t status = B_ERROR;
	if (Lock()) {
		fLink->StartMessage(AS_DIRECT_WINDOW_SET_FULLSCREEN);
		fLink->Attach<bool>(enable);

		if (fLink->FlushWithReply(status) == B_OK
			&& status == B_OK) {
			fIsFullScreen = enable;
		}
		Unlock();
	}
	return status;
}


/** @brief Returns whether this window is currently in fullscreen mode.
 *
 *  @return true if the window occupies the entire screen, false otherwise.
 */
bool
BDirectWindow::IsFullScreen() const
{
	return fIsFullScreen;
}


/** @brief Queries whether the display hardware supports windowed direct
 *         access for the given screen.
 *
 *  Retrieves the current display_mode for @p id and tests for the
 *  B_PARALLEL_ACCESS flag.  Returns false if GetMode() fails.
 *
 *  @param id  Identifier of the screen to query.
 *  @return true if B_PARALLEL_ACCESS is set in the display mode flags.
 */
/*static*/ bool
BDirectWindow::SupportsWindowMode(screen_id id)
{
	display_mode mode;
	status_t status = BScreen(id).GetMode(&mode);
	if (status == B_OK)
		return (mode.flags & B_PARALLEL_ACCESS) != 0;

	return false;
}


//	#pragma mark - Private methods


/** @brief Static thread entry point for the direct daemon.
 *
 *  Casts @p arg to BDirectWindow* and tail-calls _DirectDaemon().
 *
 *  @param arg  Pointer to the owning BDirectWindow instance.
 *  @return Exit status of _DirectDaemon().
 */
/*static*/ int32
BDirectWindow::_daemon_thread(void* arg)
{
	return static_cast<BDirectWindow*>(arg)->_DirectDaemon();
}


/** @brief Main loop of the direct-buffer daemon thread.
 *
 *  Blocks on fDisableSem, which the app_server releases each time the
 *  window's direct-buffer connection state changes (move, resize, clip
 *  region update, etc.).  On each wakeup the daemon:
 *   -# Acquires the direct lock via _LockDirect().
 *   -# Re-maps the bits area from the ServerMemoryAllocator if fBufferDesc->bits
 *      is NULL (area was swapped out or re-created by the server).
 *   -# Sets fInDirectConnected and calls DirectConnected(fBufferDesc).
 *   -# Releases the direct lock via _UnlockDirect().
 *   -# Releases fDisableSemAck so the app_server can proceed.
 *
 *  The loop exits when fDaemonKiller is set to true by _DisposeData().
 *
 *  @return 0 on clean exit, -1 if a semaphore operation fails.
 */
int32
BDirectWindow::_DirectDaemon()
{
	while (!fDaemonKiller) {
		// This sem is released by the app_server when our
		// clipping region changes, or when our window is moved,
		// resized, etc. etc.
		status_t status;
		do {
			status = acquire_sem(fDisableSem);
		} while (status == B_INTERRUPTED);

		if (status != B_OK) {
			//fprintf(stderr, "DirectDaemon: failed to acquire direct sem: %s\n",
				// strerror(status));
			return -1;
		}

#if DEBUG
		print_direct_buffer_info(*fBufferDesc);
#endif

		if (_LockDirect()) {
			if ((fBufferDesc->buffer_state & B_DIRECT_MODE_MASK)
					== B_DIRECT_START)
				fConnectionEnable = true;

			if (fBufferDesc->bits == NULL) {
				// (re-)clone the bits area
				BPrivate::AppServerLink linkLocker;
					// protects ServerAllocator
				BPrivate::ServerMemoryAllocator* allocator
					= BApplication::Private::ServerAllocator();
				allocator->RemoveArea(fSourceBitsArea);
				area_id localArea;
				uint8* bits;
				status_t status = allocator->AddArea(fBufferDesc->bits_area,
					localArea, bits, (size_t)-1);
				if (status != B_OK) {
					_UnlockDirect();
					return -1;
				}

				fBufferDesc->bits = (void*)bits;
				fSourceBitsArea = fBufferDesc->bits_area;
			}

			fInDirectConnected = true;
			DirectConnected(fBufferDesc);
			fInDirectConnected = false;

			if ((fBufferDesc->buffer_state & B_DIRECT_MODE_MASK)
					== B_DIRECT_STOP)
				fConnectionEnable = false;

			_UnlockDirect();
		}

		// The app_server then waits (with a timeout) on this sem.
		// If we aren't quick enough to release this sem, our app
		// will be terminated by the app_server
		if ((status = release_sem(fDisableSemAck)) != B_OK) {
			//fprintf(stderr, "DirectDaemon: failed to release sem: %s\n",
				//strerror(status));
			return -1;
		}
	}

	return 0;
}


/** @brief Acquires the direct lock, serialising access to direct-buffer
 *         state shared between the daemon thread and public API callers.
 *
 *  When DW_NEEDS_LOCKING is non-zero this performs an atomic increment
 *  on fDirectLock and, if the lock was contended, blocks on fDirectSem.
 *  When DW_NEEDS_LOCKING is zero (the current build) this is a no-op
 *  that always returns true.
 *
 *  @return true if the lock was acquired successfully, false on error.
 */
bool
BDirectWindow::_LockDirect() const
{
	// LockDirect() and UnlockDirect() are no-op on BeOS. I tried to call BeOS's
	// version repeatedly, from the same thread and from different threads,
	// nothing happened.
	// They're not needed though, as the direct_daemon_thread doesn't change
	// any shared data. They are probably here for future enhancements.
	status_t status = B_OK;

#if DW_NEEDS_LOCKING
	BDirectWindow* casted = const_cast<BDirectWindow*>(this);

	if (atomic_add(&casted->fDirectLock, 1) > 0) {
		do {
			status = acquire_sem(casted->fDirectSem);
		} while (status == B_INTERRUPTED);
	}

	if (status == B_OK) {
		casted->fDirectLockOwner = find_thread(NULL);
		casted->fDirectLockCount++;
	}
#endif

	return status == B_OK;
}


/** @brief Releases the direct lock acquired by _LockDirect().
 *
 *  When DW_NEEDS_LOCKING is non-zero this performs an atomic decrement
 *  and releases fDirectSem if other threads are waiting.  When
 *  DW_NEEDS_LOCKING is zero this is a no-op.
 */
void
BDirectWindow::_UnlockDirect() const
{
#if DW_NEEDS_LOCKING
	BDirectWindow* casted = const_cast<BDirectWindow*>(this);

	if (atomic_add(&casted->fDirectLock, -1) > 1)
		release_sem(casted->fDirectSem);

	casted->fDirectLockCount--;
#endif
}


/** @brief Initialises all direct-window infrastructure.
 *
 *  Performs the following sequence:
 *   -# Sends AS_DIRECT_WINDOW_GET_SYNC_DATA to the app_server and reads
 *      back the direct_window_sync_data structure containing the shared
 *      area ID and the two synchronisation semaphores.
 *   -# Clones the shared area as fClonedDirectArea so that fBufferDesc
 *      points into the read/write mapping.
 *   -# Spawns the daemon thread via spawn_thread() and resumes it.
 *
 *  The DW_STATUS_* bits in fInitStatus record which steps succeeded so
 *  that _DisposeData() can perform partial cleanup on failure.
 */
void
BDirectWindow::_InitData()
{
	fConnectionEnable = false;
	fIsFullScreen = false;
	fInDirectConnected = false;

	fInitStatus = 0;

	status_t status = B_ERROR;
	struct direct_window_sync_data syncData;

	fLink->StartMessage(AS_DIRECT_WINDOW_GET_SYNC_DATA);
	if (fLink->FlushWithReply(status) == B_OK && status == B_OK)
		fLink->Read<direct_window_sync_data>(&syncData);

	if (status != B_OK)
		return;

#if DW_NEEDS_LOCKING
	fDirectLock = 0;
	fDirectLockCount = 0;
	fDirectLockOwner = -1;
	fDirectLockStack = NULL;
	fDirectSem = create_sem(0, "direct sem");
	if (fDirectSem > 0)
		fInitStatus |= DW_STATUS_SEM_CREATED;
#endif

	fSourceDirectArea = syncData.area;
	fDisableSem = syncData.disable_sem;
	fDisableSemAck = syncData.disable_sem_ack;

	fClonedDirectArea = clone_area("cloned direct area", (void**)&fBufferDesc,
		B_ANY_ADDRESS, B_READ_AREA | B_WRITE_AREA, fSourceDirectArea);

	fSourceBitsArea = -1;

	if (fClonedDirectArea > 0) {
		fInitStatus |= DW_STATUS_AREA_CLONED;

		fDirectDaemonId = spawn_thread(_daemon_thread, "direct daemon",
			B_DISPLAY_PRIORITY, this);

		if (fDirectDaemonId > 0) {
			fDaemonKiller = false;
			if (resume_thread(fDirectDaemonId) == B_OK)
				fInitStatus |= DW_STATUS_THREAD_STARTED;
			else
				kill_thread(fDirectDaemonId);
		}
	}
}


/** @brief Tears down all direct-window infrastructure allocated by
 *         _InitData().
 *
 *  Waits in a 50 ms polling loop until the direct connection terminates
 *  (fConnectionEnable becomes false), ensuring the daemon has delivered
 *  the final B_DIRECT_STOP notification before resources are freed.
 *  Then signals the daemon thread to exit by setting fDaemonKiller and
 *  deleting fDisableSem, waits for the thread, and finally deletes the
 *  cloned area.
 */
void
BDirectWindow::_DisposeData()
{
	// wait until the connection terminates: we can't destroy
	// the object until the client receives the B_DIRECT_STOP
	// notification, or bad things will happen
	while (fConnectionEnable)
		snooze(50000);

	_LockDirect();

	if (fInitStatus & DW_STATUS_THREAD_STARTED) {
		fDaemonKiller = true;
		// delete this sem, otherwise the Direct daemon thread
		// will wait forever on it
		delete_sem(fDisableSem);
		status_t retVal;
		wait_for_thread(fDirectDaemonId, &retVal);
	}

#if DW_NEEDS_LOCKING
	if (fInitStatus & DW_STATUS_SEM_CREATED)
		delete_sem(fDirectSem);
#endif

	if (fInitStatus & DW_STATUS_AREA_CLONED)
		delete_area(fClonedDirectArea);
}


/** @brief Reserved virtual for future binary-compatible extensions. */
void BDirectWindow::_ReservedDirectWindow1() {}
/** @brief Reserved virtual for future binary-compatible extensions. */
void BDirectWindow::_ReservedDirectWindow2() {}
/** @brief Reserved virtual for future binary-compatible extensions. */
void BDirectWindow::_ReservedDirectWindow3() {}
/** @brief Reserved virtual for future binary-compatible extensions. */
void BDirectWindow::_ReservedDirectWindow4() {}
