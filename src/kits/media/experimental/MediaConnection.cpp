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
 *   Copyright 2015, Dario Casalinuovo. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MediaConnection.cpp
 * @brief Implementation of BMediaConnection, BMediaInput, and BMediaOutput.
 *
 * BMediaConnection is the base class representing a single endpoint in the
 * experimental media graph API. BMediaInput and BMediaOutput extend it with
 * consumer and producer roles respectively. Instances are owned and managed
 * by a BMediaClient.
 *
 * @see BMediaClient, BMediaClientNode
 */


#include <MediaConnection.h>

#include <string.h>

#include "MediaClientNode.h"

#include "MediaDebug.h"


/**
 * @brief Constructs a BMediaConnection with the given direction and optional name.
 *
 * Initialises the owner and bind pointers to NULL and sets the connection
 * ID to -1 (unregistered).
 *
 * @param kinds  B_MEDIA_INPUT or B_MEDIA_OUTPUT.
 * @param name   Human-readable name copied into the connection descriptor,
 *               or NULL to leave the name field empty.
 */
BMediaConnection::BMediaConnection(media_connection_kinds kinds,
	const char* name)
	:
	fOwner(NULL),
	fBind(NULL)
{
	CALLED();

	fConnection.kinds = kinds;
	fConnection.id = -1;
	//fConnection.client = media_client::null;
	if (name != NULL)
		strcpy(fConnection.name, name);
}


/**
 * @brief Destroys the BMediaConnection.
 */
BMediaConnection::~BMediaConnection()
{
	CALLED();

}


/**
 * @brief Returns the media_connection descriptor for this connection.
 *
 * @return Reference to the internal media_connection struct.
 */
const media_connection&
BMediaConnection::Connection() const
{
	return fConnection;
}


/**
 * @brief Returns the BMediaClient that owns this connection.
 *
 * @return Pointer to the owning BMediaClient, or NULL if not yet registered.
 */
BMediaClient*
BMediaConnection::Client() const
{
	return fOwner;
}


/**
 * @brief Returns the human-readable name of this connection.
 *
 * @return Null-terminated name string stored in the connection descriptor.
 */
const char*
BMediaConnection::Name() const
{
	return fConnection.name;
}


/**
 * @brief Returns whether this connection is bound to another local connection.
 *
 * @return true if fBind is non-NULL.
 */
bool
BMediaConnection::HasBinding() const
{
	CALLED();

	return fBind != NULL;
}


/**
 * @brief Returns the connection this endpoint is bound to, if any.
 *
 * @return Pointer to the bound BMediaConnection, or NULL if unbound.
 */
BMediaConnection*
BMediaConnection::Binding() const
{
	CALLED();

	return fBind;
}


/**
 * @brief Returns whether this connection is currently connected in the media graph.
 *
 * @return true if a successful Connected() call has been received and
 *         no subsequent Disconnected() has occurred.
 */
bool
BMediaConnection::IsConnected() const
{
	CALLED();

	return fConnected;
}


/**
 * @brief Disconnects this connection from the media graph.
 *
 * Delegates to BMediaClient::_DisconnectConnection().
 *
 * @return B_OK on success, or an error code from the Media Roster.
 */
status_t
BMediaConnection::Disconnect()
{
	CALLED();

	return fOwner->_DisconnectConnection(this);
}


/**
 * @brief Removes this connection from its owner and deletes it.
 *
 * Calls BMediaClient::_ReleaseConnection() and then deletes this object.
 * After this call the pointer is invalid.
 *
 * @return B_OK on success, or the error code from _ReleaseConnection.
 */
status_t
BMediaConnection::Release()
{
	CALLED();

	status_t ret = fOwner->_ReleaseConnection(this);
	if (ret != B_OK)
		return ret;

	delete this;
	return ret;
}


/**
 * @brief Called when the media graph has established a connection to this endpoint.
 *
 * Updates the stored format and sets fConnected to true. Subclasses should
 * call this base implementation.
 *
 * @param format  The negotiated media_format for this connection.
 */
void
BMediaConnection::Connected(const media_format& format)
{
	// Update the status of our connection format.
	fConnection.format = format;

	fConnected = true;
}


/**
 * @brief Called when the media graph connection has been torn down.
 *
 * Sets fConnected to false. Subclasses should call this base implementation.
 */
void
BMediaConnection::Disconnected()
{
	CALLED();

	fConnected = false;
}


/**
 * @brief Internal method called by BMediaClient to assign owner and connection ID.
 *
 * Sets up the media_source or media_destination port and ID based on the
 * connection direction, and stores the client descriptor.
 *
 * @param owner  The BMediaClient registering this connection.
 * @param id     The unique ID assigned by the client for this connection.
 */
void
BMediaConnection::_ConnectionRegistered(BMediaClient* owner,
	media_connection_id id)
{
	fOwner = owner;
	fConnection.id = id;
	fConnection.client = fOwner->Client();

	if (fConnection.IsOutput()) {
		fConnection.source.port = fOwner->fNode->ControlPort();
		fConnection.source.id = fConnection.id;

		fConnection.destination = media_destination::null;
	} else {
		fConnection.destination.port = fOwner->fNode->ControlPort();
		fConnection.destination.id = fConnection.id;

		fConnection.source = media_source::null;
	}
}


/**
 * @brief Returns the media_source endpoint of this connection.
 *
 * @return Reference to the source field of the internal connection descriptor.
 */
const media_source&
BMediaConnection::_Source() const
{
	return fConnection.source;
}


/**
 * @brief Returns the media_destination endpoint of this connection.
 *
 * @return Reference to the destination field of the internal connection descriptor.
 */
