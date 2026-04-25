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
 *   Copyright 2001-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Michael Lotz <mmlr@mlotz.ch>
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file DWindowHWInterface.cpp
 * @brief HWInterface that nests the app_server frame buffer inside a
 *        BDirectWindow plus a real graphics accelerant.
 *
 * Used by "app_server -test" mode: rather than driving the screen directly,
 * the test server opens an accelerant for a graphics card, hosts a
 * BDirectWindow on the running desktop, and exposes that window's direct
 * frame buffer to the drawing engine via DWindowBuffer. Input events arrive
 * through the embedded DView and are forwarded to the input port using the
 * same wire format as the real input_server.
 */


#include "DWindowHWInterface.h"

#include <malloc.h>
#include <new>
#include <stdio.h>

#include <Application.h>
#include <Bitmap.h>
#include <Cursor.h>
#include <DirectWindow.h>
#include <Locker.h>
#include <Message.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Region.h>
#include <Screen.h>
#include <String.h>
#include <View.h>

#include <Accelerant.h>
#include <graphic_driver.h>
#include <FindDirectory.h>
#include <image.h>
#include <dirent.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <ServerProtocol.h>

#include "DWindowBuffer.h"
#include "PortLink.h"
#include "RGBColor.h"
#include "ServerConfig.h"
#include "ServerCursor.h"


#ifdef DEBUG_DRIVER_MODULE
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


/** @brief 16x16 fully transparent cursor shape used to hide the host
           desktop's pointer while it is over the app_server test window. */
const unsigned char kEmptyCursor[] = { 16, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};


/**
 * @brief Thread entry point that runs the embedded BApplication's loop.
 *
 * @param cookie  Opaque pointer expected to be a BApplication*.
 * @return        Always 0; the application is destroyed by its caller.
 */
static int32
run_app_thread(void* cookie)
{
	if (BApplication* app = (BApplication*)cookie) {
		app->Lock();
		app->Run();
	}
	return 0;
}


//#define INPUTSERVER_TEST_MODE 1


/** @brief BView hosted inside the test app_server window that captures
           input messages and forwards them to the input port. */
class DView : public BView {
public:
								DView(BRect bounds);
	virtual						~DView();

								// DView
			void				ForwardMessage(BMessage* message = NULL);

private:
			port_id				fInputPort;
};

/** @brief BDirectWindow subclass that owns the embedded DView and lets the
           HWInterface know when the host moves the window. */
class DWindow : public BWindow {
public:
								DWindow(BRect frame,
									DWindowHWInterface* interface,
									DWindowBuffer* buffer);
	virtual						~DWindow();

	virtual	bool				QuitRequested();

//	virtual	void				DirectConnected(direct_buffer_info* info);

	virtual	void				FrameMoved(BPoint newOffset);

private:
	DWindowHWInterface*			fHWInterface;
	DWindowBuffer*				fBuffer;
};

/** @brief BMessageFilter that intercepts raw input events on the embedded
           DView and forwards them as input_server-format messages. */
class DirectMessageFilter : public BMessageFilter {
public:
								DirectMessageFilter(DView* view);

	virtual filter_result		Filter(BMessage *message, BHandler** _target);

private:
			DView*				fView;
};


//	#pragma mark -


/**
 * @brief Constructs the input-capture view spanning the window bounds.
 *
 * Creates the input port (named SERVER_INPUT_PORT or "ViewInputDevice"
 * depending on INPUTSERVER_TEST_MODE) and, when input-server emulation is
 * enabled at compile time, attaches a DirectMessageFilter.
 *
 * @param bounds  View bounds in window coordinates.
 */
DView::DView(BRect bounds)
	:
	BView(bounds, "graphics card view", B_FOLLOW_ALL, 0)
{
	SetViewColor(B_TRANSPARENT_COLOR);
#ifndef INPUTSERVER_TEST_MODE
	fInputPort = create_port(200, SERVER_INPUT_PORT);
#else
	fInputPort = create_port(100, "ViewInputDevice");
#endif

#ifdef ENABLE_INPUT_SERVER_EMULATION
	AddFilter(new DirectMessageFilter(this));
#endif
}


/**
 * @brief Destroys the view; the input port is intentionally left for the
 *        next launch to reuse.
 */
DView::~DView()
{
}


/**
 * @brief Forwards a window event to the app_server input port verbatim.
 *
 * Emulates the input_server by re-flattening the message and writing it to
 * the captured input port. Fields that would confuse the server's own
 * message dispatch are stripped before flattening.
 *
 * @param message  Message to forward, or NULL to forward the window's
 *                 current message.
 * @note  Silently drops the message if no current message exists or if
 *        flattening fails.
 */
