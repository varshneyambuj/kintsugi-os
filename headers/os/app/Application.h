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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */
#ifndef _APPLICATION_H
#define _APPLICATION_H

/**
 * @file Application.h
 * @brief Defines the BApplication class, the central object for Kintsugi OS
 *        GUI and command-line applications.
 *
 * Every Kintsugi OS application that interacts with the app_server must
 * create exactly one BApplication (or a subclass). BApplication establishes
 * the connection to the app_server, manages the main message loop, and
 * provides hooks for application lifecycle events such as ReadyToRun(),
 * ArgvReceived(), RefsReceived(), and Quit().
 *
 * The global pointer @c be_app and the global messenger @c be_app_messenger
 * are set when a BApplication is constructed and cleared when it is
 * destroyed.
 */


#include <AppDefs.h>
#include <InterfaceDefs.h>
#include <Looper.h>
#include <Messenger.h>
#include <Point.h>
#include <Rect.h>


class BCursor;
class BList;
class BLocker;
class BMessageRunner;
class BResources;
class BServer;
class BWindow;

/** @brief Forward declaration of the app_info structure. */
struct app_info;


namespace BPrivate {
	class PortLink;
	class ServerMemoryAllocator;
}


/**
 * @class BApplication
 * @brief The main application class for Kintsugi OS programs.
 *
 * BApplication is the top of the BHandler -> BLooper -> BApplication
 * hierarchy. It represents the running application, owns the main message
 * loop thread, and manages the connection to the app_server. Typically you
 * subclass BApplication and override its hook methods to respond to
 * application-level events.
 *
 * Lifecycle overview:
 * 1. Construct a BApplication with your application signature.
 * 2. Create windows and set up resources.
 * 3. Call Run() -- this enters the main message loop and calls ReadyToRun().
 * 4. The loop dispatches messages until Quit() is called.
 *
 * Key features:
 * - **Lifecycle hooks**: ReadyToRun(), Pulse(), ArgvReceived(),
 *   RefsReceived(), AboutRequested(), AppActivated().
 * - **Cursor management**: ShowCursor(), HideCursor(), SetCursor().
 * - **Window enumeration**: CountWindows(), WindowAt().
 * - **Pulse timer**: SetPulseRate() triggers periodic Pulse() calls.
 *
 * @note Only one BApplication may exist per team. Constructing a second one
 *       will fail.
 *
 * @see BLooper, BWindow, BHandler, be_app, be_app_messenger
 */
class BApplication : public BLooper {
public:
	/**
	 * @brief Constructs a BApplication with the given MIME signature.
	 *
	 * Connects to the app_server and initializes the application. If
	 * initialization fails, subsequent operations may not work correctly.
	 * Use the two-argument constructor to check for errors.
	 *
	 * @param signature The application's MIME type signature
	 *        (e.g., "application/x-vnd.MyApp").
	 * @see InitCheck()
	 */
								BApplication(const char* signature);

	/**
	 * @brief Constructs a BApplication and reports initialization status.
	 * @param signature The application's MIME type signature.
	 * @param error Pointer to a status_t that receives the initialization
	 *        result. B_OK on success, or an error code on failure.
	 * @see InitCheck()
	 */
								BApplication(const char* signature,
									status_t* error);

	/**
	 * @brief Destructor. Disconnects from the app_server and cleans up
	 *        application resources.
	 */
	virtual						~BApplication();

	// Archiving

	/**
	 * @brief Archive constructor. Reconstructs a BApplication from an
	 *        archived BMessage.
	 * @param data The archived BMessage containing application state.
	 */
								BApplication(BMessage* data);

	/**
	 * @brief Creates a new BApplication from an archived BMessage.
	 * @param data The archive message to instantiate from.
	 * @return A new BApplication object, or NULL if the archive is not
	 *         a BApplication.
	 * @see Archive()
	 */
	static	BArchivable*		Instantiate(BMessage* data);

