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
 *   Copyright 2020, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file GameSound.h
 *  @brief Abstract base class for all Game Sound Kit sound objects.
 */

#ifndef _GAMESOUND_H
#define _GAMESOUND_H


#include <GameSoundDefs.h>
#include <new>


class BGameSoundDevice;

/** @brief Abstract base class representing a single game sound; manages device association, format, and playback. */
class BGameSound {
public:
	/** @brief Constructs a BGameSound associated with the given device.
	 *  @param device Audio device to use; NULL selects the default device.
	 */
								BGameSound(BGameSoundDevice* device = NULL);

	/** @brief Destroys the sound and releases its resources. */
	virtual						~BGameSound();

	/** @brief Creates an independent copy of this sound object.
	 *  @return Pointer to the newly cloned BGameSound.
	 */
	virtual	BGameSound*			Clone() const = 0;

	/** @brief Returns the initialization status of the sound object.
	 *  @return B_OK if initialized successfully, or an error code.
	 */
			status_t			InitCheck() const;

	/** @brief Returns the audio device this sound is associated with.
	 *  @return Pointer to the BGameSoundDevice.
	 */
			BGameSoundDevice*	Device() const;

	/** @brief Returns the unique identifier for this sound on its device.
	 *  @return The gs_id handle.
	 */
			gs_id				ID() const;

	/** @brief Returns the audio format used by this sound.
	 *  @return Reference to the gs_audio_format descriptor.
	 */
	const	gs_audio_format&	Format() const;

	/** @brief Starts playback of the sound.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			StartPlaying();

	/** @brief Returns whether the sound is currently playing.
	 *  @return True if playing, false otherwise.
	 */
	virtual	bool				IsPlaying();

	/** @brief Stops playback of the sound.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			StopPlaying();

	/** @brief Sets the playback gain, optionally ramped over time.
	 *  @param gain     Target gain value (0.0 = silent, 1.0 = full volume).
	 *  @param duration Ramp duration in microseconds; 0 applies immediately.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetGain(float gain, bigtime_t duration = 0);

	/** @brief Sets the stereo pan position, optionally ramped over time.
	 *  @param pan      Pan value (-1.0 = full left, 0.0 = center, 1.0 = full right).
	 *  @param duration Ramp duration in microseconds; 0 applies immediately.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetPan(float pan, bigtime_t duration = 0);

	/** @brief Returns the current gain value.
	 *  @return Current gain in the range [0.0, 1.0].
	 */
			float				Gain();

	/** @brief Returns the current pan value.
	 *  @return Current pan in the range [-1.0, 1.0].
	 */
			float				Pan();

	/** @brief Sets one or more sound attributes.
	 *  @param attributes     Array of gs_attribute descriptors.
	 *  @param attributeCount Number of elements in the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SetAttributes(gs_attribute* attributes,
									size_t attributeCount);

	/** @brief Retrieves one or more sound attributes.
	 *  @param attributes     Array of gs_attribute descriptors to fill.
	 *  @param attributeCount Number of elements in the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetAttributes(gs_attribute* attributes,
									size_t attributeCount);

	/** @brief Allocates a BGameSound instance from the managed memory pool.
	 *  @param size Number of bytes to allocate.
	 *  @return Pointer to the allocated memory.
	 */
			void* 				operator new(size_t size);

	/** @brief Non-throwing pool allocation for BGameSound.
	 *  @param size Number of bytes to allocate.
	 *  @return Pointer to the allocated memory, or NULL on failure.
	 */
			void*				operator new(size_t size,
									const std::nothrow_t&) throw();

	/** @brief Returns memory to the managed pool.
	 *  @param ptr Pointer previously returned by operator new.
	 */
			void				operator delete(void* ptr);

	/** @brief Non-throwing pool deallocation for BGameSound.
	 *  @param ptr Pointer previously returned by the non-throwing operator new.
	 */
			void				operator delete(void* ptr,
									const std::nothrow_t&) throw();

	/** @brief Sets the size of the shared memory pool used for sound allocations.
	 *  @param poolSize Desired pool size in bytes.
	 *  @return B_OK on success, or an error code.
	 */
	static	status_t			SetMemoryPoolSize(size_t poolSize);

	/** @brief Locks or unlocks the sound memory pool in physical RAM.
	 *  @param lockInCore True to lock pages in RAM, false to allow paging.
	 *  @return B_OK on success, or an error code.
	 */
	static	status_t			LockMemoryPool(bool lockInCore);

	/** @brief Sets the maximum number of simultaneous sounds.
	 *  @param maxCount Maximum sound count.
	 *  @return The previous maximum count.
	 */
	static	int32				SetMaxSoundCount(int32 maxCount);

	/** @brief Executes an extended operation identified by selector.
	 *  @param selector Operation selector code.
	 *  @param data     Pointer to operation-specific data.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Perform(int32 selector, void* data);

protected:
	/** @brief Records an initialization error for later retrieval via InitCheck().
	 *  @param initError The error code to store.
	 *  @return The stored error code.
	 */
			status_t			SetInitError(status_t initError);

	/** @brief Completes initialization by registering the given gs_id handle.
	 *  @param handle The sound handle returned by the device.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Init(gs_id handle);

	/** @brief Copy constructor for use by subclasses.
	 *  @param other The source BGameSound to copy.
	 */
								BGameSound(const BGameSound& other);

	/** @brief Assignment operator for use by subclasses.
	 *  @param other The source BGameSound to copy.
	 *  @return Reference to this object.
	 */
			BGameSound&			operator=(const BGameSound& other);

private:
								BGameSound();

	virtual	status_t			_Reserved_BGameSound_0(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_1(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_2(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_3(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_4(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_5(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_6(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_7(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_8(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_9(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_10(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_11(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_12(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_13(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_14(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_15(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_16(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_17(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_18(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_19(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_20(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_21(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_22(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_23(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_24(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_25(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_26(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_27(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_28(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_29(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_30(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_31(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_32(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_33(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_34(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_35(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_36(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_37(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_38(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_39(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_40(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_41(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_42(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_43(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_44(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_45(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_46(int32 arg, ...);
	virtual	status_t			_Reserved_BGameSound_47(int32 arg, ...);

private:
			BGameSoundDevice*	fDevice;
			status_t			fInitError;

			gs_audio_format		fFormat;
			gs_id				fSound;

			uint32				_reserved[16];
};


#endif
