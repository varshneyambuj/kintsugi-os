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
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file SimpleGameSound.h
 *  @brief BSimpleGameSound class for playing pre-loaded, in-memory audio samples.
 */

#ifndef _SIMPLEGAMESOUND_H
#define _SIMPLEGAMESOUND_H


#include <GameSound.h>
#include <GameSoundDefs.h>

struct entry_ref;

/** @brief Plays a fully buffered (in-memory) audio sample with optional looping. */
class BSimpleGameSound : public BGameSound {
public:
	/** @brief Constructs a BSimpleGameSound by loading audio from a file reference.
	 *  @param file   Pointer to the file's entry_ref.
	 *  @param device Audio device to use; NULL selects the default device.
	 */
							BSimpleGameSound(const entry_ref* file,
								BGameSoundDevice* device = NULL);

	/** @brief Constructs a BSimpleGameSound by loading audio from a file path.
	 *  @param file   Path to the audio file.
	 *  @param device Audio device to use; NULL selects the default device.
	 */
							BSimpleGameSound(const char* file,
								BGameSoundDevice* device = NULL);

	/** @brief Constructs a BSimpleGameSound from a raw PCM data buffer.
	 *  @param data       Pointer to raw PCM sample data.
	 *  @param frameCount Number of audio frames in the buffer.
	 *  @param format     Pointer to the audio format descriptor.
	 *  @param device     Audio device to use; NULL selects the default device.
	 */
							BSimpleGameSound(const void* data,
								size_t frameCount,
								const gs_audio_format* format,
								BGameSoundDevice* device = NULL);

	/** @brief Copy constructor; creates a new sound sharing the same audio data.
	 *  @param other Source BSimpleGameSound to copy.
	 */
							BSimpleGameSound(const BSimpleGameSound& other);

	/** @brief Destroys the BSimpleGameSound and releases its resources. */
	virtual					~BSimpleGameSound();

	/** @brief Creates an independent copy of this sound object.
	 *  @return Pointer to the newly cloned BGameSound.
	 */
	virtual	BGameSound*		Clone() const;

	/** @brief Executes an extended operation identified by selector.
	 *  @param selector Operation selector code.
	 *  @param data     Pointer to operation-specific data.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t 		Perform(int32 selector, void* data);

	/** @brief Enables or disables looped playback.
	 *  @param looping True to loop continuously, false to play once.
	 *  @return B_OK on success, or an error code.
	 */
			status_t		SetIsLooping(bool looping);

	/** @brief Returns whether the sound is set to loop.
	 *  @return True if looping is enabled.
	 */
			bool			IsLooping() const;
private:
							BSimpleGameSound();

	BSimpleGameSound&		operator=(const BSimpleGameSound& other);

			status_t		Init(const entry_ref* file);
			status_t 		Init(const void* data, int64 frameCount,
								const gs_audio_format* format);

	virtual	status_t		_Reserved_BSimpleGameSound_0(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_1(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_2(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_3(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_4(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_5(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_6(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_7(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_8(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_9(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_10(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_11(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_12(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_13(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_14(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_15(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_16(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_17(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_18(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_19(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_20(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_21(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_22(int32 arg, ...);
	virtual	status_t		_Reserved_BSimpleGameSound_23(int32 arg, ...);
private:
			uint32			_reserved[12];
};

#endif