	/**
	 * @brief Archives this BApplication into a BMessage.
	 * @param data The BMessage to archive into.
	 * @param deep If true, child objects are also archived.
	 * @return B_OK on success, or an error code on failure.
	 * @see Instantiate()
	 */
	virtual	status_t			Archive(BMessage* data, bool deep = true) const;

	/**
	 * @brief Returns the initialization status of the application.
	 * @return B_OK if the application was initialized successfully, or an
	 *         error code indicating what went wrong.
	 */
			status_t			InitCheck() const;

	// App control and System Message handling

	/**
	 * @brief Begins the application's main message loop.
	 *
	 * This method does not return until the application quits. It calls
	 * ReadyToRun() once the loop is set up. Call this from your main()
	 * function after constructing the BApplication.
	 *
	 * @return The thread_id of the main loop thread.
	 * @see Quit(), ReadyToRun()
	 */
	virtual	thread_id			Run();

	/**
	 * @brief Terminates the application.
	 *
	 * Closes all windows, stops the message loop, and deletes the
	 * BApplication object. Do not use the object after calling Quit().
	 *
	 * @see QuitRequested(), Run()
	 */
	virtual	void				Quit();

	/**
	 * @brief Hook function called to determine if the application may quit.
	 *
	 * Override this to display confirmation dialogs or perform cleanup
	 * checks. The default implementation returns true.
	 *
	 * @return true if the application should quit, false to cancel.
	 * @see Quit()
	 */
	virtual bool				QuitRequested();

	/**
	 * @brief Hook function called at regular intervals when a pulse rate
	 *        is set.
	 *
	 * Override this to perform periodic tasks. The interval is configured
	 * via SetPulseRate().
	 *
	 * @see SetPulseRate()
	 */
	virtual	void				Pulse();

	/**
	 * @brief Hook function called when the message loop is ready to run.
	 *
	 * This is called once from within Run(), after the message loop thread
	 * has started. Use this as the place to show your initial windows and
	 * perform post-construction setup.
	 *
	 * @see Run()
	 */
	virtual	void				ReadyToRun();

	/**
	 * @brief Hook function called when the application receives a message.
	 *
	 * Override this to handle application-level messages. Always call the
	 * base class implementation for messages you do not handle.
	 *
	 * @param message The incoming message. Do not delete this pointer.
	 */
	virtual	void				MessageReceived(BMessage* message);

	/**
	 * @brief Hook function called when the application is launched with
	 *        command-line arguments.
	 *
	 * This is called when arguments are passed at launch or when a running
	 * application receives argv-style messages.
	 *
	 * @param argc The number of arguments.
	 * @param argv Array of null-terminated argument strings.
	 */
	virtual	void				ArgvReceived(int32 argc, char** argv);

	/**
	 * @brief Hook function called when the application is activated or
	 *        deactivated.
	 * @param active true if the application has become the active
	 *        application, false if it has been deactivated.
	 */
	virtual	void				AppActivated(bool active);

	/**
	 * @brief Hook function called when files are dropped on the application
	 *        icon or opened via the application.
	 *
	 * The message contains entry_ref items under the name "refs".
	 *
	 * @param message A B_REFS_RECEIVED message containing the file
	 *        references.
	 */
	virtual	void				RefsReceived(BMessage* message);

	/**
	 * @brief Hook function called when the user requests "About" information.
	 *
	 * Override this to display an about dialog or window for your
	 * application.
	 */
	virtual	void				AboutRequested();

	// Scripting

	/**
	 * @brief Resolves a scripting specifier for the application.
	 *
	 * Extends BLooper's scripting to handle application-specific properties
	 * such as "Window", "Looper", and "Name".
	 *
	 * @param message The scripting message being resolved.
	 * @param index The current index into the specifier stack.
	 * @param specifier The current specifier message.
	 * @param form The specifier's form code (e.g., B_DIRECT_SPECIFIER).
	 * @param property The name of the property being accessed.
	 * @return The BHandler that should handle this specifier.
	 * @see GetSupportedSuites()
	 */
	virtual BHandler*			ResolveSpecifier(BMessage* message, int32 index,
									BMessage* specifier, int32 form,
									const char* property);

