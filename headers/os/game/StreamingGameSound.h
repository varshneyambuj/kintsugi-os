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
 *   Copyright 2020 Haiku, Inc. All Rights Reserved.
 *   Author: Christopher ML Zumwalt May (zummy@users.sf.net)
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file StreamingGameSound.h
 *  @brief BStreamingGameSound base class for callback-driven streaming audio output.
 */

#ifndef _STREAMINGGAMESOUND_H
#define _STREAMINGGAMESOUND_H


#include <GameSound.h>
#include <Locker.h>
#include <SupportDefs.h>


/** @brief Base class for game sounds that stream audio via a fill callback rather than a static buffer. */
class BStreamingGameSound : public BGameSound
{
public:
	/** @brief Constructs a BStreamingGameSound with the given buffer geometry.
	 *  @param bufferFrameCount Number of frames per audio buffer.
	 *  @param format           Pointer to the desired audio format.
	 *  @param bufferCount      Number of buffers in the ring; defaults to 2.
	 *  @param device           Audio device to use; NULL selects the default.
	 */
						BStreamingGameSound(size_t bufferFrameCount,
							const gs_audio_format* format,
							size_t bufferCount = 2,
							BGameSoundDevice* device = NULL);

	/** @brief Destroys the BStreamingGameSound and releases resources. */
	virtual				~BStreamingGameSound();

	/** @brief Creates an independent copy of this sound object.
	 *  @return Pointer to the newly cloned BGameSound.
	 */
	virtual	BGameSound*	Clone() const;

	/** @brief Signature of the stream fill callback. */
	typedef	void		(*hook)(void* cookie, void* buffer, size_t byteCount,
							BStreamingGameSound* me);

	/** @brief Installs a callback invoked each time the audio subsystem needs more data.
	 *  @param h      The hook function to install.
	 *  @param cookie Arbitrary pointer passed back to the hook on each call.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	SetStreamHook(hook h, void* cookie);

	/** @brief Fills the output buffer by invoking the registered stream hook.
	 *  @param buffer    Destination buffer to fill.
	 *  @param byteCount Number of bytes requested.
	 */
	virtual	void		FillBuffer(void* buffer, size_t byteCount);

	/** @brief Sets one or more sound attributes; some attributes are read-only for streaming sounds.
	 *  @param attributes     Array of gs_attribute descriptors.
	 *  @param attributeCount Number of elements in the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	SetAttributes(gs_attribute* attributes,
							size_t attributeCount);

	/** @brief Executes an extended operation identified by selector.
	 *  @param selector Operation selector code.
	 *  @param data     Pointer to operation-specific data.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	Perform(int32 selector, void* data);

protected:
	/** @brief Protected constructor for device-only initialization used by subclasses.
	 *  @param device Audio device to use; NULL selects the default.
	 */
						BStreamingGameSound(BGameSoundDevice* device);

	/** @brief Configures buffer geometry; called by subclasses after construction.
	 *  @param bufferFrameCount Number of frames per audio buffer.
	 *  @param format           Pointer to the desired audio format.
	 *  @param bufferCount      Number of buffers in the ring.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t	SetParameters(size_t bufferFrameCount,
							const gs_audio_format* format,
							size_t bufferCount);

	/** @brief Acquires the internal mutex; returns true if the lock was obtained.
	 *  @return True if locked successfully.
	 */
			bool		Lock();

	/** @brief Releases the internal mutex acquired by Lock(). */
			void		Unlock();

private:
						BStreamingGameSound();
						BStreamingGameSound(const BStreamingGameSound& other);
						BStreamingGameSound&
							operator=(const BStreamingGameSound& other);

	virtual	status_t	_Reserved_BStreamingGameSound_0(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_1(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_2(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_3(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_4(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_5(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_6(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_7(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_8(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_9(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_10(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_11(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_12(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_13(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_14(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_15(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_16(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_17(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_18(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_19(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_20(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_21(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_22(int32 arg, ...);
	virtual	status_t	_Reserved_BStreamingGameSound_23(int32 arg, ...);

private:
			hook		fStreamHook;
			void*		fStreamCookie;
			BLocker		fLock;

			uint32		_reserved[12];
};

#endif
