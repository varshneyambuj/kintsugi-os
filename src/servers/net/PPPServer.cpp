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
   Copyright 2004-2005, Waldemar Kornewald <wkornew@gmx.net>
   Distributed under the terms of the MIT License.
 */
/** @file PPPServer.cpp
 *  @brief PPP connection server handling dial-up networking. */
#include "PPPServer.h"
#include <Application.h>


/**
 * @brief Constructs the PPP server and begins watching for PPP events.
 *
 * Registers itself as a handler on the application, starts the PPP
 * manager listener, and initialises any existing PPP interfaces.
 */
PPPServer::PPPServer()
	: BHandler("PPPServer"),
	fListener(this)
{
	be_app->AddHandler(this);

	fListener.WatchManager();

	InitInterfaces();
}


/**
 * @brief Destroys the PPP server, tearing down interfaces and stopping the listener.
 */
PPPServer::~PPPServer()
{
	UninitInterfaces();

	fListener.StopWatchingManager();

	be_app->RemoveHandler(this);
}


/**
 * @brief Handles incoming messages, dispatching PPP report messages.
 *
 * @param message The message to process.
 */
void
PPPServer::MessageReceived(BMessage *message)
{
	switch(message->what) {
		case PPP_REPORT_MESSAGE:
			HandleReportMessage(message);
		break;

		default:
			BHandler::MessageReceived(message);
	}
}


/**
 * @brief Initialises PPP interfaces by creating connection request windows.
 *
 * Currently a stub; will create one ConnectionRequestWindow per interface.
 */
void
PPPServer::InitInterfaces()
{
	// TODO: create one ConnectionRequestWindow per interface
}


/**
 * @brief Tears down all PPP interfaces and their associated windows.
 *
 * Currently a stub; will delete all ConnectionRequestWindows.
 */
void
PPPServer::UninitInterfaces()
{
	// TODO: delete all ConnectionRequestWindows
}


/**
 * @brief Processes a PPP report message and creates a connection window if needed.
 *
 * Extracts the interface ID and report type/code from the message. If the
 * report indicates a new interface was created by the PPP manager, a
 * connection request window is opened for that interface.
 *
 * @param message The PPP report message containing interface ID, type, and code.
 */
void
PPPServer::HandleReportMessage(BMessage *message)
{
	ppp_interface_id id;
	if (message->FindInt32("interface", reinterpret_cast<int32*>(&id)) != B_OK)
		return;

	int32 type, code;
	message->FindInt32("type", &type);
	message->FindInt32("code", &code);

	if (type == PPP_MANAGER_REPORT && code == PPP_REPORT_INTERFACE_CREATED)
		CreateConnectionRequestWindow(id);
}


/**
 * @brief Creates a connection request window for the given PPP interface.
 *
 * Currently a stub; will create a window and register it as a report
 * receiver for the specified interface. If a window for the given ID
 * already exists, it is only registered as a report receiver.
 *
 * @param id The PPP interface identifier.
 */
void
PPPServer::CreateConnectionRequestWindow(ppp_interface_id id)
{
	// TODO: create window, register window as report receiver for the interface
	// XXX: if a window for that ID exists then only register it as report receiver
}
