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
 *   Copyright 2002-2007, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file DataExchange.cpp
 *  @brief Low-level port- and BMessage-based communication with the media server
 *         and add-on server. */


#include <DataExchange.h>

#include <string.h>
#include <unistd.h>

#include <Messenger.h>
#include <OS.h>

#include <MediaDebug.h>
#include <MediaMisc.h>


#define TIMEOUT 15000000 // 15 seconds timeout!


namespace BPrivate {
namespace media {
namespace dataexchange {


static BMessenger sMediaServerMessenger;
static BMessenger sMediaRosterMessenger;
static port_id sMediaServerPort;
static port_id sMediaAddonServerPort;

static void find_media_server_port();
static void find_media_addon_server_port();


/** @brief Locates the well-known media_server port and caches it. */
static void
find_media_server_port()
{
	sMediaServerPort = find_port(MEDIA_SERVER_PORT_NAME);
	if (sMediaServerPort < 0) {
		TRACE("couldn't find sMediaServerPort\n");
		sMediaServerPort = BAD_MEDIA_SERVER_PORT; // make this a unique number
	}
}


/** @brief Locates the well-known media_addon_server port and caches it. */
static void
find_media_addon_server_port()
{
	sMediaAddonServerPort = find_port(MEDIA_ADDON_SERVER_PORT_NAME);
	if (sMediaAddonServerPort < 0) {
		TRACE("couldn't find sMediaAddonServerPort\n");
		sMediaAddonServerPort = BAD_MEDIA_ADDON_SERVER_PORT; // make this a unique number
	}
}


// #pragma mark -


/** @brief Initialises the messengers and port IDs used to talk to the media server. */
void
InitServerDataExchange()
{
	sMediaServerMessenger = BMessenger(B_MEDIA_SERVER_SIGNATURE);
	find_media_server_port();
	find_media_addon_server_port();
}


/** @brief Stores the BMessenger used for BMessage-based communication with the local
 *         BMediaRoster instance.
 *  @param rosterMessenger  Messenger targeting the local BMediaRoster looper. */
void
InitRosterDataExchange(const BMessenger& rosterMessenger)
{
	sMediaRosterMessenger = rosterMessenger;
}


/** @brief BMessage-based data exchange with the local BMediaRoster.
 *  @param msg  The message to send (modified in place if the roster handles it).
 *  @return B_OK on success, or a messenger error on failure. */
status_t
SendToRoster(BMessage* msg)
{
	status_t status = sMediaRosterMessenger.SendMessage(msg,
		static_cast<BHandler*>(NULL), TIMEOUT);
	if (status != B_OK) {
		ERROR("SendToRoster: SendMessage failed: %s\n", strerror(status));
		DEBUG_ONLY(msg->PrintToStream());
	}
	return status;
}


/** @brief BMessage-based fire-and-forget delivery to the media_server.
 *  @param msg  The message to send.
 *  @return B_OK on success, or a messenger error on failure. */
status_t
SendToServer(BMessage* msg)
{
	status_t status = sMediaServerMessenger.SendMessage(msg,
		static_cast<BHandler*>(NULL), TIMEOUT);
	if (status != B_OK) {
		ERROR("SendToServer: SendMessage failed: %s\n", strerror(status));
		DEBUG_ONLY(msg->PrintToStream());
	}
	return status;
}


/** @brief Synchronous BMessage-based query to the media_server.
 *  @param request  The outgoing request message.
 *  @param reply    Receives the server's reply.
 *  @return B_OK on success, or a messenger error on failure. */
status_t
QueryServer(BMessage& request, BMessage& reply)
{
	status_t status = sMediaServerMessenger.SendMessage(&request, &reply,
		TIMEOUT, TIMEOUT);
	if (status != B_OK) {
		ERROR("QueryServer: SendMessage failed: %s\n", strerror(status));
		DEBUG_ONLY(request.PrintToStream());
		DEBUG_ONLY(reply.PrintToStream());
	}
	return status;
}


/** @brief Raw-data fire-and-forget delivery to the media_server port.
 *  @param msgCode  The port message code identifying the command.
 *  @param msg      Pointer to the command_data payload.
 *  @param size     Size in bytes of the payload.
 *  @return B_OK on success, or a port error on failure. */
status_t
SendToServer(int32 msgCode, command_data* msg, size_t size)
{
	return SendToPort(sMediaServerPort, msgCode, msg, size);
}

/** @brief Synchronous raw-data query to the media_server port.
 *  @param msgCode      The port message code identifying the request.
 *  @param request      Pointer to the request_data payload.
 *  @param requestSize  Size of the request payload in bytes.
 *  @param reply        Pointer to the reply buffer.
 *  @param replySize    Size of the reply buffer in bytes.
 *  @return B_OK on success, or a port error on failure. */
status_t
QueryServer(int32 msgCode, request_data* request, size_t requestSize,
	reply_data* reply, size_t replySize)
{
	return QueryPort(sMediaServerPort, msgCode, request, requestSize, reply,
		replySize);
}


/** @brief Raw-data fire-and-forget delivery to the media_addon_server port.
 *  @param msgCode  The port message code identifying the command.
 *  @param msg      Pointer to the command_data payload.
 *  @param size     Size in bytes of the payload.
 *  @return B_OK on success, or a port error on failure. */
status_t
SendToAddOnServer(int32 msgCode, command_data* msg, size_t size)
{
	return SendToPort(sMediaAddonServerPort, msgCode, msg, size);
}


/** @brief Synchronous raw-data query to the media_addon_server port.
 *  @param msgCode      The port message code identifying the request.
 *  @param request      Pointer to the request_data payload.
 *  @param requestSize  Size of the request payload in bytes.
 *  @param reply        Pointer to the reply buffer.
 *  @param replySize    Size of the reply buffer in bytes.
 *  @return B_OK on success, or a port error on failure. */
status_t
QueryAddOnServer(int32 msgCode, request_data* request, size_t requestSize,
	reply_data* reply, size_t replySize)
{
	return QueryPort(sMediaAddonServerPort, msgCode, request, requestSize,
		reply, replySize);
}


/** @brief Writes a raw command to the given port, with automatic port rediscovery
 *         on B_BAD_PORT_ID.
 *  @param sendPort  The destination port_id.
 *  @param msgCode   The message code.
 *  @param msg       Pointer to the command_data payload.
 *  @param size      Size in bytes of the payload.
 *  @return B_OK on success, or a port error on failure. */
status_t
SendToPort(port_id sendPort, int32 msgCode, command_data* msg, size_t size)
{
	status_t status = write_port_etc(sendPort, msgCode, msg, size,
		B_RELATIVE_TIMEOUT, TIMEOUT);
	if (status != B_OK) {
		ERROR("SendToPort: write_port failed, msgcode 0x%" B_PRIx32 ", port %"
			B_PRId32 ": %s\n", msgCode, sendPort, strerror(status));
		if (status == B_BAD_PORT_ID && sendPort == sMediaServerPort) {
			find_media_server_port();
			sendPort = sMediaServerPort;
		} else if (status == B_BAD_PORT_ID
			&& sendPort == sMediaAddonServerPort) {
			find_media_addon_server_port();
			sendPort = sMediaAddonServerPort;
		} else
			return status;

		status = write_port_etc(sendPort, msgCode, msg, size,
			B_RELATIVE_TIMEOUT, TIMEOUT);
		if (status != B_OK) {
			ERROR("SendToPort: retrying write_port failed, msgCode 0x%" B_PRIx32
				", port %" B_PRId32 ": %s\n", msgCode, sendPort,
				strerror(status));
			return status;
		}
	}
	return B_OK;
}


/** @brief Sends a request to the given port and waits for the reply on
 *         request->reply_port, with automatic port rediscovery on B_BAD_PORT_ID.
 *  @param requestPort  Destination port for the request.
 *  @param msgCode      The message code identifying the operation.
 *  @param request      Pointer to the request_data (must contain a valid reply_port).
 *  @param requestSize  Size of the request payload in bytes.
 *  @param reply        Buffer that receives the server reply.
 *  @param replySize    Size of the reply buffer in bytes.
 *  @return B_OK on success, or a port/result error on failure. */
status_t
QueryPort(port_id requestPort, int32 msgCode, request_data* request,
	size_t requestSize, reply_data* reply, size_t replySize)
{
	status_t status = write_port_etc(requestPort, msgCode, request, requestSize,
		B_RELATIVE_TIMEOUT, TIMEOUT);
	if (status != B_OK) {
		ERROR("QueryPort: write_port failed, msgcode 0x%" B_PRIx32 ", port %"
			B_PRId32 ": %s\n", msgCode, requestPort, strerror(status));

		if (status == B_BAD_PORT_ID && requestPort == sMediaServerPort) {
			find_media_server_port();
			requestPort = sMediaServerPort;
		} else if (status == B_BAD_PORT_ID
			&& requestPort == sMediaAddonServerPort) {
			find_media_addon_server_port();
			requestPort = sMediaAddonServerPort;
		} else
			return status;

		status = write_port_etc(requestPort, msgCode, request, requestSize,
			B_RELATIVE_TIMEOUT, TIMEOUT);
		if (status != B_OK) {
			ERROR("QueryPort: retrying write_port failed, msgcode 0x%" B_PRIx32
				", port %" B_PRId32 ": %s\n", msgCode, requestPort,
				strerror(status));
			return status;
		}
	}

	int32 code;
	status = read_port_etc(request->reply_port, &code, reply, replySize,
		B_RELATIVE_TIMEOUT, TIMEOUT);
	if (status < B_OK) {
		ERROR("QueryPort: read_port failed, msgcode 0x%" B_PRIx32 ", port %"
			B_PRId32 ": %s\n", msgCode, request->reply_port, strerror(status));
	}

	return status < B_OK ? status : reply->result;
}


}	// dataexchange
}	// media
}	// BPrivate
