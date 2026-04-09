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
 *   Copyright 2020, Haiku Inc. All Rights Reserved.
 *   Author: Christopher ML Zumwalt May (zummy@users.sf.net)
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file FileGameSound.h
 *  @brief BFileGameSound class for playing audio files via the Game Sound Kit.
 */

#ifndef _FILEGAMESOUND_H
#define _FILEGAMESOUND_H


#include <StreamingGameSound.h>
#include <DataIO.h>


struct entry_ref;
struct _gs_media_tracker;
struct _gs_ramp;


/** @brief Plays audio from a file or data source, with optional looping and pause ramping. */
class BFileGameSound : public BStreamingGameSound {
public:

	/** @brief Constructs a BFileGameSound from an entry_ref file reference.
	 *  @param file    Pointer to the file's entry_ref.
	 *  @param looping True to loop playback continuously.
	 *  @param device  Audio device to use; NULL selects the default device.
	 */
								BFileGameSound(const entry_ref* file,
									bool looping = true,
									BGameSoundDevice* device = NULL);

	/** @brief Constructs a BFileGameSound from a file path string.
	 *  @param file    Path to the audio file.
	 *  @param looping True to loop playback continuously.
	 *  @param device  Audio device to use; NULL selects the default device.
	 */
								BFileGameSound(const char* file,
									bool looping = true,
									BGameSoundDevice* device = NULL);

	/** @brief Constructs a BFileGameSound from an arbitrary BDataIO source.
	 *  @param data    Data source to read audio from.
	 *  @param looping True to loop playback continuously.
	 *  @param device  Audio device to use; NULL selects the default device.
	 */
								BFileGameSound(BDataIO* data,
									bool looping = true,
									BGameSoundDevice* device = NULL);

	/** @brief Destroys the BFileGameSound and releases associated resources. */
	virtual						~BFileGameSound();

	/** @brief Creates an independent copy of this sound object.
	 *  @return Pointer to the newly cloned BGameSound.
	 */
	virtual	BGameSound*			Clone() const;

	/** @brief Begins audio playback.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			StartPlaying();

	/** @brief Stops audio playback.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			StopPlaying();

	/** @brief Preloads the audio data into memory to minimize playback latency.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Preload();

	/** @brief Fills the output buffer with audio data decoded from the file.
	 *  @param buffer    Destination buffer to fill.
	 *  @param byteCount Number of bytes to fill.
	 */
	virtual	void				FillBuffer(void* buffer, size_t byteCount);

	/** @brief Executes an extended operation identified by selector.
	 *  @param selector Operation selector code.
	 *  @param data     Pointer to operation-specific data.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t 			Perform(int32 selector, void* data);

	/** @brief Sets the paused state of the sound, optionally ramping gain over time.
	 *  @param isPaused True to pause, false to resume.
	 *  @param rampTime Duration in microseconds over which to ramp the gain.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SetPaused(bool isPaused, bigtime_t rampTime);

			/** @brief Pause state constants returned by IsPaused(). */
			enum {
				B_NOT_PAUSED,
				B_PAUSE_IN_PROGRESS,
				B_PAUSED
			};

	/** @brief Returns the current pause state of the sound.
	 *  @return One of B_NOT_PAUSED, B_PAUSE_IN_PROGRESS, or B_PAUSED.
	 */
			int32				IsPaused();
private:
								BFileGameSound();
								BFileGameSound(const BFileGameSound& other);

			BFileGameSound&		operator=(const BFileGameSound& other);

			status_t			Init(BDataIO* data);

			bool				Load();
			bool				Read(void* buffer, size_t bytes);

			status_t			_Reserved_BFileGameSound_0(int32 arg, ...);
								// SetPaused(bool paused, bigtime_t ramp);
	virtual	status_t			_Reserved_BFileGameSound_1(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_2(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_3(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_4(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_5(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_6(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_7(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_8(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_9(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_10(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_11(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_12(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_13(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_14(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_15(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_16(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_17(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_18(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_19(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_20(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_21(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_22(int32 arg, ...);
	virtual	status_t			_Reserved_BFileGameSound_23(int32 arg, ...);
private:
			_gs_media_tracker*	fAudioStream;

			bool				fStopping;
			bool				fLooping;
			char				fReserved;
			char*				fBuffer;

			size_t				fFrameSize;
			size_t				fBufferSize;
			size_t				fPlayPosition;

			_gs_ramp*			fPausing;
			bool				fPaused;
			float				fPauseGain;

			BDataIO*			fDataSource;

#ifdef B_HAIKU_64_BIT
			uint32				_reserved[9];
#else
			uint32				_reserved[10];
#endif
};

#endif
