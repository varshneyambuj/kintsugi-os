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
 *   Copyright 2001-2013 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file InputServer.h
 *  @brief Top-level input server: device, filter, and method orchestration. */

#ifndef INPUT_SERVER_APP_H
#define INPUT_SERVER_APP_H


#include <stdlib.h>
#include <string.h>
#include <unistd.h>

//#define DEBUG 1

#include <Application.h>
#include <Debug.h>
#include <FindDirectory.h>
#include <InputServerDevice.h>
#include <InputServerFilter.h>
#include <InputServerMethod.h>
#include <InterfaceDefs.h>
#include <Locker.h>
#include <Message.h>
#include <ObjectList.h>
#include <OS.h>
#include <Screen.h>
#include <StringList.h>
#include <SupportDefs.h>

#include <shared_cursor_area.h>

#include "AddOnManager.h"
#include "KeyboardSettings.h"
#include "MouseSettings.h"
#include "PathList.h"


/** @brief MIME signature the input server registers under. */
#define INPUTSERVER_SIGNATURE "application/x-vnd.Be-input_server"
	// use this when target should replace R5 input_server

/** @brief Owning list of pending input event BMessages. */
typedef BObjectList<BMessage> EventList;

class BottomlineWindow;

/** @brief One registered (device, server-device) pair tracked by the input server. */
class InputDeviceListItem {
	public:
		InputDeviceListItem(BInputServerDevice& serverDevice,
			const input_device_ref& device);
		~InputDeviceListItem();

		/** @brief Asks the underlying server device to start producing events. */
		void Start();
		/** @brief Asks the underlying server device to stop producing events. */
		void Stop();
		/** @brief Forwards a control message to the underlying server device. */
		void Control(uint32 code, BMessage* message);

		/** @brief Returns the device's display name. */
		const char* Name() const { return fDevice.name; }
		/** @brief Returns the device's input type (keyboard, pointing, …). */
		input_device_type Type() const { return fDevice.type; }
		/** @brief Returns true if the device is currently running. */
		bool Running() const { return fRunning; }

		/** @brief Returns true if the device's name equals @p name. */
		bool HasName(const char* name) const;
		/** @brief Returns true if the device's type equals @p type. */
		bool HasType(input_device_type type) const;
		/** @brief Returns true if @p name and @p type both match this device. */
		bool Matches(const char* name, input_device_type type) const;

		/** @brief Returns the BInputServerDevice this entry wraps. */
		BInputServerDevice* ServerDevice() { return fServerDevice; }

	private:
		BInputServerDevice* fServerDevice;  /**< The wrapped add-on instance (not owned). */
		input_device_ref   	fDevice;        /**< Device descriptor (name + type). */
		bool 				fRunning;       /**< Whether Start() has been called. */
};

namespace BPrivate {

/** @brief Records the device paths an input device add-on is monitoring. */
class DeviceAddOn {
public:
								DeviceAddOn(BInputServerDevice* device);
								~DeviceAddOn();

	/** @brief Returns true if @p path is currently monitored by this add-on. */
			bool				HasPath(const char* path) const;
	/** @brief Adds @p path to the monitored set. */
			status_t			AddPath(const char* path);
	/** @brief Removes @p path from the monitored set. */
			status_t			RemovePath(const char* path);
	/** @brief Returns the number of paths currently monitored. */
			int32				CountPaths() const;

	/** @brief Returns the underlying BInputServerDevice (not owned). */
			BInputServerDevice*	Device() { return fDevice; }

private:
			BInputServerDevice*	fDevice;          /**< The owning input server device add-on. */
			PathList			fMonitoredPaths;  /**< Paths currently being watched on its behalf. */
};

}	// namespace BPrivate

/** @brief Internal helper that backs BInputServerMethod's user-visible API. */
class _BMethodAddOn_ {
	public:
		_BMethodAddOn_(BInputServerMethod *method, const char* name,
			const uchar* icon);
		~_BMethodAddOn_();

