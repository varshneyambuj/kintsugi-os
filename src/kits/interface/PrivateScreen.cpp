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
 *   Copyright 2002-2009 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (burton666@libero.it)
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file PrivateScreen.cpp
 * @brief Implementation of BPrivateScreen, the internal screen proxy class
 *
 * BPrivateScreen is the reference-counted implementation class behind the public BScreen
 * proxy. It communicates with the app_server to query and modify screen modes, color
 * maps, and the frame buffer.
 *
 * @see BScreen
 */


#include <PrivateScreen.h>

#include <new>
#include <pthread.h>
#include <stdlib.h>

#include <Application.h>
#include <Autolock.h>
#include <Bitmap.h>
#include <Locker.h>
#include <ObjectList.h>
#include <Window.h>

#include <AutoLocker.h>

#include <AppMisc.h>
#include <AppServerLink.h>
#include <ApplicationPrivate.h>
#include <ServerProtocol.h>
#include <ServerReadOnlyMemory.h>


using namespace BPrivate;


namespace {

/**
 * @brief Singleton container that holds all live BPrivateScreen objects.
 *
 * Access is protected by an internal BLocker.  The singleton itself is
 * created exactly once via pthread_once() to avoid static-initialisation
 * order issues.
 */
struct Screens {
	BObjectList<BPrivateScreen, true> list;

	Screens()
		:
		list(2),
		fLock("screen list")
	{
	}

	/**
	 * @brief Acquires the internal lock; returns true on success.
	 */
	bool Lock()
	{
		return fLock.Lock();
	}

	/**
	 * @brief Releases the internal lock.
	 */
	void Unlock()
	{
		fLock.Unlock();
	}

	/**
	 * @brief Returns the process-wide singleton Screens instance.
	 *
	 * Initialises the singleton on the first call using pthread_once().
	 *
	 * @return Pointer to the singleton Screens object.
	 */
	static Screens* Default()
	{
		if (sDefaultInstance == NULL)
			pthread_once(&sDefaultInitOnce, &_InitSingleton);

		return sDefaultInstance;
	}

private:
	/**
	 * @brief pthread_once callback that allocates the singleton.
	 */
	static void _InitSingleton()
	{
		sDefaultInstance = new Screens;
	}

private:
	BLocker					fLock;

	static pthread_once_t	sDefaultInitOnce;
	static Screens*			sDefaultInstance;
};

pthread_once_t Screens::sDefaultInitOnce = PTHREAD_ONCE_INIT;
Screens* Screens::sDefaultInstance = NULL;

}	// unnamed namespace


/**
 * @brief Returns a reference-counted BPrivateScreen for the screen hosting @a window.
 *
 * Queries the app_server for the screen ID of the given window and delegates
 * to _Get().  If @a window is NULL, the main screen is returned.
 *
 * @param window The window whose screen is requested, or NULL for the main screen.
 *
 * @return A BPrivateScreen with its reference count incremented, or NULL on failure.
 *
 * @see Put(), _Get()
 */
BPrivateScreen*
BPrivateScreen::Get(BWindow* window)
{
	int32 id = B_MAIN_SCREEN_ID.id;

	if (window != NULL) {
		BPrivate::AppServerLink link;
		link.StartMessage(AS_GET_SCREEN_ID_FROM_WINDOW);
		link.Attach<int32>(_get_object_token_(window));

		status_t status;
		if (link.FlushWithReply(status) == B_OK && status == B_OK)
			link.Read<int32>(&id);
	}

	return _Get(id, false);
}


/**
 * @brief Returns a reference-counted BPrivateScreen for the given screen ID.
 *
 * Validates that @a id refers to a real screen before returning the object.
 *
 * @param id The numeric screen identifier (e.g. B_MAIN_SCREEN_ID.id).
 *
 * @return A BPrivateScreen with its reference count incremented, or NULL if
 *         the ID is invalid or allocation fails.
 *
 * @see Put(), _Get()
 */
BPrivateScreen*
BPrivateScreen::Get(int32 id)
{
	return _Get(id, true);
}