void
DView::ForwardMessage(BMessage* message)
{
	if (message == NULL)
		message = Window()->CurrentMessage();
	if (message == NULL)
		return;

	// remove some fields that potentially mess up our own message processing
	BMessage copy = *message;
	copy.RemoveName("screen_where");
	copy.RemoveName("be:transit");
	copy.RemoveName("be:view_where");
	copy.RemoveName("be:cursor_needed");

	size_t length = copy.FlattenedSize();
	char stream[length];

	if (copy.Flatten(stream, length) == B_OK)
		write_port(fInputPort, 0, stream, length);
}


//	#pragma mark -


/**
 * @brief Constructs a filter that observes every message delivered to the
 *        view, regardless of source.
 *
 * @param view  Target view whose ForwardMessage() will be invoked.
 */
DirectMessageFilter::DirectMessageFilter(DView* view)
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fView(view)
{
}


/**
 * @brief Intercepts mouse and keyboard events and ships them through the
 *        view's input-port forwarder.
 *
 * On B_MOUSE_MOVED into the view, also installs an empty cursor so the host
 * desktop's pointer is hidden while inside the test window.
 *
 * @param message  Incoming message.
 * @param target   Unused output target slot.
 * @return         B_SKIP_MESSAGE if the message was forwarded, otherwise
 *                 B_DISPATCH_MESSAGE so the view handles it normally.
 */
filter_result
DirectMessageFilter::Filter(BMessage* message, BHandler** target)
{
	switch (message->what) {
		case B_KEY_DOWN:
		case B_UNMAPPED_KEY_DOWN:
		case B_KEY_UP:
		case B_UNMAPPED_KEY_UP:
		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
		case B_MOUSE_WHEEL_CHANGED:
			fView->ForwardMessage(message);
			return B_SKIP_MESSAGE;

		case B_MOUSE_MOVED:
		{
			int32 transit;
			if (message->FindInt32("be:transit", &transit) == B_OK
				&& transit == B_ENTERED_VIEW) {
				// A bug in R5 prevents this call from having an effect if
				// called elsewhere, and calling it here works, if we're lucky :-)
				BCursor cursor(kEmptyCursor);
				fView->SetViewCursor(&cursor, true);
			}
			fView->ForwardMessage(message);
			return B_SKIP_MESSAGE;
		}
	}

	return B_DISPATCH_MESSAGE;
}


//	#pragma mark -


/**
 * @brief Builds the test app_server host window with an embedded DView.
 *
 * The window is non-zoomable, non-resizable, and non-movable so the
 * app_server's frame buffer geometry stays fixed for the duration of the
 * session.
 *
 * @param frame      Initial window frame in screen coordinates.
 * @param interface  Owning HWInterface notified about frame moves.
 * @param buffer     Buffer that mirrors the direct frame buffer (currently
 *                   only stored for diagnostic use).
 */
DWindow::DWindow(BRect frame, DWindowHWInterface* interface,
		DWindowBuffer* buffer)
	:
	BWindow(frame, "Haiku App Server", B_TITLED_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_NOT_MOVABLE),
	fHWInterface(interface),
	fBuffer(buffer)
{
	DView* view = new DView(Bounds());
	AddChild(view);
	view->MakeFocus();
		// make it receive key events
}


/**
 * @brief Destroys the window; the embedded view is owned by the BWindow.
 */
DWindow::~DWindow()
{
}


/**
 * @brief Asks the app_server's main port to shut down rather than quitting
 *        this window directly.
 *
 * @return     Always false; the window stays alive and is destroyed only
 *             when the app_server itself tears it down.
 */
bool
DWindow::QuitRequested()
{
	port_id serverport = find_port(SERVER_PORT_NAME);

	if (serverport >= 0) {
		BPrivate::PortLink link(serverport);
		link.StartMessage(B_QUIT_REQUESTED);
		link.Flush();
	} else
		printf("ERROR: couldn't find the app_server's main port!");

	// we don't quit on ourself, we let us be Quit()!
	return false;
}


