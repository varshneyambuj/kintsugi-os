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
 *   Copyright (c) 2004, Marcus Overhagen <marcus@overhagen.de>. All rights reserved.
 *   Copyright (c) 2007, Jérôme Duval. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file MediaFilePlayer.cpp
 *  @brief Implementation of media file playback via BSoundPlayer for system sounds. */


#include "MediaFilePlayer.h"

#include <MediaFiles.h>
#include <ObjectList.h>

#include <stdlib.h>


/** @brief Global list of active MediaFilePlayer instances. */
BObjectList<MediaFilePlayer> list;


/**
 * @brief Predicate function that matches a MediaFilePlayer by its name.
 *
 * Used with BObjectList::EachElement to find a player matching the
 * given media name string.
 *
 * @param player     The player to test.
 * @param media_name The name string to compare against (cast from void*).
 * @return The player if its name matches, or NULL otherwise.
 */
MediaFilePlayer*
FindMediaFilePlayer(MediaFilePlayer* player, void* media_name)
{
	if (strcmp(player->Name(), (const char*)media_name) == 0)
		return player;
	return NULL;
}


/**
 * @brief Plays a named media file, reusing or creating a MediaFilePlayer.
 *
 * Looks up the entry_ref for the given type/name pair. If a player already
 * exists for the same name and ref, it is restarted. If the ref has
 * changed, the old player is replaced. A new player is created if none
 * exists.
 *
 * @param media_type The media type category (e.g., "Sounds").
 * @param media_name The media item name (e.g., "Startup").
 */
void
PlayMediaFile(const char* media_type, const char* media_name)
{
	entry_ref ref;
	if (BMediaFiles().GetRefFor(media_type, media_name, &ref) != B_OK
		|| !BEntry(&ref).Exists())
		return;

	MediaFilePlayer* player = list.EachElement(FindMediaFilePlayer,
		(void*)media_name);

	if (player != NULL) {
		if (*(player->Ref()) == ref) {
			player->Restart();
			return;
		}

		list.RemoveItem(player);
		delete player;
		player = NULL;
	}

	if (player == NULL) {
		player = new MediaFilePlayer(media_type, media_name, &ref);
		if (player->InitCheck() == B_OK)
			list.AddItem(player);
		else
			delete player;
	}
}



/**
 * @brief Constructs a player and begins playback of the referenced media file.
 *
 * Opens the media file, finds the first audio track, decodes it to raw
 * audio format, creates a BSoundPlayer, and starts playback.
 *
 * @param media_type The media type category.
 * @param media_name The media item name, also used as the BSoundPlayer name.
 * @param ref        The entry_ref pointing to the media file to play.
 */
MediaFilePlayer::MediaFilePlayer(const char* media_type,
	const char* media_name, entry_ref* ref)
	:
	fName(media_name),
	fInitCheck(B_ERROR),
	fRef(*ref),
	fSoundPlayer(NULL),
	fPlayTrack(NULL)
{
	fPlayFile = new BMediaFile(&fRef);
	fInitCheck = fPlayFile->InitCheck();
	if (fInitCheck != B_OK)
		return;

	fPlayFormat.Clear();

	for (int i=0; i < fPlayFile->CountTracks(); i++) {
		BMediaTrack* track = fPlayFile->TrackAt(i);
		if (track == NULL)
			continue;
		fPlayFormat.type = B_MEDIA_RAW_AUDIO;
		fPlayFormat.u.raw_audio.buffer_size = 256;
		if ((track->DecodedFormat(&fPlayFormat) == B_OK)
			&& (fPlayFormat.type == B_MEDIA_RAW_AUDIO)) {
			fPlayTrack = track;
			break;
		}
		fPlayFile->ReleaseTrack(track);
	}

	if (fPlayTrack == NULL) {
		fInitCheck = B_BAD_VALUE;
		return;
	}

	fSoundPlayer = new BSoundPlayer(&fPlayFormat.u.raw_audio,
		media_name, PlayFunction, NULL, this);

	fInitCheck = fSoundPlayer->InitCheck();
	if (fInitCheck != B_OK)
		return;

	fSoundPlayer->SetHasData(true);
	fSoundPlayer->Start();
}


/** @brief Destroys the player and releases the sound player and media file. */
MediaFilePlayer::~MediaFilePlayer()
{
	delete fSoundPlayer;
	delete fPlayFile;
}


/** @brief Returns the initialization status of this player. */
status_t
MediaFilePlayer::InitCheck()
{
	return fInitCheck;
}


/** @brief Returns the media item name associated with this player. */
const char*
MediaFilePlayer::Name()
{
	return fName;
}


/** @brief Returns the entry_ref of the media file being played. */
const entry_ref*
MediaFilePlayer::Ref()
{
	return &fRef;
}


/** @brief Returns whether this player is currently playing audio data. */
bool
MediaFilePlayer::IsPlaying()
{
	return (fSoundPlayer != NULL && fSoundPlayer->HasData());
}

/** @brief Stops playback, seeks to the beginning, and restarts. */
void
MediaFilePlayer::Restart()
{
	fSoundPlayer->Stop();
	int64 frame = 0;
	fPlayTrack->SeekToFrame(&frame);
	fSoundPlayer->SetHasData(true);
	fSoundPlayer->Start();
}


/** @brief Stops playback. */
void
MediaFilePlayer::Stop()
{
	fSoundPlayer->Stop();
}


/**
 * @brief BSoundPlayer callback that fills the audio buffer from the media track.
 *
 * Reads decoded frames from the play track into the provided buffer.
 * Signals no more data when the track is exhausted.
 *
 * @param cookie Pointer to the owning MediaFilePlayer instance.
 * @param buffer The audio buffer to fill.
 * @param size   Size of the buffer in bytes.
 * @param format The raw audio format description.
 */
void
MediaFilePlayer::PlayFunction(void* cookie, void* buffer,
	size_t size, const media_raw_audio_format& format)
{
	MediaFilePlayer* player = (MediaFilePlayer*)cookie;
	int64 frames = 0;
	player->fPlayTrack->ReadFrames(buffer, &frames);

	if (frames <= 0)
		player->fSoundPlayer->SetHasData(false);
}
