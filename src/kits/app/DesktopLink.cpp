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
 *   Copyright 2006-2009, Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file DesktopLink.cpp
 *  @brief BPrivate::DesktopLink implementation for desktop app_server connections.
 *
 *  Provides a dedicated communication link to the desktop's app_server,
 *  separate from the main application server link. Creates its own port
 *  for receiving replies from the desktop.
 */

#include <DesktopLink.h>

#include <AppMisc.h>
#include <ServerProtocol.h>


namespace BPrivate {


/** @brief Constructs a DesktopLink by creating a new desktop connection.
 *
 *  Establishes a dedicated port-based connection to the desktop's app_server
 *  using the "desktop reply" port with a capacity of 1 message.
 */
DesktopLink::DesktopLink()
{
	create_desktop_connection(this, "desktop reply", 1);
}


/** @brief Destroys the DesktopLink, deleting the receiver port. */
DesktopLink::~DesktopLink()
{
	delete_port(fReceiver->Port());
}


/** @brief Checks whether the desktop connection was initialized successfully.
 *  @return B_OK on success, or a negative error code if the port is invalid.
 */
status_t
DesktopLink::InitCheck() const
{
	return fReceiver->Port() < B_OK ? fReceiver->Port() : B_OK;
}


}	// namespace BPrivate
