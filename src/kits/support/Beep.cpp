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
 *   Copyright 2004-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2007, Jérôme Duval. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file Beep.cpp
 * @brief System beep and audio-event notification helpers.
 *
 * Provides three C-linkage functions for triggering audio feedback through
 * the media add-on server: a generic named-event beep, a default beep
 * shortcut, and a registration call for adding new system beep events.
 *
 * @see system_beep(), beep(), add_system_beep_event()
 */


#include <Beep.h>

#include <stdio.h>

#include <DataExchange.h>
#include <MediaSounds.h>


/**
 * @brief Play a named system-beep event through the media add-on server.
 *
 * Sends a play request for the sound event identified by \a eventName to
 * the media add-on host ("application/x-vnd.Be.addon-host"). If
 * \a eventName is NULL the default beep sound (MEDIA_SOUNDS_BEEP) is used.
 *
 * @param eventName Name of the sound event to play, or NULL for the
 *                  default beep.
 * @return B_OK on success; B_ERROR if the messenger is not valid;
 *         B_BAD_REPLY if the server reply indicates failure.
 * @see beep(), add_system_beep_event()
 */
status_t
system_beep(const char* eventName)
{
	BMessenger messenger("application/x-vnd.Be.addon-host");
	if (!messenger.IsValid())
		return B_ERROR;

	BMessage msg(MEDIA_ADD_ON_SERVER_PLAY_MEDIA), reply;
	msg.AddString(MEDIA_NAME_KEY, eventName ? eventName : MEDIA_SOUNDS_BEEP);
	msg.AddString(MEDIA_TYPE_KEY, MEDIA_TYPE_SOUNDS);

	status_t status = messenger.SendMessage(&msg, &reply);
	if (status != B_OK || reply.FindInt32("error", &status) != B_OK)
		status = B_BAD_REPLY;

	return status;
}


/**
 * @brief Play the default system beep.
 *
 * Convenience wrapper that calls system_beep() with a NULL event name,
 * causing the default MEDIA_SOUNDS_BEEP sound to be played.
 *
 * @return B_OK on success; propagates errors from system_beep().
 * @see system_beep()
 */
status_t
beep()
{
	return system_beep(NULL);
}


/**
 * @brief Register a new named system beep event with the media server.
 *
 * Sends a registration request to the media server
 * ("application/x-vnd.Be.media-server") so that a new beep event
 * identified by \a name becomes available for use with system_beep().
 *
 * @param name  Unique name for the new beep event; must not be NULL.
 * @param flags Optional modifier flags for the event (reserved; pass 0).
 * @return B_OK on success; B_ERROR if the messenger is not valid;
 *         B_BAD_REPLY if the server reply indicates failure.
 * @see system_beep()
 */
status_t
add_system_beep_event(const char* name, uint32 flags)
{
	BMessenger messenger("application/x-vnd.Be.media-server");
	if (!messenger.IsValid())
		return B_ERROR;

	BMessage msg(MEDIA_SERVER_ADD_SYSTEM_BEEP_EVENT), reply;
	msg.AddString(MEDIA_NAME_KEY, name);
	msg.AddString(MEDIA_TYPE_KEY, MEDIA_TYPE_SOUNDS);
	msg.AddInt32(MEDIA_FLAGS_KEY, flags);

	status_t status = messenger.SendMessage(&msg, &reply);
	if (status != B_OK || reply.FindInt32("error", &status) != B_OK)
		status = B_BAD_REPLY;

	return status;
}
