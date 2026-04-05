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
 *   Copyright 2001-2015 Haiku, inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Jerome Duval
 *       Erik Jaesler, erik@cgsoftware.com
 */


/**
 * @file Application.cpp
 * @brief Implementation of BApplication, the central application class.
 *
 * BApplication is the fundamental class for any application with a message
 * loop. Every application must create exactly one BApplication (or subclass)
 * instance, which becomes the global @c be_app. BApplication manages the
 * connection to the app_server and the registrar, handles the main message
 * loop, dispatches system messages (argv, refs, activation, pulse), and
 * provides cursor control and window/looper enumeration. It also supports
 * the BeOS scripting protocol for introspection of windows and loopers.
 *
 * @see BLooper, BWindow, BRoster, BCursor
 */


#include <Application.h>

#include <new>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include <Alert.h>
#include <AppFileInfo.h>
#include <Cursor.h>
#include <Debug.h>
#include <Entry.h>
#include <File.h>
#include <Locker.h>
#include <MessageRunner.h>
#include <ObjectList.h>
#include <Path.h>
#include <PropertyInfo.h>
#include <RegistrarDefs.h>
#include <Resources.h>
#include <Roster.h>
#include <Window.h>

#include <AppMisc.h>
#include <AppServerLink.h>
#include <AutoLocker.h>
#include <BitmapPrivate.h>
#include <DraggerPrivate.h>
#include <LaunchDaemonDefs.h>
#include <LaunchRoster.h>
#include <LooperList.h>
#include <MenuWindow.h>
#include <PicturePrivate.h>
#include <PortLink.h>
#include <RosterPrivate.h>
#include <ServerMemoryAllocator.h>
#include <ServerProtocol.h>


using namespace BPrivate;


/** @brief Default name for the BApplication's BLooper message port. */
static const char* kDefaultLooperName = "AppLooperPort";

/** @brief The global application instance pointer, set during BApplication construction. */
BApplication* be_app = NULL;

/** @brief A BMessenger targeting the global BApplication instance. */
BMessenger be_app_messenger;

/** @brief pthread_once control for one-time initialization of application resources. */
pthread_once_t sAppResourcesInitOnce = PTHREAD_ONCE_INIT;

/** @brief Cached application resources, lazily initialized via _InitAppResources(). */
BResources* BApplication::sAppResources = NULL;

/**
 * @brief List of non-window loopers registered to be quit when the application exits.
 * @see BApplication::RegisterLooper(), BApplication::UnregisterLooper()
 */
BObjectList<BLooper> sOnQuitLooperList;


/**
 * @brief Indices into sPropertyInfo for scripting property resolution.
 *
 * These constants identify which scripting property and specifier combination
 * matched in ResolveSpecifier() and ScriptReceived().
 */
enum {
	kWindowByIndex,
	kWindowByName,
	kLooperByIndex,
	kLooperByID,
	kLooperByName,
	kApplication
};


/**
 * @brief Scripting property table for BApplication.
 *
 * Defines the properties exposed via the BeOS scripting protocol, including
 * "Window" (by index or name), "Looper" (by index, ID, or name),
 * "Name" (GET), and count properties for windows and loopers.
 *
 * @see BApplication::ResolveSpecifier(), BApplication::ScriptReceived()
 * @see BApplication::GetSupportedSuites()
 */
static property_info sPropertyInfo[] = {
	{
		"Window",
		{},
		{B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER},
		NULL, kWindowByIndex,
		{},
		{},
		{}
	},
	{
		"Window",
		{},
		{B_NAME_SPECIFIER},
		NULL, kWindowByName,
		{},
		{},
		{}
	},
	{
		"Looper",
		{},
		{B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER},
		NULL, kLooperByIndex,
		{},
		{},
		{}
	},
	{
		"Looper",
		{},
		{B_ID_SPECIFIER},
		NULL, kLooperByID,
		{},
		{},
		{}
	},
	{
		"Looper",
		{},
		{B_NAME_SPECIFIER},
		NULL, kLooperByName,
		{},
		{},
		{}
	},
	{
		"Name",
		{B_GET_PROPERTY},
		{B_DIRECT_SPECIFIER},
		NULL, kApplication,
		{B_STRING_TYPE},
		{},
		{}
	},
	{
		"Window",
		{B_COUNT_PROPERTIES},
		{B_DIRECT_SPECIFIER},
		NULL, kApplication,
		{B_INT32_TYPE},
		{},
		{}
	},
	{
		"Loopers",
		{B_GET_PROPERTY},
		{B_DIRECT_SPECIFIER},
		NULL, kApplication,
		{B_MESSENGER_TYPE},
		{},
		{}
	},
	{
		"Windows",
		{B_GET_PROPERTY},
		{B_DIRECT_SPECIFIER},
		NULL, kApplication,
		{B_MESSENGER_TYPE},
		{},
		{}
	},
	{
		"Looper",
		{B_COUNT_PROPERTIES},
		{B_DIRECT_SPECIFIER},
		NULL, kApplication,
		{B_INT32_TYPE},
		{},
		{}
	},

	{ 0 }
};


// argc/argv
extern const int __libc_argc;
extern const char* const *__libc_argv;


// debugging
//#define DBG(x) x
#define DBG(x)
#define OUT	printf


//	#pragma mark - static helper functions


/*!
	\brief Checks whether the supplied string is a valid application signature.

	An error message is printed, if the string is no valid app signature.

	\param signature The string to be checked.

	\return A status code.
	\retval B_OK \a signature is a valid app signature.
	\retval B_BAD_VALUE \a signature is \c NULL or no valid app signature.
*/
static status_t
check_app_signature(const char* signature)
{
	bool isValid = false;
	BMimeType type(signature);

	if (type.IsValid() && !type.IsSupertypeOnly()
		&& BMimeType("application").Contains(&type)) {
		isValid = true;
	}

	if (!isValid) {
		printf("bad signature (%s), must begin with \"application/\" and "
			   "can't conflict with existing registered mime types inside "
			   "the \"application\" media type.\n", signature);
	}

	return (isValid ? B_OK : B_BAD_VALUE);
}


#ifndef RUN_WITHOUT_REGISTRAR
/**
 * @brief Populates a BMessage with B_ARGV_RECEIVED data from the process's argc/argv.
 *
 * Sets the message's @c what field to @c B_ARGV_RECEIVED and attaches the
 * argument count ("argc"), each argument string ("argv"), and the current
 * working directory ("cwd").
 *
 * @param message The BMessage to fill; its @c what field will be overwritten.
 *
 * @note Uses the global __libc_argc and __libc_argv symbols provided by the C library.
 * @see BApplication::_InitData(), BApplication::_ArgvReceived()
 */
static void
fill_argv_message(BMessage &message)
{
	message.what = B_ARGV_RECEIVED;

	int32 argc = __libc_argc;
	const char* const *argv = __libc_argv;

	// add argc
	message.AddInt32("argc", argc);

	// add argv
	for (int32 i = 0; i < argc; i++) {
		if (argv[i] != NULL)
			message.AddString("argv", argv[i]);
	}

	// add current working directory
	char cwd[B_PATH_NAME_LENGTH];
	if (getcwd(cwd, B_PATH_NAME_LENGTH))
		message.AddString("cwd", cwd);
}
#endif


//	#pragma mark - BApplication


/**
 * @brief Constructs a BApplication with the given MIME signature.
 *
 * Initializes the application with GUI support enabled. If initialization
 * fails, the application will call exit(0).
 *
 * @param signature The application's MIME signature (e.g. "application/x-vnd.MyApp").
 *                  Must begin with "application/" and be a valid MIME type.
 *
 * @note Only one BApplication instance may exist at a time; creating a second
 *       will trigger a debugger() call.
 * @see _InitData(), BApplication(const char*, status_t*)
 */
BApplication::BApplication(const char* signature)
	:
	BLooper(kDefaultLooperName)
{
	_InitData(signature, true, NULL);
}


/**
 * @brief Constructs a BApplication with the given MIME signature, returning errors.
 *
 * Identical to the single-argument constructor, but instead of calling exit()
 * on failure, the initialization error code is stored in @a _error.
 *
 * @param signature The application's MIME signature.
 * @param _error    If non-NULL, receives the initialization status code.
 *                  B_OK on success, or an error code on failure.
 *
 * @see _InitData(), InitCheck()
 */
BApplication::BApplication(const char* signature, status_t* _error)
	:
	BLooper(kDefaultLooperName)
{
	_InitData(signature, true, _error);
}


/**
 * @brief Constructs a BApplication with extended options for port, looper name, and GUI.
 *
 * This constructor allows fine-grained control over the message port, the
 * looper thread name, and whether the GUI context (app_server connection)
 * should be initialized. If @a port is negative, a port is obtained from
 * the launch daemon via _GetPort().
 *
 * @param signature  The application's MIME signature.
 * @param looperName The name for the looper thread, or NULL for the default.
 * @param port       The message port to use, or a negative value to auto-acquire.
 * @param initGUI    If true, connect to the app_server and initialize the GUI context.
 * @param _error     If non-NULL, receives the initialization status code.
 *
 * @note When @a port is negative, the port ownership flag (fOwnsPort) is set
 *       to false, since the port was acquired externally.
 * @see _InitData(), _GetPort()
 */
