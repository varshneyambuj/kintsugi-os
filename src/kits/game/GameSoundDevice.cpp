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
 *   Copyright 2001-2002, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stefano Ceccherini (stefano.ceccherini@gmail.com)
 *       Adrien Destugues (pulkomandy)
 *       Jérôme Duval (korli)
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 */

/**
 * @file GameSoundDevice.cpp
 * @brief Singleton BGameSoundDevice that manages an array of GameSoundBuffer objects.
 *
 * BGameSoundDevice is the central coordinator for all Game Kit audio output.
 * It maintains a ref-counted singleton instance (accessed via
 * GetDefaultDevice() / ReleaseDevice()) and owns a dynamic array of
 * GameSoundBuffer pointers indexed by gs_id. Public methods allow callers to
 * create, release, start, stop, and query sound buffers, as well as get and
 * set per-sound attributes such as gain and pan.
 *
 * @note This class is intended for internal use by the GameKit only. Its
 *       interface may change without notice.
 */

//	Description:	Manages the game producer. The class may change without
//					notice and was only intended for use by the GameKit at
//					this time. Use at your own risk.

#include "GameSoundDevice.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <List.h>
#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <MediaTheme.h>
#include <TimeSource.h>
#include <locks.h>

#include "GameSoundBuffer.h"


static const int32 kInitSoundCount = 32;
static const int32 kGrowth = 16;

static int32 sDeviceCount = 0;
static BGameSoundDevice* sDevice = NULL;
static mutex sDeviceRefCountLock = MUTEX_INITIALIZER("GameSound device lock");


/**
 * @brief Returns the process-wide singleton BGameSoundDevice, creating it if needed.
 *
 * Thread-safe via sDeviceRefCountLock. Each successful call must be paired
 * with a call to ReleaseDevice() to avoid leaking the singleton.
 *
 * @return Pointer to the shared BGameSoundDevice instance.
 */
BGameSoundDevice*
BGameSoundDevice::GetDefaultDevice()
{
	MutexLocker _(sDeviceRefCountLock);

	if (!sDevice)
		sDevice = new BGameSoundDevice();

	sDeviceCount++;
	return sDevice;
}


/**
 * @brief Decrements the singleton reference count and destroys the device if zero.
 *
 * Must be called once for every successful call to GetDefaultDevice(). When
 * the last reference is released the singleton is deleted and the pointer is
 * set to NULL so that the next call to GetDefaultDevice() creates a fresh
 * instance.
 */
void
BGameSoundDevice::ReleaseDevice()
{
	MutexLocker _(sDeviceRefCountLock);

	sDeviceCount--;

	if (sDeviceCount <= 0) {
		delete sDevice;
		sDevice = NULL;
	}
}


/**
 * @brief Constructs a BGameSoundDevice with an initial sound-buffer array.
 *
 * Allocates kInitSoundCount (32) GameSoundBuffer pointer slots, all
 * initialised to NULL. The audio format is zeroed; it will be filled in by
 * the individual buffer creation paths.
 */
BGameSoundDevice::BGameSoundDevice()
	:
	fIsConnected(false),
	fSoundCount(kInitSoundCount)
{
	memset(&fFormat, 0, sizeof(gs_audio_format));

	fInitError = B_OK;

	fSounds = new GameSoundBuffer*[kInitSoundCount];
	for (int32 i = 0; i < kInitSoundCount; i++)
		fSounds[i] = NULL;
}


/**
 * @brief Destroys the BGameSoundDevice, stopping and deleting all sound buffers.
 *
 * Iterates over the entire buffer array, stops any playing sound, deletes
 * each GameSoundBuffer, and then frees the array itself.
 */
BGameSoundDevice::~BGameSoundDevice()
{
	// We need to stop all the sounds before we stop the mixer
	for (int32 i = 0; i < fSoundCount; i++) {
		if (fSounds[i])
			fSounds[i]->StopPlaying();
		delete fSounds[i];
	}

	delete[] fSounds;
}


/**
 * @brief Returns the initialisation status of the device.
 * @return B_OK if the device was constructed successfully; an error code
 *         set via SetInitError() otherwise.
 */
status_t
BGameSoundDevice::InitCheck() const
{
	return fInitError;
}


/**
 * @brief Returns the global device audio format.
 * @return A const reference to the device-level gs_audio_format.
 */
const gs_audio_format&
BGameSoundDevice::Format() const
{
	return fFormat;
}


/**
 * @brief Returns the audio format of a specific sound buffer.
 * @param sound gs_id of the sound (1-based index into the buffer array).
 * @return A const reference to the format of the requested sound buffer.
 */
const gs_audio_format&
BGameSoundDevice::Format(gs_id sound) const
{
	return fSounds[sound - 1]->Format();
}


/**
 * @brief Records an initialisation error for later retrieval via InitCheck().
 * @param error Error code to store (typically set during subclass construction).
 */
