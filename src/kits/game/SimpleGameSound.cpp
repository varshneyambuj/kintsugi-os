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
 *   Copyright 2001-2012 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 */

/**
 * @file SimpleGameSound.cpp
 * @brief BSimpleGameSound: a GameKit sound loaded entirely into memory.
 *
 * BSimpleGameSound reads a complete audio file (or accepts a raw PCM block)
 * into a fixed-size buffer, then hands it to the BGameSoundDevice as a
 * SimpleSoundBuffer. Because all data is loaded at construction time,
 * playback is low-latency and the sound can be cloned to play multiple
 * simultaneous instances. Looping is controlled via the B_GS_LOOPING
 * attribute on the underlying device buffer.
 *
 * Supported construction paths:
 *   - entry_ref or char* path: decoded via BMediaFile / BMediaTrack.
 *   - Raw PCM block: copied and registered directly.
 *   - Copy constructor / Clone(): retrieves the stored PCM block from
 *     the device.
 */


#include <SimpleGameSound.h>

#include <Entry.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <stdlib.h>
#include <string.h>

#include "GameSoundBuffer.h"
#include "GameSoundDefs.h"
#include "GameSoundDevice.h"
#include "GSUtility.h"


/**
 * @brief Constructs a BSimpleGameSound from an audio file identified by entry_ref.
 *
 * Opens the file, decodes its first audio track, and loads all frames into
 * an internal buffer via Init(const entry_ref*). If the base class reports an
 * error, Init() is not called.
 *
 * @param inFile  entry_ref pointing to the audio file to load.
 * @param device  The BGameSoundDevice to use, or NULL for the process default.
 */
BSimpleGameSound::BSimpleGameSound(const entry_ref *inFile,
	BGameSoundDevice *device)
	:
	BGameSound(device)
{
	if (InitCheck() == B_OK)
		SetInitError(Init(inFile));
}


/**
 * @brief Constructs a BSimpleGameSound from an audio file identified by path string.
 *
 * Converts the path to an entry_ref and delegates to Init(const entry_ref*).
 * Sets B_ENTRY_NOT_FOUND if the path cannot be resolved.
 *
 * @param inFile  Null-terminated filesystem path to the audio file.
 * @param device  The BGameSoundDevice to use, or NULL for the process default.
 */
BSimpleGameSound::BSimpleGameSound(const char *inFile, BGameSoundDevice *device)
	:
	BGameSound(device)
{
	if (InitCheck() == B_OK) {
		entry_ref file;

		if (get_ref_for_path(inFile, &file) != B_OK)
			SetInitError(B_ENTRY_NOT_FOUND);
		else
			SetInitError(Init(&file));
	}
}


/**
 * @brief Constructs a BSimpleGameSound from a raw PCM data block.
 *
 * Makes a private copy of \a inData (the caller retains ownership of the
 * original). If \a format->byte_order is 0, it is set to B_MEDIA_HOST_ENDIAN
 * before the buffer is registered with the device.
 *
 * @param inData        Pointer to the raw PCM audio data.
 * @param inFrameCount  Number of audio frames in \a inData.
 * @param format        Audio format describing \a inData.
 * @param device        The BGameSoundDevice to use, or NULL for the default.
 */
BSimpleGameSound::BSimpleGameSound(const void *inData, size_t inFrameCount,
	const gs_audio_format *format, BGameSoundDevice *device)
	:
	BGameSound(device)
{
	if (InitCheck() != B_OK)
		return;

	gs_audio_format actual = *format;
	if (actual.byte_order == 0)
		actual.byte_order = B_MEDIA_HOST_ENDIAN;

	size_t frameSize
		= get_sample_size(format->format) * format->channel_count;
	uchar * data = new uchar[inFrameCount * frameSize];
	memcpy(data, inData, inFrameCount * frameSize);

	SetInitError(Init(data, inFrameCount, &actual));
}


