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
   Copyright (c) 2001-2002, Haiku
   Permission is hereby granted, free of charge, to any person obtaining a
   copy of this software and associated documentation files (the "Software"),
   to deal in the Software without restriction, including without limitation
   the rights to use, copy, modify, merge, publish, distribute, sublicense,
   and/or sell copies of the Software, and to permit persons to whom the
   Software is furnished to do so, subject to the following conditions:
   The above copyright notice and this permission notice shall be included in
   all copies or substantial portions of the Software.
   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
   FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
   DEALINGS IN THE SOFTWARE.
   File Name:		Registrar.h
   Author:			Ingo Weinhold (bonefish@users.sf.net)
   Description:	Registrar application.
 */

/** @file Registrar.h
 *  @brief Main registrar server application class that owns and coordinates all subsystems. */
#ifndef REGISTRAR_H
#define REGISTRAR_H

#include <Server.h>


class AuthenticationManager;
class ClipboardHandler;
class DiskDeviceManager;
class EventQueue;
class MessageEvent;
class MessageRunnerManager;
class MIMEManager;
class PackageWatchingManager;
class ShutdownProcess;

class TRoster;


/** @brief The registrar BServer subclass that initializes and owns all registrar components. */
class Registrar : public BServer {
public:
	Registrar(status_t *error);
	virtual ~Registrar();

	/** @brief Routes incoming messages to the appropriate subsystem handler. */
	virtual void MessageReceived(BMessage *message);
	/** @brief Called when the application is fully initialized and ready to process messages. */
	virtual void ReadyToRun();
	/** @brief Handles quit requests, potentially initiating the shutdown sequence. */
	virtual bool QuitRequested();

	/** @brief Returns a pointer to the registrar's event queue. */
	EventQueue *GetEventQueue() const;

	/** @brief Returns the singleton Registrar application instance. */
	static Registrar *App();

private:
	void _MessageReceived(BMessage *message);
	void _HandleShutDown(BMessage *message);
	void _HandleIsShutDownInProgress(BMessage *message);

	TRoster					*fRoster;
	ClipboardHandler		*fClipboardHandler;
	MIMEManager				*fMIMEManager;
	EventQueue				*fEventQueue;
	MessageRunnerManager	*fMessageRunnerManager;
	ShutdownProcess			*fShutdownProcess;
	AuthenticationManager	*fAuthenticationManager;
	PackageWatchingManager	*fPackageWatchingManager;
};

#endif	// REGISTRAR_H
