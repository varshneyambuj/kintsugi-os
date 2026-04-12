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
 * @file MediaClientDefs.cpp
 * @brief Inline method implementations for media_client and media_connection POD types.
 *
 * Provides the accessor and conversion methods for the media_client and
 * media_connection plain-old-data structs defined in MediaClientDefs.h.
 * These methods bridge between the experimental high-level API and the
 * legacy media_input/media_output/media_node types used by BMediaRoster.
 *
 * @see BMediaClient, BMediaConnection
 */


#include <MediaClient.h>
#include <MediaConnection.h>

#include <string.h>

#include "MediaDebug.h"


/**
 * @brief Returns the unique node ID for this media client.
 *
 * @return The media_node_id of the underlying BMediaNode.
 */
media_client_id
media_client::Id() const
{
	return node.node;
}


/**
 * @brief Returns the capability kinds of this media client.
 *
 * @return Bitmask of media_client_kinds flags set at construction time.
 */
media_client_kinds
media_client::Kinds() const
{
	return kinds;
}


/**
 * @brief Returns the media_client descriptor that owns this connection.
 *
 * @return Reference to the client field of this connection.
 */
const media_client&
media_connection::Client() const
{
	return client;
}


/**
 * @brief Returns the unique identifier of this connection within its client.
 *
 * @return The connection ID assigned during registration.
 */
media_connection_id
media_connection::Id() const
{
	return id;
}


/**
 * @brief Returns the direction kinds of this connection.
 *
 * @return B_MEDIA_INPUT or B_MEDIA_OUTPUT.
 */
media_connection_kinds
media_connection::Kinds() const
{
	return kinds;
}


/**
 * @brief Returns whether this connection is an input (consumer) endpoint.
 *
 * @return true if Kinds() equals B_MEDIA_INPUT.
 */
bool
media_connection::IsInput() const
{
	return Kinds() == B_MEDIA_INPUT;
}


/**
 * @brief Returns whether this connection is an output (producer) endpoint.
 *
 * @return true if Kinds() equals B_MEDIA_OUTPUT.
 */
bool
media_connection::IsOutput() const
{
	return Kinds() == B_MEDIA_OUTPUT;
}


/**
 * @brief Builds a legacy media_input struct from this connection's fields.
 *
 * Copies the client node, source, destination, format, and name into a
 * media_input struct suitable for use with BMediaRoster::Connect().
 *
 * @return A fully populated media_input corresponding to this connection.
 */
media_input
media_connection::_BuildMediaInput() const
{
	media_input input;
	input.node = client.node;
	input.source = source;
	input.destination = destination;
	input.format = format;
	strcpy(input.name, name);
	return input;
}


/**
 * @brief Builds a legacy media_output struct from this connection's fields.
 *
 * Copies the client node, source, destination, format, and name into a
 * media_output struct suitable for use with BMediaRoster::Connect().
 *
 * @return A fully populated media_output corresponding to this connection.
 */
media_output
media_connection::_BuildMediaOutput() const
{
	media_output output;
	output.node = client.node;
	output.source = source;
	output.destination = destination;
	output.format = format;
	strcpy(output.name, name);
	return output;
}


/**
 * @brief Returns the media_node of the client that owns this connection.
 *
 * @return The media_node associated with this connection's client.
 */
media_node
media_connection::_Node() const
{
	return client.node;
}
