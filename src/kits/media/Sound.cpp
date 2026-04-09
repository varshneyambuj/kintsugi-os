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
 *   Copyright 2009-2019, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jacob Secunda
 *       Marcus Overhagen
 *       Michael Lotz <mmlr@mlotz.ch>
 */

/** @file Sound.cpp
 *  @brief Implements BSound, a reference-counted in-memory or file-backed
 *         raw audio buffer used with BSoundPlayer.
 */

#include <Sound.h>

#include <new>
#include <string.h>

#include <File.h>
#include <MediaDebug.h>

#include "TrackReader.h"


/**
 * @brief Construct a BSound from an existing raw memory buffer.
 *
 * @param data         Pointer to the raw audio data.
 * @param size         Size of the data buffer in bytes.
 * @param format       Raw audio format describing the data.
 * @param freeWhenDone If true, @p data will be freed when the BSound is
 *                     destroyed.
 */
BSound::BSound(void* data, size_t size, const media_raw_audio_format& format,
	bool freeWhenDone)
	:	fData(data),
		fDataSize(size),
		fFile(NULL),
		fRefCount(1),
		fStatus(B_NO_INIT),
		fFormat(format),
		fFreeWhenDone(freeWhenDone),
		fTrackReader(NULL)
{
	if (fData == NULL)
		return;

	fStatus = B_OK;
}


/**
 * @brief Construct a BSound from a sound file on disk.
 *
 * @param soundFile       Entry ref pointing to the audio file to open.
 * @param loadIntoMemory  Reserved; not yet used.
 */
