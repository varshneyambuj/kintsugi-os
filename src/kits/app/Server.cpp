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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2005-2015, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Ingo Weinhold <bonefish@cs.tu-berlin.de>
 */

/** @file Server.cpp
 *  @brief BServer implementation for server-type application registration.
 *
 *  Extends BApplication to provide server-specific functionality, including
 *  optional GUI context initialization. Servers can operate with or without
 *  a graphical user interface connection to the app_server.
 */

#include <Server.h>


/** @brief Constructs a BServer with the given application signature.
 *
 *  Registers the server with the registrar and optionally initializes
 *  the GUI context for app_server communication.
 *
 *  @param signature The application MIME signature string.
 *  @param initGUI Whether to initialize the GUI context on construction.
 *  @param error Optional pointer to receive the initialization status code.
 */
BServer::BServer(const char* signature, bool initGUI, status_t *error)
	:
	BApplication(signature, NULL, -1, initGUI, error),
	fGUIContextInitialized(false)
{
	fGUIContextInitialized = initGUI && (error == NULL || *error == B_OK);
}


/** @brief Constructs a BServer with a custom looper name and port.
 *
 *  Allows specifying a custom looper name and port for the server's
 *  message loop, in addition to the application signature and GUI options.
 *
 *  @param signature The application MIME signature string.
 *  @param looperName The name for the server's message looper.
 *  @param port The port ID for the server's message port.
 *  @param initGUI Whether to initialize the GUI context on construction.
 *  @param error Optional pointer to receive the initialization status code.
 */
BServer::BServer(const char* signature, const char* looperName, port_id port,
	bool initGUI, status_t *error)
	:
	BApplication(signature, looperName, port, initGUI, error),
	fGUIContextInitialized(false)
{
	fGUIContextInitialized = initGUI && (error == NULL || *error == B_OK);
}


/** @brief Initializes the GUI context if not already initialized.
 *
 *  Establishes the connection to the app_server for graphical operations.
 *  This is a no-op if the GUI context was already initialized during
 *  construction.
 *
 *  @return B_OK on success, or an error code if initialization fails.
 */
status_t
BServer::InitGUIContext()
{
	if (fGUIContextInitialized)
		return B_OK;

	status_t error = _InitGUIContext();
	fGUIContextInitialized = (error == B_OK);
	return error;
}