/**
 * @brief Internal factory: finds or allocates a BPrivateScreen for @a id.
 *
 * Searches the global Screens list for an existing object with the given ID and
 * increments its reference count if found.  Otherwise, optionally validates the
 * ID against the app_server and allocates a new BPrivateScreen.
 *
 * @param id    Numeric screen identifier.
 * @param check If true, the ID is validated via _IsValid() before creating a new object.
 *
 * @return A BPrivateScreen (reference count incremented), or NULL on failure.
 *
 * @see Get(BWindow*), Get(int32), _IsValid()
 */
BPrivateScreen*
BPrivateScreen::_Get(int32 id, bool check)
{
	// Nothing works without an app_server connection
	if (be_app == NULL)
		return NULL;

	Screens* screens = Screens::Default();
	AutoLocker<Screens> locker(screens);

	// search for the screen ID

	for (int32 i = screens->list.CountItems(); i-- > 0;) {
		BPrivateScreen* screen = screens->list.ItemAt(i);

		if (screen->ID() == id) {
			screen->_Acquire();
			return screen;
		}
	}

	if (check) {
		// check if ID is valid
		if (!_IsValid(id))
			return NULL;
	}

	// we need to allocate a new one

	BPrivateScreen* screen = new (std::nothrow) BPrivateScreen(id);
	if (screen == NULL)
		return NULL;

	screens->list.AddItem(screen);
	return screen;
}


/**
 * @brief Decrements the reference count of @a screen and removes it if it reaches zero.
 *
 * The main screen object is never removed from the list even when its reference
 * count drops to zero, because it may be needed again if a monitor is reconnected.
 *
 * @param screen The BPrivateScreen to release; ignored if NULL.
 *
 * @see Get(), _Release()
 */
void
BPrivateScreen::Put(BPrivateScreen* screen)
{
	if (screen == NULL)
		return;

	Screens* screens = Screens::Default();
	AutoLocker<Screens> locker(screens);

	if (screen->_Release()) {
		if (screen->ID() != B_MAIN_SCREEN_ID.id) {
			// we always keep the main screen object around - it will
			// never go away, even if you disconnect all monitors.
			screens->list.RemoveItem(screen);
		}
	}
}


/**
 * @brief Advances to the next screen in the system and returns a reference to it.
 *
 * Queries the app_server for the screen that follows @a screen in enumeration
 * order.  If successful, the reference to @a screen is released and a new
 * reference to the successor screen is returned.
 *
 * @param screen The current screen; its reference is released on success.
 *
 * @return A BPrivateScreen for the next screen, or NULL if there is none or on error.
 *
 * @see Get(), Put(), GetNextID()
 */
BPrivateScreen*
BPrivateScreen::GetNext(BPrivateScreen* screen)
{
	Screens* screens = Screens::Default();
	AutoLocker<Screens> locker(screens);

	int32 id;
	status_t status = screen->GetNextID(id);
	if (status != B_OK)
		return NULL;

	BPrivateScreen* nextScreen = Get(id);
	if (nextScreen == NULL)
		return NULL;

	Put(screen);
	return nextScreen;
}


/**
 * @brief Asks the app_server whether the given screen ID is currently active.
 *
 * @param id The numeric screen identifier to validate.
 *
 * @return True if the server reports the screen as valid, false otherwise.
 */
bool
BPrivateScreen::_IsValid(int32 id)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_VALID_SCREEN_ID);
	link.Attach<int32>(id);

	status_t status;
	if (link.FlushWithReply(status) != B_OK || status < B_OK)
		return false;

	return true;
}


//	#pragma mark -


/**
 * @brief Returns the color space of the screen's current display mode.
 *
 * Queries the current workspace's display mode and extracts the color space field.
 *
 * @return The color_space of the active mode, or B_NO_COLOR_SPACE on failure.
 *
 * @see GetMode(), BScreen::ColorSpace()
 */
color_space
BPrivateScreen::ColorSpace()
{
	display_mode mode;
	if (GetMode(B_CURRENT_WORKSPACE_INDEX, &mode) == B_OK)
		return (color_space)mode.space;

	return B_NO_COLOR_SPACE;
}


/**
 * @brief Returns the bounding rectangle of the screen in screen coordinates.
 *
 * The result is cached and refreshed from the app_server at most once every
 * 10 milliseconds to avoid excessive IPC overhead.
 *
 * @return A BRect describing the screen's bounds; (0,0,0,0) on error.
 *
 * @see BScreen::Frame()
 */
