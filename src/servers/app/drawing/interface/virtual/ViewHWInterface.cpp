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
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file ViewHWInterface.cpp
 * @brief HWInterface that renders the app_server screen into a BBitmap shown
 *        through a regular BView/BWindow.
 *
 * Used in "app_server -test" mode when no accelerant is available: the front
 * and (optional) back buffers are BBitmaps; CopyBackToFront() composites
 * into the front bitmap and the host window invalidates to redraw it.
 * Input is forwarded by an embedded CardView via BMessenger to the
 * input_server port.
 */


#include "ViewHWInterface.h"

#include <new>
#include <stdio.h>

#include <Application.h>
#include <Bitmap.h>
#include <Cursor.h>
#include <Locker.h>
#include <Message.h>
#include <MessageFilter.h>
#include <MessageRunner.h>
#include <Region.h>
#include <Screen.h>
#include <String.h>
#include <View.h>
#include <Window.h>

#include <ServerProtocol.h>

#include "BBitmapBuffer.h"
#include "PortLink.h"
#include "ServerConfig.h"
#include "ServerCursor.h"


#ifdef DEBUG_DRIVER_MODULE
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


/** @brief 16x16 fully transparent cursor used to hide the host pointer
           while it is over the test app_server's window. */
const unsigned char kEmptyCursor[] = { 16, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

/** @brief Internal message constants used by CardWindow. */
enum {
	/** @brief Coalesced asynchronous redraw request. */
	MSG_UPDATE = 'updt'
};


/**
 * @brief Returns a printable name for the given colour-space constant.
 *
 * @param format  Colour space to name.
 * @return        Static string literal naming @a format, or
 *                "<unkown format>" when @a format is unrecognised.
 */
const char*
string_for_color_space(color_space format)
{
	const char* name = "<unkown format>";
	switch (format) {
		case B_RGBA64:
			name = "B_RGBA64";
			break;
		case B_RGBA64_BIG:
			name = "B_RGBA64_BIG";
			break;
		case B_RGB48:
			name = "B_RGB48";
			break;
		case B_RGB48_BIG:
			name = "B_RGB48_BIG";
			break;
		case B_RGB32:
			name = "B_RGB32";
			break;
		case B_RGBA32:
			name = "B_RGBA32";
			break;
		case B_RGB32_BIG:
			name = "B_RGB32_BIG";
			break;
		case B_RGBA32_BIG:
			name = "B_RGBA32_BIG";
			break;
		case B_RGB24:
			name = "B_RGB24";
			break;
		case B_RGB24_BIG:
			name = "B_RGB24_BIG";
			break;
		case B_CMAP8:
			name = "B_CMAP8";
			break;
		case B_GRAY8:
			name = "B_GRAY8";
			break;
		case B_GRAY1:
			name = "B_GRAY1";
			break;
		default:
			break;
	}
	return name;
}


/**
 * @brief Thread entry point for the embedded BApplication's run loop.
 *
 * @param cookie  BApplication pointer cast to void*.
 * @return        Always 0; the application is deleted on loop exit.
 */
static int32
run_app_thread(void* cookie)
{
	if (BApplication* app = (BApplication*)cookie) {
		app->Lock();
		app->Run();
		delete app;
	}
	return 0;
}


//#define INPUTSERVER_TEST_MODE 1


/** @brief BView that displays the app_server's front-buffer bitmap and
           captures input events for forwarding to the input port. */
class CardView : public BView {
public:
								CardView(BRect bounds);
	virtual						~CardView();

	virtual	void				AttachedToWindow();
	virtual	void				Draw(BRect updateRect);
	virtual	void				MessageReceived(BMessage* message);

								// CardView
			void				SetBitmap(const BBitmap* bitmap);

			void				ForwardMessage(BMessage* message = NULL);

private:
			port_id				fInputPort;
			const BBitmap*		fBitmap;
};

/** @brief BWindow subclass that hosts the CardView, accumulates dirty
           regions, and asks the app_server to quit on close. */
class CardWindow : public BWindow {
public:
								CardWindow(BRect frame);
	virtual						~CardWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

								// CardWindow
			void				SetBitmap(const BBitmap* bitmap);
			void				Invalidate(const BRect& area);

private:
			CardView*			fView;
			BRegion				fUpdateRegion;
			BLocker				fUpdateLock;
};

/** @brief BMessageFilter that intercepts mouse and keyboard events on the
           CardView and forwards them to the input port. */
class CardMessageFilter : public BMessageFilter {
public:
								CardMessageFilter(CardView* view);

	virtual filter_result		Filter(BMessage* message, BHandler** _target);

private:
			CardView*			fView;
};


//	#pragma mark -


/**
 * @brief Constructs the input-capture / bitmap-display view.
 *
 * Creates the input port (named SERVER_INPUT_PORT or a debug name depending
 * on INPUTSERVER_TEST_MODE) and, when input-server emulation is enabled,
 * installs a CardMessageFilter to forward events.
 *
 * @param bounds  View bounds in window coordinates.
 */
CardView::CardView(BRect bounds)
	:
	BView(bounds, "graphics card view", B_FOLLOW_ALL, B_WILL_DRAW),
	fBitmap(NULL)
{
	SetViewColor(B_TRANSPARENT_32_BIT);

#ifndef INPUTSERVER_TEST_MODE
	fInputPort = create_port(200, SERVER_INPUT_PORT);
#else
	fInputPort = create_port(100, "ViewInputDevice");
#endif

#ifdef ENABLE_INPUT_SERVER_EMULATION
	AddFilter(new CardMessageFilter(this));
#endif
}


/**
 * @brief Destroys the view; the input port is left for reuse.
 */
CardView::~CardView()
{
}


/**
 * @brief Hook invoked once the view is attached to its window; nothing to
 *        do here.
 */
void
CardView::AttachedToWindow()
{
}


/**
 * @brief Repaints the dirty rectangle by blitting the current front bitmap.
 *
 * @param updateRect  Rectangle requested by the view system.
 */
void
CardView::Draw(BRect updateRect)
{
	if (fBitmap != NULL)
		DrawBitmapAsync(fBitmap, updateRect, updateRect);
}


/**
 * @brief Forwards a window event to the app_server input port verbatim.
 *
 * Emulates the input_server: re-flattens the message after stripping fields
 * the server adds itself, and writes it to the captured input port.
 *
 * @param message  Message to forward, or NULL to forward the window's
 *                 current message.
 */
void
CardView::ForwardMessage(BMessage* message)
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
	copy.RemoveName("_view_token");

	size_t length = copy.FlattenedSize();
	char stream[length];

	if (copy.Flatten(stream, length) == B_OK)
		write_port(fInputPort, 0, stream, length);
}


