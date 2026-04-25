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
 *   Copyright 2009, Haiku, Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Lotz <mmlr@mlotz.ch>
 */


/**
 * @file RemoteHWInterface.cpp
 * @brief HWInterface that streams app_server output to a remote viewer.
 *
 * Listens on a configurable TCP port; on connect it spawns a NetSender to
 * push outbound RemoteMessage bytes from a ring buffer over the socket and
 * uses a NetReceiver to feed inbound bytes into a parallel ring. A
 * dedicated event thread decodes inbound RP_* messages: input goes to a
 * RemoteEventStream that fronts the EventDispatcher, while replies to
 * outstanding queries are routed via per-token callbacks registered by
 * RemoteDrawingEngine. SetCursor / Invalidate / SetMode etc. are
 * forwarded as RemoteMessage frames; FrontBuffer/BackBuffer return NULL
 * because the actual pixels live on the viewer.
 */


#include "RemoteHWInterface.h"
#include "RemoteDrawingEngine.h"
#include "RemoteEventStream.h"
#include "RemoteMessage.h"

#include "NetReceiver.h"
#include "NetSender.h"
#include "StreamingRingBuffer.h"

#include "SystemPalette.h"

#include <Autolock.h>
#include <NetEndpoint.h>

#include <new>
#include <string.h>


#define TRACE(x...)				/*debug_printf("RemoteHWInterface: " x)*/
#define TRACE_ALWAYS(x...)		debug_printf("RemoteHWInterface: " x)
#define TRACE_ERROR(x...)		debug_printf("RemoteHWInterface: " x)


/** @brief Token-keyed reply callback registration kept in a sorted list. */
struct callback_info {
	/** @brief Match key chosen by the caller (typically a sequence number). */
	uint32				token;
	/** @brief Function invoked when a message tagged with @c token arrives. */
	RemoteHWInterface::CallbackFunction	callback;
	/** @brief Opaque cookie forwarded into the callback. */
	void*				cookie;
};


/**
 * @brief Builds the listener, ring buffers, receiver, and event thread.
 *
 * Parses @a target as a port number, binds a listening BNetEndpoint to it,
 * allocates two 16 KiB ring buffers (one outbound, one inbound), constructs
 * the NetReceiver in listening mode (its connection callback installs the
 * NetSender once a viewer connects), and spawns the event-decode thread.
 * fInitStatus records the first failure encountered; Initialize() simply
 * returns it.
 *
 * @param target  Decimal listening port, parsed via sscanf.
 */
RemoteHWInterface::RemoteHWInterface(const char* target)
	:
	HWInterface(),
	fTarget(target),
	fIsConnected(false),
	fProtocolVersion(100),
	fConnectionSpeed(0),
	fListenPort(10901),
	fListenEndpoint(NULL),
	fSendBuffer(NULL),
	fReceiveBuffer(NULL),
	fSender(NULL),
	fReceiver(NULL),
	fEventThread(-1),
	fEventStream(NULL),
	fCallbackLocker("callback locker")
{
	memset(&fFallbackMode, 0, sizeof(fFallbackMode));
	fFallbackMode.virtual_width = 640;
	fFallbackMode.virtual_height = 480;
	fFallbackMode.space = B_RGB32;
	_FillDisplayModeTiming(fFallbackMode);

	fCurrentMode = fClientMode = fFallbackMode;

	if (sscanf(fTarget, "%" B_SCNu16, &fListenPort) != 1) {
		fInitStatus = B_BAD_VALUE;
		return;
	}

	fListenEndpoint.SetTo(new(std::nothrow) BNetEndpoint());
	if (!fListenEndpoint.IsSet()) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	fInitStatus = fListenEndpoint->Bind(fListenPort);
	if (fInitStatus != B_OK)
		return;

	fSendBuffer.SetTo(new(std::nothrow) StreamingRingBuffer(16 * 1024));
	if (!fSendBuffer.IsSet()) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	fInitStatus = fSendBuffer->InitCheck();
	if (fInitStatus != B_OK)
		return;

	fReceiveBuffer.SetTo(new(std::nothrow) StreamingRingBuffer(16 * 1024));
	if (!fReceiveBuffer.IsSet()) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	fInitStatus = fReceiveBuffer->InitCheck();
	if (fInitStatus != B_OK)
		return;

	fReceiver.SetTo(new(std::nothrow) NetReceiver(fListenEndpoint.Get(), fReceiveBuffer.Get(),
		_NewConnectionCallback, this));
	if (!fReceiver.IsSet()) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	fEventStream.SetTo(new(std::nothrow) RemoteEventStream());
	if (!fEventStream.IsSet()) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	fEventThread = spawn_thread(_EventThreadEntry, "remote event thread",
		B_NORMAL_PRIORITY, this);
	if (fEventThread < 0) {
		fInitStatus = fEventThread;
		return;
	}

	resume_thread(fEventThread);
}