BRect
BPrivateScreen::Frame()
{
	if (system_time() > fLastUpdate + 10000) {
		// invalidate the settings after 10 msecs
		BPrivate::AppServerLink link;
		link.StartMessage(AS_GET_SCREEN_FRAME);
		link.Attach<int32>(ID());
		link.Attach<uint32>(B_CURRENT_WORKSPACE_INDEX);

		status_t status = B_ERROR;
		if (link.FlushWithReply(status) == B_OK && status == B_OK) {
			link.Read<BRect>(&fFrame);
			fLastUpdate = system_time();
		}
	}

	return fFrame;
}


/**
 * @brief Returns whether this screen object refers to a currently active screen.
 *
 * @return True if the screen ID is recognised by the app_server, false otherwise.
 *
 * @see _IsValid(), BScreen::IsValid()
 */
bool
BPrivateScreen::IsValid() const
{
	return BPrivateScreen::_IsValid(ID());
}


/**
 * @brief Retrieves the ID of the next screen in enumeration order.
 *
 * Sends AS_GET_NEXT_SCREEN_ID to the app_server and reads the result into @a id.
 *
 * @param id On success, receives the ID of the next screen.
 *
 * @return B_OK on success, or an error code if there is no next screen.
 *
 * @see GetNext()
 */
status_t
BPrivateScreen::GetNextID(int32& id)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_NEXT_SCREEN_ID);
	link.Attach<int32>(ID());

	status_t status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<int32>(&id);
		return B_OK;
	}

	return status;
}


/**
 * @brief Blocks until the next vertical retrace or until @a timeout elapses.
 *
 * Obtains the retrace semaphore on the first call and caches it.  If the
 * graphics accelerant does not support retrace synchronisation the method
 * returns an error immediately.
 *
 * @param timeout Maximum time in microseconds to wait (B_INFINITE_TIMEOUT to block forever).
 *
 * @return B_OK when a retrace occurred, B_TIMED_OUT if @a timeout elapsed,
 *         or a negative error code if retrace is unsupported.
 *
 * @see RetraceSemaphore(), BScreen::WaitForRetrace()
 */
status_t
BPrivateScreen::WaitForRetrace(bigtime_t timeout)
{
	// Get the retrace semaphore if it's the first time
	// we are called. Cache the value then.
	if (!fRetraceSemValid)
		fRetraceSem = _RetraceSemaphore();

	if (fRetraceSem < 0) {
		// syncing to retrace is not supported by the accelerant
		return fRetraceSem;
	}

	status_t status;
	do {
		status = acquire_sem_etc(fRetraceSem, 1, B_RELATIVE_TIMEOUT, timeout);
	} while (status == B_INTERRUPTED);

	return status;
}


/**
 * @brief Returns the palette index that best matches the given RGBA colour.
 *
 * The transparent colour (B_TRANSPARENT_COLOR) is handled as a special case
 * and always maps to B_TRANSPARENT_8_BIT.  For all other colours the index
 * is looked up from the colour map using a 15-bit packed RGB key.
 *
 * @param red   Red component (0–255).
 * @param green Green component (0–255).
 * @param blue  Blue component (0–255).
 * @param alpha Alpha component (0–255); only used to detect the transparent colour.
 *
 * @return The closest palette index, or 0 if the colour map is unavailable.
 *
 * @see ColorForIndex(), ColorMap()
 */
uint8
BPrivateScreen::IndexForColor(uint8 red, uint8 green, uint8 blue, uint8 alpha)
{
	// Looks like this check is necessary
	if (red == B_TRANSPARENT_COLOR.red
		&& green == B_TRANSPARENT_COLOR.green
		&& blue == B_TRANSPARENT_COLOR.blue
		&& alpha == B_TRANSPARENT_COLOR.alpha)
		return B_TRANSPARENT_8_BIT;

	uint16 index = ((red & 0xf8) << 7) | ((green & 0xf8) << 2) | (blue >> 3);
	if (const color_map* colormap = ColorMap())
		return colormap->index_map[index];

	return 0;
}


/**
 * @brief Returns the rgb_color stored at @a index in the current colour palette.
 *
 * @param index An 8-bit palette index.
 *
 * @return The corresponding rgb_color, or a default-initialised rgb_color{} if
 *         the colour map is unavailable.
 *
 * @see IndexForColor(), ColorMap()
 */