		/** @brief Updates the displayed method name. */
		status_t SetName(const char* name);
		/** @brief Updates the displayed method icon. */
		status_t SetIcon(const uchar* icon);
		/** @brief Sets the pop-up menu the replicant should show for this method. */
		status_t SetMenu(const BMenu* menu, const BMessenger& messenger);
		/** @brief Notifies the method add-on of activation/deactivation. */
		status_t MethodActivated(bool activate);
		/** @brief Registers the method with the input server. */
		status_t AddMethod();
		/** @brief Returns the cookie used by the deskbar replicant to refer to this method. */
		int32 Cookie() { return fCookie; }

	private:
		BInputServerMethod* fMethod;        /**< The user-visible method instance (not owned). */
		char* fName;                        /**< Cached method name. */
		uchar fIcon[16*16*1];               /**< Cached 16×16 indexed icon. */
		const BMenu* fMenu;                 /**< Optional pop-up menu shown for this method. */
		BMessenger fMessenger;              /**< Messenger that receives menu invocation messages. */
		int32 fCookie;                      /**< Identifier shared with the deskbar replicant. */
};

/** @brief Built-in input method that always provides the system keymap. */
class KeymapMethod : public BInputServerMethod {
	public:
		KeymapMethod();
		~KeymapMethod();
};

/** @brief Top-level input server BApplication.
 *
 * Owns the device list, the active input method, the input filter chain,
 * the keyboard/mouse settings, and the event loop that pulls events from
 * devices and ships them to app_server. */
class InputServer : public BApplication {
	public:
		InputServer();
		virtual ~InputServer();

		virtual void ArgvReceived(int32 argc, char** argv);

		virtual bool QuitRequested();
		virtual void ReadyToRun();
		virtual void MessageReceived(BMessage* message);

		void HandleSetMethod(BMessage* message);
		status_t HandleGetSetMouseType(BMessage* message, BMessage* reply);
		status_t HandleGetSetMouseAcceleration(BMessage* message, BMessage* reply);
		status_t HandleGetSetKeyRepeatDelay(BMessage* message, BMessage* reply);
		status_t HandleGetKeyInfo(BMessage* message, BMessage* reply);
		status_t HandleGetModifiers(BMessage* message, BMessage* reply);
		status_t HandleGetModifierKey(BMessage* message, BMessage* reply);
		status_t HandleSetModifierKey(BMessage* message, BMessage* reply);
		status_t HandleSetKeyboardLocks(BMessage* message, BMessage* reply);
		status_t HandleGetSetMouseSpeed(BMessage* message, BMessage* reply);
		status_t HandleSetMousePosition(BMessage* message, BMessage* reply);
		status_t HandleGetSetMouseMap(BMessage* message, BMessage* reply);
		status_t HandleGetSetKeyboardID(BMessage* message, BMessage* reply);
		status_t HandleGetSetClickSpeed(BMessage* message, BMessage* reply);
		status_t HandleGetSetKeyRepeatRate(BMessage* message, BMessage* reply);
		status_t HandleGetSetKeyMap(BMessage* message, BMessage* reply);
		status_t HandleFocusUnfocusIMAwareView(BMessage* message, BMessage* reply);

		status_t EnqueueDeviceMessage(BMessage* message);
		status_t EnqueueMethodMessage(BMessage* message);
		status_t SetNextMethod(bool direction);
		void SetActiveMethod(BInputServerMethod* method);
		const BMessenger* MethodReplicant();
		void SetMethodReplicant(const BMessenger *replicant);
		bool EventLoopRunning();

		status_t GetDeviceInfo(const char* name, input_device_type *_type,
					bool *_isRunning = NULL);
		status_t GetDeviceInfos(BMessage *msg);
		status_t UnregisterDevices(BInputServerDevice& serverDevice,
					input_device_ref** devices = NULL);
		status_t RegisterDevices(BInputServerDevice& serverDevice,
					input_device_ref** devices);
		status_t StartStopDevices(const char* name, input_device_type type,
					bool doStart);
		status_t StartStopDevices(BInputServerDevice& serverDevice, bool start);
		status_t ControlDevices(const char *name, input_device_type type,
					uint32 code, BMessage* message);

