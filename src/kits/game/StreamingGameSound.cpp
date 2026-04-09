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
 *   Copyright (c) 2001-2002, Haiku
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 */

/**
 * @file StreamingGameSound.cpp
 * @brief BStreamingGameSound base class for streaming (on-the-fly) game sounds.
 *
 * BStreamingGameSound is the public base class for all GameKit sound classes
 * that generate or supply audio data incrementally rather than from a
 * pre-loaded buffer. It owns a BLocker for synchronisation between the
 * audio-fill thread and the application, and an optional stream hook
 * (function pointer + cookie) that is called each time the Media Kit
 * requests a new buffer.
 *
 * Concrete subclasses (BPushGameSound, BFileGameSound) override FillBuffer()
 * or install a hook via SetStreamHook() to supply audio data.
 */


#include "StreamingGameSound.h"

#include "GameSoundDevice.h"


/**
 * @brief Constructs a fully initialised BStreamingGameSound.
 *
 * If the base BGameSound initialises successfully, SetParameters() is called
 * to create the streaming buffer on the device and connect it to the mixer.
 * Any error from SetParameters() is propagated via SetInitError().
 *
 * @param inBufferFrameCount Number of frames per streaming buffer.
 * @param format             Audio format for the streaming connection.
 * @param inBufferCount      Number of buffers in the buffer group.
 * @param device             The BGameSoundDevice to use, or NULL for the default.
 */
BStreamingGameSound::BStreamingGameSound(size_t inBufferFrameCount,
	const gs_audio_format *format, size_t inBufferCount,
	BGameSoundDevice *device)
	:
	BGameSound(device),
	fStreamHook(NULL),
	fStreamCookie(NULL)
{
	if (InitCheck() == B_OK) {
		status_t error = SetParameters(inBufferFrameCount, format,
			inBufferCount);
		SetInitError(error);
	}
}


/**
 * @brief Constructs an uninitialised BStreamingGameSound for subclass use.
 *
 * Does not call SetParameters(). Subclasses are expected to complete
 * initialisation themselves (e.g. BPushGameSound, BFileGameSound).
 *
 * @param device The BGameSoundDevice to use, or NULL for the default.
 */
BStreamingGameSound::BStreamingGameSound(BGameSoundDevice *device)
	:
	BGameSound(device),
	fStreamHook(NULL),
	fStreamCookie(NULL)
{
}


/**
 * @brief Destroys the BStreamingGameSound.
 */
BStreamingGameSound::~BStreamingGameSound()
{
}


/**
 * @brief Cloning is not supported for streaming sounds.
 *
 * Streaming sounds represent live data sources that cannot be trivially
 * duplicated.
 *
 * @return Always NULL.
 */
BGameSound *
BStreamingGameSound::Clone() const
{
	return NULL;
}


/**
 * @brief Installs a callback function that is invoked each time a new buffer
 *        of audio data is needed.
 *
 * The hook signature is:
 * @code
 *   void hook(void* cookie, void* buffer, size_t byteCount,
 *             BStreamingGameSound* me);
 * @endcode
 * The hook is called from FillBuffer() on the Media Kit's buffer-production
 * thread. \a cookie is an arbitrary user-supplied value forwarded to the hook.
 *
 * @param hook   Function pointer to the stream hook callback.
 * @param cookie Opaque value passed to the hook on each invocation.
 * @return Always B_OK.
 */
status_t
BStreamingGameSound::SetStreamHook(void (*hook)(void* inCookie, void* inBuffer,
	size_t inByteCount, BStreamingGameSound * me), void * cookie)
{
	fStreamHook = hook;
	fStreamCookie = cookie;

	return B_OK;
}


/**
 * @brief Invokes the installed stream hook to fill the given audio buffer.
 *
 * If no hook has been installed (fStreamHook is NULL) the call is a no-op
 * and the buffer is left untouched (the caller is responsible for silence).
 *
 * @param inBuffer    Destination buffer to fill with audio data.
 * @param inByteCount Size of \a inBuffer in bytes.
 */
void
BStreamingGameSound::FillBuffer(void *inBuffer,
								size_t inByteCount)
{
	if (fStreamHook)
		(fStreamHook)(fStreamCookie, inBuffer, inByteCount, this);
}


/**
 * @brief Reserved virtual dispatch hook; not implemented.
 * @param selector Selector identifying the desired operation.
 * @param data     Pointer to operation-specific data.
 * @return Always B_ERROR.
 */
status_t
BStreamingGameSound::Perform(int32 selector, void *data)
{
	return B_ERROR;
}


/**
 * @brief Sets sound attributes, delegating to BGameSound::SetAttributes().
 *
 * @param inAttributes     Array of gs_attribute structs to apply.
 * @param inAttributeCount Number of entries in \a inAttributes.
 * @return Result of BGameSound::SetAttributes().
 */
status_t
BStreamingGameSound::SetAttributes(gs_attribute * inAttributes,
									size_t inAttributeCount)
{
	return BGameSound::SetAttributes(inAttributes, inAttributeCount);
}


/**
 * @brief Creates the streaming buffer on the device and registers the sound.
 *
 * Calls BGameSoundDevice::CreateBuffer() with a back-pointer to this object
 * so that the internal StreamingSoundBuffer knows to call FillBuffer() on
 * this instance. On success, BGameSound::Init() is called to record the
 * assigned gs_id.
 *
 * @param inBufferFrameCount Number of frames per buffer passed to the device.
 * @param format             Audio format for the streaming buffer.
 * @param inBufferCount      Number of buffers in the buffer group.
 * @return B_OK on success; an error code if buffer creation or registration fails.
 */
status_t
BStreamingGameSound::SetParameters(size_t inBufferFrameCount,
	const gs_audio_format *format, size_t inBufferCount)
{
	gs_id sound;
	status_t error = Device()->CreateBuffer(&sound, this, format,
		inBufferFrameCount, inBufferCount);
	if (error != B_OK) return error;

	return BGameSound::Init(sound);
}


/**
 * @brief Acquires the internal BLocker, blocking until it is available.
 *
 * Used by subclasses to serialise access to shared state (e.g. the pause
 * ramp in BFileGameSound) between the application thread and the Media Kit
 * buffer-production thread.
 *
 * @return \c true if the lock was acquired; \c false on error.
 */
bool
BStreamingGameSound::Lock()
{
	return fLock.Lock();
}


/**
 * @brief Releases the internal BLocker.
 *
 * Must be paired with a successful call to Lock().
 */
void
BStreamingGameSound::Unlock()
{
	fLock.Unlock();
}


/* unimplemented for protection of the user:
 *
 * BStreamingGameSound::BStreamingGameSound()
 * BStreamingGameSound::BStreamingGameSound(const BStreamingGameSound &)
 * BStreamingGameSound &BStreamingGameSound::operator=(const BStreamingGameSound &)
 */


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_0(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_1(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_2(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_3(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_4(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_5(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_6(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_7(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_8(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_9(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_10(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_11(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_12(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_13(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_14(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_15(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_16(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_17(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_18(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_19(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_20(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_21(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_22(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BStreamingGameSound::_Reserved_BStreamingGameSound_23(int32 arg, ...)
{
	return B_ERROR;
}