BApplication::BApplication(const char* signature, const char* looperName,
	port_id port, bool initGUI, status_t* _error)
	:
	BLooper(B_NORMAL_PRIORITY + 1, port < 0 ? _GetPort(signature) : port,
		looperName != NULL ? looperName : kDefaultLooperName)
{
	_InitData(signature, initGUI, _error);
	if (port < 0)
		fOwnsPort = false;
}


/**
 * @brief Constructs a BApplication from an archived BMessage.
 *
 * Unarchives the application signature from the "mime_sig" field and the
 * pulse rate from the "_pulse" field. Used by the BArchivable instantiation
 * mechanism.
 *
 * @param data The archived BMessage containing "mime_sig" and optionally "_pulse".
 *
 * @see Instantiate(), Archive()
 */
BApplication::BApplication(BMessage* data)
	// Note: BeOS calls the private BLooper(int32, port_id, const char*)
	// constructor here, test if it's needed
	:
	BLooper(kDefaultLooperName)
{
	const char* signature = NULL;
	data->FindString("mime_sig", &signature);

	_InitData(signature, true, NULL);

	bigtime_t pulseRate;
	if (data->FindInt64("_pulse", &pulseRate) == B_OK)
		SetPulseRate(pulseRate);
}


#ifdef __HAIKU_BEOS_COMPATIBLE
/** @brief Legacy BeOS R3 constructor (stub, not implemented). */
BApplication::BApplication(uint32 signature)
{
}


/** @brief Legacy BeOS copy constructor (stub, not implemented). */
BApplication::BApplication(const BApplication &rhs)
{
}


/** @brief Legacy BeOS assignment operator (stub, not implemented). */
BApplication&
BApplication::operator=(const BApplication &rhs)
{
	return *this;
}
#endif


/**
 * @brief Destroys the BApplication, quitting all windows and cleaning up resources.
 *
 * The destructor performs the following in order:
 * 1. Locks the application.
 * 2. Quits all windows via _QuitAllWindows() with force.
 * 3. Quits all registered non-window loopers from sOnQuitLooperList.
 * 4. Unregisters this application from the roster.
 * 5. Notifies app_server that the application is quitting and tears down the
 *    server link.
 * 6. Deletes the server memory allocator.
 * 7. Sets the global be_app to NULL.
 *
 * @note The application must be locked before destruction. The destructor
 *       acquires the lock itself.
 * @see Quit(), _QuitAllWindows(), RegisterLooper()
 */
BApplication::~BApplication()
{
	Lock();

	// tell all loopers(usually windows) to quit. Also, wait for them.
	_QuitAllWindows(true);

	// quit registered loopers
	for (int32 i = 0; i < sOnQuitLooperList.CountItems(); i++) {
		BLooper* looper = sOnQuitLooperList.ItemAt(i);
		if (looper->Lock())
			looper->Quit();
	}

	// unregister from the roster
	BRoster::Private().RemoveApp(Team());

#ifndef RUN_WITHOUT_APP_SERVER
	// tell app_server we're quitting...
	if (be_app) {
		// be_app can be NULL here if the application fails to initialize
		// correctly. For example, if it's already running and it's set to
		// exclusive launch.
		BPrivate::AppServerLink link;
		link.StartMessage(B_QUIT_REQUESTED);
		link.Flush();
	}
	// the sender port belongs to the app_server
	delete_port(fServerLink->ReceiverPort());
	delete fServerLink;
#endif	// RUN_WITHOUT_APP_SERVER

	delete fServerAllocator;

	// uninitialize be_app, the be_app_messenger is invalidated automatically
	be_app = NULL;
}


/**
 * @brief Core initialization routine called by all constructors.
 *
 * Performs the complete setup of the BApplication instance:
 * 1. Verifies no other BApplication instance exists.
 * 2. Initializes internal state (server link, pulse, workspace).
 * 3. Validates the application MIME signature.
 * 4. Resolves the application's entry_ref and reads BAppFileInfo.
 * 5. Registers with (or completes pre-registration at) the registrar.
 * 6. Handles B_ALREADY_RUNNING for single/exclusive launch modes by
 *    forwarding argv to the existing instance.
 * 7. Posts B_ARGV_RECEIVED and B_READY_TO_RUN messages to self.
 * 8. Sets the global be_app and be_app_messenger.
 * 9. Optionally connects to the app_server via _InitGUIContext().
 *
 * @param signature The application's MIME signature string.
 * @param initGUI   If true, establish the app_server connection and
 *                  initialize the GUI (Interface Kit) context.
 * @param _error    If non-NULL, receives the initialization status.
 *                  If NULL and initialization fails, exit(0) is called.
 *
 * @note This method enforces the singleton constraint on BApplication:
 *       only one instance may exist per team. A debugger() call is triggered
 *       if a second instance is detected.
 * @see _InitGUIContext(), _ConnectToServer(), check_app_signature()
 */
void
BApplication::_InitData(const char* signature, bool initGUI, status_t* _error)
{
	DBG(OUT("BApplication::InitData(`%s', %p)\n", signature, _error));
	// check whether there exists already an application
	if (be_app != NULL)
		debugger("2 BApplication objects were created. Only one is allowed.");

	fServerLink = new BPrivate::PortLink(-1, -1);
	fServerAllocator = NULL;
	fServerReadOnlyMemory = NULL;
	fInitialWorkspace = 0;
	//fDraggedMessage = NULL;
	fReadyToRunCalled = false;

	// initially, there is no pulse
	fPulseRunner = NULL;
	fPulseRate = 0;

	// check signature
	fInitError = check_app_signature(signature);
	fAppName = signature;

#ifndef RUN_WITHOUT_REGISTRAR
	bool registerApp = signature == NULL
		|| (strcasecmp(signature, B_REGISTRAR_SIGNATURE) != 0
			&& strcasecmp(signature, kLaunchDaemonSignature) != 0);
	// get team and thread
	team_id team = Team();
	thread_id thread = BPrivate::main_thread_for(team);
#endif

	// get app executable ref
	entry_ref ref;
	if (fInitError == B_OK) {
		fInitError = BPrivate::get_app_ref(&ref);
		if (fInitError != B_OK) {
			DBG(OUT("BApplication::InitData(): Failed to get app ref: %s\n",
				strerror(fInitError)));
		}
	}

	// get the BAppFileInfo and extract the information we need
	uint32 appFlags = B_REG_DEFAULT_APP_FLAGS;
	if (fInitError == B_OK) {
		BAppFileInfo fileInfo;
		BFile file(&ref, B_READ_ONLY);
		fInitError = fileInfo.SetTo(&file);
		if (fInitError == B_OK) {
			fileInfo.GetAppFlags(&appFlags);
			char appFileSignature[B_MIME_TYPE_LENGTH];
			// compare the file signature and the supplied signature
			if (fileInfo.GetSignature(appFileSignature) == B_OK
				&& strcasecmp(appFileSignature, signature) != 0) {
				printf("Signature in rsrc doesn't match constructor arg. (%s, %s)\n",
					signature, appFileSignature);
			}
		} else {
			DBG(OUT("BApplication::InitData(): Failed to get info from: "
				"BAppFileInfo: %s\n", strerror(fInitError)));
		}
	}

#ifndef RUN_WITHOUT_REGISTRAR
	// check whether be_roster is valid
	if (fInitError == B_OK && registerApp
		&& !BRoster::Private().IsMessengerValid(false)) {
		printf("FATAL: be_roster is not valid. Is the registrar running?\n");
		fInitError = B_NO_INIT;
	}

	// check whether or not we are pre-registered
	bool preRegistered = false;
	app_info appInfo;
	if (fInitError == B_OK && registerApp) {
		if (BRoster::Private().IsAppRegistered(&ref, team, 0, &preRegistered,
				&appInfo) != B_OK) {
			preRegistered = false;
		}
	}
	if (preRegistered) {
		// we are pre-registered => the app info has been filled in
		// Check whether we need to replace the looper port with a port
		// created by the roster.
		if (appInfo.port >= 0 && appInfo.port != fMsgPort) {
			delete_port(fMsgPort);
			fMsgPort = appInfo.port;
		} else
			appInfo.port = fMsgPort;
		// check the signature and correct it, if necessary, also the case
		if (strcmp(appInfo.signature, fAppName))
			BRoster::Private().SetSignature(team, fAppName);
		// complete the registration
		fInitError = BRoster::Private().CompleteRegistration(team, thread,
						appInfo.port);
	} else if (fInitError == B_OK) {
		// not pre-registered -- try to register the application
		team_id otherTeam = -1;
		if (registerApp) {
			fInitError = BRoster::Private().AddApplication(signature, &ref,
				appFlags, team, thread, fMsgPort, true, NULL, &otherTeam);
			if (fInitError != B_OK) {
				DBG(OUT("BApplication::InitData(): Failed to add app: %s\n",
					strerror(fInitError)));
			}
		}
		if (fInitError == B_ALREADY_RUNNING) {
			// An instance is already running and we asked for
			// single/exclusive launch. Send our argv to the running app.
			// Do that only, if the app is NOT B_ARGV_ONLY.
			if (otherTeam >= 0) {
				BMessenger otherApp(NULL, otherTeam);
				app_info otherAppInfo;
				bool argvOnly = be_roster->GetRunningAppInfo(otherTeam,
						&otherAppInfo) == B_OK
					&& (otherAppInfo.flags & B_ARGV_ONLY) != 0;

				if (__libc_argc > 1 && !argvOnly) {
					// create an B_ARGV_RECEIVED message
					BMessage argvMessage(B_ARGV_RECEIVED);
					fill_argv_message(argvMessage);

					// replace the first argv string with the path of the
					// other application
					BPath path;
					if (path.SetTo(&otherAppInfo.ref) == B_OK)
						argvMessage.ReplaceString("argv", 0, path.Path());

					// send the message
					otherApp.SendMessage(&argvMessage);
				} else if (!argvOnly)
					otherApp.SendMessage(B_SILENT_RELAUNCH);
			}
		} else if (fInitError == B_OK) {
			// the registrations was successful
			// Create a B_ARGV_RECEIVED message and send it to ourselves.
			// Do that even, if we are B_ARGV_ONLY.
			// TODO: When BLooper::AddMessage() is done, use that instead of
			// PostMessage().

			DBG(OUT("info: BApplication successfully registered.\n"));

			if (__libc_argc > 1) {
				BMessage argvMessage(B_ARGV_RECEIVED);
				fill_argv_message(argvMessage);
				PostMessage(&argvMessage, this);
			}
			// send a B_READY_TO_RUN message as well
			PostMessage(B_READY_TO_RUN, this);
		} else if (fInitError > B_ERRORS_END) {
			// Registrar internal errors shouldn't fall into the user's hands.
			fInitError = B_ERROR;
		}
	}
#else
	// We need to have ReadyToRun called even when we're not using the registrar
	PostMessage(B_READY_TO_RUN, this);
#endif	// ifndef RUN_WITHOUT_REGISTRAR

	if (fInitError == B_OK) {
		// TODO: Not completely sure about the order, but this should be close.

		// init be_app and be_app_messenger
		be_app = this;
		be_app_messenger = BMessenger(NULL, this);

		// set the BHandler's name
		SetName(ref.name);

		// create meta MIME
		BPath path;
		if (registerApp && path.SetTo(&ref) == B_OK)
			create_app_meta_mime(path.Path(), false, true, false);

#ifndef RUN_WITHOUT_APP_SERVER
		// app server connection and IK initialization
		if (initGUI)
			fInitError = _InitGUIContext();
#endif	// RUN_WITHOUT_APP_SERVER
	}

	// Return the error or exit, if there was an error and no error variable
	// has been supplied.
	if (_error != NULL) {
		*_error = fInitError;
	} else if (fInitError != B_OK) {
		DBG(OUT("BApplication::InitData() failed: %s\n", strerror(fInitError)));
		exit(0);
	}
DBG(OUT("BApplication::InitData() done\n"));
}