		bool SafeMode();

		::AddOnManager* AddOnManager() { return fAddOnManager; }

		static BList gInputFilterList;
		static BLocker gInputFilterListLocker;

		static BList gInputMethodList;
		static BLocker gInputMethodListLocker;

		static KeymapMethod gKeymapMethod;

		BRect& ScreenFrame() { return fFrame; }

	private:
		typedef BApplication _inherited;

		status_t _LoadKeymap();
		status_t _LoadSystemKeymap();
		status_t _SaveKeymap(bool isDefault = false);
		void _InitKeyboardMouseStates();

		MouseSettings* _RunningMouseSettings();
		void _RunningMiceSettings(BList& settings);
		void _DeviceStarted(InputDeviceListItem& item);
		void _DeviceStopping(InputDeviceListItem& item);
		MouseSettings* _GetSettingsForMouse(BString mouseName);
		status_t _PostMouseControlMessage(int32 code, const BString& mouseName);

		status_t _StartEventLoop();
		void _EventLoop();
		static status_t _EventLooper(void *arg);

		void _UpdateMouseAndKeys(EventList& events);
		bool _SanitizeEvents(EventList& events);
		bool _MethodizeEvents(EventList& events);
		bool _FilterEvents(EventList& events);
		void _DispatchEvents(EventList& events);

		void _FilterEvent(BInputServerFilter* filter, EventList& events,
					int32& index, int32& count);
		status_t _DispatchEvent(BMessage* event);

		status_t _AcquireInput(BMessage& message, BMessage& reply);
		void _ReleaseInput(BMessage* message);

	private:
		uint16			fKeyboardID;

		BList			fInputDeviceList;
		BLocker 		fInputDeviceListLocker;

		KeyboardSettings fKeyboardSettings;
		MultipleMouseSettings	fMouseSettings;
		MouseSettings	fDefaultMouseSettings;
		BStringList		fRunningMouseList;
		BLocker 		fRunningMouseListLocker;

		BPoint			fMousePos;		// current mouse position
		key_info		fKeyInfo;		// current key info
		key_map			fKeys;			// current key_map
		char*			fChars;			// current keymap chars
		uint32			fCharsSize;		// current keymap char count

		port_id      	fEventLooperPort;

		::AddOnManager*	fAddOnManager;

		BScreen			fScreen;
		BRect			fFrame;

		BLocker			fEventQueueLock;
		EventList 		fEventQueue;

		BInputServerMethod*	fActiveMethod;
		EventList			fMethodQueue;
		const BMessenger*	fReplicantMessenger;
		BottomlineWindow*	fInputMethodWindow;
		bool				fInputMethodAware;

		sem_id 			fCursorSem;
		port_id			fAppServerPort;
		team_id			fAppServerTeam;
		area_id			fCursorArea;
		shared_cursor*	fCursorBuffer;
};

extern InputServer* gInputServer;

#if DEBUG >= 1
#	if DEBUG == 2
#		undef PRINT
		inline void _iprint(const char *fmt, ...) {
			FILE* log = fopen("/var/log/input_server.log", "a");
			char buf[1024];
			va_list ap;
			va_start(ap, fmt);
			vsprintf(buf, fmt, ap);
			va_end(ap);
			fputs(buf, log);
			fflush(log);
			fclose(log);
        }
#		define PRINT(x)	_iprint x
#	else
#		undef PRINT
#		define PRINT(x)	SERIAL_PRINT(x)
#	endif
#	define PRINTERR(x)		PRINT(x)
#	define EXIT()          PRINT(("EXIT %s\n", __PRETTY_FUNCTION__))
#	define CALLED()        PRINT(("CALLED %s\n", __PRETTY_FUNCTION__))
#else
#	define EXIT()          ((void)0)
#	define CALLED()        ((void)0)
#	define PRINTERR(x)		SERIAL_PRINT(x)
#endif

#endif	/* INPUT_SERVER_APP_H */