rgb_color
BPrivateScreen::ColorForIndex(const uint8 index)
{
	if (const color_map* colormap = ColorMap())
		return colormap->color_list[index];

	return rgb_color();
}


/**
 * @brief Returns the palette index whose colour is the visual inverse of @a index.
 *
 * Uses the inversion map from the system colour map, which is pre-computed by
 * the app_server for fast XOR-style drawing.
 *
 * @param index The source palette index.
 *
 * @return The inverted palette index, or 0 if the colour map is unavailable.
 *
 * @see ColorMap()
 */
uint8
BPrivateScreen::InvertIndex(uint8 index)
{
	if (const color_map* colormap = ColorMap())
		return colormap->inversion_map[index];

	return 0;
}


/**
 * @brief Returns a pointer to the system-wide colour map in read-only shared memory.
 *
 * The colour map is mapped directly from the app_server's read-only memory
 * region and is valid for the lifetime of the application.  Returns NULL if
 * the application has not yet connected to the server.
 *
 * @return Const pointer to the server's color_map, or NULL on failure.
 *
 * @see IndexForColor(), ColorForIndex(), InvertIndex()
 */
const color_map*
BPrivateScreen::ColorMap()
{
	if (be_app == NULL || BApplication::Private::ServerReadOnlyMemory() == NULL)
		return NULL;

	return &BApplication::Private::ServerReadOnlyMemory()->colormap;
}


/**
 * @brief Captures the screen (or a sub-region) into a newly allocated BBitmap.
 *
 * Allocates a BBitmap in the screen's current colour space and fills it via
 * ReadBitmap().  The caller takes ownership of the returned object.
 *
 * @param _bitmap    On success, receives a pointer to the newly allocated BBitmap.
 * @param drawCursor If true, the hardware cursor is composited into the capture.
 * @param bounds     The region to capture in screen coordinates, or NULL for the
 *                   full screen.
 *
 * @return A status code.
 * @retval B_OK       The bitmap was allocated and filled successfully.
 * @retval B_BAD_VALUE @a _bitmap is NULL.
 * @retval B_NO_MEMORY Could not allocate the BBitmap.
 *
 * @see ReadBitmap(), BScreen::GetBitmap()
 */
status_t
BPrivateScreen::GetBitmap(BBitmap**_bitmap, bool drawCursor, BRect* bounds)
{
	if (_bitmap == NULL)
		return B_BAD_VALUE;

	BRect rect;
	if (bounds != NULL)
		rect = *bounds;
	else
		rect = Frame();

	BBitmap* bitmap = new (std::nothrow) BBitmap(rect, ColorSpace());
	if (bitmap == NULL)
		return B_NO_MEMORY;

	status_t status = bitmap->InitCheck();
	if (status == B_OK)
		status = ReadBitmap(bitmap, drawCursor, &rect);
	if (status != B_OK) {
		delete bitmap;
		return status;
	}

	*_bitmap = bitmap;
	return B_OK;
}


/**
 * @brief Reads screen pixels into a caller-supplied BBitmap.
 *
 * Sends AS_READ_BITMAP to the app_server specifying the target bitmap's server
 * token, the cursor compositing flag, and the capture rectangle.
 *
 * @param bitmap     The destination BBitmap (must be non-NULL and already initialised).
 * @param drawCursor If true, the cursor is composited into the result.
 * @param bounds     The region to read in screen coordinates, or NULL for the full screen.
 *
 * @return B_OK on success, or an error code if the server request failed.
 *
 * @see GetBitmap(), BScreen::ReadBitmap()
 */
status_t
BPrivateScreen::ReadBitmap(BBitmap* bitmap, bool drawCursor, BRect* bounds)
{
	if (bitmap == NULL)
		return B_BAD_VALUE;

	BRect rect;
	if (bounds != NULL)
		rect = *bounds;
	else
		rect = Frame();

	BPrivate::AppServerLink link;
	link.StartMessage(AS_READ_BITMAP);
	link.Attach<int32>(ID());
	link.Attach<int32>(bitmap->_ServerToken());
	link.Attach<bool>(drawCursor);
	link.Attach<BRect>(rect);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) < B_OK || status != B_OK)
		return status;

	return B_OK;
}