/**
 * @brief Default message dispatch; delegates everything to the base view.
 *
 * @param message  Incoming BMessage.
 */
void
CardView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Replaces the bitmap displayed by the view.
 *
 * Triggers a full invalidation when the underlying pointer changes so the
 * new contents are picked up.
 *
 * @param bitmap  New bitmap to display; ownership stays with the caller.
 */
void
CardView::SetBitmap(const BBitmap* bitmap)
{
	if (bitmap != fBitmap) {
		fBitmap = bitmap;

		if (Parent())
			Invalidate();
	}
}


//	#pragma mark -


/**
 * @brief Constructs a filter watching every message bound for the view.
 *
 * @param view  Target view whose ForwardMessage() will be invoked.
 */
CardMessageFilter::CardMessageFilter(CardView* view)
	:
	BMessageFilter(B_ANY_DELIVERY, B_ANY_SOURCE),
	fView(view)
{
}


/**
 * @brief Intercepts mouse and keyboard events and forwards them through the
 *        view's input-port forwarder.
 *
 * On B_MOUSE_DOWN, captures pointer events; on B_MOUSE_MOVED into the view,
 * installs the empty cursor so the host pointer is hidden.
 *
 * @param message  Incoming message.
 * @param target   Output target (unused).
 * @return         B_SKIP_MESSAGE if forwarded, B_DISPATCH_MESSAGE otherwise.
 */