void
BGameSoundDevice::SetInitError(status_t error)
{
	fInitError = error;
}


/**
 * @brief Creates a SimpleSoundBuffer from a fixed block of PCM data.
 *
 * Allocates a slot in the buffer array via AllocateSound(), constructs a
 * SimpleSoundBuffer with the supplied data and frame count, connects it to
 * the system audio mixer, and returns the new gs_id through \a sound.
 *
 * @param sound   Output parameter that receives the new sound's gs_id.
 * @param format  Audio format of the supplied PCM data.
 * @param data    Pointer to the raw PCM data; the buffer takes ownership.
 * @param frames  Number of audio frames in \a data. Must be > 0.
 * @return B_OK on success; B_BAD_VALUE if \a frames <= 0 or \a sound is NULL;
 *         B_MEDIA_TOO_MANY_BUFFERS if no slot is available; a Media Kit error
 *         code if the connection fails.
 */
status_t
BGameSoundDevice::CreateBuffer(gs_id* sound, const gs_audio_format* format,
	const void* data, int64 frames)
{
	if (frames <= 0 || !sound)
		return B_BAD_VALUE;

	// Make sure BMediaRoster is created before we AllocateSound()
	BMediaRoster* roster = BMediaRoster::Roster();

	status_t err = B_MEDIA_TOO_MANY_BUFFERS;
	int32 position = AllocateSound();

	if (position >= 0) {
		fSounds[position] = new SimpleSoundBuffer(format, data, frames);

		media_node systemMixer;
		roster->GetAudioMixer(&systemMixer);
		err = fSounds[position]->Connect(&systemMixer);
	}

	if (err == B_OK)
		*sound = gs_id(position + 1);
	return err;
}


/**
 * @brief Creates a StreamingSoundBuffer driven by a BStreamingGameSound object.
 *
 * Allocates a slot via AllocateSound(), constructs a StreamingSoundBuffer
 * that will call back into \a object to fill audio data, connects it to
 * the system mixer, and returns the new gs_id through \a sound.
 *
 * @param sound               Output parameter that receives the new sound's gs_id.
 * @param object              The BStreamingGameSound whose FillBuffer() is called.
 * @param format              Audio format for the streaming buffer.
 * @param inBufferFrameCount  Frames per buffer; 0 means use default.
 * @param inBufferCount       Number of buffers in the buffer group; 0 = default.
 * @return B_OK on success; B_BAD_VALUE if \a object or \a sound is NULL;
 *         B_MEDIA_TOO_MANY_BUFFERS if no slot is available; a Media Kit error
 *         code if the connection fails.
 */
status_t
BGameSoundDevice::CreateBuffer(gs_id* sound, const void* object,
	const gs_audio_format* format, size_t inBufferFrameCount,
	size_t inBufferCount)
{
	if (!object || !sound)
		return B_BAD_VALUE;

	// Make sure BMediaRoster is created before we AllocateSound()
	BMediaRoster* roster = BMediaRoster::Roster();

	status_t err = B_MEDIA_TOO_MANY_BUFFERS;
	int32 position = AllocateSound();

	if (position >= 0) {
		fSounds[position] = new StreamingSoundBuffer(format, object,
			inBufferFrameCount, inBufferCount);

		media_node systemMixer;
		roster->GetAudioMixer(&systemMixer);
		err = fSounds[position]->Connect(&systemMixer);
	}

	if (err == B_OK)
		*sound = gs_id(position + 1);
	return err;
}


/**
 * @brief Stops and destroys the GameSoundBuffer identified by \a sound.
 *
 * Playback is stopped before destruction to avoid fatal errors from the
 * media pipeline. The slot in the buffer array is set to NULL so it can
 * be reused by AllocateSound().
 *
 * @param sound gs_id of the sound to release (1-based). Ignored if <= 0.
 */
void
BGameSoundDevice::ReleaseBuffer(gs_id sound)
{
	if (sound <= 0)
		return;

	if (fSounds[sound - 1]) {
		// We must stop playback befor destroying the sound or else
		// we may receive fatal errors.
		fSounds[sound - 1]->StopPlaying();

		delete fSounds[sound - 1];
		fSounds[sound - 1] = NULL;
	}
}


/**
 * @brief Copies the format and data of a sound buffer to the caller.
 *
 * Fills \a format with the buffer's audio format. If the buffer has
 * associated data, a malloc'd copy is returned in \a data (caller must
 * free it); otherwise \a data is set to NULL.
 *
 * @param sound  gs_id of the sound (1-based).
 * @param format Output parameter filled with the buffer's gs_audio_format.
 * @param data   Output parameter set to a malloc'd copy of the buffer data,
 *               or NULL if the buffer exposes no raw data.
 * @return B_OK on success; B_BAD_VALUE if \a format is NULL or \a sound <= 0.
 */