/**
 * @brief Returns the current desktop background colour for @a workspace.
 *
 * @param workspace The workspace index, or B_CURRENT_WORKSPACE_INDEX.
 *
 * @return The desktop rgb_color; a default blue is returned if the server
 *         request fails.
 */
rgb_color
BPrivateScreen::DesktopColor(uint32 workspace)
{
	rgb_color color = { 51, 102, 152, 255 };
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_DESKTOP_COLOR);
	link.Attach<uint32>(workspace);

	int32 code;
	if (link.FlushWithReply(code) == B_OK
		&& code == B_OK)
		link.Read<rgb_color>(&color);

	return color;
}


/**
 * @brief Sets the desktop background colour for @a workspace.
 *
 * @param color       The new background colour.
 * @param workspace   The workspace index, or B_CURRENT_WORKSPACE_INDEX.
 * @param makeDefault If true, the colour is persisted as the default for the workspace.
 */
void
BPrivateScreen::SetDesktopColor(rgb_color color, uint32 workspace,
	bool makeDefault)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_DESKTOP_COLOR);
	link.Attach<rgb_color>(color);
	link.Attach<uint32>(workspace);
	link.Attach<bool>(makeDefault);
	link.Flush();
}


/**
 * @brief Asks the app_server to find a display mode close to @a target within limits.
 *
 * The server searches for a supported mode that is as close as possible to
 * @a target while staying within the constraints defined by @a low and @a high.
 * On success @a target is overwritten with the proposed mode.
 *
 * @param target On entry, the desired mode; on success, the closest supported mode.
 * @param low    Lower bound constraints for the proposal.
 * @param high   Upper bound constraints for the proposal.
 *
 * @return B_OK if the proposed mode falls within [low, high]; B_BAD_VALUE if
 *         the best mode found is outside the limits; B_ERROR if any pointer is
 *         NULL or the server request fails.
 *
 * @see GetModeList(), SetMode(), BScreen::ProposeMode()
 */
status_t
BPrivateScreen::ProposeMode(display_mode* target,
	const display_mode* low, const display_mode* high)
{
	// We can't return B_BAD_VALUE here, because it's used to indicate
	// that the mode returned is supported, but it doesn't fall
	// within the limit (see ProposeMode() documentation)
	if (target == NULL || low == NULL || high == NULL)
		return B_ERROR;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_PROPOSE_MODE);
	link.Attach<int32>(ID());
	link.Attach<display_mode>(*target);
	link.Attach<display_mode>(*low);
	link.Attach<display_mode>(*high);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<display_mode>(target);

		bool withinLimits;
		link.Read<bool>(&withinLimits);
		if (!withinLimits)
			status = B_BAD_VALUE;
	}

	return status;
}


/**
 * @brief Retrieves the list of all display modes supported by this screen.
 *
 * Allocates a malloc()-owned array of display_mode structs and stores a
 * pointer in @a _modeList.  The caller is responsible for calling free() on it.
 *
 * @param _modeList On success, receives a pointer to the allocated mode array.
 * @param _count    On success, receives the number of entries in @a _modeList.
 *
 * @return B_OK on success, B_BAD_VALUE if either pointer is NULL, B_NO_MEMORY
 *         if allocation fails, or another error code from the server.
 *
 * @see ProposeMode(), SetMode(), BScreen::GetModeList()
 */
status_t
BPrivateScreen::GetModeList(display_mode** _modeList, uint32* _count)
{
	if (_modeList == NULL || _count == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_MODE_LIST);
	link.Attach<int32>(ID());

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		uint32 count;
		if (link.Read<uint32>(&count) < B_OK)
			return B_ERROR;

		// TODO: this could get too big for the link
		int32 size = count * sizeof(display_mode);
		display_mode* modeList = (display_mode *)malloc(size);
		if (modeList == NULL)
			return B_NO_MEMORY;

		if (link.Read(modeList, size) < B_OK) {
			free(modeList);
			return B_ERROR;
		}

		*_modeList = modeList;
		*_count = count;
	}

	return status;
}


/**
 * @brief Retrieves the display mode currently active on @a workspace.
 *
 * @param workspace The workspace index, or B_CURRENT_WORKSPACE_INDEX.
 * @param mode      Output parameter that receives the current display_mode.
 *
 * @return B_OK on success, B_BAD_VALUE if @a mode is NULL, or another error code.
 *
 * @see SetMode(), GetModeList(), BScreen::GetMode()
 */
