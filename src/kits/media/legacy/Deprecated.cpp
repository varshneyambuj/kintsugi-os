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
 *   Copyright 2002-2015, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Deprecated.cpp
 * @brief Stub implementations of deprecated and unimplemented Media Kit APIs.
 *
 * Contains BMediaRoster methods that were never meaningfully implemented as
 * well as the legacy media_realtime_init_image() and media_realtime_init_thread()
 * free functions. All entries return error codes or no-op, and are retained
 * only to satisfy binary compatibility with old add-ons.
 *
 * @see BMediaRoster
 */


#include <MediaDefs.h>
#include <MediaRoster.h>
#include <SupportDefs.h>

#include "MediaDebug.h"

// This file contains parts of the media_kit that can be removed
// as considered useless, deprecated and/or not worth to be
// implemented.

// BMediaRoster

/**
 * @brief Sets real-time media flags (deprecated, unimplemented).
 *
 * @param enabled  Bitmask of real-time flags to enable.
 * @return B_ERROR always.
 */
status_t
BMediaRoster::SetRealtimeFlags(uint32 enabled)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/**
 * @brief Retrieves real-time media flags (deprecated, unimplemented).
 *
 * @param _enabled  Output pointer for the current flag bitmask.
 * @return B_ERROR always.
 */
status_t
BMediaRoster::GetRealtimeFlags(uint32* _enabled)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/**
 * @brief Parses and executes a media server command message (deprecated, unimplemented).
 *
 * @param reply  The BMessage containing the command to parse.
 * @return B_ERROR always.
 */
/*static*/ status_t
BMediaRoster::ParseCommand(BMessage& reply)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/**
 * @brief Retrieves default configuration for a media node role (deprecated, unimplemented).
 *
 * @param forDefault  The media_node_id representing the default role.
 * @param config      Output BMessage to fill with configuration data.
 * @return B_ERROR always.
 */
status_t
BMediaRoster::GetDefaultInfo(media_node_id forDefault, BMessage& config)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/**
 * @brief Sets the node that provides a given default role (deprecated, unimplemented).
 *
 * @param forDefault  The media_node_id representing the default role to set.
 * @param node        The media_node to designate as the new default.
 * @return B_ERROR always.
 */
status_t
BMediaRoster::SetRunningDefault(media_node_id forDefault,
	const media_node& node)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/**
 * @brief Associates an external buffer group with an output source (deprecated).
 *
 * This call was never properly implemented and triggers a debugger stop.
 *
 * @param output      The media_source whose buffer group to set.
 * @param group       The BBufferGroup to use.
 * @param willReclaim If true, the caller will reclaim the buffers after use.
 * @return B_ERROR always.
 */
//! Deprecated call.
status_t
BMediaRoster::SetOutputBuffersFor(const media_source& output,
	BBufferGroup* group, bool willReclaim)
{
	UNIMPLEMENTED();
	debugger("BMediaRoster::SetOutputBuffersFor missing\n");
	return B_ERROR;
}

// MediaDefs.h

status_t launch_media_server(uint32 flags);

status_t media_realtime_init_image(image_id image, uint32 flags);

status_t media_realtime_init_thread(thread_id thread, size_t stack_used,
	uint32 flags);


/**
 * @brief Launches the media server using the zero-argument legacy signature.
 *
 * Forwards to the four-argument version with NULL callback parameters.
 *
 * @param flags  Launch flags forwarded to the full launch_media_server().
 * @return Status code from the full launch_media_server() call.
 */
status_t
launch_media_server(uint32 flags)
{
	return launch_media_server(0, NULL, NULL, flags);
}


/**
 * @brief Prepares an image for real-time media operation (unimplemented).
 *
 * Given an image_id, prepare that image_id for realtime media.
 * If the kind of media indicated by "flags" is not enabled for real-time,
 * B_MEDIA_REALTIME_DISABLED is returned.
 * If there are not enough system resources to enable real-time performance,
 * B_MEDIA_REALTIME_UNAVAILABLE is returned.
 *
 * @param image  The image_id of the shared library or executable to prepare.
 * @param flags  Bitmask indicating which real-time media types are requested.
 * @return B_OK always (currently a no-op).
 */
status_t
media_realtime_init_image(image_id image, uint32 flags)
{
	UNIMPLEMENTED();
	return B_OK;
}


/**
 * @brief Prepares a thread for real-time media performance by locking its stack.
 *
 * Given a thread ID, and an optional indication of what the thread is
 * doing in "flags", prepare the thread for real-time media performance.
 * Currently, this means locking the thread stack, up to size_used bytes,
 * or all of it if 0 is passed. Typically, you will not be using all
 * 256 kB of the stack, so you should pass some smaller value you determine
 * from profiling the thread; typically in the 32-64kB range.
 * Return values are the same as for media_prepare_realtime_image().
 *
 * @param thread      The thread_id of the thread to prepare.
 * @param stack_used  Bytes of stack to lock, or 0 to lock the entire stack.
 * @param flags       Bitmask indicating the kind of real-time media work.
 * @return B_OK always (currently a no-op).
 */
status_t
media_realtime_init_thread(thread_id thread, size_t stack_used, uint32 flags)
{
	UNIMPLEMENTED();
	return B_OK;
}