/**
 * @brief Copy-constructs a BSimpleGameSound from an existing instance.
 *
 * Retrieves the PCM buffer from the source sound's device slot and
 * initialises a new buffer with the same data.
 *
 * @param other The BSimpleGameSound to copy.
 */
BSimpleGameSound::BSimpleGameSound(const BSimpleGameSound &other)
	:
	BGameSound(other)
{
	gs_audio_format format;
	void *data = NULL;

	status_t error = other.Device()->Buffer(other.ID(), &format, data);
	if (error != B_OK)
		SetInitError(error);

	Init(data, 0, &format);
	free(data);
}


/**
 * @brief Destroys the BSimpleGameSound.
 *
 * The underlying device buffer is released by the BGameSound base class
 * destructor.
 */
BSimpleGameSound::~BSimpleGameSound()
{
}


/**
 * @brief Creates an independent copy of this sound that can be played
 *        simultaneously.
 *
 * Retrieves the stored PCM data from the device and constructs a new
 * BSimpleGameSound with the same data and format.
 *
 * @return Pointer to the cloned BSimpleGameSound, or NULL on error.
 */
BGameSound *
BSimpleGameSound::Clone() const
{
	gs_audio_format format;
	void *data = NULL;

	status_t error = Device()->Buffer(ID(), &format, data);
	if (error != B_OK)
		return NULL;

	BSimpleGameSound *clone = new BSimpleGameSound(data, 0, &format, Device());
	free(data);

	return clone;
}


/**
 * @brief Reserved virtual dispatch hook; not implemented.
 * @param selector Selector identifying the desired operation.
 * @param data     Pointer to operation-specific data.
 * @return Always B_ERROR.
 */
/* virtual */ status_t
BSimpleGameSound::Perform(int32 selector, void * data)
{
	return B_ERROR;
}


/**
 * @brief Enables or disables looped playback of this sound.
 *
 * Sets the B_GS_LOOPING attribute on the device buffer. A value of -1.0
 * enables looping; 0.0 disables it.
 *
 * @param looping \c true to loop; \c false to play once.
 * @return B_OK on success; an error code from BGameSoundDevice::SetAttributes()
 *         otherwise.
 */
status_t
BSimpleGameSound::SetIsLooping(bool looping)
{
	gs_attribute attribute;

	attribute.attribute = B_GS_LOOPING;
	attribute.value = (looping) ? -1.0 : 0.0;
	attribute.duration = bigtime_t(0);
	attribute.flags = 0;

	return Device()->SetAttributes(ID(), &attribute, 1);
}


/**
 * @brief Returns whether this sound is currently configured to loop.
 *
 * Reads the B_GS_LOOPING attribute from the device buffer.
 *
 * @return \c true if looping is enabled; \c false if not or on error.
 */
bool
BSimpleGameSound::IsLooping() const
{
	gs_attribute attribute;

	attribute.attribute = B_GS_LOOPING;
	attribute.flags = 0;

	if (Device()->GetAttributes(ID(), &attribute, 1) != B_OK)
		return false;

	return bool(attribute.value);
}


/**
 * @brief Loads an audio file and creates the device buffer from its decoded data.
 *
 * Opens the file via BMediaFile, extracts the first audio track, requests
 * raw audio decoding, and reads all frames into a heap-allocated buffer.
 * If the track uses B_AUDIO_CHAR (signed 8-bit) format, the samples are
 * converted to B_GS_U8 (unsigned 8-bit) by adding 128. In all other cases
 * the samples are read directly. Delegates to Init(const void*, int64,
 * const gs_audio_format*) to register the buffer with the device.
 *
 * @param inFile entry_ref identifying the audio file to decode.
 * @return B_OK on success; B_ERROR if the track is not audio;
 *         B_MEDIA_BAD_FORMAT or another Media Kit error on decoding failure.
 */
