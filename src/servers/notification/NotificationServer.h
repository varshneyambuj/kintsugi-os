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
 */

/** @file NotificationServer.h
 *  @brief BServer that owns the on-screen notification window and routes incoming notification messages. */

#ifndef _NOTIFICATION_SERVER_H
#define _NOTIFICATION_SERVER_H


#include <Server.h>


class NotificationWindow;


/** @brief Top-level notification daemon.
 *
 * Listens on the notification server port for @c kNotificationMessage
 * requests, plays an optional system beep, and forwards each notification
 * to the singleton NotificationWindow that draws it on screen. */
class NotificationServer : public BServer {
public:
	/** @brief Registers the daemon with launch_daemon under its well-known signature.
	 *  @param error Set to B_OK on success, or an error code on failure. */
								NotificationServer(status_t& error);
	virtual						~NotificationServer();

	/** @brief Creates the on-screen NotificationWindow once the looper is running. */
	virtual	void				ReadyToRun();
	/** @brief Dispatches incoming notification messages to the window. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Reports the scripting suites this server supports. */
	virtual	status_t			GetSupportedSuites(BMessage* msg);
	/** @brief Resolves scripting specifiers, forwarding "message" to the window. */
	virtual	BHandler*			ResolveSpecifier(BMessage* msg, int32 index,
									BMessage* spec, int32 form,
									const char* prop);

private:
			NotificationWindow*	fWindow;  /**< Singleton on-screen notification window. */
};


#endif	// _NOTIFICATION_SERVER_H