/**
 * @brief Retrieves a message port from the launch daemon for the given signature.
 *
 * @param signature The application's MIME signature used to look up the port.
 *
 * @return The port_id obtained from the BLaunchRoster, or a negative error code.
 *
 * @see BLaunchRoster::GetPort()
 */
port_id
BApplication::_GetPort(const char* signature)
{
	return BLaunchRoster().GetPort(signature, NULL);
}


/**
 * @brief Creates a new BApplication from an archived BMessage.
 *
 * Validates that @a data represents a "BApplication" archive, then constructs
 * a new instance via the BMessage constructor.
 *
 * @param data The archived BMessage to instantiate from.
 *
 * @return A new BApplication instance, or NULL if validation fails.
 *
 * @see Archive(), BApplication(BMessage*)
 */
BArchivable*
BApplication::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BApplication"))
		return new BApplication(data);

	return NULL;
}


/**
 * @brief Archives the BApplication into a BMessage.
 *
 * Stores the base BLooper archive data, the application's MIME signature
 * ("mime_sig"), and the current pulse rate ("_pulse").
 *
 * @param data The BMessage to archive into.
 * @param deep If true, child BHandlers are archived as well (passed to BLooper::Archive).
 *
 * @return B_OK on success, or an error code if archiving fails.
 *
 * @see Instantiate(), BLooper::Archive()
 */
status_t
BApplication::Archive(BMessage* data, bool deep) const
{
	status_t status = BLooper::Archive(data, deep);
	if (status < B_OK)
		return status;

	app_info info;
	status = GetAppInfo(&info);
	if (status < B_OK)
		return status;

	status = data->AddString("mime_sig", info.signature);
	if (status < B_OK)
		return status;

	return data->AddInt64("_pulse", fPulseRate);
}


/**
 * @brief Returns the initialization status of the BApplication.
 *
 * @return B_OK if the application was initialized successfully, or an error
 *         code describing the failure (e.g. B_BAD_VALUE for an invalid
 *         signature, B_ALREADY_RUNNING for single-launch conflicts).
 *
 * @see _InitData()
 */
status_t
BApplication::InitCheck() const
{
	return fInitError;
}


/**
 * @brief Starts the application's main message loop.
 *
 * Enters the BLooper message dispatching loop (via Loop()). This method
 * does not return until the application quits. After the loop exits, the
 * pulse runner is deleted.
 *
 * @return The thread_id of the application's main thread, or a negative
 *         error code if InitCheck() was not B_OK.
 *
 * @note Must be called from the main thread. Only call once per application
 *       lifetime.
 * @see Quit(), QuitRequested(), BLooper::Loop()
 */
thread_id
BApplication::Run()
{
	if (fInitError != B_OK)
		return fInitError;

	Loop();

	delete fPulseRunner;
	return fThread;
}


/**
 * @brief Terminates the application's message loop and deletes the object.
 *
 * Behavior depends on the calling context:
 * - If Run() has not been called, the BApplication is deleted immediately.
 * - If called from a thread other than the looper thread, a _QUIT_ message
 *   is posted to the looper's port. This may block if the port is full.
 * - If called from the looper thread, sets fTerminating to true, causing
 *   the message loop to exit naturally.
 *
 * @note The application MUST be locked before calling Quit(). If it is not,
 *       an error is printed and the method attempts to lock it. Failure to
 *       lock causes an early return.
 * @see Run(), QuitRequested()
 */
void
BApplication::Quit()
{
	bool unlock = false;
	if (!IsLocked()) {
		const char* name = Name();
		if (name == NULL)
			name = "no-name";

		printf("ERROR - you must Lock the application object before calling "
			   "Quit(), team=%" B_PRId32 ", looper=%s\n", Team(), name);
		unlock = true;
		if (!Lock())
			return;
	}
	// Delete the object, if not running only.
	if (!fRunCalled) {
		delete this;
	} else if (find_thread(NULL) != fThread) {
// ToDo: why shouldn't we set fTerminating to true directly in this case?
		// We are not the looper thread.
		// We push a _QUIT_ into the queue.
		// TODO: When BLooper::AddMessage() is done, use that instead of
		// PostMessage()??? This would overtake messages that are still at
		// the port.
		// NOTE: We must not unlock here -- otherwise we had to re-lock, which
		// may not work. This is bad, since, if the port is full, it
		// won't get emptier, as the looper thread needs to lock the object
		// before dispatching messages.
		while (PostMessage(_QUIT_, this) == B_WOULD_BLOCK)
			snooze(10000);
	} else {
		// We are the looper thread.
		// Just set fTerminating to true which makes us fall through the
		// message dispatching loop and return from Run().
		fTerminating = true;
	}

	// If we had to lock the object, unlock now.
	if (unlock)
		Unlock();
}


/**
 * @brief Hook called to determine if the application may quit.
 *
 * Delegates to _QuitAllWindows() without forcing. Each window's
 * QuitRequested() is called; if any window refuses, the application
 * will not quit.
 *
 * @return true if all windows agreed to quit, false if any window refused.
 *
 * @see _QuitAllWindows(), BWindow::QuitRequested()
 */
bool
BApplication::QuitRequested()
{
	return _QuitAllWindows(false);
}


/**
 * @brief Hook called at regular intervals when a pulse rate is set.
 *
 * Subclasses override this to perform periodic work. The interval is
 * configured via SetPulseRate(). The default implementation does nothing.
 *
 * @see SetPulseRate(), DispatchMessage()
 */
void
BApplication::Pulse()
{
	// supposed to be implemented by subclasses
}


