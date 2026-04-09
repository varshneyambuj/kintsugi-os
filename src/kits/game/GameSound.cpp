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
 *   Copyright 2002-2012 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 */


/**
 * @file GameSound.cpp
 * @brief Abstract base class for all Game Kit sound objects
 *
 * Implements BGameSound, which provides the common interface for gain and pan
 * control, playback start/stop, and attribute get/set. All concrete sound
 * classes (BSimpleGameSound, BFileGameSound, BStreamingGameSound, etc.) derive
 * from this class. Each instance acquires a reference to the shared
 * BGameSoundDevice singleton on construction and releases it on destruction.
 *
 * @see GameSoundDevice.cpp, SimpleGameSound.cpp, FileGameSound.cpp
 */


#include <GameSound.h>

#include <stdio.h>
#include <string.h>

#include "GameSoundBuffer.h"
#include "GameSoundDevice.h"


using std::nothrow;


/**
 * @brief Constructs a BGameSound using the default (or provided) device.
 *
 * The \a device parameter is currently ignored; the BeBook documents that
 * BGameSoundDevice must always be NULL. The shared default device is acquired
 * automatically.
 *
 * @param device Unused; pass NULL.
 */
BGameSound::BGameSound(BGameSoundDevice *device)
	:
	fSound(-1)
{
	// TODO: device is ignored!
	// NOTE: BeBook documents that BGameSoundDevice must currently always
	// be NULL...
	fDevice = BGameSoundDevice::GetDefaultDevice();
	fInitError = fDevice->InitCheck();
}


/**
 * @brief Copy constructor. Shares the same default device as \a other.
 * @param other The BGameSound to copy format information from.
 */
BGameSound::BGameSound(const BGameSound &other)
	:
	fSound(-1)
{
	memcpy(&fFormat, &other.fFormat, sizeof(gs_audio_format));
	// TODO: device from other is ignored!
	fDevice = BGameSoundDevice::GetDefaultDevice();

	fInitError = fDevice->InitCheck();
}


/**
 * @brief Destroys the BGameSound, releasing the sound buffer and the device reference.
 */
BGameSound::~BGameSound()
{
	if (fSound >= 0)
		fDevice->ReleaseBuffer(fSound);

	BGameSoundDevice::ReleaseDevice();
}


/**
 * @brief Returns the initialization status of this sound object.
 * @return B_OK if the device is available, or an error code on failure.
 */
status_t
BGameSound::InitCheck() const
{
	return fInitError;
}


/**
 * @brief Returns the game sound device used by this object.
 *
 * @note Per the BeBook specification, this should return NULL when the default
 *     device is in use, but the current implementation returns the device pointer.
 *
 * @return Pointer to the BGameSoundDevice, or NULL if no device is set.
 */
BGameSoundDevice *
BGameSound::Device() const
{
	// TODO: Must return NULL if default device is being used!
	return fDevice;
}


/**
 * @brief Returns the sound buffer ID assigned to this sound.
 *
 * @note Should be 0 if no sound has been selected, but is currently
 *     initialized to -1 in the constructors.
 *
 * @return The \c gs_id handle, or -1 if not yet initialized.
 */
gs_id
BGameSound::ID() const
{
	// TODO: Should be 0 if no sound has been selected! But fSound
	// is initialized with -1 in the constructors.
	return fSound;
}


/**
 * @brief Returns the audio format descriptor for this sound.
 * @return A const reference to the \c gs_audio_format of the sound buffer.
 */
const gs_audio_format &
BGameSound::Format() const
{
	return fDevice->Format(fSound);
}


/**
 * @brief Starts playback of the sound.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BGameSound::StartPlaying()
{
	fDevice->StartPlaying(fSound);
	return B_OK;
}


/**
 * @brief Returns whether the sound is currently playing.
 * @return \c true if the sound is actively playing.
 */
bool
BGameSound::IsPlaying()
{
	return fDevice->IsPlaying(fSound);
}


/**
 * @brief Stops playback of the sound.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BGameSound::StopPlaying()
{
	fDevice->StopPlaying(fSound);
	return B_OK;
}


/**
 * @brief Sets the playback gain (volume) of the sound.
 *
 * @param gain     Target gain in the range [0.0, 1.0].
 * @param duration Ramp duration in microseconds (0 for immediate change).
 * @return B_OK on success, or an error code if the attribute cannot be set.
 */
status_t
BGameSound::SetGain(float gain, bigtime_t duration)
{
	gs_attribute attribute;

	attribute.attribute = B_GS_GAIN;
	attribute.value = gain;
	attribute.duration = duration;
	attribute.flags = 0;

	return fDevice->SetAttributes(fSound, &attribute, 1);
}


/**
 * @brief Sets the stereo pan position of the sound.
 *
 * @param pan      Pan position in the range [-1.0 (full left), 1.0 (full right)].
 * @param duration Ramp duration in microseconds (0 for immediate change).
 * @return B_OK on success, or an error code if the attribute cannot be set.
 */