status_t
BGameSoundDevice::Buffer(gs_id sound, gs_audio_format* format, void*& data)
{
	if (!format || sound <= 0)
		return B_BAD_VALUE;

	memcpy(format, &fSounds[sound - 1]->Format(), sizeof(gs_audio_format));
	if (fSounds[sound - 1]->Data()) {
		data = malloc(format->buffer_size);
		memcpy(data, fSounds[sound - 1]->Data(), format->buffer_size);
	} else
		data = NULL;

	return B_OK;
}


/**
 * @brief Starts playback of the specified sound.
 *
 * If the sound is already playing, its state is reset and EALREADY is
 * returned. Otherwise StartPlaying() is forwarded to the underlying buffer.
 *
 * @param sound gs_id of the sound to start (1-based).
 * @return B_OK on successful start; EALREADY if already playing;
 *         B_BAD_VALUE if \a sound <= 0.
 */
status_t
BGameSoundDevice::StartPlaying(gs_id sound)
{
	if (sound <= 0)
		return B_BAD_VALUE;

	if (!fSounds[sound - 1]->IsPlaying()) {
		// tell the producer to start playing the sound
		return fSounds[sound - 1]->StartPlaying();
	}

	fSounds[sound - 1]->Reset();
	return EALREADY;
}


/**
 * @brief Stops playback of the specified sound.
 *
 * If the sound is not currently playing EALREADY is returned. Otherwise
 * the buffer state is reset and StopPlaying() is forwarded.
 *
 * @param sound gs_id of the sound to stop (1-based).
 * @return B_OK on success; EALREADY if not playing; B_BAD_VALUE if \a sound <= 0.
 */
status_t
BGameSoundDevice::StopPlaying(gs_id sound)
{
	if (sound <= 0)
		return B_BAD_VALUE;

	if (fSounds[sound - 1]->IsPlaying()) {
		// Tell the producer to stop play this sound
		fSounds[sound - 1]->Reset();
		return fSounds[sound - 1]->StopPlaying();
	}

	return EALREADY;
}


/**
 * @brief Returns whether the specified sound is currently playing.
 * @param sound gs_id of the sound (1-based).
 * @return \c true if the sound is playing; \c false if not or if \a sound <= 0.
 */
bool
BGameSoundDevice::IsPlaying(gs_id sound)
{
	if (sound <= 0)
		return false;
	return fSounds[sound - 1]->IsPlaying();
}


/**
 * @brief Reads attributes (gain, pan, looping) from the specified sound buffer.
 *
 * Delegates to GameSoundBuffer::GetAttributes() for the identified buffer.
 *
 * @param sound          gs_id of the sound (1-based).
 * @param attributes     Array of gs_attribute structs to fill in.
 * @param attributeCount Number of entries in \a attributes.
 * @return B_OK on success; B_ERROR if the sound slot is empty.
 */
status_t
BGameSoundDevice::GetAttributes(gs_id sound, gs_attribute* attributes,
	size_t attributeCount)
{
	if (!fSounds[sound - 1])
		return B_ERROR;

	return fSounds[sound - 1]->GetAttributes(attributes, attributeCount);
}


/**
 * @brief Applies attributes (gain, pan, looping) to the specified sound buffer.
 *
 * Delegates to GameSoundBuffer::SetAttributes() for the identified buffer.
 *
 * @param sound          gs_id of the sound (1-based).
 * @param attributes     Array of gs_attribute structs to apply.
 * @param attributeCount Number of entries in \a attributes.
 * @return B_OK on success; B_ERROR if the sound slot is empty.
 */
status_t
BGameSoundDevice::SetAttributes(gs_id sound, gs_attribute* attributes,
	size_t attributeCount)
{
	if (!fSounds[sound - 1])
		return B_ERROR;

	return fSounds[sound - 1]->SetAttributes(attributes, attributeCount);
}


/**
 * @brief Finds a free slot in the sound buffer array, growing it if necessary.
 *
 * Performs a linear scan for the first NULL slot. If none is found, the
 * array is grown by kGrowth (16) elements; the new first slot is returned
 * and the rest of the new slots are initialised to NULL.
 *
 * @return 0-based index of the allocated slot, or -1 if allocation fails
 *         (currently cannot fail because new[] is used).
 */
int32
BGameSoundDevice::AllocateSound()
{
	for (int32 i = 0; i < fSoundCount; i++)
		if (!fSounds[i])
			return i;

	// we need to allocate new space for the sound
	GameSoundBuffer ** sounds = new GameSoundBuffer*[fSoundCount + kGrowth];
	for (int32 i = 0; i < fSoundCount; i++)
		sounds[i] = fSounds[i];

	for (int32 i = fSoundCount; i < fSoundCount + kGrowth; i++)
		sounds[i] = NULL;

	// replace the old list
	delete [] fSounds;
	fSounds = sounds;
	fSoundCount += kGrowth;

	return fSoundCount - kGrowth;
}