/*
void
DWindow::DirectConnected(direct_buffer_info* info)
{
//	fDesktop->LockClipping();
//
//	fEngine.Lock();
//
	switch(info->buffer_state & B_DIRECT_MODE_MASK) {
		case B_DIRECT_START:
		case B_DIRECT_MODIFY:
			fBuffer->SetTo(info);
//			fDesktop->SetOffset(info->window_bounds.left, info->window_bounds.top);
			break;
		case B_DIRECT_STOP:
			fBuffer->SetTo(NULL);
			break;
	}
//
//	fDesktop->SetMasterClipping(&fBuffer.WindowClipping());
//
//	fEngine.Unlock();
//
//	fDesktop->UnlockClipping();
}
*/


/**
 * @brief Notifies the HWInterface that the host moved the window so its
 *        frame buffer offset can be recomputed.
 *
 * @param newOffset  New top-left screen position of the window.
 */
void
DWindow::FrameMoved(BPoint newOffset)
{
	fHWInterface->SetOffset((int32)newOffset.x, (int32)newOffset.y);
}


//	#pragma mark -


/** @brief Default capacity hint used when sizing accelerant parameter
           buffers (currently unused but retained for parity with other
           HWInterface backends). */
const int32 kDefaultParamsCount = 64;

/**
 * @brief Constructs the interface and prepares accelerant hook slots.
 *
 * All accelerant function pointers start out NULL; they are populated by
 * Initialize() through _OpenAccelerant() and _SetupDefaultHooks(). The
 * default desired mode is 800x600 B_RGBA32 at offset (50, 50).
 */
DWindowHWInterface::DWindowHWInterface()
	:
	HWInterface(),
	fFrontBuffer(new DWindowBuffer()),
	fWindow(NULL),

	fXOffset(50),
	fYOffset(50),

	fCardFD(-1),
	fAccelerantImage(-1),
	fAccelerantHook(NULL),

	// required hooks
	fAccAcquireEngine(NULL),
	fAccReleaseEngine(NULL),
	fAccSyncToToken(NULL),
	fAccGetModeCount(NULL),
	fAccGetModeList(NULL),
	fAccGetFrameBufferConfig(NULL),
	fAccSetDisplayMode(NULL),
	fAccGetDisplayMode(NULL),
	fAccGetPixelClockLimits(NULL),

	// optional accelerant hooks
	fAccGetTimingConstraints(NULL),
	fAccProposeDisplayMode(NULL),
	fAccFillRect(NULL),
	fAccInvertRect(NULL),
	fAccScreenBlit(NULL),
	fAccSetCursorShape(NULL),
	fAccMoveCursor(NULL),
	fAccShowCursor(NULL)
{
	fDisplayMode.virtual_width = 800;
	fDisplayMode.virtual_height = 600;
	fDisplayMode.space = B_RGBA32;
}


/**
 * @brief Tears down the host window and the embedded BApplication.
 */
DWindowHWInterface::~DWindowHWInterface()
{
	if (fWindow) {
		fWindow->Lock();
		fWindow->Quit();
	}

	be_app->Lock();
	be_app->Quit();
	delete be_app;
}


/**
 * @brief Opens the first available graphics device and clones its accelerant.
 *
 * Iterates over /dev/graphics entries (skipping the VESA/framebuffer fallback
 * unless nothing else opens) and probes each one until both the device and
 * its accelerant load successfully.
 *
 * @return     B_OK on success; the underlying error code otherwise.
 * @retval B_OK            A device and accelerant were initialised.
 * @retval B_ENTRY_NOT_FOUND  No usable graphics device found.
 */
