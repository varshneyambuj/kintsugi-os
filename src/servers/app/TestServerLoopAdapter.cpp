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
 *   Copyright 2001-2015, Haiku, Inc.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Christian Packmann
 *       Julian Harnath <julian.harnath@rwth-aachen.de>
 */

/** @file TestServerLoopAdapter.cpp
 *  @brief Test-mode message looper that replaces the production AppServer main loop. */

#include "TestServerLoopAdapter.h"

#include "Desktop.h"
#include "ServerConfig.h"
#include "ServerProtocol.h"

#include <PortLink.h>

#include <stdio.h>

//#define DEBUG_SERVER
#ifdef DEBUG_SERVER
#	include <stdio.h>
#	define STRACE(x) printf x
#else
#	define STRACE(x) ;
#endif


/**
 * @brief Constructs the TestServerLoopAdapter, creating the server message port.
 * @param signature  Application signature (unused in test mode).
 * @param[out] outError Set to B_OK on success.
 */
TestServerLoopAdapter::TestServerLoopAdapter(const char* signature,
	const char*, port_id, bool, status_t* outError)
	:
	MessageLooper("test-app_server"),
	fMessagePort(_CreatePort())
{
	fLink.SetReceiverPort(fMessagePort);
	*outError = B_OK;
}


/**
 * @brief Destroys the TestServerLoopAdapter.
 */
TestServerLoopAdapter::~TestServerLoopAdapter()
{
}


/**
 * @brief Runs the message loop in the current thread.
 *
 * Renames the current thread to "picasso" before entering the loop.
 *
 * @return B_OK always.
 */
status_t
TestServerLoopAdapter::Run()
{
 	rename_thread(find_thread(NULL), "picasso");
	_message_thread((void*)this);
	return B_OK;
}


/**
 * @brief Dispatches incoming server messages during the test loop.
 *
 * Handles AS_GET_DESKTOP by reading the request parameters, looking up (or
 * creating) the matching Desktop, and sending the Desktop's message port back
 * to the requester. Handles B_QUIT_REQUESTED to exit. Logs unexpected codes.
 *
 * @param code The server protocol code identifying the message.
 * @param link The LinkReceiver used to read message parameters.
 */
void
TestServerLoopAdapter::_DispatchMessage(int32 code,
	BPrivate::LinkReceiver& link)
{
	switch (code) {
		case AS_GET_DESKTOP:
		{
			port_id replyPort = 0;
			link.Read<port_id>(&replyPort);

			int32 userID = -1;
			link.Read<int32>(&userID);

			char* targetScreen = NULL;
			link.ReadString(&targetScreen);

			int32 version = -1;
			link.Read<int32>(&version);

 			BMessage message(AS_GET_DESKTOP);
			message.AddInt32("user", userID);
			message.AddInt32("version", version);
			message.AddString("target", targetScreen);
 			MessageReceived(&message);

 			// AppServer will try to send a reply, we just let that fail
 			// since we can find out the port by getting the desktop instance
 			// ourselves

			Desktop* desktop = _FindDesktop(userID, targetScreen);
			free(targetScreen);

			BPrivate::LinkSender reply(replyPort);
			if (desktop != NULL) {
				reply.StartMessage(B_OK);
				reply.Attach<port_id>(desktop->MessagePort());
			} else
			reply.StartMessage(B_ERROR);

			reply.Flush();

			break;
		}

		case B_QUIT_REQUESTED:
		{
			QuitRequested();
			break;
		}

		default:
			STRACE(("Server::MainLoop received unexpected code %" B_PRId32 " "
				"(offset %" B_PRId32 ")\n", code, code - SERVER_TRUE));
			break;
	}
}


/**
 * @brief Creates the server message port used by the test adapter.
 * @return The newly created port_id.
 */
port_id
TestServerLoopAdapter::_CreatePort()
{
	port_id port = create_port(DEFAULT_MONITOR_PORT_SIZE, SERVER_PORT_NAME);
	if (port < B_OK)
		debugger("test-app_server could not create message port");
	return port;
}