status_t
BGameSound::SetPan(float pan, bigtime_t duration)
{
	gs_attribute attribute;

	attribute.attribute = B_GS_PAN;
	attribute.value = pan;
	attribute.duration = duration;
	attribute.flags = 0;

	return fDevice->SetAttributes(fSound, &attribute, 1);
}


/**
 * @brief Returns the current gain (volume) of the sound.
 * @return The current gain value, or 0.0 on error.
 */
float
BGameSound::Gain()
{
	gs_attribute attribute;

	attribute.attribute = B_GS_GAIN;
	attribute.flags = 0;

	if (fDevice->GetAttributes(fSound, &attribute, 1) != B_OK)
		return 0.0;

	return attribute.value;
}


/**
 * @brief Returns the current stereo pan position of the sound.
 * @return The current pan value in [-1.0, 1.0], or 0.0 on error.
 */
float
BGameSound::Pan()
{
	gs_attribute attribute;

	attribute.attribute = B_GS_PAN;
	attribute.flags = 0;

	if (fDevice->GetAttributes(fSound, &attribute, 1) != B_OK)
		return 0.0;

	return attribute.value;
}


/**
 * @brief Sets multiple sound attributes at once.
 * @param inAttributes     Array of \c gs_attribute structs describing the changes.
 * @param inAttributeCount Number of elements in \a inAttributes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BGameSound::SetAttributes(gs_attribute *inAttributes, size_t inAttributeCount)
{
	return fDevice->SetAttributes(fSound, inAttributes, inAttributeCount);
}


/**
 * @brief Retrieves multiple sound attributes at once.
 * @param outAttributes    Array of \c gs_attribute structs to fill.
 * @param inAttributeCount Number of elements in \a outAttributes.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BGameSound::GetAttributes(gs_attribute *outAttributes, size_t inAttributeCount)
{
	return fDevice->GetAttributes(fSound, outAttributes, inAttributeCount);
}


/**
 * @brief Extension hook for subclasses (not currently implemented).
 * @return B_ERROR always.
 */
status_t
BGameSound::Perform(int32 selector, void *data)
{
	return B_ERROR;
}


/** @brief Custom operator new — delegates to the global allocator. */
void *
BGameSound::operator new(size_t size)
{
	return ::operator new(size);
}


/** @brief Non-throwing operator new — delegates to the global nothrow allocator. */
void *
BGameSound::operator new(size_t size, const std::nothrow_t &nt) throw()
{
	return ::operator new(size, nt);
}


/** @brief Custom operator delete — delegates to the global deallocator. */
void
BGameSound::operator delete(void *ptr)
{
	::operator delete(ptr);
}


#if !__MWERKS__
/** @brief Non-throwing operator delete — delegates to the global nothrow deallocator. */
void
BGameSound::operator delete(void *ptr, const std::nothrow_t &nt) throw()
{
	::operator delete(ptr, nt);
}
#endif


/**
 * @brief Sets the size of the memory pool used for sound data (stub).
 * @return B_ERROR — not implemented.
 */
status_t
BGameSound::SetMemoryPoolSize(size_t in_poolSize)
{
	return B_ERROR;
}


/**
 * @brief Locks the memory pool into physical RAM (stub).
 * @return B_ERROR — not implemented.
 */
status_t
BGameSound::LockMemoryPool(bool in_lockInCore)
{
	return B_ERROR;
}


/**
 * @brief Sets the maximum simultaneous sound count (stub; returns the requested count).
 * @param in_maxCount The desired maximum sound count.
 * @return The value of \a in_maxCount unchanged.
 */
int32
BGameSound::SetMaxSoundCount(int32 in_maxCount)
{
	return in_maxCount;
}


/**
 * @brief Sets the initialization error code (for use by subclasses during Init()).
 * @param in_initError The error code to record.
 * @return B_OK always.
 */
status_t
BGameSound::SetInitError(status_t in_initError)
{
	fInitError = in_initError;
	return B_OK;
}


/**
 * @brief Records the sound buffer handle assigned by the device.
 *
 * Called by subclasses after successfully creating a buffer via
 * BGameSoundDevice::CreateBuffer(). Has no effect if a handle is already set.
 *
 * @param handle The \c gs_id returned by the device.
 * @return B_OK always.
 */
status_t
BGameSound::Init(gs_id handle)
{
	if (fSound < 0)
		fSound = handle;

	return B_OK;
}


//	#pragma mark - FBC protection


status_t BGameSound::_Reserved_BGameSound_0(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_1(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_2(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_3(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_4(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_5(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_6(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_7(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_8(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_9(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_10(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_11(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_12(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_13(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_14(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_15(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_16(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_17(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_18(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_19(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_20(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_21(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_22(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_23(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_24(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_25(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_26(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_27(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_28(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_29(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_30(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_31(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_32(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_33(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_34(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_35(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_36(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_37(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_38(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_39(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_40(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_41(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_42(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_43(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_44(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_45(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_46(int32 arg, ...) { return B_ERROR; }
status_t BGameSound::_Reserved_BGameSound_47(int32 arg, ...) { return B_ERROR; }