status_t
DWindowHWInterface::Initialize()
{
	status_t ret = HWInterface::Initialize();

	if (ret >= B_OK) {
		for (int32 i = 1; fCardFD != B_ENTRY_NOT_FOUND; i++) {
			fCardFD = _OpenGraphicsDevice(i);
			if (fCardFD < 0) {
				STRACE(("Failed to open graphics device\n"));
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
 * @brief Opens a graphics device for read-write access.
 *
 * The @a deviceNumber is relative to the number of graphics devices that
 * can be successfully opened. One represents the first card that opens
 * (not necessarily the first directory entry). Graphics drivers must be
 * openable more than once, so this iterates until a working entry is found.
 * Falls back to /dev/graphics/vesa or /dev/graphics/framebuffer when nothing
 * else is available and @a deviceNumber is 1.
 *
 * @param deviceNumber  1-based index of the graphics card to open.
 * @return              File descriptor on success, or B_ENTRY_NOT_FOUND
 *                      when no matching device exists.
 */
int
DWindowHWInterface::_OpenGraphicsDevice(int deviceNumber)
{
	DIR *directory = opendir("/dev/graphics");
	if (!directory)
		return B_ENTRY_NOT_FOUND;

	// TODO: We do not need to avoid the "vesa" or "framebuffer" drivers this way
	// once they been ported to the new driver architecture - the special case here
	// can then be removed.
	int count = 0;
	struct dirent *entry = NULL;
	int current_card_fd = -1;
	char path[PATH_MAX];
	while (count < deviceNumber && (entry = readdir(directory)) != NULL) {
		if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")
			|| !strcmp(entry->d_name, "vesa") || !strcmp(entry->d_name, "framebuffer"))
			continue;

		if (current_card_fd >= 0) {
			close(current_card_fd);
			current_card_fd = -1;
		}

		sprintf(path, "/dev/graphics/%s", entry->d_name);
		current_card_fd = open(path, B_READ_WRITE);
		if (current_card_fd >= 0)
			count++;
	}

	// Open VESA or Framebuffer driver if we were not able to get a better one.
	if (count < deviceNumber) {
		if (deviceNumber == 1) {
			sprintf(path, "/dev/graphics/vesa");
			current_card_fd = open(path, B_READ_WRITE);
			if (current_card_fd < 0) {
				sprintf(path, "/dev/graphics/framebuffer");
				current_card_fd = open(path, B_READ_WRITE);
			}
		} else {
			close(current_card_fd);
			current_card_fd = B_ENTRY_NOT_FOUND;
		}
	}

	if (entry)
		fCardNameInDevFS = entry->d_name;

	return current_card_fd;
}


/**
 * @brief Loads the accelerant add-on for the given graphics device and
 *        clones its primary instance.
 *
 * Queries the accelerant signature via ioctl, searches the user/system add-on
 * directories for a matching .so, loads it, looks up B_ACCELERANT_ENTRY_POINT,
 * clones the accelerant, then resolves the standard hook table via
 * _SetupDefaultHooks() and refreshes the frame buffer config.
 *
 * @param device  File descriptor of the graphics device.
 * @return        B_OK on success, B_ERROR on any failure (the partially
 *                loaded image is unloaded before returning).
 */
status_t
DWindowHWInterface::_OpenAccelerant(int device)
{
	char signature[1024];
	if (ioctl(device, B_GET_ACCELERANT_SIGNATURE,
			&signature, sizeof(signature)) != B_OK)
		return B_ERROR;

	STRACE(("accelerant signature is: %s\n", signature));

	struct stat accelerant_stat;
	const static directory_which dirs[] = {
		B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		B_USER_ADDONS_DIRECTORY,
		B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		B_SYSTEM_ADDONS_DIRECTORY
	};

	fAccelerantImage = -1;

	for (uint32 i = 0; i < sizeof(dirs) / sizeof(directory_which); i++) {
		char path[PATH_MAX];
		if (find_directory(dirs[i], -1, false, path, PATH_MAX) != B_OK)
			continue;

		strcat(path, "/accelerants/");
		strcat(path, signature);
		if (stat(path, &accelerant_stat) != 0)
			continue;

		fAccelerantImage = load_add_on(path);
		if (fAccelerantImage >= 0) {
			if (get_image_symbol(fAccelerantImage, B_ACCELERANT_ENTRY_POINT,
				B_SYMBOL_TYPE_ANY, (void**)(&fAccelerantHook)) != B_OK ) {
				STRACE(("unable to get B_ACCELERANT_ENTRY_POINT\n"));
				unload_add_on(fAccelerantImage);
				fAccelerantImage = -1;
				return B_ERROR;
			}


			accelerant_clone_info_size cloneInfoSize;
			cloneInfoSize = (accelerant_clone_info_size)fAccelerantHook(
				B_ACCELERANT_CLONE_INFO_SIZE, NULL);
			if (!cloneInfoSize) {
				STRACE(("unable to get B_ACCELERANT_CLONE_INFO_SIZE (%s)\n", path));
				unload_add_on(fAccelerantImage);
				fAccelerantImage = -1;
				return B_ERROR;
			}

			ssize_t cloneSize = cloneInfoSize();
			void* cloneInfoData = malloc(cloneSize);

//			get_accelerant_clone_info getCloneInfo;
//			getCloneInfo = (get_accelerant_clone_info)fAccelerantHook(B_GET_ACCELERANT_CLONE_INFO, NULL);
//			if (!getCloneInfo) {
//				STRACE(("unable to get B_GET_ACCELERANT_CLONE_INFO (%s)\n", path));
//				unload_add_on(fAccelerantImage);
//				fAccelerantImage = -1;
//				return B_ERROR;
//			}
//			printf("getCloneInfo: %p\n", getCloneInfo);
//
//			getCloneInfo(cloneInfoData);
// TODO: this is what works for the ATI Radeon driver...
sprintf((char*)cloneInfoData, "graphics/%s", fCardNameInDevFS.String());

			clone_accelerant cloneAccelerant;
			cloneAccelerant = (clone_accelerant)fAccelerantHook(B_CLONE_ACCELERANT, NULL);
			if (!cloneAccelerant) {
				STRACE(("unable to get B_CLONE_ACCELERANT\n"));
				unload_add_on(fAccelerantImage);
				fAccelerantImage = -1;
				free(cloneInfoData);
				return B_ERROR;
			}
			status_t ret = cloneAccelerant(cloneInfoData);
			if (ret  != B_OK) {
				STRACE(("Cloning accelerant unsuccessful: %s\n", strerror(ret)));
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
		STRACE(("cannot setup default hooks\n"));

		uninit_accelerant uninitAccelerant = (uninit_accelerant)
			fAccelerantHook(B_UNINIT_ACCELERANT, NULL);
		if (uninitAccelerant != NULL)
			uninitAccelerant();

		unload_add_on(fAccelerantImage);
		return B_ERROR;
	} else {
		_UpdateFrameBufferConfig();
	}

	return B_OK;
}


/**
 * @brief Resolves the required and optional accelerant hook pointers.
 *
 * Required hooks (acquire/release engine, get/set mode, frame buffer config,
 * pixel-clock limits, mode list) must all resolve or the function fails.
 * Optional hooks (timing constraints, propose mode, fill/invert/blit,
 * cursor) are stored when present and left NULL otherwise.
 *
 * @return     B_OK when every required hook is present, B_ERROR otherwise.
 */
status_t
DWindowHWInterface::_SetupDefaultHooks()
{
	// required
	fAccAcquireEngine = (acquire_engine)fAccelerantHook(B_ACQUIRE_ENGINE, NULL);
	fAccReleaseEngine = (release_engine)fAccelerantHook(B_RELEASE_ENGINE, NULL);
	fAccSyncToToken = (sync_to_token)fAccelerantHook(B_SYNC_TO_TOKEN, NULL);
	fAccGetModeCount = (accelerant_mode_count)fAccelerantHook(
		B_ACCELERANT_MODE_COUNT, NULL);
	fAccGetModeList = (get_mode_list)fAccelerantHook(B_GET_MODE_LIST, NULL);
	fAccGetFrameBufferConfig = (get_frame_buffer_config)fAccelerantHook(
		B_GET_FRAME_BUFFER_CONFIG, NULL);
	fAccSetDisplayMode = (set_display_mode)fAccelerantHook(
		B_SET_DISPLAY_MODE, NULL);
	fAccGetDisplayMode = (get_display_mode)fAccelerantHook(
		B_GET_DISPLAY_MODE, NULL);
	fAccGetPixelClockLimits = (get_pixel_clock_limits)fAccelerantHook(
		B_GET_PIXEL_CLOCK_LIMITS, NULL);

	if (!fAccAcquireEngine || !fAccReleaseEngine || !fAccGetFrameBufferConfig
		|| !fAccGetModeCount || !fAccGetModeList || !fAccSetDisplayMode
		|| !fAccGetDisplayMode || !fAccGetPixelClockLimits) {
		return B_ERROR;
	}

	// optional
	fAccGetTimingConstraints = (get_timing_constraints)fAccelerantHook(
		B_GET_TIMING_CONSTRAINTS, NULL);
	fAccProposeDisplayMode = (propose_display_mode)fAccelerantHook(
		B_PROPOSE_DISPLAY_MODE, NULL);

	// cursor
	fAccSetCursorShape = (set_cursor_shape)fAccelerantHook(
		B_SET_CURSOR_SHAPE, NULL);
	fAccMoveCursor = (move_cursor)fAccelerantHook(B_MOVE_CURSOR, NULL);
	fAccShowCursor = (show_cursor)fAccelerantHook(B_SHOW_CURSOR, NULL);

	// update acceleration hooks
	// TODO: would actually have to pass a valid display_mode!
	fAccFillRect = (fill_rectangle)fAccelerantHook(B_FILL_RECTANGLE, NULL);
	fAccInvertRect = (invert_rectangle)fAccelerantHook(B_INVERT_RECTANGLE, NULL);
	fAccScreenBlit = (screen_to_screen_blit)fAccelerantHook(
		B_SCREEN_TO_SCREEN_BLIT, NULL);

	return B_OK;
}


/**
 * @brief Re-queries the accelerant frame buffer config and rebinds the front
 *        buffer at the configured (X, Y) offset.
 *
 * @return     B_OK on success, B_ERROR if the accelerant call fails.
 */
status_t
DWindowHWInterface::_UpdateFrameBufferConfig()
{
	frame_buffer_config config;
	if (fAccGetFrameBufferConfig(&config) != B_OK) {
		STRACE(("unable to get frame buffer config\n"));
		return B_ERROR;
	}

	fFrontBuffer->SetTo(&config, fXOffset, fYOffset, fDisplayMode.virtual_width,
		fDisplayMode.virtual_height, (color_space)fDisplayMode.space);

	return B_OK;
}


/**
 * @brief Uninitialises the accelerant, unloads the add-on, and closes the
 *        graphics device.
 *
 * @return     Always B_OK.
 */
status_t
DWindowHWInterface::Shutdown()
{
	printf("DWindowHWInterface::Shutdown()\n");
	if (fAccelerantHook) {
		uninit_accelerant UninitAccelerant
			= (uninit_accelerant)fAccelerantHook(B_UNINIT_ACCELERANT, NULL);
		if (UninitAccelerant)
			UninitAccelerant();
	}

	if (fAccelerantImage >= 0)
		unload_add_on(fAccelerantImage);

	if (fCardFD >= 0)
		close(fCardFD);

	return B_OK;
}


/**
 * @brief Switches the test interface to the requested display mode.
 *
 * Validates @a mode against the supported list, lazily creates the
 * BApplication and host DWindow on first call, resizes the existing window
 * on subsequent calls, then refreshes the frame buffer config.
 *
 * @param mode  Desired width/height/colour-space tuple.
 * @return      B_OK on success, B_BAD_VALUE if @a mode is unsupported,
 *              B_NO_MEMORY on allocation failure, or any error returned
 *              from spawning the embedded BApplication thread.
 */
status_t
DWindowHWInterface::SetMode(const display_mode& mode)
{
	AutoWriteLocker _(this);

	status_t ret = B_OK;
	// prevent from doing the unnecessary
	if (fFrontBuffer.IsSet()
		&& fDisplayMode.virtual_width == mode.virtual_width
		&& fDisplayMode.virtual_height == mode.virtual_height
		&& fDisplayMode.space == mode.space)
		return ret;

	// check if we support the mode

	display_mode *modes;
	uint32 modeCount, i;
	if (GetModeList(&modes, &modeCount) != B_OK)
		return B_NO_MEMORY;

	for (i = 0; i < modeCount; i++) {
		// we only care for the bare minimum
		if (modes[i].virtual_width == mode.virtual_width
			&& modes[i].virtual_height == mode.virtual_height
			&& modes[i].space == mode.space) {
			// take over settings
			fDisplayMode = modes[i];
			break;
		}
	}

	delete[] modes;

	if (i == modeCount)
		return B_BAD_VALUE;

	BRect frame(0.0, 0.0,
				fDisplayMode.virtual_width - 1,
				fDisplayMode.virtual_height - 1);

	// create the window if we don't have one already
	if (!fWindow) {
		// if the window has not been created yet, the BApplication
		// has not been created either, but we need one to display
		// a real BWindow in the test environment.
		// be_app->Run() needs to be called in another thread
		BApplication* app = new BApplication(
			"application/x-vnd.Haiku-test-app_server");
		app->Unlock();

		thread_id appThread = spawn_thread(run_app_thread, "app thread",
										   B_NORMAL_PRIORITY, app);
		if (appThread >= B_OK)
			ret = resume_thread(appThread);
		else
			ret = appThread;

		if (ret < B_OK)
			return ret;

		fWindow = new DWindow(frame.OffsetByCopy(fXOffset, fYOffset), this,
			fFrontBuffer.Get());

		// fire up the window thread but don't show it on screen yet
		fWindow->Hide();
		fWindow->Show();
	}

	if (fWindow->Lock()) {
		// free and reallocate the bitmaps while the window is locked,
		// so that the view does not accidentally draw a freed bitmap

		if (ret >= B_OK) {
			// change the window size and update the bitmap used for drawing
			fWindow->ResizeTo(frame.Width(), frame.Height());
		}

		// window is hidden when this function is called the first time
		if (fWindow->IsHidden())
			fWindow->Show();

		fWindow->Unlock();
	} else {
		ret = B_ERROR;
	}

	_UpdateFrameBufferConfig();
	_NotifyFrameBufferChanged();

	return ret;
}


/**
 * @brief Copies the currently active display mode into @a mode.
 *
 * @param mode  Destination; may be NULL, in which case the call is a no-op.
 */
void
DWindowHWInterface::GetMode(display_mode* mode)
{
	if (mode && ReadLock()) {
		*mode = fDisplayMode;
		ReadUnlock();
	}
}


/**
 * @brief Fills out a synthetic accelerant_device_info for the test driver.
 *
 * The values are decorative since this interface is purely software-driven
 * inside a host window.
 *
 * @param info  Destination structure; populated only when the read lock can
 *              be taken.
 * @return      Always B_OK.
 */
status_t
DWindowHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	// We really don't have to provide anything here because this is strictly
	// a software-only driver, but we'll have some fun, anyway.
	if (ReadLock()) {
		info->version = 100;
		sprintf(info->name, "Haiku, Inc. DWindowHWInterface");
		sprintf(info->chipset, "Haiku, Inc. Chipset");
		sprintf(info->serial_no, "3.14159265358979323846");
		info->memory = 134217728;	// 128 MB, not that we really have that much. :)
		info->dac_speed = 0xFFFFFFFF;	// *heh*

		ReadUnlock();
	}

	return B_OK;
}


/**
 * @brief Returns the canned list of supported display modes.
 *
 * Builds an array of common resolutions in B_RGB32 and returns it. The
 * caller takes ownership and frees with delete[].
 *
 * @param _modes  Output pointer receiving the newly allocated array.
 * @param _count  Output count of modes in @a _modes.
 * @return        B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
DWindowHWInterface::GetModeList(display_mode** _modes, uint32* _count)
{
	AutoReadLocker _(this);

#if 1
	// setup a whole bunch of different modes
	const struct resolution { int32 width, height; } resolutions[] = {
		{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 960},
		{1280, 1024}, {1400, 1050}, {1600, 1200}
	};
	uint32 resolutionCount = sizeof(resolutions) / sizeof(resolutions[0]);
//	const uint32 colors[] = {B_CMAP8, B_RGB15, B_RGB16, B_RGB32};
	uint32 count = resolutionCount/* * 4*/;

	display_mode* modes = new(std::nothrow) display_mode[count];
	if (modes == NULL)
		return B_NO_MEMORY;

	*_modes = modes;
	*_count = count;

	int32 index = 0;
	for (uint32 i = 0; i < resolutionCount; i++) {
		modes[index].virtual_width = resolutions[i].width;
		modes[index].virtual_height = resolutions[i].height;
		modes[index].space = B_RGB32;

		modes[index].h_display_start = 0;
		modes[index].v_display_start = 0;
		modes[index].timing.h_display = resolutions[i].width;
		modes[index].timing.v_display = resolutions[i].height;
		modes[index].timing.h_total = 22000;
		modes[index].timing.v_total = 22000;
		modes[index].timing.pixel_clock = ((uint32)modes[index].timing.h_total
			* modes[index].timing.v_total * 60) / 1000;
		modes[index].flags = B_PARALLEL_ACCESS;

		index++;
	}
#else
	// support only a single mode, useful
	// for testing a specific mode
	display_mode *modes = new(std::nothrow) display_mode[1];
	modes[0].virtual_width = 800;
	modes[0].virtual_height = 600;
	modes[0].space = B_BRGB32;

	*_modes = modes;
	*_count = 1;
#endif

	return B_OK;
}


/**
 * @brief Pixel-clock limits are not modelled by this software driver.
 *
 * @param mode  Mode in question (ignored).
 * @param low   Output low limit (ignored).
 * @param high  Output high limit (ignored).
 * @return      Always B_ERROR.
 */
status_t
DWindowHWInterface::GetPixelClockLimits(display_mode* mode, uint32* low,
	uint32* high)
{
	return B_ERROR;
}


/**
 * @brief Timing constraints are not modelled by this software driver.
 *
 * @param constraints  Output (ignored).
 * @return             Always B_ERROR.
 */
status_t
DWindowHWInterface::GetTimingConstraints(
	display_timing_constraints* constraints)
{
	return B_ERROR;
}


/**
 * @brief Accepts any proposed mode without modification.
 *
 * Because the interface does not drive real hardware it can support any
 * candidate mode within reason.
 *
 * @param candidate  Mode the client wants to use (untouched).
 * @param low        Lower bound (ignored).
 * @param high       Upper bound (ignored).
 * @return           Always B_OK.
 */
status_t
DWindowHWInterface::ProposeMode(display_mode* candidate,
	const display_mode* low, const display_mode* high)
{
	// We should be able to get away with this because we're not dealing with
	// any specific hardware. This is a Good Thing(TM) because we can support
	// any hardware we wish within reasonable expectaions and programmer
	// laziness. :P
	return B_OK;
}


/**
 * @brief No retrace semaphore is exposed by the test driver.
 *
 * @return     Always -1.
 */
sem_id
DWindowHWInterface::RetraceSemaphore()
{
	return -1;
}


/**
 * @brief Waits for vertical retrace on the host BScreen.
 *
 * @param timeout  Maximum wait in microseconds; defaults to infinite.
 * @return         The status_t returned by BScreen::WaitForRetrace().
 */
status_t
DWindowHWInterface::WaitForRetrace(bigtime_t timeout)
{
	// Locking shouldn't be necessary here - R5 should handle this for us. :)
	BScreen screen;
	return screen.WaitForRetrace(timeout);
}


/**
 * @brief Forwards a DPMS state change to the host BScreen.
 *
 * @param state  Desired DPMS state.
 * @return       The status_t returned by BScreen::SetDPMS().
 */
status_t
DWindowHWInterface::SetDPMSMode(uint32 state)
{
	AutoWriteLocker _(this);

	return BScreen().SetDPMS(state);
}


/**
 * @brief Returns the host BScreen's current DPMS state.
 *
 * @return     DPMS state bitmask.
 */
uint32
DWindowHWInterface::DPMSMode()
{
	AutoReadLocker _(this);

	return BScreen().DPMSState();
}


/**
 * @brief Returns the DPMS capability bitmask from the host BScreen.
 *
 * @return     Bitmask of supported DPMS modes.
 */
uint32
DWindowHWInterface::DPMSCapabilities()
{
	AutoReadLocker _(this);

	return BScreen().DPMSCapabilites();
}


/**
 * @brief Forwards a brightness change to the host BScreen.
 *
 * @param brightness  Brightness in [0.0, 1.0].
 * @return            The status_t returned by BScreen::SetBrightness().
 */
status_t
DWindowHWInterface::SetBrightness(float brightness)
{
	AutoReadLocker _(this);

	return BScreen().SetBrightness(brightness);
}


/**
 * @brief Reads the host BScreen's current brightness.
 *
 * @param brightness  Output, populated with the current brightness in
 *                    [0.0, 1.0].
 * @return            The status_t returned by BScreen::GetBrightness().
 */
status_t
DWindowHWInterface::GetBrightness(float* brightness)
{
	AutoReadLocker _(this);

	return BScreen().GetBrightness(brightness);
}


/**
 * @brief Returns the front buffer (the host window's direct frame buffer).
 *
 * @return     RenderingBuffer pointing at the live pixels.
 */
RenderingBuffer*
DWindowHWInterface::FrontBuffer() const
{
	return fFrontBuffer.Get();
}


/**
 * @brief Returns the back buffer; same as FrontBuffer() since this driver
 *        is single-buffered.
 *
 * @return     The same RenderingBuffer FrontBuffer() returns.
 */
RenderingBuffer*
DWindowHWInterface::BackBuffer() const
{
	return fFrontBuffer.Get();
}


/**
 * @brief Reports that the driver is single-buffered.
 *
 * @return     Always false.
 */
bool
DWindowHWInterface::IsDoubleBuffered() const
{
	return false;
}


/**
 * @brief Invalidates a frame rectangle through the base implementation.
 *
 * @param frame  Rectangle to invalidate.
 * @return       The status_t returned by HWInterface::Invalidate().
 */
status_t
DWindowHWInterface::Invalidate(const BRect& frame)
{
	return HWInterface::Invalidate(frame);
}


/**
 * @brief Updates the (X, Y) origin used when binding the front buffer.
 *
 * Called by DWindow::FrameMoved() so the buffer follows the host window.
 *
 * @param left  New X offset in pixels.
 * @param top   New Y offset in pixels.
 * @todo  Trigger DrawingEngine::Update() so the frame buffer change is
 *        actually picked up by clients.
 */
void
DWindowHWInterface::SetOffset(int32 left, int32 top)
{
	if (!WriteLock())
		return;

	fXOffset = left;
	fYOffset = top;

	_UpdateFrameBufferConfig();

	// TODO: someone would have to call DrawingEngine::Update() now!

	WriteUnlock();
}