/**
 * @brief Hook called when the application has finished launching and is ready to run.
 *
 * This is invoked in response to the B_READY_TO_RUN message posted during
 * _InitData(). It is called exactly once, after all B_ARGV_RECEIVED and
 * B_REFS_RECEIVED messages from launch have been processed. Subclasses
 * typically override this to open their initial window.
 *
 * The default implementation does nothing.
 *
 * @see _InitData(), DispatchMessage()
 */
void
BApplication::ReadyToRun()
{
	// supposed to be implemented by subclasses
}


/**
 * @brief Handles messages not dispatched by DispatchMessage().
 *
 * Processes the following message types:
 * - B_COUNT_PROPERTIES / B_GET_PROPERTY / B_SET_PROPERTY: Delegates to
 *   ScriptReceived() for BeOS scripting protocol handling.
 * - B_SILENT_RELAUNCH: Activates this application when a single-launch app
 *   is re-launched without arguments.
 * - kMsgAppServerStarted: Triggers reconnection to the app_server via
 *   _ReconnectToServer().
 * - All other messages are forwarded to BLooper::MessageReceived().
 *
 * @param message The incoming BMessage to handle.
 *
 * @see ScriptReceived(), _ReconnectToServer(), BLooper::MessageReceived()
 */
void
BApplication::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_COUNT_PROPERTIES:
		case B_GET_PROPERTY:
		case B_SET_PROPERTY:
		{
			int32 index;
			BMessage specifier;
			int32 what;
			const char* property = NULL;
			if (message->GetCurrentSpecifier(&index, &specifier, &what,
					&property) < B_OK
				|| !ScriptReceived(message, index, &specifier, what,
					property)) {
				BLooper::MessageReceived(message);
			}
			break;
		}

		case B_SILENT_RELAUNCH:
			// Sent to a B_SINGLE_LAUNCH application when it's launched again
			// (see _InitData())
			be_roster->ActivateApp(Team());
			break;

		case kMsgAppServerStarted:
			_ReconnectToServer();
			break;

		default:
			BLooper::MessageReceived(message);
	}
}


/**
 * @brief Hook called when the application receives command-line arguments.
 *
 * Invoked when a B_ARGV_RECEIVED message is dispatched, either at launch
 * or when a running single-launch application is re-launched with arguments.
 * The default implementation does nothing; subclasses override this to
 * process command-line parameters.
 *
 * @param argc The number of arguments (always >= 1; argv[0] is the app path).
 * @param argv A NULL-terminated array of argument strings.
 *
 * @see _ArgvReceived(), DispatchMessage()
 */
void
BApplication::ArgvReceived(int32 argc, char** argv)
{
	// supposed to be implemented by subclasses
}


/**
 * @brief Hook called when the application is activated or deactivated.
 *
 * Dispatched in response to a B_APP_ACTIVATED message from the app_server.
 * The default implementation does nothing; subclasses override this to
 * respond to activation changes (e.g. updating menus or focus state).
 *
 * @param active true if the application has become the active (foreground)
 *               application, false if it has been deactivated.
 *
 * @see DispatchMessage()
 */
void
BApplication::AppActivated(bool active)
{
	// supposed to be implemented by subclasses
}


/**
 * @brief Hook called when the application receives file references.
 *
 * Dispatched in response to a B_REFS_RECEIVED message, typically when files
 * are dropped on the application icon or opened via Tracker. DispatchMessage()
 * adds the referenced entries to the recent documents/folders lists before
 * calling this hook. The default implementation does nothing.
 *
 * @param message The B_REFS_RECEIVED message containing "refs" entry_ref fields.
 *
 * @see DispatchMessage()
 */
void
BApplication::RefsReceived(BMessage* message)
{
	// supposed to be implemented by subclasses
}


/**
 * @brief Hook called when the user requests "About" information.
 *
 * Dispatched in response to a B_ABOUT_REQUESTED message, typically from a
 * menu item. Subclasses override this to display an about dialog. The
 * default implementation does nothing.
 *
 * @see DispatchMessage()
 */
void
BApplication::AboutRequested()
{
	// supposed to be implemented by subclasses
}


/**
 * @brief Resolves a scripting specifier to the appropriate handler.
 *
 * Matches the incoming specifier against sPropertyInfo to find windows or
 * loopers by index, name, or ID. If the specifier targets a window or
 * looper, the message is forwarded to that object. If the specifier targets
 * the application itself (e.g. "Name"), returns @c this. Unrecognized
 * specifiers are delegated to BLooper::ResolveSpecifier().
 *
 * @param message   The scripting message being resolved.
 * @param index     The current specifier index in the message.
 * @param specifier The current specifier BMessage.
 * @param what      The specifier type (e.g. B_INDEX_SPECIFIER, B_NAME_SPECIFIER).
 * @param property  The property name being targeted.
 *
 * @return The BHandler that should handle the message, or NULL if the
 *         message was forwarded or an error reply was sent.
 *
 * @see ScriptReceived(), GetSupportedSuites(), sPropertyInfo
 */
BHandler*
BApplication::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BPropertyInfo propInfo(sPropertyInfo);
	status_t err = B_OK;
	uint32 data;

	if (propInfo.FindMatch(message, 0, specifier, what, property, &data) >= 0) {
		switch (data) {
			case kWindowByIndex:
			{
				int32 index;
				err = specifier->FindInt32("index", &index);
				if (err != B_OK)
					break;

				if (what == B_REVERSE_INDEX_SPECIFIER)
					index = CountWindows() - index;

				BWindow* window = WindowAt(index);
				if (window != NULL) {
					message->PopSpecifier();
					BMessenger(window).SendMessage(message);
				} else
					err = B_BAD_INDEX;
				break;
			}

			case kWindowByName:
			{
				const char* name;
				err = specifier->FindString("name", &name);
				if (err != B_OK)
					break;

				for (int32 i = 0;; i++) {
					BWindow* window = WindowAt(i);
					if (window == NULL) {
						err = B_NAME_NOT_FOUND;
						break;
					}
					if (window->Title() != NULL && !strcmp(window->Title(),
							name)) {
						message->PopSpecifier();
						BMessenger(window).SendMessage(message);
						break;
					}
				}
				break;
			}

			case kLooperByIndex:
			{
				int32 index;
				err = specifier->FindInt32("index", &index);
				if (err != B_OK)
					break;

				if (what == B_REVERSE_INDEX_SPECIFIER)
					index = CountLoopers() - index;

				BLooper* looper = LooperAt(index);
				if (looper != NULL) {
					message->PopSpecifier();
					BMessenger(looper).SendMessage(message);
				} else
					err = B_BAD_INDEX;

				break;
			}

			case kLooperByID:
				// TODO: implement getting looper by ID!
				break;

			case kLooperByName:
			{
				const char* name;
				err = specifier->FindString("name", &name);
				if (err != B_OK)
					break;

				for (int32 i = 0;; i++) {
					BLooper* looper = LooperAt(i);
					if (looper == NULL) {
						err = B_NAME_NOT_FOUND;
						break;
					}
					if (looper->Name() != NULL
						&& strcmp(looper->Name(), name) == 0) {
						message->PopSpecifier();
						BMessenger(looper).SendMessage(message);
						break;
					}
				}
				break;
			}

			case kApplication:
				return this;
		}
	} else {
		return BLooper::ResolveSpecifier(message, index, specifier, what,
			property);
	}

	if (err != B_OK) {
		BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
		reply.AddInt32("error", err);
		reply.AddString("message", strerror(err));
		message->SendReply(&reply);
	}

	return NULL;

}


/**
 * @brief Shows the mouse cursor if it was previously hidden.
 *
 * Sends an AS_SHOW_CURSOR message to the app_server to make the cursor
 * visible again after a call to HideCursor() or ObscureCursor().
 *
 * @see HideCursor(), ObscureCursor(), IsCursorHidden()
 */
void
BApplication::ShowCursor()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SHOW_CURSOR);
	link.Flush();
}


/**
 * @brief Hides the mouse cursor.
 *
 * Sends an AS_HIDE_CURSOR message to the app_server. The cursor remains
 * hidden until ShowCursor() is called. Unlike ObscureCursor(), movement
 * does not automatically restore visibility.
 *
 * @see ShowCursor(), ObscureCursor(), IsCursorHidden()
 */
void
BApplication::HideCursor()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_HIDE_CURSOR);
	link.Flush();
}


/**
 * @brief Hides the cursor until the mouse is moved.
 *
 * Sends an AS_OBSCURE_CURSOR message to the app_server. The cursor
 * disappears immediately but reappears automatically on the next mouse
 * movement. Useful during keyboard input to avoid visual clutter.
 *
 * @see ShowCursor(), HideCursor(), IsCursorHidden()
 */
void
BApplication::ObscureCursor()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_OBSCURE_CURSOR);
	link.Flush();
}


/**
 * @brief Queries whether the mouse cursor is currently hidden.
 *
 * Sends an AS_QUERY_CURSOR_HIDDEN message to the app_server and waits
 * synchronously for a reply.
 *
 * @return true if the cursor is hidden, false otherwise.
 *
 * @see ShowCursor(), HideCursor(), ObscureCursor()
 */
