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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Haiku, Inc.
 * Original author: Michael Lotz.
 */

/** @file RemoteHWInterface.h
    @brief HWInterface implementation that streams the screen to a network
           viewer instead of driving local hardware. */

#ifndef REMOTE_HW_INTERFACE_H
#define REMOTE_HW_INTERFACE_H

#include "HWInterface.h"

#include <AutoDeleter.h>
#include <Locker.h>
#include <ObjectList.h>

class BNetEndpoint;
class StreamingRingBuffer;
class NetSender;
class NetReceiver;
class RemoteEventStream;
class RemoteMessage;

struct callback_info;


/** @brief HWInterface that listens on a TCP port for a remote viewer to
           connect, then forwards every drawing operation as RP_*
           RemoteMessage frames and replays the viewer's input back into
           the EventDispatcher via RemoteEventStream. */
class RemoteHWInterface : public HWInterface {
public:
									RemoteHWInterface(const char* target);
virtual								~RemoteHWInterface();

virtual	status_t					Initialize();
virtual	status_t					Shutdown();

virtual	DrawingEngine*				CreateDrawingEngine();
virtual	EventStream*				CreateEventStream();

virtual	status_t					SetMode(const display_mode& mode);
virtual	void						GetMode(display_mode* mode);
virtual	status_t					GetPreferredMode(display_mode* mode);

virtual status_t					GetDeviceInfo(accelerant_device_info* info);

virtual status_t					GetModeList(display_mode** _modeList,
										uint32* _count);
virtual status_t					GetPixelClockLimits(display_mode* mode,
										uint32* _low, uint32* _high);
virtual status_t					GetTimingConstraints(
										display_timing_constraints* constraints);
virtual status_t					ProposeMode(display_mode* candidate,
										const display_mode* low,
										const display_mode* high);

virtual sem_id						RetraceSemaphore();
virtual status_t					WaitForRetrace(
										bigtime_t timeout = B_INFINITE_TIMEOUT);

virtual status_t					SetDPMSMode(uint32 state);
virtual uint32						DPMSMode();
virtual uint32						DPMSCapabilities();

virtual status_t			SetBrightness(float);
virtual status_t			GetBrightness(float*);

		// cursor handling
virtual	void						SetCursor(ServerCursor* cursor);
virtual	void						SetCursorVisible(bool visible);
virtual	void						MoveCursorTo(float x, float y);
virtual	void						SetDragBitmap(const ServerBitmap* bitmap,
										const BPoint& offsetFormCursor);

		// frame buffer access
virtual	RenderingBuffer*			FrontBuffer() const;
virtual	RenderingBuffer*			BackBuffer() const;
virtual	bool						IsDoubleBuffered() const;

virtual	status_t					InvalidateRegion(const BRegion& region);
virtual	status_t					Invalidate(const BRect& frame);
virtual	status_t					CopyBackToFront(const BRect& frame);

		// drawing engine interface
		/** @brief Returns the inbound ring used by the protocol decoder
		           thread, primarily for the drawing engine's reply
		           callbacks. */
		StreamingRingBuffer*		ReceiveBuffer()
										{ return fReceiveBuffer.Get(); }
		/** @brief Returns the outbound ring fed by RemoteMessage encoders;
		           the NetSender drains it onto the wire. */
		StreamingRingBuffer*		SendBuffer() { return fSendBuffer.Get(); }

/** @brief Reply-callback signature: invoked when a message tagged with a
           registered token arrives.
    @param cookie   Opaque value supplied to AddCallback().
    @param message  Decoded RemoteMessage positioned past the token.
    @return true if the callback consumed the message. */
typedef bool (*CallbackFunction)(void* cookie, RemoteMessage& message);

		status_t					AddCallback(uint32 token,
										CallbackFunction callback,
										void* cookie);
		bool						RemoveCallback(uint32 token);

private:
		callback_info*				_FindCallback(uint32 token);
static	int							_CallbackCompare(const uint32* key,
										const callback_info* info);

static	int32						_EventThreadEntry(void* data);
		status_t					_EventThread();

static	status_t					_NewConnectionCallback(void *cookie,
										BNetEndpoint &endpoint);
		status_t					_NewConnection(BNetEndpoint &endpoint);

		void						_Disconnect();

		void						_FillDisplayModeTiming(display_mode &mode);

		const char*					fTarget;
		status_t					fInitStatus;
		bool						fIsConnected;
		uint32						fProtocolVersion;
		uint32						fConnectionSpeed;
		display_mode				fFallbackMode;
		display_mode				fCurrentMode;
		display_mode				fClientMode;
		uint16						fListenPort;

		ObjectDeleter<BNetEndpoint>	fListenEndpoint;
		ObjectDeleter<StreamingRingBuffer>
									fSendBuffer;
		ObjectDeleter<StreamingRingBuffer>
									fReceiveBuffer;

		ObjectDeleter<NetSender>	fSender;
		ObjectDeleter<NetReceiver>	fReceiver;

		thread_id					fEventThread;
		ObjectDeleter<RemoteEventStream>
									fEventStream;

		BLocker						fCallbackLocker;
		BObjectList<callback_info>	fCallbacks;
};

#endif // REMOTE_HW_INTERFACE_H