/**
 * @brief Tears down the receiver, ring buffers, sender, listener, and
 *        event stream.
 *
 * @todo  Audit the destruction order; the original Haiku code carries a
 *        TODO for the same reason.
 */
RemoteHWInterface::~RemoteHWInterface()
{
	//TODO: check order
	fReceiver.Unset();
	fReceiveBuffer.Unset();

	fSendBuffer.Unset();
	fSender.Unset();

	fListenEndpoint.Unset();

	fEventStream.Unset();
}


/**
 * @brief Returns the cached construction status; nothing extra to do here.
 *
 * @return     B_OK when construction succeeded, otherwise the first error
 *             encountered.
 */
status_t
RemoteHWInterface::Initialize()
{
	return fInitStatus;
}


/**
 * @brief Closes the listener and notifies the viewer that we are
 *        disconnecting.
 *
 * @return     Always B_OK.
 */
status_t
RemoteHWInterface::Shutdown()
{
	_Disconnect();
	return B_OK;
}


/**
 * @brief Creates a fresh RemoteDrawingEngine bound to this interface.
 *
 * @return     Newly allocated drawing engine, or NULL on allocation failure.
 */
DrawingEngine*
RemoteHWInterface::CreateDrawingEngine()
{
	return new(std::nothrow) RemoteDrawingEngine(this);
}


/**
 * @brief Returns the shared RemoteEventStream that injects remote input
 *        events into the EventDispatcher.
 *
 * @return     EventStream owned by this interface; the caller must not free
 *             it.
 */
EventStream*
RemoteHWInterface::CreateEventStream()
{
	return fEventStream.Get();
}


/**
 * @brief Registers a token-keyed reply callback.
 *
 * @param token     Caller-chosen identifier; must be unique within the
 *                  callback table.
 * @param callback  Function to invoke when a message tagged with @a token
 *                  is decoded.
 * @param cookie    Opaque pointer forwarded into @a callback.
 * @return          B_OK on success, B_NAME_IN_USE if @a token is already
 *                  registered, or B_NO_MEMORY on allocation failure.
 */
status_t
RemoteHWInterface::AddCallback(uint32 token, CallbackFunction callback,
	void* cookie)
{
	BAutolock lock(fCallbackLocker);
	int32 index = fCallbacks.BinarySearchIndexByKey(token, &_CallbackCompare);
	if (index >= 0)
		return B_NAME_IN_USE;

	callback_info* info = new(std::nothrow) callback_info;
	if (info == NULL)
		return B_NO_MEMORY;

	info->token = token;
	info->callback = callback;
	info->cookie = cookie;

	fCallbacks.AddItem(info, -index - 1);
	return B_OK;
}


/**
 * @brief Removes a previously registered reply callback.
 *
 * @param token  Identifier passed to AddCallback().
 * @return       true if a callback was removed, false if @a token was not
 *               registered.
 */
bool
RemoteHWInterface::RemoveCallback(uint32 token)
{
	BAutolock lock(fCallbackLocker);
	int32 index = fCallbacks.BinarySearchIndexByKey(token, &_CallbackCompare);
	if (index < 0)
		return false;

	delete fCallbacks.RemoveItemAt(index);
	return true;
}


/**
 * @brief Looks up the callback descriptor for @a token under the lock.
 *
 * @param token  Identifier registered via AddCallback().
 * @return       Pointer to the descriptor, or NULL if not present.
 */
callback_info*
RemoteHWInterface::_FindCallback(uint32 token)
{
	BAutolock lock(fCallbackLocker);
	return fCallbacks.BinarySearchByKey(token, &_CallbackCompare);
}