filter_result
CardMessageFilter::Filter(BMessage* message, BHandler** target)
{
	switch (message->what) {
		case B_KEY_DOWN:
		case B_UNMAPPED_KEY_DOWN:
		case B_KEY_UP:
		case B_UNMAPPED_KEY_UP:
		case B_MOUSE_DOWN:
		case B_MOUSE_UP:
		case B_MOUSE_WHEEL_CHANGED:
			if (message->what == B_MOUSE_DOWN)
				fView->SetMouseEventMask(B_POINTER_EVENTS);

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
 * @brief Builds the host window and its embedded CardView.
 *
 * The window is non-zoomable, non-resizable, and uses no server-side
 * window modifiers because the test app_server itself owns the modifier
 * bindings.
 *
 * @param frame  Initial window frame in screen coordinates.
 */
CardWindow::CardWindow(BRect frame)
	:
	BWindow(frame, "Haiku App Server", B_TITLED_WINDOW,
		B_NOT_ZOOMABLE | B_NOT_RESIZABLE | B_NO_SERVER_SIDE_WINDOW_MODIFIERS),
	fUpdateRegion(),
	fUpdateLock("update lock")
{
	fView = new CardView(Bounds());
	AddChild(fView);
	fView->MakeFocus();
		// make it receive key events
}


/**
 * @brief Destroys the window; the embedded view is owned by the BWindow.
 */
CardWindow::~CardWindow()
{
}


/**
 * @brief Handles internal coalesced redraw messages.
 *
 * MSG_UPDATE flushes the accumulated dirty region into the view's
 * Invalidate(); all other messages fall through to BWindow.
 *
 * @param msg  Incoming message.
 */
void
CardWindow::MessageReceived(BMessage* msg)
{
	STRACE("CardWindow::MessageReceived()\n");
	switch (msg->what) {
		case MSG_UPDATE:
			STRACE("MSG_UPDATE\n");
			// invalidate all areas in the view that need redrawing
			if (fUpdateLock.LockWithTimeout(2000LL) >= B_OK) {
/*				int32 count = fUpdateRegion.CountRects();
				for (int32 i = 0; i < count; i++) {
					fView->Invalidate(fUpdateRegion.RectAt(i));
				}*/
				BRect frame = fUpdateRegion.Frame();
				if (frame.IsValid()) {
					fView->Invalidate(frame);
//					fView->Invalidate();
				}
				fUpdateRegion.MakeEmpty();
				fUpdateLock.Unlock();
			} else {
				// see you next time
			}
			break;
		default:
			BWindow::MessageReceived(msg);
			break;
	}
	STRACE("CardWindow::MessageReceived() - exit\n");
}


/**
 * @brief Asks the app_server's main port to shut down rather than quitting
 *        this window directly.
 *
 * @return     Always false; the window is destroyed only when the
 *             app_server tears it down.
 */
bool
CardWindow::QuitRequested()
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


/**
 * @brief Replaces the bitmap shown by the embedded CardView.
 *
 * @param bitmap  New bitmap to display; ownership stays with the caller.
 */
void
CardWindow::SetBitmap(const BBitmap* bitmap)
{
	fView->SetBitmap(bitmap);
}


/**
 * @brief Invalidates a frame on the embedded view, briefly locking the
 *        window if necessary.
 *
 * @param frame  Rectangle to invalidate in view coordinates.
 */
void
CardWindow::Invalidate(const BRect& frame)
{
	if (LockWithTimeout(1000000) >= B_OK) {
		fView->Invalidate(frame);
		Unlock();
	}
}


//	#pragma mark -


/**
 * @brief Constructs the interface with no buffers and a default 640x480
 *        B_RGBA32 desired mode.
 */
ViewHWInterface::ViewHWInterface()
	:
	HWInterface(),
	fBackBuffer(NULL),
	fFrontBuffer(NULL),
	fWindow(NULL)
{
	fDisplayMode.virtual_width = 640;
	fDisplayMode.virtual_height = 480;
	fDisplayMode.space = B_RGBA32;
}


/**
 * @brief Tears down the host window and the embedded BApplication.
 */
ViewHWInterface::~ViewHWInterface()
{
	if (fWindow) {
		fWindow->Lock();
		fWindow->Quit();
	}

	be_app->Lock();
	be_app->Quit();
}


/**
 * @brief Initialises the interface; nothing to do until SetMode() runs.
 *
 * @return     Always B_OK.
 */
status_t
ViewHWInterface::Initialize()
{
	return B_OK;
}


/**
 * @brief Shuts the interface down; the destructor handles teardown.
 *
 * @return     Always B_OK.
 */
status_t
ViewHWInterface::Shutdown()
{
	return B_OK;
}


/**
 * @brief Switches the test interface to the requested display mode.
 *
 * Validates @a mode against the supported list, lazily spins up a
 * BApplication and a CardWindow on first call, allocates fresh BBitmap
 * front and (when double-buffered) back buffers, and rebinds the window's
 * bitmap to the new front buffer.
 *
 * @param mode  Desired width/height/colour-space tuple.
 * @return      B_OK on success, B_BAD_VALUE if @a mode is unsupported,
 *              B_NO_MEMORY on allocation failure, or any error returned
 *              from spawning the embedded BApplication thread.
 */
status_t
ViewHWInterface::SetMode(const display_mode& mode)
{
	AutoWriteLocker _(this);

	status_t ret = B_OK;
	// prevent from doing the unnecessary
	if (fBackBuffer.IsSet() && fFrontBuffer.IsSet()
		&& fDisplayMode.virtual_width == mode.virtual_width
		&& fDisplayMode.virtual_height == mode.virtual_height
		&& fDisplayMode.space == mode.space)
		return ret;

	// check if we support the mode

	display_mode* modes;
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

	BRect frame(0.0, 0.0, fDisplayMode.virtual_width - 1,
		fDisplayMode.virtual_height - 1);

	// create the window if we don't have one already
	if (!fWindow) {
		// if the window has not been created yet, the BApplication
		// has not been created either, but we need one to display
		// a real BWindow in the test environment.
		// be_app->Run() needs to be called in another thread

		if (be_app == NULL) {
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
		}

		fWindow = new CardWindow(frame.OffsetToCopy(BPoint(50.0, 50.0)));

		// fire up the window thread but don't show it on screen yet
		fWindow->Hide();
		fWindow->Show();
	}

	if (fWindow->Lock()) {
		// just to be save
		fWindow->SetBitmap(NULL);

		// free and reallocate the bitmaps while the window is locked,
		// so that the view does not accidentally draw a freed bitmap
		fBackBuffer.Unset();
		fFrontBuffer.Unset();

		// NOTE: backbuffer is always B_RGBA32, this simplifies the
		// drawing backend implementation tremendously for the time
		// being. The color space conversion is handled in CopyBackToFront()

		// TODO: Above not true anymore for single buffered mode!!!
		// -> fall back to double buffer for fDisplayMode.space != B_RGB32
		// as intermediate solution...
		bool doubleBuffered = true;
		if ((color_space)fDisplayMode.space != B_RGB32
			&& (color_space)fDisplayMode.space != B_RGBA32)
			doubleBuffered = true;

		BBitmap* frontBitmap
			= new BBitmap(frame, 0, (color_space)fDisplayMode.space);
		fFrontBuffer.SetTo(new BBitmapBuffer(frontBitmap));

		status_t err = fFrontBuffer->InitCheck();
		if (err < B_OK) {
			fFrontBuffer.Unset();
			ret = err;
		}

		if (err >= B_OK && doubleBuffered) {
			// backbuffer is always B_RGBA32
			// since we override IsDoubleBuffered(), the drawing buffer
			// is in effect also always B_RGBA32.
			BBitmap* backBitmap = new BBitmap(frame, 0, B_RGBA32);
			fBackBuffer.SetTo(new BBitmapBuffer(backBitmap));

			err = fBackBuffer->InitCheck();
			if (err < B_OK) {
				fBackBuffer.Unset();
				ret = err;
			}
		}

		_NotifyFrameBufferChanged();

		if (ret >= B_OK) {
			// clear out buffers, alpha is 255 this way
			// TODO: maybe this should handle different color spaces in different ways
			if (fBackBuffer.IsSet())
				memset(fBackBuffer->Bits(), 255, fBackBuffer->BitsLength());
			memset(fFrontBuffer->Bits(), 255, fFrontBuffer->BitsLength());

			// change the window size and update the bitmap used for drawing
			fWindow->ResizeTo(frame.Width(), frame.Height());
			fWindow->SetBitmap(fFrontBuffer->Bitmap());
		}

		// window is hidden when this function is called the first time
		if (fWindow->IsHidden())
			fWindow->Show();

		fWindow->Unlock();
	} else {
		ret = B_ERROR;
	}
	return ret;
}


/**
 * @brief Copies the currently active display mode into @a mode.
 *
 * @param mode  Destination; may be NULL, in which case the call is a no-op.
 */
void
ViewHWInterface::GetMode(display_mode* mode)
{
	if (mode && ReadLock()) {
		*mode = fDisplayMode;
		ReadUnlock();
	}
}


/**
 * @brief Fills out a synthetic accelerant_device_info for the test driver.
 *
 * @param info  Destination structure; populated only when the read lock can
 *              be taken.
 * @return      Always B_OK.
 */
status_t
ViewHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	// We really don't have to provide anything here because this is strictly
	// a software-only driver, but we'll have some fun, anyway.
	if (ReadLock()) {
		info->version = 100;
		sprintf(info->name, "Haiku, Inc. ViewHWInterface");
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
 * Builds a cross-product of common resolutions and colour spaces and
 * returns the resulting array. The caller takes ownership and frees
 * with delete[].
 *
 * @param _modes  Output pointer receiving the newly allocated array.
 * @param _count  Output count of modes in @a _modes.
 * @return        B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
ViewHWInterface::GetModeList(display_mode** _modes, uint32* _count)
{
	AutoReadLocker _(this);

#if 1
	// setup a whole bunch of different modes
	const struct resolution { int32 width, height; } resolutions[] = {
		{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 960},
		{1280, 1024}, {1400, 1050}, {1600, 1200}
	};
	uint32 resolutionCount = sizeof(resolutions) / sizeof(resolutions[0]);
	const uint32 colors[] = {B_CMAP8, B_RGB15, B_RGB16, B_RGB32};
	uint32 count = resolutionCount * 4;

	display_mode* modes = new(std::nothrow) display_mode[count];
	if (modes == NULL)
		return B_NO_MEMORY;

	*_modes = modes;
	*_count = count;

	int32 index = 0;
	for (uint32 i = 0; i < resolutionCount; i++) {
		for (uint32 c = 0; c < 4; c++) {
			modes[index].virtual_width = resolutions[i].width;
			modes[index].virtual_height = resolutions[i].height;
			modes[index].space = colors[c];

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
	}
#else
	// support only a single mode, useful
	// for testing a specific mode
	display_mode *modes = new(nothrow) display_mode[1];
	modes[0].virtual_width = 640;
	modes[0].virtual_height = 480;
	modes[0].space = B_CMAP8;

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
ViewHWInterface::GetPixelClockLimits(display_mode* mode, uint32* low,
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
ViewHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	return B_ERROR;
}


/**
 * @brief Accepts any proposed mode without modification.
 *
 * @param candidate  Mode the client wants to use (untouched).
 * @param low        Lower bound (ignored).
 * @param high       Upper bound (ignored).
 * @return           Always B_OK.
 */
status_t
ViewHWInterface::ProposeMode(display_mode* candidate, const display_mode* low,
	const display_mode* high)
{
	// We should be able to get away with this because we're not dealing with
	// any specific hardware. This is a Good Thing(TM) because we can support
	// any hardware we wish within reasonable expectaions and programmer
	// laziness. :P
	return B_OK;
}


/**
 * @brief Forwards a DPMS state change to the host BScreen.
 *
 * @param state  Desired DPMS state.
 * @return       The status_t returned by BScreen::SetDPMS().
 */
status_t
ViewHWInterface::SetDPMSMode(uint32 state)
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
ViewHWInterface::DPMSMode()
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
ViewHWInterface::DPMSCapabilities()
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
ViewHWInterface::SetBrightness(float brightness)
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
ViewHWInterface::GetBrightness(float* brightness)
{
	AutoReadLocker _(this);

	return BScreen().GetBrightness(brightness);
}


/**
 * @brief No retrace semaphore is exposed by the test driver.
 *
 * @return     Always -1.
 */
sem_id
ViewHWInterface::RetraceSemaphore()
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
ViewHWInterface::WaitForRetrace(bigtime_t timeout)
{
	// Locking shouldn't be necessary here - R5 should handle this for us. :)
	BScreen screen;
	return screen.WaitForRetrace(timeout);
}


/**
 * @brief Returns the front BBitmap-backed RenderingBuffer.
 *
 * @return     RenderingBuffer wrapping the visible bitmap, or NULL if no
 *             mode has been set yet.
 */
RenderingBuffer*
ViewHWInterface::FrontBuffer() const
{
	return fFrontBuffer.Get();
}


/**
 * @brief Returns the back BBitmap-backed RenderingBuffer.
 *
 * @return     RenderingBuffer wrapping the off-screen B_RGBA32 bitmap, or
 *             NULL when running single-buffered.
 */
RenderingBuffer*
ViewHWInterface::BackBuffer() const
{
	return fBackBuffer.Get();
}


/**
 * @brief Reports whether double buffering is active.
 *
 * @return     true when both front and back buffers exist; false if only
 *             the front buffer is allocated or no mode has been set.
 */
bool
ViewHWInterface::IsDoubleBuffered() const
{
	if (fFrontBuffer.IsSet())
		return fBackBuffer.IsSet();

	return false;
}


/**
 * @brief Marks @a frame dirty and, in single-buffered mode, asks the host
 *        window to repaint it immediately.
 *
 * @param frame  Rectangle to invalidate in screen coordinates.
 * @return       The status_t returned by HWInterface::Invalidate().
 */
status_t
ViewHWInterface::Invalidate(const BRect& frame)
{
	status_t ret = HWInterface::Invalidate(frame);

	if (ret >= B_OK && fWindow && !IsDoubleBuffered())
		fWindow->Invalidate(frame);
	return ret;
}


/**
 * @brief Composites the back buffer into the front buffer and asks the host
 *        window to redraw the changed region.
 *
 * @param frame  Rectangle to copy in screen coordinates.
 * @return       The status_t returned by HWInterface::CopyBackToFront().
 */
status_t
ViewHWInterface::CopyBackToFront(const BRect& frame)
{
	status_t ret = HWInterface::CopyBackToFront(frame);

	if (ret >= B_OK && fWindow)
		fWindow->Invalidate(frame);
	return ret;
}