status_t
BPrivateScreen::GetMode(uint32 workspace, display_mode *mode)
{
	if (mode == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SCREEN_GET_MODE);
	link.Attach<int32>(ID());
	link.Attach<uint32>(workspace);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK
		|| status != B_OK)
		return status;

	link.Read<display_mode>(mode);
	return B_OK;
}


/**
 * @brief Sets the display mode for @a workspace.
 *
 * Sends the new mode to the app_server.  If @a makeDefault is true the mode
 * is also stored as the persistent default for that workspace.
 *
 * @param workspace   The workspace index, or B_CURRENT_WORKSPACE_INDEX.
 * @param mode        The desired display_mode.
 * @param makeDefault If true, persist @a mode as the workspace default.
 *
 * @return B_OK on success, B_BAD_VALUE if @a mode is NULL, or another error code.
 *
 * @see GetMode(), ProposeMode(), BScreen::SetMode()
 */
status_t
BPrivateScreen::SetMode(uint32 workspace, display_mode *mode, bool makeDefault)
{
	if (mode == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SCREEN_SET_MODE);
	link.Attach<int32>(ID());
	link.Attach<uint32>(workspace);
	link.Attach<display_mode>(*mode);
	link.Attach<bool>(makeDefault);

	status_t status = B_ERROR;
	link.FlushWithReply(status);

	return status;
}


/**
 * @brief Fills @a info with hardware accelerant information for this screen.
 *
 * @param info Output parameter; must be non-NULL.
 *
 * @return B_OK on success, B_BAD_VALUE if @a info is NULL, or another error code.
 */
status_t
BPrivateScreen::GetDeviceInfo(accelerant_device_info *info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_ACCELERANT_INFO);
	link.Attach<int32>(ID());

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<accelerant_device_info>(info);
		return B_OK;
	}

	return status;
}


/**
 * @brief Fills @a info with physical monitor information for this screen.
 *
 * @param info Output parameter; must be non-NULL.
 *
 * @return B_OK on success, B_BAD_VALUE if @a info is NULL, or another error code.
 */
status_t
BPrivateScreen::GetMonitorInfo(monitor_info* info)
{
	if (info == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_MONITOR_INFO);
	link.Attach<int32>(ID());

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<monitor_info>(info);
		return B_OK;
	}

	return status;
}


/**
 * @brief Retrieves the minimum and maximum pixel clock rates for @a mode.
 *
 * @param mode The display mode for which clock limits are requested.
 * @param low  On success, receives the minimum pixel clock in kHz.
 * @param high On success, receives the maximum pixel clock in kHz.
 *
 * @return B_OK on success, B_BAD_VALUE if any pointer is NULL, or another error code.
 */
status_t
BPrivateScreen::GetPixelClockLimits(display_mode *mode, uint32 *low, uint32 *high)
{
	if (mode == NULL || low == NULL || high == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_PIXEL_CLOCK_LIMITS);
	link.Attach<int32>(ID());
	link.Attach<display_mode>(*mode);

	status_t status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<uint32>(low);
		link.Read<uint32>(high);
		return B_OK;
	}

	return status;
}


/**
 * @brief Retrieves the display timing constraints for the current hardware.
 *
 * @param constraints Output parameter; must be non-NULL.
 *
 * @return B_OK on success, B_BAD_VALUE if @a constraints is NULL, or another error code.
 */
status_t
BPrivateScreen::GetTimingConstraints(display_timing_constraints *constraints)
{
	if (constraints == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_TIMING_CONSTRAINTS);
	link.Attach<int32>(ID());

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<display_timing_constraints>(constraints);
		return B_OK;
	}

	return status;
}


/**
 * @brief Sets the DPMS (power management) state of the monitor.
 *
 * @param dpmsState One of the B_DPMS_* constants defined in GraphicsDefs.h.
 *
 * @return B_OK on success, or an error code from the server.
 */
status_t
BPrivateScreen::SetDPMS(uint32 dpmsState)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_DPMS);
	link.Attach<int32>(ID());
	link.Attach<uint32>(dpmsState);

	status_t status = B_ERROR;
	link.FlushWithReply(status);

	return status;
}


/**
 * @brief Returns the current DPMS power state of the monitor.
 *
 * @return One of the B_DPMS_* constants, or 0 if the query fails.
 */
