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

/** @file PushGameSound.h
 *  @brief BPushGameSound class for producer-driven (push) streaming audio output.
 */

#ifndef _PUSHGAMESOUND_H
#define _PUSHGAMESOUND_H


#include <StreamingGameSound.h>


class BList;

/** @brief Streaming sound class where the application pushes audio data page-by-page into a cyclic buffer. */
class BPushGameSound : public BStreamingGameSound {
public:
	/** @brief Constructs a BPushGameSound with the given buffer geometry.
	 *  @param inBufferFrameCount Number of frames per audio page.
	 *  @param format             Pointer to the desired audio format.
	 *  @param inBufferCount      Number of pages in the cyclic buffer; defaults to 2.
	 *  @param device             Audio device to use; NULL selects the default.
	 */
						BPushGameSound(size_t inBufferFrameCount,
							const gs_audio_format* format,
							size_t inBufferCount = 2,
							BGameSoundDevice* device = NULL);

	/** @brief Destroys the BPushGameSound and releases buffer resources. */
	virtual				~BPushGameSound();

	/** @brief Status codes returned by page-locking operations. */
	enum lock_status {
		lock_failed = -1,         /**< Page could not be locked. */
		lock_ok = 0,              /**< Page locked successfully. */
		lock_ok_frames_dropped    /**< Page locked but frames were dropped. */
	};

	/** @brief Locks the next available output page for writing.
	 *  @param _pagePtr  Set to the address of the locked page.
	 *  @param _pageSize Set to the size of the locked page in bytes.
	 *  @return lock_ok, lock_ok_frames_dropped, or lock_failed.
	 */
	virtual	lock_status	LockNextPage(void** _pagePtr, size_t* _pageSize);

	/** @brief Releases a previously locked page and queues it for playback.
	 *  @param pagePtr Pointer returned by LockNextPage().
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	UnlockPage(void* pagePtr);

	/** @brief Locks the entire cyclic buffer for direct cyclic-write access.
	 *  @param _basePtr Set to the base address of the buffer.
	 *  @param _size    Set to the total buffer size in bytes.
	 *  @return lock_ok, lock_ok_frames_dropped, or lock_failed.
	 */
	virtual	lock_status	LockForCyclic(void** _basePtr, size_t* _size);

	/** @brief Releases the cyclic-mode lock acquired by LockForCyclic().
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	UnlockCyclic();

	/** @brief Returns the current write position within the cyclic buffer.
	 *  @return Byte offset from the start of the cyclic buffer.
	 */
	virtual	size_t		CurrentPosition();

	/** @brief Creates an independent copy of this sound object.
	 *  @return Pointer to the newly cloned BGameSound.
	 */
	virtual	BGameSound*	Clone() const;

	/** @brief Executes an extended operation identified by selector.
	 *  @param selector Operation selector code.
	 *  @param data     Pointer to operation-specific data.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	Perform(int32 selector, void* data);

protected:
	/** @brief Protected constructor for subclass use; defers buffer setup.
	 *  @param device Audio device to use; NULL selects the default.
	 */
						BPushGameSound(BGameSoundDevice* device);

	/** @brief Configures buffer geometry after construction.
	 *  @param bufferFrameCount Number of frames per audio page.
	 *  @param format           Pointer to the desired audio format.
	 *  @param bufferCount      Number of pages in the cyclic buffer.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	SetParameters(size_t bufferFrameCount,
							const gs_audio_format* format, size_t bufferCount);

	/** @brief Installs a callback hook invoked each time the buffer needs data.
	 *  @param hook   Function pointer called to fill the buffer.
	 *  @param cookie Arbitrary pointer passed back to the hook.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	SetStreamHook(void (*hook)(void* inCookie,
								void* buffer, size_t byteCount,
								BStreamingGameSound* me),
							void* cookie);

	/** @brief Fills the output buffer from the push buffer; called by the audio subsystem.
	 *  @param buffer    Destination buffer to fill.
	 *  @param byteCount Number of bytes requested.
	 */
	virtual	void		FillBuffer(void* buffer, size_t byteCount);

private:
						BPushGameSound();
						BPushGameSound(const BPushGameSound& other);
	BPushGameSound&		operator=(const BPushGameSound& other);

			bool		BytesReady(size_t* bytes);

	virtual	status_t	_Reserved_BPushGameSound_0(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_1(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_2(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_3(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_4(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_5(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_6(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_7(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_8(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_9(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_10(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_11(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_12(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_13(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_14(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_15(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_16(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_17(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_18(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_19(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_20(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_21(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_22(int32 arg, ...);
	virtual	status_t	_Reserved_BPushGameSound_23(int32 arg, ...);

private:
			sem_id		fLock;
			BList*		fPageLocked;
			size_t		fLockPos;

			size_t		fPlayPos;
			char*		fBuffer;

			size_t		fPageSize;
			int32		fPageCount;
			size_t		fBufferSize;

			uint32		_reserved[12];
};

#endif
