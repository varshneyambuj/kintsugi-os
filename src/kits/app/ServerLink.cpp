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
 *   Copyright 2001-2009, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Pahtz <pahtz@yahoo.com.au>
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file ServerLink.cpp
 *  @brief BPrivate::ServerLink implementation for low-overhead app_server messaging.
 *
 *  Provides utility methods on top of the base PortLink for communicating
 *  with the app_server, including port reassignment and a convenience method
 *  to flush the send buffer and wait for a reply in a single call.
 */

/*!	Class for low-overhead port-based messaging */


#include <ServerLink.h>

#include <stdlib.h>
#include <string.h>
#include <new>

#include <ServerProtocol.h>


namespace BPrivate {


/** @brief Default constructor. */
ServerLink::ServerLink()
{
}


/** @brief Destructor. */
ServerLink::~ServerLink()
{
}


/** @brief Reassigns both the sender and receiver to new ports.
 *  @param sender The new port ID for outgoing messages.
 *  @param receiver The new port ID for incoming messages.
 */
void
ServerLink::SetTo(port_id sender, port_id receiver)
{
	fSender->SetPort(sender);
	fReceiver->SetPort(receiver);
}


/** @brief Flushes the send buffer and blocks until a reply is received.
 *
 *  Sends all buffered data with an infinite timeout (marking the flush as
 *  expecting a reply), then reads the next incoming message code.
 *
 *  @param code On success, receives the reply message code.
 *  @return B_OK on success, or an error code if the flush or read fails.
 */
status_t
ServerLink::FlushWithReply(int32& code)
{
	status_t status = Flush(B_INFINITE_TIMEOUT, true);
	if (status < B_OK)
		return status;

	return GetNextMessage(code);
}


}	// namespace BPrivate