status_t
BSimpleGameSound::Init(const entry_ref* inFile)
{
	BMediaFile file(inFile);
	gs_audio_format gsformat;
	media_format mformat;
	int64 framesRead, framesTotal = 0;

	if (file.InitCheck() != B_OK)
		return file.InitCheck();

	BMediaTrack* audioStream = file.TrackAt(0);
	audioStream->EncodedFormat(&mformat);
	if (!mformat.IsAudio())
		return B_ERROR;

	int64 frames = audioStream->CountFrames();

	mformat.Clear();
	mformat.type = B_MEDIA_RAW_AUDIO;
//	mformat.u.raw_audio.byte_order
//		= (B_HOST_IS_BENDIAN) ? B_MEDIA_BIG_ENDIAN : B_MEDIA_LITTLE_ENDIAN;
	status_t error = audioStream->DecodedFormat(&mformat);
	if (error != B_OK)
		return error;

	memset(&gsformat, 0, sizeof(gs_audio_format));
	media_to_gs_format(&gsformat, &mformat.u.raw_audio);

	if (mformat.u.raw_audio.format == media_raw_audio_format::B_AUDIO_CHAR) {
		// The GameKit doesnt support this format so we will have to reformat
		// the data into something the GameKit does support.
		char * buffer = new char[gsformat.buffer_size];
		uchar * data = new uchar[frames * gsformat.channel_count];

		while (framesTotal < frames) {
			// read the next chunck from the stream
			memset(buffer, 0, gsformat.buffer_size);
			audioStream->ReadFrames(buffer, &framesRead);

			// refomat the buffer from
			int64 position = framesTotal * gsformat.channel_count;
			for (int32 i = 0; i < (int32)gsformat.buffer_size; i++)
				data[i + position] = buffer[i] + 128;

			framesTotal += framesRead;
		}
		delete [] buffer;

		gsformat.format = gs_audio_format::B_GS_U8;

		error = Init(data, frames, &gsformat);

		// free the buffers we no longer need
	} else {
		// We need to determine the size, in bytes, of a single sample.
		// At the same time, we will store the format of the audio buffer
		size_t frameSize
			= get_sample_size(gsformat.format) * gsformat.channel_count;
		char * data = new char[frames * frameSize];
		gsformat.buffer_size = frames * frameSize;

		while (framesTotal < frames) {
			char * position = &data[framesTotal * frameSize];
			audioStream->ReadFrames(position, &framesRead);

			framesTotal += framesRead;
		}

		error = Init(data, frames, &gsformat);
	}

	file.ReleaseTrack(audioStream);
	return error;
}


/**
 * @brief Registers a raw PCM buffer with the BGameSoundDevice.
 *
 * Calls BGameSoundDevice::CreateBuffer() to create a SimpleSoundBuffer and
 * connect it to the system mixer, then calls BGameSound::Init() to record
 * the assigned gs_id. Ownership of \a inData is transferred to the device.
 *
 * @param inData       Pointer to the raw PCM audio data (ownership transferred).
 * @param inFrameCount Number of audio frames in \a inData.
 * @param format       Audio format of the data.
 * @return B_OK on success; an error code from CreateBuffer() on failure.
 */
status_t
BSimpleGameSound::Init(const void* inData, int64 inFrameCount,
	const gs_audio_format* format)
{
	gs_id sound;

	status_t error
		= Device()->CreateBuffer(&sound, format, inData, inFrameCount);
	if (error != B_OK)
		return error;

	BGameSound::Init(sound);

	return B_OK;
}


/* unimplemented for protection of the user:
 *
 * BSimpleGameSound::BSimpleGameSound()
 * BSimpleGameSound &BSimpleGameSound::operator=(const BSimpleGameSound &)
 */


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_0(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_1(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_2(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_3(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_4(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_5(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_6(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_7(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_8(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_9(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_10(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_11(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_12(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_13(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_14(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_15(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_16(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_17(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_18(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_19(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_20(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_21(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_22(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BSimpleGameSound::_Reserved_BSimpleGameSound_23(int32 arg, ...)
{
	return B_ERROR;
}
