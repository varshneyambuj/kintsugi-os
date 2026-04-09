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
 *   Copyright 2019, Ryan Leavengood
 *   Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file request_data.cpp
 *  @brief Implements request_data lifetime management and reply dispatch. */


#include <ServerInterface.h>

#include <set>

#include <Autolock.h>
#include <Locker.h>

#include <DataExchange.h>
#include <MediaDebug.h>

#include "PortPool.h"


namespace BPrivate {
namespace media {


/** @brief Default constructor; acquires a reply port from the global PortPool. */
request_data::request_data()
{
	reply_port = gPortPool->GetPort();
}


/** @brief Destructor; returns the reply port to the global PortPool. */
request_data::~request_data()
{
	gPortPool->PutPort(reply_port);
}


/** @brief Sends a reply to the requester on the reply port.
 *  @param result  The status code to embed in the reply.
 *  @param reply   Pointer to the reply_data structure to send.
 *  @param replySize  Size in bytes of the reply structure.
 *  @return B_OK on success, or a port error code on failure. */
status_t
request_data::SendReply(status_t result, reply_data *reply,
	size_t replySize) const
{
	reply->result = result;
	// we cheat and use the (command_data *) version of SendToPort
	return SendToPort(reply_port, 0, reinterpret_cast<command_data *>(reply),
		replySize);
}


}	// namespace media
}	// namespace BPrivate