bool
BApplication::IsCursorHidden() const
{
	BPrivate::AppServerLink link;
	int32 status = B_ERROR;
	link.StartMessage(AS_QUERY_CURSOR_HIDDEN);
	link.FlushWithReply(status);

	return status == B_OK;
}


/**
 * @brief Sets the cursor from raw cursor data.
 *
 * Wraps the raw data in a temporary BCursor and calls the BCursor-based
 * SetCursor() overload with synchronous mode enabled.
 *
 * @param cursorData Pointer to raw cursor bitmap data in the format expected
 *                   by the BCursor(const void*) constructor.
 *
 * @see SetCursor(const BCursor*, bool), BCursor
 */
void
BApplication::SetCursor(const void* cursorData)
{
	BCursor cursor(cursorData);
	SetCursor(&cursor, true);
		// forces the cursor to be sync'ed
}


/**
 * @brief Sets the application cursor to the given BCursor.
 *
 * Sends an AS_SET_CURSOR message to the app_server with the cursor's
 * server-side token. When @a sync is true, the call blocks until the
 * app_server confirms the cursor change.
 *
 * @param cursor Pointer to the BCursor to activate.
 * @param sync   If true, the call is synchronous (waits for server reply).
 *               If false, the message is sent asynchronously.
 *
 * @see SetCursor(const void*), ShowCursor(), HideCursor()
 */
void
BApplication::SetCursor(const BCursor* cursor, bool sync)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_SET_CURSOR);
	link.Attach<bool>(sync);
	link.Attach<int32>(cursor->fServerToken);

	if (sync) {
		int32 code;
		link.FlushWithReply(code);
	} else
		link.Flush();
}


/**
 * @brief Returns the number of windows owned by this application.
 *
 * Counts only on-screen windows, excluding menu windows and offscreen windows.
 *
 * @return The number of visible, non-menu windows.
 *
 * @see WindowAt(), _CountWindows()
 */
int32
BApplication::CountWindows() const
{
	return _CountWindows(false);
		// we're ignoring menu windows
}


/**
 * @brief Returns the window at the given index.
 *
 * Returns only on-screen windows, excluding menu windows and offscreen windows.
 * The index is zero-based among non-menu windows.
 *
 * @param index Zero-based index of the window to retrieve.
 *
 * @return Pointer to the BWindow at the given index, or NULL if the index
 *         is out of range.
 *
 * @see CountWindows(), _WindowAt()
 */
BWindow*
BApplication::WindowAt(int32 index) const
{
	return _WindowAt(index, false);
		// we're ignoring menu windows
}


/**
 * @brief Returns the total number of loopers (including windows) in this team.
 *
 * Acquires the global looper list lock to safely count all registered loopers.
 *
 * @return The number of loopers, or B_ERROR if the looper list lock could
 *         not be acquired.
 *
 * @see LooperAt(), CountWindows()
 */
int32
BApplication::CountLoopers() const
{
	AutoLocker<BLooperList> ListLock(gLooperList);
	if (ListLock.IsLocked())
		return gLooperList.CountLoopers();

	// Some bad, non-specific thing has happened
	return B_ERROR;
}


/**
 * @brief Returns the looper at the given index from the global looper list.
 *
 * Acquires the global looper list lock and retrieves the looper at the
 * specified zero-based index. The list includes all loopers (windows and
 * non-windows) in this team.
 *
 * @param index Zero-based index of the looper to retrieve.
 *
 * @return Pointer to the BLooper at the given index, or NULL if the index
 *         is out of range or the looper list lock could not be acquired.
 *
 * @see CountLoopers(), WindowAt()
 */
BLooper*
BApplication::LooperAt(int32 index) const
{
	BLooper* looper = NULL;
	AutoLocker<BLooperList> listLock(gLooperList);
	if (listLock.IsLocked())
		looper = gLooperList.LooperAt(index);

	return looper;
}


/**
 * @brief Registers a non-window looper to be quit when the application exits.
 *
 * Adds the looper to sOnQuitLooperList so that BApplication's destructor
 * will lock and quit it during shutdown. Only non-window BLoopers may be
 * registered; passing a BWindow returns B_BAD_VALUE, since windows are
 * managed separately via _QuitAllWindows().
 *
 * @param looper The BLooper to register. Must not be a BWindow.
 *
 * @return B_OK on success, B_BAD_VALUE if @a looper is a BWindow,
 *         B_ERROR if the looper is already registered or could not be added.
 *
 * @see UnregisterLooper(), ~BApplication()
 */
