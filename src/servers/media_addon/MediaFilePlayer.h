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
 *   Distributed under the terms of the MIT License.
 */

/** @file MediaFilePlayer.h
 *  @brief Plays back media files (e.g. system sounds) via BSoundPlayer. */

#ifndef _MEDIA_FILE_PLAYER_H
#define _MEDIA_FILE_PLAYER_H


#include <Entry.h>
#include <MediaDefs.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <SoundPlayer.h>
#include <String.h>


/** @brief Look up and play a named media file by type and name. */
void PlayMediaFile(const char* media_type, const char* media_name);


/** @brief Decodes and plays a single media file through BSoundPlayer. */
class MediaFilePlayer
{
public:
	/** @brief Open and begin playing the referenced media file. */
							MediaFilePlayer(const char* media_type,
								const char* media_name,
								entry_ref* ref);
	/** @brief Stop playback and release media resources. */
							~MediaFilePlayer();

	/** @brief Return the initialization status. */
	status_t				InitCheck();

	/** @brief Check whether the sound player currently has data to play. */
	bool					IsPlaying();
	/** @brief Seek to the beginning and restart playback. */
	void					Restart();
	/** @brief Stop the sound player. */
	void					Stop();

	/** @brief Return the media name string. */
	const char*				Name();
	/** @brief Return a pointer to the file's entry_ref. */
	const entry_ref*		Ref();

	/** @brief BSoundPlayer callback that reads decoded frames into the buffer. */
	static void				PlayFunction(void* cookie, void* buffer,
								size_t size,
								const media_raw_audio_format& format);

private:
	BString					fName;
	status_t				fInitCheck;
	entry_ref				fRef;
	BSoundPlayer*			fSoundPlayer;
	BMediaFile*				fPlayFile;
	BMediaTrack*			fPlayTrack;
	media_format			fPlayFormat;
};

#endif // _MEDIA_FILE_PLAYER_H
