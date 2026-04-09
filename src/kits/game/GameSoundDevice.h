/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Incorporates work from the Haiku project, originally copyrighted:
 *   Copyright 2001-2002, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file GameSoundDevice.h
 *  @brief BGameSoundDevice: manages the audio hardware connection for game sounds.
 */

#ifndef _GAMESOUNDDEVICE_H
#define _GAMESOUNDDEVICE_H


#include <GameSoundDefs.h>


class BMediaNode;
class GameSoundBuffer;
struct Connection;


/** @brief Represents the audio output device used by the Game Sound Kit; manages sound buffers and playback state. */
class BGameSoundDevice {
public:
	/** @brief Returns the process-wide singleton default audio device.
	 *  @return Pointer to the shared BGameSoundDevice instance.
	 */
	static	BGameSoundDevice*		GetDefaultDevice();

	/** @brief Releases the reference held by GetDefaultDevice(). */
	static	void					ReleaseDevice();

public:
	/** @brief Constructs a BGameSoundDevice and initialises the media graph connection. */
									BGameSoundDevice();

	/** @brief Destroys the device and releases all sound buffers. */
	virtual							~BGameSoundDevice();

	/** @brief Returns the initialisation status of the device.
	 *  @return B_OK if ready, or an error code if initialisation failed.
	 */
			status_t				InitCheck() const;

	/** @brief Returns the default audio format supported by the device.
	 *  @return Reference to the device-level gs_audio_format.
	 */
	virtual const gs_audio_format &	Format() const;

	/** @brief Returns the audio format used by a specific sound.
	 *  @param sound Handle of the sound to query.
	 *  @return Reference to the gs_audio_format for that sound.
	 */
	virtual const gs_audio_format &	Format(gs_id sound) const;

	/** @brief Allocates a static sound buffer from raw PCM data.
	 *  @param sound   Set to the new gs_id handle on success.
	 *  @param format  Pointer to the desired audio format.
	 *  @param data    Pointer to the raw PCM sample data.
	 *  @param frames  Number of frames in the data block.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t				CreateBuffer(gs_id * sound,
										const gs_audio_format * format,
										const void * data,
										int64 frames);

	/** @brief Allocates a streaming sound buffer from a hook object.
	 *  @param sound              Set to the new gs_id handle on success.
	 *  @param object             Pointer to the streaming hook object.
	 *  @param format             Pointer to the desired audio format.
	 *  @param inBufferFrameCount Frames per buffer page; 0 uses a default.
	 *  @param inBufferCount      Number of buffer pages; 0 uses a default.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t				CreateBuffer(gs_id * sound,
										const void * object,
										const gs_audio_format * format,
										size_t inBufferFrameCount = 0,
										size_t inBufferCount = 0);

	/** @brief Releases the buffer identified by the given handle.
	 *  @param sound Handle of the sound to release.
	 */
	virtual void					ReleaseBuffer(gs_id sound);

	/** @brief Retrieves the format and raw data pointer for a sound buffer.
	 *  @param sound  Handle of the sound to query.
	 *  @param format Filled with the buffer's audio format.
	 *  @param data   Set to the raw buffer data pointer.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t				Buffer(gs_id sound,
										gs_audio_format * format,
										void *& data);

	/** @brief Returns whether the given sound is currently playing.
	 *  @param sound Handle of the sound to query.
	 *  @return True if playing, false otherwise.
	 */
	virtual	bool					IsPlaying(gs_id sound);

	/** @brief Starts playback of the given sound.
	 *  @param sound Handle of the sound to start.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t				StartPlaying(gs_id sound);

	/** @brief Stops playback of the given sound.
	 *  @param sound Handle of the sound to stop.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t				StopPlaying(gs_id sound);

	/** @brief Retrieves one or more attributes for the specified sound.
	 *  @param sound          Handle of the sound to query.
	 *  @param attributes     Array of gs_attribute descriptors to fill.
	 *  @param attributeCount Number of elements in the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t				GetAttributes(gs_id sound,
										gs_attribute * attributes,
										size_t attributeCount);

	/** @brief Sets one or more attributes on the specified sound.
	 *  @param sound          Handle of the sound to modify.
	 *  @param attributes     Array of gs_attribute descriptors.
	 *  @param attributeCount Number of elements in the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t				SetAttributes(gs_id sound,
										gs_attribute * attributes,
										size_t attributeCount);

protected:
	/** @brief Records an initialisation error for later retrieval via InitCheck().
	 *  @param error The error code to store.
	 */
			void					SetInitError(status_t error);

			gs_audio_format			fFormat; /**< Default audio format for this device. */

private:
	/** @brief Allocates the next free sound slot and returns its index.
	 *  @return Index of the allocated slot, or a negative error code.
	 */
			int32					AllocateSound();

			status_t				fInitError;

			bool					fIsConnected;

			int32					fSoundCount;
			GameSoundBuffer **		fSounds;
};


#endif