status_t
BApplication::RegisterLooper(BLooper* looper)
{
	BWindow* window = dynamic_cast<BWindow*>(looper);
	if (window != NULL)
		return B_BAD_VALUE;

	if (sOnQuitLooperList.HasItem(looper))
		return B_ERROR;

	if (sOnQuitLooperList.AddItem(looper) != true)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Unregisters a non-window looper from the on-quit list.
 *
 * Removes the looper from sOnQuitLooperList so it will no longer be
 * automatically quit during application shutdown.
 *
 * @param looper The BLooper to unregister. Must not be a BWindow.
 *
 * @return B_OK on success, B_BAD_VALUE if @a looper is a BWindow,
 *         B_ERROR if the looper was not registered or could not be removed.
 *
 * @see RegisterLooper()
 */
status_t
BApplication::UnregisterLooper(BLooper* looper)
{
	BWindow* window = dynamic_cast<BWindow*>(looper);
	if (window != NULL)
		return B_BAD_VALUE;

	if (!sOnQuitLooperList.HasItem(looper))
		return B_ERROR;

	if (sOnQuitLooperList.RemoveItem(looper) != true)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Returns whether the application is still in the launching phase.
 *
 * The application is considered "launching" until ReadyToRun() has been
 * called (i.e. until the B_READY_TO_RUN message has been dispatched).
 *
 * @return true if ReadyToRun() has not yet been called, false otherwise.
 *
 * @see ReadyToRun()
 */
bool
BApplication::IsLaunching() const
{
	return !fReadyToRunCalled;
}


/**
 * @brief Returns the application's MIME signature string.
 *
 * @return The MIME signature passed to the constructor (e.g. "application/x-vnd.MyApp").
 *
 * @see _InitData()
 */
const char*
BApplication::Signature() const
{
	return fAppName;
}


/**
 * @brief Retrieves information about this running application from the roster.
 *
 * @param info Pointer to an app_info structure to be filled with the
 *             application's registration data (signature, ref, team, etc.).
 *
 * @return B_OK on success, B_NO_INIT if be_app or be_roster is NULL,
 *         or another error from the roster.
 *
 * @see BRoster::GetRunningAppInfo()
 */
status_t
BApplication::GetAppInfo(app_info* info) const
{
	if (be_app == NULL || be_roster == NULL)
		return B_NO_INIT;
	return be_roster->GetRunningAppInfo(be_app->Team(), info);
}


/**
 * @brief Returns the application's resource file, lazily initialized.
 *
 * On the first call, invokes _InitAppResources() via pthread_once to open
 * the application's executable file and load its resources. Subsequent
 * calls return the cached BResources pointer.
 *
 * @return Pointer to the application's BResources object, or NULL if the
 *         resources could not be loaded.
 *
 * @note This is a static method and may be called before Run() or even
 *       before a BApplication instance is created.
 * @see _InitAppResources()
 */
BResources*
BApplication::AppResources()
{
	if (sAppResources == NULL)
		pthread_once(&sAppResourcesInitOnce, &_InitAppResources);

	return sAppResources;
}


/**
 * @brief Dispatches a message to the appropriate hook method.
 *
 * If @a handler is not @c this, delegates to BLooper::DispatchMessage().
 * Otherwise, handles the following system messages directly:
 * - B_ARGV_RECEIVED: Calls _ArgvReceived() to parse and forward to ArgvReceived().
 * - B_REFS_RECEIVED: Adds refs to recent lists, then calls RefsReceived().
 * - B_READY_TO_RUN: Calls ReadyToRun() exactly once.
 * - B_ABOUT_REQUESTED: Calls AboutRequested().
 * - B_PULSE: Calls Pulse().
 * - B_APP_ACTIVATED: Extracts the "active" flag and calls AppActivated().
 * - B_COLORS_UPDATED: Forwards to all non-offscreen windows.
 * - _SHOW_DRAG_HANDLES_: Updates dragger visibility.
 * - All others: Delegates to BLooper::DispatchMessage().
 *
 * @param message The BMessage to dispatch.
 * @param handler The target BHandler for the message.
 *
 * @see MessageReceived(), ReadyToRun(), ArgvReceived(), RefsReceived()
 */
void
BApplication::DispatchMessage(BMessage* message, BHandler* handler)
{
	if (handler != this) {
		// it's not ours to dispatch
		BLooper::DispatchMessage(message, handler);
		return;
	}

	switch (message->what) {
		case B_ARGV_RECEIVED:
			_ArgvReceived(message);
			break;

		case B_REFS_RECEIVED:
		{
			// this adds the refs that are part of this message to the recent
			// lists, but only folders and documents are handled here
			entry_ref ref;
			int32 i = 0;
			while (message->FindRef("refs", i++, &ref) == B_OK) {
				BEntry entry(&ref, true);
				if (entry.InitCheck() != B_OK)
					continue;

				if (entry.IsDirectory())
					BRoster().AddToRecentFolders(&ref);
				else {
					// filter out applications, we only want to have documents
					// in the recent files list
					BNode node(&entry);
					BNodeInfo info(&node);

					char mimeType[B_MIME_TYPE_LENGTH];
					if (info.GetType(mimeType) != B_OK
						|| strcasecmp(mimeType, B_APP_MIME_TYPE))
						BRoster().AddToRecentDocuments(&ref);
				}
			}

			RefsReceived(message);
			break;
		}

		case B_READY_TO_RUN:
			if (!fReadyToRunCalled) {
				ReadyToRun();
				fReadyToRunCalled = true;
			}
			break;

		case B_ABOUT_REQUESTED:
			AboutRequested();
			break;

		case B_PULSE:
			Pulse();
			break;

		case B_APP_ACTIVATED:
		{
			bool active;
			if (message->FindBool("active", &active) == B_OK)
				AppActivated(active);
			break;
		}

		case B_COLORS_UPDATED:
		{
			AutoLocker<BLooperList> listLock(gLooperList);
			if (!listLock.IsLocked())
				break;

			BWindow* window = NULL;
			uint32 count = gLooperList.CountLoopers();
			for (uint32 index = 0; index < count; ++index) {
				window = dynamic_cast<BWindow*>(gLooperList.LooperAt(index));
				if (window == NULL || (window != NULL && window->fOffscreen))
					continue;
				window->PostMessage(message);
			}
			break;
		}

		case _SHOW_DRAG_HANDLES_:
		{
			bool show;
			if (message->FindBool("show", &show) != B_OK)
				break;

			BDragger::Private::UpdateShowAllDraggers(show);
			break;
		}

		// TODO: Handle these as well
		case _DISPOSE_DRAG_:
		case _PING_:
			puts("not yet handled message:");
			DBG(message->PrintToStream());
			break;

		default:
			BLooper::DispatchMessage(message, handler);
			break;
	}
}


/**
 * @brief Sets the interval for B_PULSE messages.
 *
 * Creates or reconfigures a BMessageRunner that posts B_PULSE messages to
 * the application at the specified interval. Setting the rate to zero
 * disables pulse messages. The granularity is 100,000 microseconds (0.1s)
 * as documented in the BeBook.
 *
 * @param rate The desired pulse interval in microseconds. Values below zero
 *             are clamped to zero. The actual rate is rounded down to the
 *             nearest 100,000 microsecond boundary.
 *
 * @note This method locks the application internally. If the lock cannot
 *       be acquired, the call has no effect.
 * @see Pulse(), DispatchMessage()
 */
void
BApplication::SetPulseRate(bigtime_t rate)
{
	if (rate < 0)
		rate = 0;

	// BeBook states that we have only 100,000 microseconds granularity
	rate -= rate % 100000;

	if (!Lock())
		return;

	if (rate != 0) {
		// reset existing pulse runner, or create new one
		if (fPulseRunner == NULL) {
			BMessage pulse(B_PULSE);
			fPulseRunner = new BMessageRunner(be_app_messenger, &pulse, rate);
		} else
			fPulseRunner->SetInterval(rate);
	} else {
		// turn off pulse messages
		delete fPulseRunner;
		fPulseRunner = NULL;
	}

	fPulseRate = rate;
	Unlock();
}


/**
 * @brief Reports the scripting suites supported by this application.
 *
 * Adds the "suite/vnd.Be-application" suite name and the flattened
 * sPropertyInfo table to @a data, then chains to BLooper::GetSupportedSuites().
 *
 * @param data The BMessage to populate with suite information.
 *
 * @return B_OK on success, B_BAD_VALUE if @a data is NULL, or another
 *         error code if adding data fails.
 *
 * @see ResolveSpecifier(), ScriptReceived(), sPropertyInfo
 */
status_t
BApplication::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = data->AddString("suites", "suite/vnd.Be-application");
	if (status == B_OK) {
		BPropertyInfo propertyInfo(sPropertyInfo);
		status = data->AddFlat("messages", &propertyInfo);
		if (status == B_OK)
			status = BLooper::GetSupportedSuites(data);
	}

	return status;
}


/**
 * @brief Performs a reserved virtual function call for binary compatibility.
 *
 * Delegates to BLooper::Perform(). This mechanism allows future additions
 * to the vtable without breaking binary compatibility.
 *
 * @param d   The perform_code identifying the operation.
 * @param arg Operation-specific argument data.
 *
 * @return The result from BLooper::Perform().
 *
 * @see BLooper::Perform()
 */
status_t
BApplication::Perform(perform_code d, void* arg)
{
	return BLooper::Perform(d, arg);
}


/** @brief Reserved virtual function slots for future binary-compatible extensions. */
void BApplication::_ReservedApplication1() {}
void BApplication::_ReservedApplication2() {}
void BApplication::_ReservedApplication3() {}
void BApplication::_ReservedApplication4() {}
void BApplication::_ReservedApplication5() {}
void BApplication::_ReservedApplication6() {}
void BApplication::_ReservedApplication7() {}
void BApplication::_ReservedApplication8() {}


/**
 * @brief Handles scripting property requests for the application.
 *
 * Processes B_GET_PROPERTY and B_COUNT_PROPERTIES requests for the
 * following properties:
 * - "Loopers": Returns BMessengers for all loopers.
 * - "Windows": Returns BMessengers for all windows.
 * - "Window": Returns a BMessenger for a specific window (by index or name).
 * - "Looper": Returns a BMessenger for a specific looper (by index, name, or ID).
 * - "Name": Returns the application's name.
 * - "Looper" (COUNT): Returns the count of loopers.
 * - "Window" (COUNT): Returns the count of windows.
 *
 * @param message   The scripting request message.
 * @param index     The current specifier index.
 * @param specifier The current specifier BMessage.
 * @param what      The specifier type (e.g. B_INDEX_SPECIFIER).
 * @param property  The property name being queried.
 *
 * @return true if the message was handled and a reply was sent, false if
 *         the syntax was not recognized (caller should handle it).
 *
 * @see ResolveSpecifier(), MessageReceived(), GetSupportedSuites()
 */
bool
BApplication::ScriptReceived(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	BMessage reply(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;

	switch (message->what) {
		case B_GET_PROPERTY:
			if (strcmp("Loopers", property) == 0) {
				int32 count = CountLoopers();
				err = B_OK;
				for (int32 i=0; err == B_OK && i<count; i++) {
					BMessenger messenger(LooperAt(i));
					err = reply.AddMessenger("result", messenger);
				}
			} else if (strcmp("Windows", property) == 0) {
				int32 count = CountWindows();
				err = B_OK;
				for (int32 i=0; err == B_OK && i<count; i++) {
					BMessenger messenger(WindowAt(i));
					err = reply.AddMessenger("result", messenger);
				}
			} else if (strcmp("Window", property) == 0) {
				switch (what) {
					case B_INDEX_SPECIFIER:
					case B_REVERSE_INDEX_SPECIFIER:
					{
						int32 index = -1;
						err = specifier->FindInt32("index", &index);
						if (err != B_OK)
							break;

						if (what == B_REVERSE_INDEX_SPECIFIER)
							index = CountWindows() - index;

						err = B_BAD_INDEX;
						BWindow* window = WindowAt(index);
						if (window == NULL)
							break;

						BMessenger messenger(window);
						err = reply.AddMessenger("result", messenger);
						break;
					}

					case B_NAME_SPECIFIER:
					{
						const char* name;
						err = specifier->FindString("name", &name);
						if (err != B_OK)
							break;
						err = B_NAME_NOT_FOUND;
						for (int32 i = 0; i < CountWindows(); i++) {
							BWindow* window = WindowAt(i);
							if (window && window->Name() != NULL
								&& !strcmp(window->Name(), name)) {
								BMessenger messenger(window);
								err = reply.AddMessenger("result", messenger);
								break;
							}
						}
						break;
					}
				}
			} else if (strcmp("Looper", property) == 0) {
				switch (what) {
					case B_INDEX_SPECIFIER:
					case B_REVERSE_INDEX_SPECIFIER:
					{
						int32 index = -1;
						err = specifier->FindInt32("index", &index);
						if (err != B_OK)
							break;

						if (what == B_REVERSE_INDEX_SPECIFIER)
							index = CountLoopers() - index;

						err = B_BAD_INDEX;
						BLooper* looper = LooperAt(index);
						if (looper == NULL)
							break;

						BMessenger messenger(looper);
						err = reply.AddMessenger("result", messenger);
						break;
					}

					case B_NAME_SPECIFIER:
					{
						const char* name;
						err = specifier->FindString("name", &name);
						if (err != B_OK)
							break;
						err = B_NAME_NOT_FOUND;
						for (int32 i = 0; i < CountLoopers(); i++) {
							BLooper* looper = LooperAt(i);
							if (looper != NULL && looper->Name()
								&& strcmp(looper->Name(), name) == 0) {
								BMessenger messenger(looper);
								err = reply.AddMessenger("result", messenger);
								break;
							}
						}
						break;
					}

					case B_ID_SPECIFIER:
					{
						// TODO
						debug_printf("Looper's ID specifier used but not "
							"implemented.\n");
						break;
					}
				}
			} else if (strcmp("Name", property) == 0)
				err = reply.AddString("result", Name());

			break;

		case B_COUNT_PROPERTIES:
			if (strcmp("Looper", property) == 0)
				err = reply.AddInt32("result", CountLoopers());
			else if (strcmp("Window", property) == 0)
				err = reply.AddInt32("result", CountWindows());

			break;
	}
	if (err == B_BAD_SCRIPT_SYNTAX)
		return false;

	if (err < B_OK) {
		reply.what = B_MESSAGE_NOT_UNDERSTOOD;
		reply.AddString("message", strerror(err));
	}
	reply.AddInt32("error", err);
	message->SendReply(&reply);

	return true;
}


/**
 * @brief Begins tracking a selection rectangle on screen.
 *
 * Sends an AS_BEGIN_RECT_TRACKING message to the app_server to display
 * a selection rectangle at the given position.
 *
 * @param rect       The initial bounding rectangle for the tracking feedback.
 * @param trackWhole If true, tracks the entire rectangle; if false, tracks
 *                   only the outline.
 *
 * @see EndRectTracking()
 */
void
BApplication::BeginRectTracking(BRect rect, bool trackWhole)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_BEGIN_RECT_TRACKING);
	link.Attach<BRect>(rect);
	link.Attach<int32>(trackWhole);
	link.Flush();
}