	// Cursor control, window/looper list, and app info

	/**
	 * @brief Shows the mouse cursor if it was previously hidden.
	 * @see HideCursor(), ObscureCursor(), IsCursorHidden()
	 */
			void				ShowCursor();

	/**
	 * @brief Hides the mouse cursor.
	 *
	 * The cursor remains hidden until ShowCursor() is called. Calls to
	 * HideCursor() do not nest.
	 *
	 * @see ShowCursor(), IsCursorHidden()
	 */
			void				HideCursor();

	/**
	 * @brief Temporarily hides the cursor until the mouse is moved.
	 *
	 * Unlike HideCursor(), the cursor reappears as soon as the user moves
	 * the mouse.
	 *
	 * @see HideCursor(), ShowCursor()
	 */
			void				ObscureCursor();

	/**
	 * @brief Checks whether the cursor is currently hidden.
	 * @return true if the cursor is hidden, false otherwise.
	 * @see HideCursor(), ShowCursor()
	 */
			bool				IsCursorHidden() const;

	/**
	 * @brief Sets the mouse cursor to a legacy cursor bitmap.
	 * @param cursor Pointer to a 68-byte cursor data block (16x16 1-bit
	 *        bitmap plus mask and hotspot).
	 * @see SetCursor(const BCursor*, bool)
	 */
			void				SetCursor(const void* cursor);

	/**
	 * @brief Sets the mouse cursor to a BCursor object.
	 * @param cursor The BCursor to use. Must not be NULL.
	 * @param sync If true, the call blocks until the app_server has
	 *        applied the cursor change. If false, the change is
	 *        asynchronous.
	 * @see SetCursor(const void*)
	 */
			void				SetCursor(const BCursor* cursor,
									bool sync = true);

	/**
	 * @brief Returns the number of windows owned by this application.
	 * @return The window count.
	 * @see WindowAt()
	 */
			int32				CountWindows() const;

	/**
	 * @brief Returns the window at the specified index.
	 * @param index Zero-based index into the window list.
	 * @return The BWindow at @p index, or NULL if the index is out of range.
	 * @see CountWindows()
	 */
			BWindow*			WindowAt(int32 index) const;

	/**
	 * @brief Returns the total number of loopers in this application.
	 * @return The looper count.
	 * @see LooperAt()
	 */
			int32				CountLoopers() const;

	/**
	 * @brief Returns the looper at the specified index.
	 * @param index Zero-based index into the looper list.
	 * @return The BLooper at @p index, or NULL if the index is out of range.
	 * @see CountLoopers()
	 */
			BLooper*			LooperAt(int32 index) const;

	/**
	 * @brief Checks whether the application is still in its launch phase.
	 * @return true if the application has not yet finished launching
	 *         (ReadyToRun() has not been called), false otherwise.
	 * @see ReadyToRun()
	 */
			bool				IsLaunching() const;

	/**
	 * @brief Returns the application's MIME type signature.
	 * @return A null-terminated string containing the signature.
	 */
			const char*			Signature() const;

	/**
	 * @brief Retrieves information about this application.
	 * @param info Pointer to an app_info structure that will be filled in.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			GetAppInfo(app_info* info) const;

	/**
	 * @brief Returns the application's resource file.
	 *
	 * The returned BResources object provides read-only access to the
	 * resources compiled into the application binary.
	 *
	 * @return A pointer to the application's BResources, or NULL if
	 *         resources could not be loaded. The object is owned by the
	 *         framework -- do not delete it.
	 */
	static	BResources*			AppResources();