const media_destination&
BMediaConnection::_Destination() const
{
	return fConnection.destination;
}


void BMediaConnection::_ReservedMediaConnection0() {}
void BMediaConnection::_ReservedMediaConnection1() {}
void BMediaConnection::_ReservedMediaConnection2() {}
void BMediaConnection::_ReservedMediaConnection3() {}
void BMediaConnection::_ReservedMediaConnection4() {}
void BMediaConnection::_ReservedMediaConnection5() {}
void BMediaConnection::_ReservedMediaConnection6() {}
void BMediaConnection::_ReservedMediaConnection7() {}
void BMediaConnection::_ReservedMediaConnection8() {}
void BMediaConnection::_ReservedMediaConnection9() {}
void BMediaConnection::_ReservedMediaConnection10() {}


/**
 * @brief Constructs a BMediaInput with an optional name.
 *
 * @param name  Human-readable name for this input endpoint, or NULL.
 */
BMediaInput::BMediaInput(const char* name)
	:
	BMediaConnection(B_MEDIA_INPUT, name)
{
}


/**
 * @brief Destroys the BMediaInput.
 */
BMediaInput::~BMediaInput()
{
	CALLED();
}


/**
 * @brief Hook called when a buffer arrives on this input.
 *
 * The default implementation does nothing. Subclasses should override this
 * to process incoming media data.
 *
 * @param buffer  The received BBuffer; ownership is not transferred.
 */
void
BMediaInput::HandleBuffer(BBuffer* buffer)
{
	CALLED();
}


/**
 * @brief Called when the upstream producer has connected to this input.
 *
 * Forwards to BMediaConnection::Connected() to record the negotiated format.
 *
 * @param format  The agreed-upon media_format for this connection.
 */
void
BMediaInput::Connected(const media_format& format)
{
	BMediaConnection::Connected(format);
}


/**
 * @brief Called when the upstream producer has disconnected from this input.
 *
 * Forwards to BMediaConnection::Disconnected() to clear the connected state.
 */
void
BMediaInput::Disconnected()
{
	BMediaConnection::Disconnected();
}


void BMediaInput::_ReservedMediaInput0() {}
void BMediaInput::_ReservedMediaInput1() {}
void BMediaInput::_ReservedMediaInput2() {}
void BMediaInput::_ReservedMediaInput3() {}
void BMediaInput::_ReservedMediaInput4() {}
void BMediaInput::_ReservedMediaInput5() {}
void BMediaInput::_ReservedMediaInput6() {}
void BMediaInput::_ReservedMediaInput7() {}
void BMediaInput::_ReservedMediaInput8() {}
void BMediaInput::_ReservedMediaInput9() {}
void BMediaInput::_ReservedMediaInput10() {}


/**
 * @brief Constructs a BMediaOutput with an optional name.
 *
 * Initialises fBufferGroup to NULL; a group is allocated when the connection
 * is established.
 *
 * @param name  Human-readable name for this output endpoint, or NULL.
 */
BMediaOutput::BMediaOutput(const char* name)
	:
	BMediaConnection(B_MEDIA_OUTPUT, name),
	fBufferGroup(NULL)
{
}


/**
 * @brief Destroys the BMediaOutput.
 */
BMediaOutput::~BMediaOutput()
{
	CALLED();
}


/**
 * @brief Sends a buffer to the connected downstream consumer.
 *
 * Fails immediately if the connection is not yet established.
 *
 * @param buffer  The BBuffer to send; ownership passes to the Media Kit.
 * @return B_OK on success, B_ERROR if not connected, or an error code from
 *         BMediaClientNode::SendBuffer.
 */
status_t
BMediaOutput::SendBuffer(BBuffer* buffer)
{
	CALLED();

	if (!IsConnected())
		return B_ERROR;

	return fOwner->fNode->SendBuffer(buffer, this);
}


/**
 * @brief Called when a downstream consumer has connected to this output.
 *
 * Forwards to BMediaConnection::Connected() to record the negotiated format.
 *
 * @param format  The agreed-upon media_format for this connection.
 */
void
BMediaOutput::Connected(const media_format& format)
{
	BMediaConnection::Connected(format);
}


/**
 * @brief Called when the downstream consumer has disconnected from this output.
 *
 * Forwards to BMediaConnection::Disconnected() to clear the connected state.
 */
void
BMediaOutput::Disconnected()
{
	BMediaConnection::Disconnected();
}


/**
 * @brief Returns whether this output is enabled for data flow.
 *
 * @return true if the output is enabled (data will be produced and sent).
 */
bool
BMediaOutput::_IsEnabled() const
{
	CALLED();

	return fEnabled;
}


/**
 * @brief Enables or disables data flow on this output.
 *
 * @param enabled  true to allow buffer production; false to suppress it.
 */
void
BMediaOutput::_SetEnabled(bool enabled)
{
	fEnabled = enabled;
}


void BMediaOutput::_ReservedMediaOutput0() {}
void BMediaOutput::_ReservedMediaOutput1() {}
void BMediaOutput::_ReservedMediaOutput2() {}
void BMediaOutput::_ReservedMediaOutput3() {}
void BMediaOutput::_ReservedMediaOutput4() {}
void BMediaOutput::_ReservedMediaOutput5() {}
void BMediaOutput::_ReservedMediaOutput6() {}
void BMediaOutput::_ReservedMediaOutput7() {}
void BMediaOutput::_ReservedMediaOutput8() {}
void BMediaOutput::_ReservedMediaOutput9() {}
void BMediaOutput::_ReservedMediaOutput10() {}