/**
 * @brief Ends selection rectangle tracking on screen.
 *
 * Sends an AS_END_RECT_TRACKING message to the app_server to stop
 * displaying the selection rectangle.
 *
 * @see BeginRectTracking()
 */
void
BApplication::EndRectTracking()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_END_RECT_TRACKING);
	link.Flush();
}


/**
 * @brief Creates and initializes the server-side memory allocator.
 *
 * Allocates a new ServerMemoryAllocator instance used for sharing memory
 * areas with the app_server (e.g. for bitmaps and shared read-only data).
 *
 * @return B_OK on success, B_NO_MEMORY if allocation fails, or an error
 *         from ServerMemoryAllocator::InitCheck().
 *
 * @see _ConnectToServer(), ServerMemoryAllocator
 */
status_t
BApplication::_SetupServerAllocator()
{
	fServerAllocator = new (std::nothrow) BPrivate::ServerMemoryAllocator();
	if (fServerAllocator == NULL)
		return B_NO_MEMORY;

	return fServerAllocator->InitCheck();
}


/**
 * @brief Initializes the GUI context: app_server connection, Interface Kit, and cursors.
 *
 * Performs three initialization steps in order:
 * 1. Connects to the app_server via _ConnectToServer().
 * 2. Initializes the Interface Kit via _init_interface_kit_() (which depends
 *    on be_app being set and the server link being active).
 * 3. Creates the global system cursors (B_CURSOR_SYSTEM_DEFAULT, B_CURSOR_I_BEAM).
 * 4. Records the initial workspace number.
 *
 * @return B_OK on success, or an error code from _ConnectToServer() or
 *         _init_interface_kit_().
 *
 * @note This method must be called after be_app has been set, since
 *       AppServerLink construction depends on the global be_app pointer.
 * @see _ConnectToServer(), _InitData()
 */
status_t
BApplication::_InitGUIContext()
{
	// An app_server connection is necessary for a lot of stuff, so get that first.
	status_t error = _ConnectToServer();
	if (error != B_OK)
		return error;

	// Initialize the IK after we have set be_app because of a construction
	// of a AppServerLink (which depends on be_app) nested inside the call
	// to get_menu_info.
	error = _init_interface_kit_();
	if (error != B_OK)
		return error;

	// create global system cursors
	B_CURSOR_SYSTEM_DEFAULT = new BCursor(B_HAND_CURSOR);
	B_CURSOR_I_BEAM = new BCursor(B_I_BEAM_CURSOR);

	// TODO: would be nice to get the workspace at launch time from the registrar
	fInitialWorkspace = current_workspace();

	return B_OK;
}


/**
 * @brief Establishes a connection to the app_server.
 *
 * Creates a desktop connection port link, then sends an AS_CREATE_APP message
 * to register this application with the app_server. The server responds with
 * a dedicated communication port, a shared read-only memory area, and the
 * server's team ID. After successful registration:
 * - The server link is reconfigured to talk to the application's dedicated
 *   server port instead of the main app_server port.
 * - The server memory allocator is set up via _SetupServerAllocator().
 * - The shared read-only area is mapped into fServerReadOnlyMemory.
 *
 * @return B_OK on success, B_ERROR if the server rejected the connection
 *         (triggers a debugger() call), or another error from port creation
 *         or memory allocation.
 *
 * @note A debugger() call is triggered if the app_server cannot provide a
 *       communication port, which is a fatal condition.
 * @see _InitGUIContext(), _ReconnectToServer(), _SetupServerAllocator()
 */
status_t
BApplication::_ConnectToServer()
{
	status_t status
		= create_desktop_connection(fServerLink, "a<app_server", 100);
	if (status != B_OK)
		return status;

	// AS_CREATE_APP:
	//
	// Attach data:
	// 1) port_id - receiver port of a regular app
	// 2) port_id - looper port for this BApplication
	// 3) team_id - team identification field
	// 4) int32 - handler ID token of the app
	// 5) char* - signature of the regular app

	fServerLink->StartMessage(AS_CREATE_APP);
	fServerLink->Attach<port_id>(fServerLink->ReceiverPort());
	fServerLink->Attach<port_id>(_get_looper_port_(this));
	fServerLink->Attach<team_id>(Team());
	fServerLink->Attach<int32>(_get_object_token_(this));
	fServerLink->AttachString(fAppName);

	area_id sharedReadOnlyArea;
	team_id serverTeam;
	port_id serverPort;

	int32 code;
	if (fServerLink->FlushWithReply(code) == B_OK
		&& code == B_OK) {
		// We don't need to contact the main app_server anymore
		// directly; we now talk to our server alter ego only.
		fServerLink->Read<port_id>(&serverPort);
		fServerLink->Read<area_id>(&sharedReadOnlyArea);
		fServerLink->Read<team_id>(&serverTeam);
	} else {
		fServerLink->SetSenderPort(-1);
		debugger("BApplication: couldn't obtain new app_server comm port");
		return B_ERROR;
	}
	fServerLink->SetTargetTeam(serverTeam);
	fServerLink->SetSenderPort(serverPort);

	status = _SetupServerAllocator();
	if (status != B_OK)
		return status;

	area_id area;
	uint8* base;
	status = fServerAllocator->AddArea(sharedReadOnlyArea, area, base, true);
	if (status < B_OK)
		return status;

	fServerReadOnlyMemory = base;

	return B_OK;
}


/**
 * @brief Reconnects to the app_server after a server restart.
 *
 * Checks whether the current server connection is still valid by querying
 * the server's team. If the connection is stale (server team no longer
 * exists), tears down the old connection, re-establishes it via
 * _ConnectToServer(), and notifies all windows with kMsgAppServerStarted
 * so they can reconnect their server-side state. Also reconnects bitmaps
 * and pictures.
 *
 * @note A debugger() call is triggered if reconnection fails.
 * @see _ConnectToServer(), MessageReceived()
 */
void
BApplication::_ReconnectToServer()
{
	team_info dummy;
	if (get_team_info(fServerLink->TargetTeam(), &dummy) == B_OK) {
		// We're already connected to the correct server.
		return;
	}

	// the sender port belongs to the app_server
	delete_port(fServerLink->ReceiverPort());

	if (_ConnectToServer() != B_OK)
		debugger("Can't reconnect to app server!");

	AutoLocker<BLooperList> listLock(gLooperList);
	if (!listLock.IsLocked())
		return;

	uint32 count = gLooperList.CountLoopers();
	for (uint32 i = 0; i < count ; i++) {
		BWindow* window = dynamic_cast<BWindow*>(gLooperList.LooperAt(i));
		if (window == NULL)
			continue;
		BMessenger windowMessenger(window);
		windowMessenger.SendMessage(kMsgAppServerStarted);
	}

	reconnect_bitmaps_to_app_server();
	reconnect_pictures_to_app_server();
}


/**
 * @name Drag-and-drop support (unimplemented stubs)
 *
 * These methods are intended to handle drag-and-drop operations but are
 * not yet implemented. They are compiled out via @c \#if @c 0.
 * @{
 */