	/**
	 * @brief Dispatches a message to the given handler.
	 *
	 * Extends BLooper::DispatchMessage() to handle application-specific
	 * system messages (e.g., B_ARGV_RECEIVED, B_REFS_RECEIVED, B_READY_TO_RUN,
	 * B_PULSE).
	 *
	 * @param message The message to dispatch.
	 * @param handler The handler to receive the message.
	 */
	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);

	/**
	 * @brief Sets the interval for Pulse() calls.
	 *
	 * When a non-zero rate is set, the application will receive periodic
	 * B_PULSE messages, which trigger the Pulse() hook.
	 *
	 * @param rate The pulse interval in microseconds. Pass 0 to disable
	 *        pulsing.
	 * @see Pulse()
	 */
			void				SetPulseRate(bigtime_t rate);

	// Register a BLooper to be quit before the BApplication
	// object is destroyed.

	/**
	 * @brief Registers a looper to be automatically quit when the
	 *        application quits.
	 * @param looper The BLooper to register.
	 * @return B_OK on success, or an error code on failure.
	 * @see UnregisterLooper()
	 */
			status_t			RegisterLooper(BLooper* looper);

	/**
	 * @brief Unregisters a previously registered looper.
	 * @param looper The BLooper to unregister.
	 * @return B_OK on success, or B_BAD_VALUE if the looper was not
	 *         registered.
	 * @see RegisterLooper()
	 */
			status_t			UnregisterLooper(BLooper* looper);

	// More scripting

	/**
	 * @brief Reports the scripting suites supported by the application.
	 * @param data The BMessage to populate with suite information.
	 * @return B_OK on success, or an error code on failure.
	 * @see ResolveSpecifier()
	 */
	virtual status_t			GetSupportedSuites(BMessage* data);


	// Private or reserved

	/**
	 * @brief Reserved virtual function for binary compatibility.
	 * @param d The perform code identifying the operation.
	 * @param arg Operation-specific argument.
	 * @return B_OK or an error code depending on the operation.
	 * @note This method is for internal use and binary compatibility.
	 */
	virtual status_t			Perform(perform_code d, void* arg);

	class Private;

private:
	typedef BLooper _inherited;

	friend class Private;
	friend class BServer;

								BApplication(const char* signature,
									const char* looperName, port_id port,
									bool initGUI, status_t* error);
								BApplication(uint32 signature);
								BApplication(const BApplication&);
			BApplication&		operator=(const BApplication&);

	virtual	void				_ReservedApplication1();
	virtual	void				_ReservedApplication2();
	virtual	void				_ReservedApplication3();
	virtual	void				_ReservedApplication4();
	virtual	void				_ReservedApplication5();
	virtual	void				_ReservedApplication6();
	virtual	void				_ReservedApplication7();
	virtual	void				_ReservedApplication8();

	virtual	bool				ScriptReceived(BMessage* msg, int32 index,
									BMessage* specifier, int32 form,
									const char* property);
			void				_InitData(const char* signature, bool initGUI,
									status_t* error);
			port_id				_GetPort(const char* signature);
			void				BeginRectTracking(BRect r, bool trackWhole);
			void				EndRectTracking();
			status_t			_SetupServerAllocator();
			status_t			_InitGUIContext();
			status_t			_ConnectToServer();
			void				_ReconnectToServer();
			bool				_QuitAllWindows(bool force);
			bool				_WindowQuitLoop(bool quitFilePanels,
									bool force);
			void				_ArgvReceived(BMessage* message);

			uint32				InitialWorkspace();
			int32				_CountWindows(bool includeMenus) const;
			BWindow*			_WindowAt(uint32 index,
									bool includeMenus) const;

	static	void				_InitAppResources();

private:
	static	BResources*			sAppResources;

			const char*			fAppName;
			::BPrivate::PortLink*	fServerLink;
			::BPrivate::ServerMemoryAllocator* fServerAllocator;

			void*				fCursorData;
			bigtime_t			fPulseRate;
			uint32				fInitialWorkspace;
			BMessageRunner*		fPulseRunner;
			status_t			fInitError;
			void*				fServerReadOnlyMemory;
			uint32				_reserved[12];

			bool				fReadyToRunCalled;
};


/** @brief Global pointer to the application's BApplication instance. */
extern BApplication* be_app;

/** @brief Global BMessenger targeting the application's BApplication. */
extern BMessenger be_app_messenger;


#endif	// _APPLICATION_H