uint32
BPrivateScreen::DPMSState()
{
	uint32 state = 0;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_DPMS_STATE);
	link.Attach<int32>(ID());

	status_t status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		link.Read<uint32>(&state);

	return state;
}


/**
 * @brief Returns a bitmask of the DPMS states supported by the monitor.
 *
 * @return A combination of B_DPMS_* flags, or 0 if the query fails.
 */
uint32
BPrivateScreen::DPMSCapabilites()
{
	uint32 capabilities = 0;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_DPMS_CAPABILITIES);
	link.Attach<int32>(ID());

	status_t status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		link.Read<uint32>(&capabilities);

	return capabilities;
}


/**
 * @brief Retrieves the current display brightness as a value in [0.0, 1.0].
 *
 * @param brightness On success, receives the brightness level (0.0 = off, 1.0 = full).
 *
 * @return B_OK on success, B_BAD_VALUE if @a brightness is NULL, or another error code.
 *
 * @see SetBrightness(), BScreen::GetBrightness()
 */
status_t
BPrivateScreen::GetBrightness(float* brightness)
{
	if (brightness == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_SCREEN_GET_BRIGHTNESS);
	link.Attach<int32>(ID());

	status_t status;
	if (link.FlushWithReply(status) == B_OK && status == B_OK)
		link.Read<float>(brightness);

	return status;
}


/**
 * @brief Sets the display brightness.
 *
 * @param brightness Desired brightness in [0.0, 1.0].
 *
 * @return B_OK on success, or an error code from the server.
 *
 * @see GetBrightness(), BScreen::SetBrightness()
 */
status_t
BPrivateScreen::SetBrightness(float brightness)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SCREEN_SET_BRIGHTNESS);
	link.Attach<int32>(ID());
	link.Attach<float>(brightness);

	status_t status = B_ERROR;
	link.FlushWithReply(status);

	return status;
}


// #pragma mark - private methods


/**
 * @brief Increments the reference count and marks the cached frame as stale.
 *
 * Called by Get() each time a new BScreen object takes ownership of this
 * BPrivateScreen.  Resetting fLastUpdate forces Frame() to re-query the
 * server on the next call, ensuring the new owner gets fresh data.
 *
 * @see _Release(), Get()
 */
void
BPrivateScreen::_Acquire()
{
	fReferenceCount++;

	fLastUpdate = 0;
		// force an update for the new BScreen object
}


/**
 * @brief Decrements the reference count and returns whether it has reached zero.
 *
 * @return True if the reference count dropped to zero (the object may be deleted),
 *         false if other owners remain.
 *
 * @see _Acquire(), Put()
 */
bool
BPrivateScreen::_Release()
{
	return --fReferenceCount == 0;
}


/**
 * @brief Queries the app_server for the retrace semaphore of this screen.
 *
 * On success the semaphore ID is cached in fRetraceSem and fRetraceSemValid
 * is set to true so subsequent calls to WaitForRetrace() skip the IPC round-trip.
 *
 * @return The sem_id of the retrace semaphore, or B_BAD_SEM_ID if unsupported.
 *
 * @see WaitForRetrace()
 */
sem_id
BPrivateScreen::_RetraceSemaphore()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_RETRACE_SEMAPHORE);
	link.Attach<int32>(ID());

	sem_id id = B_BAD_SEM_ID;
	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		link.Read<sem_id>(&id);
		fRetraceSemValid = true;
	}

	return id;
}


/**
 * @brief Constructs a BPrivateScreen for the given screen @a id.
 *
 * Initialises all fields to their safe defaults.  Instances are created only
 * by the static Get() / _Get() factory methods and are never constructed directly.
 *
 * @param id The numeric screen identifier assigned by the app_server.
 */
BPrivateScreen::BPrivateScreen(int32 id)
	:
	fID(id),
	fReferenceCount(0),
	fRetraceSem(-1),
	fRetraceSemValid(false),
	fFrame(0, 0, 0, 0),
	fLastUpdate(0)
{
}


/**
 * @brief Destroys the BPrivateScreen.
 *
 * All cleanup (semaphore deletion, list removal) is handled by Put() before
 * the destructor is reached, so no additional work is needed here.
 */
BPrivateScreen::~BPrivateScreen()
{
}