BSound::BSound(const entry_ref* soundFile, bool loadIntoMemory)
	:	fData(NULL),
		fDataSize(0),
		fFile(new(std::nothrow) BFile(soundFile, B_READ_ONLY)),
		fRefCount(1),
		fStatus(B_NO_INIT),
		fFreeWhenDone(false),
		fTrackReader(NULL)
{
	if (fFile == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	fStatus = fFile->InitCheck();
	if (fStatus != B_OK)
		return;

	memset(&fFormat, 0, sizeof(fFormat));
	fTrackReader = new(std::nothrow) BPrivate::BTrackReader(fFile, fFormat);
	if (fTrackReader == NULL) {
		fStatus = B_NO_MEMORY;
		return;
	}

	fStatus = fTrackReader->InitCheck();
	if (fStatus != B_OK)
		return;

	fFormat = fTrackReader->Format();
	fStatus = B_OK;
}


/**
 * @brief Protected constructor for subclass use; not yet implemented.
 *
 * @param format Raw audio format (reserved for future use).
 */
BSound::BSound(const media_raw_audio_format& format)
	:	fData(NULL),
		fDataSize(0),
		fFile(NULL),
		fRefCount(1),
		fStatus(B_ERROR),
		fFormat(format),
		fFreeWhenDone(false),
		fTrackReader(NULL)
{
	// unimplemented protected constructor
	UNIMPLEMENTED();
}


/**
 * @brief Destructor. Frees resources including the optional data buffer.
 */
BSound::~BSound()
{
	delete fTrackReader;
	delete fFile;

	if (fFreeWhenDone)
		free(fData);
}


/**
 * @brief Returns the initialization status of this BSound.
 *
 * @return B_OK if initialized successfully, or an error code.
 */
status_t
BSound::InitCheck()
{
	return fStatus;
}


/**
 * @brief Increment the reference count and return this object.
 *
 * @return Pointer to this BSound.
 */
BSound*
BSound::AcquireRef()
{
	atomic_add(&fRefCount, 1);
	return this;
}


/**
 * @brief Decrement the reference count, deleting the object when it reaches zero.
 *
 * @return true if the object still exists, false if it was deleted.
 */
bool
BSound::ReleaseRef()
{
	if (atomic_add(&fRefCount, -1) == 1) {
		delete this;
		return false;
	}

	// TODO: verify those returns
	return true;
}


/**
 * @brief Return the current reference count.
 *
 * @return Current reference count value.
 */
int32
BSound::RefCount() const
{
	return fRefCount;
}


/**
 * @brief Compute the playback duration of the sound in microseconds.
 *
 * @return Duration in microseconds, or 0 if the frame rate is zero.
 */
bigtime_t
BSound::Duration() const
{
	float frameRate = fFormat.frame_rate;

	if (frameRate == 0.0)
		return 0;

	uint32 bytesPerSample = fFormat.format &
		media_raw_audio_format::B_AUDIO_SIZE_MASK;
	int64 frameCount = Size() / (fFormat.channel_count * bytesPerSample);

	return (bigtime_t)ceil((1000000LL * frameCount) / frameRate);
}


/**
 * @brief Return the raw audio format of this sound.
 *
 * @return Const reference to the media_raw_audio_format structure.
 */
const media_raw_audio_format&
BSound::Format() const
{
	return fFormat;
}


/**
 * @brief Return a pointer to the in-memory audio data, or NULL if file-backed.
 *
 * @return Pointer to raw audio data buffer, or NULL.
 */
const void*
BSound::Data() const
{
	return fData;
}


/**
 * @brief Return the total size of the audio data in bytes.
 *
 * @return Size in bytes of the backing store (file or memory buffer).
 */
off_t
BSound::Size() const
{
	if (fFile != NULL) {
		off_t result = 0;
		fFile->GetSize(&result);
		return result;
	}

	return fDataSize;
}


/**
 * @brief Copy audio data starting at a byte offset into a caller-supplied buffer.
 *
 * @param offset      Byte offset from the start of the audio data.
 * @param intoBuffer  Destination buffer to copy data into.
 * @param bufferSize  Maximum number of bytes to copy.
 * @param outUsed     If non-NULL, receives the number of bytes actually copied.
 * @return true on success, false on failure.
 */
bool
BSound::GetDataAt(off_t offset, void* intoBuffer, size_t bufferSize,
	size_t* outUsed)
{
	if (intoBuffer == NULL)
		return false;

	if (fData != NULL) {
		size_t copySize = MIN(bufferSize, fDataSize - offset);
		memcpy(intoBuffer, (uint8*)fData + offset, copySize);
		if (outUsed != NULL)
			*outUsed = copySize;
		return true;
	}

	if (fTrackReader != NULL) {
		int32 frameSize = fTrackReader->FrameSize();
		int64 frameCount = fTrackReader->CountFrames();
		int64 startFrame = offset / frameSize;
		if (startFrame > frameCount)
			return false;

		if (fTrackReader->SeekToFrame(&startFrame) != B_OK)
			return false;

		off_t bufferOffset = offset - startFrame * frameSize;
		int64 directStartFrame = (offset + frameSize - 1) / frameSize;
		int64 directFrameCount = (offset + bufferSize - directStartFrame
			* frameSize) / frameSize;

		if (bufferOffset != 0) {
			int64 indirectFrameCount = directStartFrame - startFrame;
			size_t indirectSize = indirectFrameCount * frameSize;
			void* buffer = malloc(indirectSize);
			if (buffer == NULL)
				return false;

			if (fTrackReader->ReadFrames(buffer, indirectFrameCount) != B_OK) {
				free(buffer);
				return false;
			}

			memcpy(intoBuffer, (uint8*)buffer + bufferOffset,
				indirectSize - bufferOffset);
			if (outUsed != NULL)
				*outUsed = indirectSize - bufferOffset;

			free(buffer);
		} else if (outUsed != NULL)
			*outUsed = 0;

		if (fTrackReader->ReadFrames((uint8*)intoBuffer + bufferOffset,
			directFrameCount) != B_OK)
			return false;

		if (outUsed != NULL)
			*outUsed += directFrameCount * frameSize;

		return true;
	}

	return false;
}


/**
 * @brief Bind this sound to a BSoundPlayer (not yet implemented).
 *
 * @param player  The BSoundPlayer to bind to.
 * @param format  The audio format to use for playback.
 * @return B_ERROR always (unimplemented).
 */
status_t
BSound::BindTo(BSoundPlayer* player, const media_raw_audio_format& format)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/**
 * @brief Unbind this sound from a BSoundPlayer (not yet implemented).
 *
 * @param player  The BSoundPlayer to unbind from.
 * @return B_ERROR always (unimplemented).
 */
status_t
BSound::UnbindFrom(BSoundPlayer* player)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/**
 * @brief Generic hook for future extension (not yet implemented).
 *
 * @param code  Operation code.
 * @return B_ERROR always (unimplemented).
 */
status_t
BSound::Perform(int32 code, ...)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSound::_Reserved_Sound_0(void*) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSound::_Reserved_Sound_1(void*) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSound::_Reserved_Sound_2(void*) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSound::_Reserved_Sound_3(void*) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSound::_Reserved_Sound_4(void*) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BSound::_Reserved_Sound_5(void*) { return B_ERROR; }