#if 0
void
BApplication::send_drag(BMessage* message, int32 vs_token, BPoint offset,
	BRect dragRect, BHandler* replyTo)
{
	// TODO: implement
}


void
BApplication::send_drag(BMessage* message, int32 vs_token, BPoint offset,
	int32 bitmapToken, drawing_mode dragMode, BHandler* replyTo)
{
	// TODO: implement
}


void
BApplication::write_drag(_BSession_* session, BMessage* message)
{
	// TODO: implement
}
#endif
/** @} */


/**
 * @brief Iterates over all windows, asking each to quit.
 *
 * Walks the window list and for each window:
 * 1. Locks the window.
 * 2. Skips file panel windows unless @a quitFilePanels is true.
 * 3. If not forcing, calls QuitRequested(); if the window refuses, returns false.
 * 4. Re-locks and calls Quit() on the window.
 * 5. Restarts iteration from the beginning (the list may have changed).
 *
 * @param quitFilePanels If true, file panel windows are also quit. If false,
 *                       they are skipped.
 * @param force          If true, windows are quit unconditionally without
 *                       calling QuitRequested().
 *
 * @return true if all targeted windows were quit, false if any window
 *         refused to quit (only possible when @a force is false).
 *
 * @note Window pointers may become stale if a window is quit by a previously
 *       quit window. Lock() on a stale pointer returns false, which is handled
 *       gracefully.
 * @see _QuitAllWindows(), QuitRequested()
 */
bool
BApplication::_WindowQuitLoop(bool quitFilePanels, bool force)
{
	int32 index = 0;
	while (true) {
		 BWindow* window = WindowAt(index);
		 if (window == NULL)
		 	break;

		// NOTE: the window pointer might be stale, in case the looper
		// was already quit by quitting an earlier looper... but fortunately,
		// we can still call Lock() on the invalid pointer, and it
		// will return false...
		if (!window->Lock())
			continue;

		// don't quit file panels if we haven't been asked for it
		if (!quitFilePanels && window->IsFilePanel()) {
			window->Unlock();
			index++;
			continue;
		}

		if (!force && !window->QuitRequested()
			&& !(quitFilePanels && window->IsFilePanel())) {
			// the window does not want to quit, so we don't either
			window->Unlock();
			return false;
		}

		// Re-lock, just to make sure that the user hasn't done nasty
		// things in QuitRequested(). Quit() unlocks fully, thus
		// double-locking is harmless.
		if (window->Lock())
			window->Quit();

		index = 0;
			// we need to continue at the start of the list again - it
			// might have changed
	}

	return true;
}


/**
 * @brief Quits all application windows in two passes.
 *
 * First pass: quits non-file-panel windows. Second pass: quits file panel
 * windows. The application is temporarily unlocked during this process to
 * avoid deadlocks, since BWindow::QuitRequested() may need to lock the
 * application.
 *
 * @param force If true, windows are quit unconditionally. If false, each
 *              window's QuitRequested() is consulted.
 *
 * @return true if all windows were quit, false if any window refused.
 *
 * @note The application must be locked when calling this method. It is
 *       temporarily unlocked internally and re-locked before returning.
 * @see _WindowQuitLoop(), QuitRequested(), ~BApplication()
 */
bool
BApplication::_QuitAllWindows(bool force)
{
	AssertLocked();

	// We need to unlock here because BWindow::QuitRequested() must be
	// allowed to lock the application - which would cause a deadlock
	Unlock();

	bool quit = _WindowQuitLoop(false, force);
	if (quit)
		quit = _WindowQuitLoop(true, force);

	Lock();

	return quit;
}


/**
 * @brief Parses a B_ARGV_RECEIVED message and calls the ArgvReceived() hook.
 *
 * Extracts the "argc" and "argv" fields from the message, builds a
 * NULL-terminated argv array by strdup'ing each string, calls
 * ArgvReceived() with the parsed arguments, then frees the array.
 *
 * @param message The B_ARGV_RECEIVED BMessage containing "argc" (int32)
 *                and "argv" (string array) fields.
 *
 * @note On memory allocation failure, the method returns early without
 *       calling the hook. On parse errors, a diagnostic is printed.
 * @see ArgvReceived(), DispatchMessage(), fill_argv_message()
 */
void
BApplication::_ArgvReceived(BMessage* message)
{
	ASSERT(message != NULL);

	// build the argv vector
	status_t error = B_OK;
	int32 argc = 0;
	char** argv = NULL;
	if (message->FindInt32("argc", &argc) == B_OK && argc > 0) {
		// allocate a NULL terminated array
		argv = new(std::nothrow) char*[argc + 1];
		if (argv == NULL)
			return;

		// copy the arguments
		for (int32 i = 0; error == B_OK && i < argc; i++) {
			const char* arg = NULL;
			error = message->FindString("argv", i, &arg);
			if (error == B_OK && arg) {
				argv[i] = strdup(arg);
				if (argv[i] == NULL)
					error = B_NO_MEMORY;
			} else
				argc = i;
		}

		argv[argc] = NULL;
	}

	// call the hook
	if (error == B_OK && argc > 0)
		ArgvReceived(argc, argv);

	if (error != B_OK) {
		printf("Error parsing B_ARGV_RECEIVED message. Message:\n");
		message->PrintToStream();
	}

	// cleanup
	if (argv) {
		for (int32 i = 0; i < argc; i++)
			free(argv[i]);
		delete[] argv;
	}
}


/**
 * @brief Returns the workspace that was active when the application launched.
 *
 * @return The workspace index recorded during _InitGUIContext().
 *
 * @see _InitGUIContext()
 */
uint32
BApplication::InitialWorkspace()
{
	return fInitialWorkspace;
}


/**
 * @brief Counts windows, optionally including menu windows.
 *
 * Iterates the global looper list and counts entries that are BWindow
 * instances, excluding offscreen windows and optionally excluding
 * BMenuWindow instances.
 *
 * @param includeMenus If true, BMenuWindow instances are counted.
 *                     If false, they are excluded.
 *
 * @return The number of matching windows.
 *
 * @see CountWindows(), _WindowAt()
 */
int32
BApplication::_CountWindows(bool includeMenus) const
{
	uint32 count = 0;
	for (int32 i = 0; i < gLooperList.CountLoopers(); i++) {
		BWindow* window = dynamic_cast<BWindow*>(gLooperList.LooperAt(i));
		if (window != NULL && !window->fOffscreen && (includeMenus
				|| dynamic_cast<BMenuWindow*>(window) == NULL)) {
			count++;
		}
	}

	return count;
}


/**
 * @brief Returns the window at the given logical index, optionally including menu windows.
 *
 * Acquires the global looper list lock and iterates through all loopers.
 * Offscreen windows and (optionally) BMenuWindow instances are skipped
 * when computing the effective index.
 *
 * @param index        Zero-based logical index among matching windows.
 * @param includeMenus If true, BMenuWindow instances are included in indexing.
 *                     If false, they are skipped.
 *
 * @return Pointer to the BWindow at the given logical index, or NULL if
 *         the index is out of range or the looper list lock cannot be acquired.
 *
 * @see WindowAt(), _CountWindows()
 */
BWindow*
BApplication::_WindowAt(uint32 index, bool includeMenus) const
{
	AutoLocker<BLooperList> listLock(gLooperList);
	if (!listLock.IsLocked())
		return NULL;

	uint32 count = gLooperList.CountLoopers();
	for (uint32 i = 0; i < count && index < count; i++) {
		BWindow* window = dynamic_cast<BWindow*>(gLooperList.LooperAt(i));
		if (window == NULL || (window != NULL && window->fOffscreen)
			|| (!includeMenus && dynamic_cast<BMenuWindow*>(window) != NULL)) {
			index++;
			continue;
		}

		if (i == index)
			return window;
	}

	return NULL;
}


/**
 * @brief One-time initializer for the application's resource file (static, called via pthread_once).
 *
 * Resolves the application's entry_ref (from GetAppInfo() if running, or
 * from BPrivate::get_app_ref() if not yet running), opens the executable
 * as a read-only BFile, and creates a BResources instance from it. The
 * result is stored in the static sAppResources member.
 *
 * @note Called exactly once via pthread_once from AppResources(). Thread-safe.
 * @see AppResources()
 */
/*static*/ void
BApplication::_InitAppResources()
{
	entry_ref ref;
	bool found = false;

	// App is already running. Get its entry ref with
	// GetAppInfo()
	app_info appInfo;
	if (be_app && be_app->GetAppInfo(&appInfo) == B_OK) {
		ref = appInfo.ref;
		found = true;
	} else {
		// Run() hasn't been called yet
		found = BPrivate::get_app_ref(&ref) == B_OK;
	}

	if (!found)
		return;

	BFile file(&ref, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BResources* resources = new (std::nothrow) BResources(&file, false);
	if (resources == NULL || resources->InitCheck() != B_OK) {
		delete resources;
		return;
	}

	sAppResources = resources;
}