/**
 * @brief BinarySearch comparator that orders descriptors by their token.
 *
 * @param key   Search key.
 * @param info  Candidate descriptor.
 * @return      0 if equal, -1 if @a info precedes @a key, +1 otherwise.
 */
int
RemoteHWInterface::_CallbackCompare(const uint32* key,
	const callback_info* info)
{
	if (info->token == *key)
		return 0;

	if (info->token < *key)
		return -1;

	return 1;
}


/**
 * @brief Static thread trampoline for the event-decode loop.
 *
 * @param data  Pointer to the owning RemoteHWInterface.
 * @return      The status_t result of _EventThread().
 */
int32
RemoteHWInterface::_EventThreadEntry(void* data)
{
	return ((RemoteHWInterface*)data)->_EventThread();
}


/**
 * @brief Decodes inbound RemoteMessage frames and dispatches them.
 *
 * RP_INIT_CONNECTION triggers the handshake reply (initial cursor and
 * cursor position). RP_UPDATE_DISPLAY_MODE updates fClientMode, marks the
 * link as connected, and notifies listeners. RP_GET_SYSTEM_PALETTE serves
 * the current SystemColorMap. Mouse / keyboard codes are forwarded to the
 * RemoteEventStream. Unrecognised codes are tried against the registered
 * reply callbacks via the leading token.
 *
 * @return     The error code that caused the loop to exit (the function
 *             only returns on protocol-level failure).
 */
status_t
RemoteHWInterface::_EventThread()
{
	RemoteMessage message(fReceiveBuffer.Get(), NULL);
	while (true) {
		uint16 code;
		status_t result = message.NextMessage(code);
		if (result != B_OK) {
			TRACE_ERROR("failed to read message from receiver: %s\n",
				strerror(result));
			return result;
		}

		TRACE("got message code %" B_PRIu16 " with %" B_PRIu32 " bytes\n", code,
			message.DataLeft());

		if (code >= RP_MOUSE_MOVED && code <= RP_MODIFIERS_CHANGED) {
			// an input event, dispatch to the event stream
			if (fEventStream->EventReceived(message))
				continue;
		}

		switch (code) {
			case RP_INIT_CONNECTION:
			{
				RemoteMessage reply(NULL, fSendBuffer.Get());
				reply.Start(RP_INIT_CONNECTION);
				status_t result = reply.Flush();
				(void)result;
				TRACE("init connection result: %s\n", strerror(result));
				reply.Start(RP_SET_CURSOR);
				reply.AddCursor(CursorAndDragBitmap().Get());
				result = reply.Flush();
				TRACE("init connection set cursor result: %s\n", strerror(result));
				reply.Start(RP_SET_CURSOR_VISIBLE);
				reply.Add(fCursorVisible);
				result = reply.Flush();
				TRACE("init connection set cursor visible result: %s\n", strerror(result));
				BPoint position = CursorPosition();
				reply.Start(RP_MOVE_CURSOR_TO);
				reply.Add(position.x);
				reply.Add(position.y);
				result = reply.Flush();
				TRACE("init connection set cursor position result: %s\n", strerror(result));
				break;
			}

			case RP_UPDATE_DISPLAY_MODE:
			{
				int32 width, height;
				message.Read(width);
				result = message.Read(height);
				if (result != B_OK) {
					TRACE_ERROR("failed to read display mode\n");
					break;
				}

				fIsConnected = true;
				fClientMode.virtual_width = width;
				fClientMode.virtual_height = height;
				_FillDisplayModeTiming(fClientMode);
				_NotifyScreenChanged();
				break;
			}

			case RP_GET_SYSTEM_PALETTE:
			{
				RemoteMessage reply(NULL, fSendBuffer.Get());
				reply.Start(RP_GET_SYSTEM_PALETTE_RESULT);

				const color_map *map = SystemColorMap();
				uint32 count = (uint32)B_COUNT_OF(map->color_list);

				reply.Add(count);
				for (size_t i = 0; i < count; i++) {
					const rgb_color &color = map->color_list[i];
					reply.Add(color.red);
					reply.Add(color.green);
					reply.Add(color.blue);
					reply.Add(color.alpha);
				}

				break;
			}

			default:
			{
				uint32 token;
				if (message.Read(token) == B_OK) {
					callback_info* info = _FindCallback(token);
					if (info != NULL && info->callback(info->cookie, message))
						break;
				}

				TRACE_ERROR("unhandled remote event code %u\n", code);
				break;
			}
		}
	}
}


