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
 *   Copyright 2003-2015, Axel Doerfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file SyslogDaemon.cpp
 *  @brief Implementation of the system-wide syslog daemon that collects and dispatches log messages. */


#include "SyslogDaemon.h"

#include <stdio.h>
#include <string.h>

#include <Alert.h>
#include <Catalog.h>
#include <FindDirectory.h>
#include <Font.h>
#include <Path.h>
#include <TextView.h>

#include <LaunchRoster.h>
#include <syscalls.h>
#include <syslog_daemon.h>

#include "listener_output.h"
#include "syslog_output.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SyslogDaemon"


/** @brief Port message code used to signal the daemon thread to quit. */
static const int32 kQuitDaemon = 'quit';


/** @brief Constructs the syslog daemon BServer with its handler lock. */
SyslogDaemon::SyslogDaemon()
	:
	BServer(B_SYSTEM_LOGGER_SIGNATURE, false, NULL),
	fHandlerLock("handler lock")
{
}


/**
 * @brief Called when the application is ready to run.
 *
 * Acquires the logger port from the launch roster, registers it with the
 * kernel, initializes syslog and listener output handlers, and spawns
 * the daemon message-reading thread. Quits if port or thread creation fails.
 */
void
SyslogDaemon::ReadyToRun()
{
	fPort = BLaunchRoster().GetPort("logger");
	fDaemon = spawn_thread(_DaemonThread, "daemon", B_NORMAL_PRIORITY, this);

	if (fPort >= 0 && fDaemon >= 0) {
		_kern_register_syslog_daemon(fPort);

		init_syslog_output(this);
		init_listener_output(this);

		resume_thread(fDaemon);
	} else
		Quit();
}


/**
 * @brief Displays an informational alert about the syslog daemon.
 *
 * Shows the daemon's name, purpose, and the path to the system log file
 * in a styled BAlert dialog.
 */
void
SyslogDaemon::AboutRequested()
{
	BPath path;
	find_directory(B_SYSTEM_LOG_DIRECTORY, &path);
	path.Append("syslog");

	BString name(B_TRANSLATE("Syslog Daemon"));
	BString message;
	snprintf(message.LockBuffer(512), 512,
		B_TRANSLATE("%s\n\nThis daemon collects all system messages and writes them to the "
			"system-wide log at \"%s\".\n\n"), name.String(), path.Path());
	message.UnlockBuffer();

	BAlert* alert = new BAlert(name.String(), message.String(),
		B_TRANSLATE("OK"));
	BTextView* view = alert->TextView();
	BFont font;

	view->SetStylable(true);

	view->GetFont(&font);
	font.SetSize(21);
	font.SetFace(B_BOLD_FACE);
	view->SetFontAndColor(0, name.Length(), &font);

	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go(NULL);
}


/**
 * @brief Handles quit requests by signaling the daemon thread to stop.
 *
 * Writes the kQuitDaemon code to the port and waits for the daemon
 * thread to finish before allowing the application to quit.
 *
 * @return @c true always, allowing the application to terminate.
 */
bool
SyslogDaemon::QuitRequested()
{
	write_port(fPort, kQuitDaemon, NULL, 0);
	wait_for_thread(fDaemon, NULL);

	return true;
}


/**
 * @brief Dispatches incoming BMessages to add or remove syslog listeners.
 *
 * @param message The message to process; handles SYSLOG_ADD_LISTENER
 *                and SYSLOG_REMOVE_LISTENER by extracting the target
 *                messenger and delegating to add_listener / remove_listener.
 */
void
SyslogDaemon::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case SYSLOG_ADD_LISTENER:
		{
			BMessenger messenger;
			if (message->FindMessenger("target", &messenger) == B_OK)
				add_listener(&messenger);
			break;
		}
		case SYSLOG_REMOVE_LISTENER:
		{
			BMessenger messenger;
			if (message->FindMessenger("target", &messenger) == B_OK)
				remove_listener(&messenger);
			break;
		}

		default:
			BApplication::MessageReceived(message);
	}
}


/**
 * @brief Registers a message handler function to be called for every syslog message.
 *
 * @param function The handler callback to add to the handler list.
 */
void
SyslogDaemon::AddHandler(handler_func function)
{
	fHandlers.AddItem((void*)function);
}


/**
 * @brief Main daemon loop that reads syslog messages from the port and dispatches them.
 *
 * Continuously reads from fPort, validates each message, null-terminates
 * the message text, and invokes all registered handler functions under
 * the handler lock. Exits when the port is deleted or a kQuitDaemon
 * message is received.
 */
void
SyslogDaemon::_Daemon()
{
	char buffer[SYSLOG_MESSAGE_BUFFER_SIZE + 1];
	syslog_message& message = *(syslog_message*)buffer;
	int32 code;

	while (true) {
		ssize_t bytesRead = read_port(fPort, &code, &message, sizeof(buffer));
		if (bytesRead == B_BAD_PORT_ID) {
			// we've been quit
			break;
		}

		if (code == kQuitDaemon)
			return;

		// if we don't get what we want, ignore it
		if (bytesRead < (ssize_t)sizeof(syslog_message)
			|| code != SYSLOG_MESSAGE)
			continue;

		// add terminating null byte
		message.message[bytesRead - sizeof(syslog_message)] = '\0';

		if (!message.message[0]) {
			// ignore empty messages
			continue;
		}

		fHandlerLock.Lock();

		for (int32 i = fHandlers.CountItems(); i-- > 0;) {
			handler_func handle = (handler_func)fHandlers.ItemAt(i);

			handle(message);
		}

		fHandlerLock.Unlock();
	}
}


/**
 * @brief Static thread entry point for the daemon worker thread.
 *
 * @param data Pointer to the SyslogDaemon instance.
 * @return B_OK always.
 */
int32
SyslogDaemon::_DaemonThread(void* data)
{
	((SyslogDaemon*)data)->_Daemon();
	return B_OK;
}


// #pragma mark -


/**
 * @brief Entry point for the syslog daemon process.
 *
 * @param argc Argument count (unused).
 * @param argv Argument vector (unused).
 * @return 0 on normal exit.
 */
int
main(int argc, char** argv)
{
	SyslogDaemon daemon;
	daemon.Run();

	return 0;
}
