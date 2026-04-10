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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 */

/** @file NotificationServer.cpp
 *  @brief Implementation of the notification daemon and its main() entry point. */


#include "NotificationServer.h"

#include <stdlib.h>

#include <Alert.h>
#include <Beep.h>
#include <Notification.h>
#include <Notifications.h>
#include <PropertyInfo.h>
#include <Roster.h>

#include "NotificationWindow.h"


/** @brief System beep event names indexed by notification type. */
const char* kSoundNames[] = {
	"Notification information",
	"Notification important",
	"Notification error",
	"Notification progress",
	NULL
};


NotificationServer::NotificationServer(status_t& error)
	:
	BServer(kNotificationServerSignature, true, &error)
{
}


NotificationServer::~NotificationServer()
{
}


void
NotificationServer::ReadyToRun()
{
	fWindow = new NotificationWindow();
}


void
NotificationServer::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kNotificationMessage:
		{
			// Skip this message if we don't have the window
			if (!fWindow)
				return;

			// Emit a sound for this event
			// Progress notifications only emit a sound if the visible percentage changed
			int32 type = 0;
			if (message->FindInt32("_type", &type) == B_OK) {
				if (type < (int32)(sizeof(kSoundNames) / sizeof(const char*))
					&& type != B_PROGRESS_NOTIFICATION)
					system_beep(kSoundNames[type]);
			}

			// Let the notification window handle this message
			BMessenger(fWindow).SendMessage(message);
			break;
		}
		default:
			BApplication::MessageReceived(message);
	}
}


status_t
NotificationServer::GetSupportedSuites(BMessage* msg)
{
	msg->AddString("suites", "suite/x-vnd.Haiku-notification_server");

	BPropertyInfo info(main_prop_list);
	msg->AddFlat("messages", &info);

	return BApplication::GetSupportedSuites(msg);
}


BHandler*
NotificationServer::ResolveSpecifier(BMessage* msg, int32 index,
	BMessage* spec, int32 from, const char* prop)
{
	BPropertyInfo info(main_prop_list);

	if (strcmp(prop, "message") == 0) {
		BMessenger messenger(fWindow);
		messenger.SendMessage(msg, fWindow);
		return NULL;
	}

	return BApplication::ResolveSpecifier(msg, index, spec, from, prop);
}


// #pragma mark -


/** @brief Daemon entry point: registers system beep events and runs the server loop. */
int
main(int argc, char* argv[])
{
	int32 i = 0;

	// Add system sounds
	while (kSoundNames[i] != NULL)
		add_system_beep_event(kSoundNames[i++], 0);

	// Start!
	status_t error;
	NotificationServer server(error);
	if (error == B_OK)
		server.Run();

	return error == B_OK ? EXIT_SUCCESS : EXIT_FAILURE;
}