/**
 * @brief Static trampoline used as NetReceiver's NewConnectionCallback.
 *
 * @param cookie    Pointer to the owning RemoteHWInterface.
 * @param endpoint  Newly accepted client endpoint.
 * @return          B_OK to accept the connection, otherwise an error code.
 */
status_t
RemoteHWInterface::_NewConnectionCallback(void *cookie, BNetEndpoint &endpoint)
{
	return ((RemoteHWInterface *)cookie)->_NewConnection(endpoint);
}


/**
 * @brief Resets the send pipeline for a freshly accepted viewer connection.
 *
 * Clears the previous sender (if any), drops stale outbound bytes from the
 * send ring, then constructs a NetSender that owns a clone of @a endpoint
 * and feeds from fSendBuffer.
 *
 * @param endpoint  Newly accepted client endpoint; cloned by the sender.
 * @return          B_OK on success, B_NO_MEMORY if either allocation fails.
 */
status_t
RemoteHWInterface::_NewConnection(BNetEndpoint &endpoint)
{
	fSender.Unset();

	fSendBuffer->MakeEmpty();

	BNetEndpoint *sendEndpoint = new(std::nothrow) BNetEndpoint(endpoint);
	if (sendEndpoint == NULL)
		return B_NO_MEMORY;

	fSender.SetTo(new(std::nothrow) NetSender(sendEndpoint, fSendBuffer.Get()));
	if (!fSender.IsSet()) {
		delete sendEndpoint;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Sends an RP_CLOSE_CONNECTION to the viewer (when connected) and
 *        closes the listener socket.
 */
void
RemoteHWInterface::_Disconnect()
{
	if (fIsConnected) {
		RemoteMessage message(NULL, fSendBuffer.Get());
		message.Start(RP_CLOSE_CONNECTION);
		message.Flush();
		fIsConnected = false;
	}

	if (fListenEndpoint.IsSet())
		fListenEndpoint->Close();
}


/**
 * @brief Records a new desired mode locally; the viewer drives the actual
 *        screen geometry over the wire.
 *
 * @param mode  Mode requested by the client.
 * @return      Always B_OK.
 */
status_t
RemoteHWInterface::SetMode(const display_mode& mode)
{
	TRACE("set mode: %" B_PRIu16 " %" B_PRIu16 "\n", mode.virtual_width,
		mode.virtual_height);
	fCurrentMode = mode;
	return B_OK;
}


/**
 * @brief Copies the currently active mode into @a mode under the read lock.
 *
 * @param mode  Destination; if NULL or the lock cannot be taken the call
 *              is a no-op.
 */
void
RemoteHWInterface::GetMode(display_mode* mode)
{
	if (mode == NULL || !ReadLock())
		return;

	*mode = fCurrentMode;
	ReadUnlock();

	TRACE("get mode: %" B_PRIu16 " %" B_PRIu16 "\n", mode->virtual_width,
		mode->virtual_height);
}


/**
 * @brief Reports the viewer-preferred display mode (width/height pinned by
 *        the connected client).
 *
 * @param mode  Destination; populated with the cached client mode.
 * @return      Always B_OK.
 */
status_t
RemoteHWInterface::GetPreferredMode(display_mode* mode)
{
	*mode = fClientMode;
	return B_OK;
}


/**
 * @brief Fills out a synthetic accelerant_device_info describing the link.
 *
 * @param info  Destination; populated only when the read lock can be taken.
 * @return      B_OK on success, B_ERROR if the lock cannot be acquired.
 */
status_t
RemoteHWInterface::GetDeviceInfo(accelerant_device_info* info)
{
	if (!ReadLock())
		return B_ERROR;

	info->version = fProtocolVersion;
	info->dac_speed = fConnectionSpeed;
	info->memory = 33554432; // 32MB
	strlcpy(info->name, "Haiku, Inc. RemoteHWInterface", sizeof(info->name));
	strlcpy(info->chipset, "Haiku, Inc. Chipset", sizeof(info->chipset));
	strlcpy(info->serial_no, fTarget, sizeof(info->serial_no));

	ReadUnlock();
	return B_OK;
}


/**
 * @brief Returns the (very small) list of supported modes: the built-in
 *        fallback and the viewer's currently advertised mode.
 *
 * @param _modes  Output, newly allocated array of length 2; caller frees
 *                with delete[].
 * @param _count  Output count, always 2 on success.
 * @return        B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
RemoteHWInterface::GetModeList(display_mode** _modes, uint32* _count)
{
	AutoReadLocker _(this);

	display_mode* modes = new(std::nothrow) display_mode[2];
	if (modes == NULL)
		return B_NO_MEMORY;

	modes[0] = fFallbackMode;
	modes[1] = fClientMode;
	*_modes = modes;
	*_count = 2;

	return B_OK;
}


/**
 * @brief Pixel-clock limits are meaningless for a network-attached display.
 *
 * @return     Always B_UNSUPPORTED.
 */
status_t
RemoteHWInterface::GetPixelClockLimits(display_mode* mode, uint32* low,
	uint32* high)
{
	TRACE("get pixel clock limits unsupported\n");
	return B_UNSUPPORTED;
}


/**
 * @brief Timing constraints are meaningless for a network-attached display.
 *
 * @return     Always B_UNSUPPORTED.
 */
status_t
RemoteHWInterface::GetTimingConstraints(display_timing_constraints* constraints)
{
	TRACE("get timing constraints unsupported\n");
	return B_UNSUPPORTED;
}


/**
 * @brief Accepts any proposed mode without modification.
 *
 * @param candidate  Candidate mode (untouched).
 * @param low        Lower bound (ignored).
 * @param high       Upper bound (ignored).
 * @return           Always B_OK.
 */
status_t
RemoteHWInterface::ProposeMode(display_mode* candidate, const display_mode* low,
	const display_mode* high)
{
	TRACE("propose mode: %" B_PRIu16 " %" B_PRIu16 "\n",
		candidate->virtual_width, candidate->virtual_height);
	return B_OK;
}


/**
 * @brief DPMS state is set by the viewer; this side cannot influence it.
 *
 * @return     Always B_UNSUPPORTED.
 */
status_t
RemoteHWInterface::SetDPMSMode(uint32 state)
{
	return B_UNSUPPORTED;
}


/**
 * @brief DPMS state is opaque on this side of the link.
 *
 * @return     Always B_UNSUPPORTED.
 */
uint32
RemoteHWInterface::DPMSMode()
{
	return B_UNSUPPORTED;
}


/**
 * @brief DPMS capabilities are not modelled for the remote link.
 *
 * @return     Always 0.
 */
uint32
RemoteHWInterface::DPMSCapabilities()
{
	return 0;
}


/**
 * @brief Brightness is set by the viewer, not by the remote app_server.
 *
 * @return     Always B_UNSUPPORTED.
 */
status_t
RemoteHWInterface::SetBrightness(float)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Brightness is opaque on this side of the link.
 *
 * @return     Always B_UNSUPPORTED.
 */
status_t
RemoteHWInterface::GetBrightness(float*)
{
	return B_UNSUPPORTED;
}


/**
 * @brief No vertical retrace semaphore is meaningful for a remote display.
 *
 * @return     Always -1.
 */
sem_id
RemoteHWInterface::RetraceSemaphore()
{
	return -1;
}


/**
 * @brief Waiting for retrace is unsupported on the remote link.
 *
 * @return     Always B_UNSUPPORTED.
 */
status_t
RemoteHWInterface::WaitForRetrace(bigtime_t timeout)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Updates the local cursor and forwards the new cursor bitmap to
 *        the viewer.
 *
 * @param cursor  New cursor; ownership transfers to the base implementation.
 */
void
RemoteHWInterface::SetCursor(ServerCursor* cursor)
{
	HWInterface::SetCursor(cursor);
	RemoteMessage message(NULL, fSendBuffer.Get());
	message.Start(RP_SET_CURSOR);
	message.AddCursor(CursorAndDragBitmap().Get());
}


/**
 * @brief Updates local cursor visibility and notifies the viewer.
 *
 * @param visible  true to show the cursor on the remote screen, false to
 *                 hide it.
 */
void
RemoteHWInterface::SetCursorVisible(bool visible)
{
	HWInterface::SetCursorVisible(visible);
	RemoteMessage message(NULL, fSendBuffer.Get());
	message.Start(RP_SET_CURSOR_VISIBLE);
	message.Add(visible);
}


/**
 * @brief Moves the local cursor and tells the viewer to do the same.
 *
 * @param x  Target X in screen pixels.
 * @param y  Target Y in screen pixels.
 */
void
RemoteHWInterface::MoveCursorTo(float x, float y)
{
	HWInterface::MoveCursorTo(x, y);
	RemoteMessage message(NULL, fSendBuffer.Get());
	message.Start(RP_MOVE_CURSOR_TO);
	message.Add(x);
	message.Add(y);
}


/**
 * @brief Updates the drag bitmap composited onto the cursor and forwards
 *        the combined cursor image to the viewer.
 *
 * @param bitmap            Drag preview bitmap; may be NULL to clear.
 * @param offsetFromCursor  Hotspot offset from the cursor origin.
 */
void
RemoteHWInterface::SetDragBitmap(const ServerBitmap* bitmap,
	const BPoint& offsetFromCursor)
{
	HWInterface::SetDragBitmap(bitmap, offsetFromCursor);
	RemoteMessage message(NULL, fSendBuffer.Get());
	message.Start(RP_SET_CURSOR);
	message.AddCursor(CursorAndDragBitmap().Get());
}


/**
 * @brief No local front buffer exists; pixels live on the viewer.
 *
 * @return     Always NULL.
 */
RenderingBuffer*
RemoteHWInterface::FrontBuffer() const
{
	return NULL;
}


/**
 * @brief No local back buffer exists; pixels live on the viewer.
 *
 * @return     Always NULL.
 */
RenderingBuffer*
RemoteHWInterface::BackBuffer() const
{
	return NULL;
}


/**
 * @brief The remote driver has no local buffers, so it is not double
 *        buffered.
 *
 * @return     Always false.
 */
bool
RemoteHWInterface::IsDoubleBuffered() const
{
	return false;
}


/**
 * @brief Sends an RP_INVALIDATE_REGION to the viewer.
 *
 * @param region  Region to invalidate in screen coordinates.
 * @return        Always B_OK; transmission errors surface on the send ring.
 */
status_t
RemoteHWInterface::InvalidateRegion(const BRegion& region)
{
	RemoteMessage message(NULL, fSendBuffer.Get());
	message.Start(RP_INVALIDATE_REGION);
	message.AddRegion(region);
	return B_OK;
}


/**
 * @brief Sends an RP_INVALIDATE_RECT to the viewer.
 *
 * @param frame  Rectangle to invalidate in screen coordinates.
 * @return       Always B_OK; transmission errors surface on the send ring.
 */
status_t
RemoteHWInterface::Invalidate(const BRect& frame)
{
	RemoteMessage message(NULL, fSendBuffer.Get());
	message.Start(RP_INVALIDATE_RECT);
	message.Add(frame);
	return B_OK;
}


/**
 * @brief No-op: the viewer composites whatever it likes from incoming
 *        drawing operations.
 *
 * @return     Always B_OK.
 */
status_t
RemoteHWInterface::CopyBackToFront(const BRect& frame)
{
	return B_OK;
}


/**
 * @brief Computes a synthetic VESA-style timing for @a mode based purely on
 *        its virtual dimensions.
 *
 * Used to populate fFallbackMode and any RP_UPDATE_DISPLAY_MODE we accept;
 * the timing fields are not used by the remote viewer but are required by
 * the display_mode contract.
 *
 * @param mode  Mode whose timing block is filled in.
 */
void
RemoteHWInterface::_FillDisplayModeTiming(display_mode &mode)
{
	mode.timing.pixel_clock
		= (uint64_t)mode.virtual_width * mode.virtual_height * 60 / 1000;
	mode.timing.h_display = mode.timing.h_sync_start = mode.timing.h_sync_end
		= mode.timing.h_total = mode.virtual_width;
	mode.timing.v_display = mode.timing.v_sync_start = mode.timing.v_sync_end
		= mode.timing.v_total = mode.virtual_height;
}
