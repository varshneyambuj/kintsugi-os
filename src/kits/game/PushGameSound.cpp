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
 *   Copyright 2001-2012 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 *       Jérôme Duval
 */

/**
 * @file PushGameSound.cpp
 * @brief BPushGameSound: push-model streaming audio for the GameKit.
 *
 * BPushGameSound divides a single circular buffer (fBuffer) into
 * fPageCount pages of fPageSize bytes each. The application locks individual
 * pages via LockNextPage() / UnlockPage(), fills them with audio data, and
 * unlocks them. The Media Kit's buffer-production thread advances fPlayPos
 * through the circular buffer via FillBuffer(), copying only pages that are
 * not currently locked. Alternatively, LockForCyclic() / UnlockCyclic()
 * expose the entire buffer for direct write access.
 *
 * The stream hook (SetStreamHook) and SetParameters() overrides are
 * intentionally unsupported and return B_UNSUPPORTED.
 */


#include <PushGameSound.h>

#include <List.h>
#include <string.h>

#include "GSUtility.h"


/**
 * @brief Constructs a fully initialised BPushGameSound with a paged circular buffer.
 *
 * Allocates the circular buffer as fPageCount * fPageSize bytes, creates the
 * BList used to track locked pages, and delegates the streaming connection
 * setup to the BStreamingGameSound constructor.
 *
 * @param inBufferFrameCount Number of frames per page (determines page size).
 * @param format             Audio format used by the circular buffer.
 * @param inBufferCount      Number of pages in the circular buffer.
 * @param device             The BGameSoundDevice to use, or NULL for the default.
 */
BPushGameSound::BPushGameSound(size_t inBufferFrameCount,
	const gs_audio_format *format, size_t inBufferCount,
	BGameSoundDevice *device)
	:
	BStreamingGameSound(inBufferFrameCount, format, inBufferCount, device),
	fLockPos(0),
	fPlayPos(0)
{
	fPageLocked = new BList;

	size_t frameSize = get_sample_size(format->format) * format->channel_count;

	fPageCount = inBufferCount;
	fPageSize = frameSize * inBufferFrameCount;
	fBufferSize = fPageSize * fPageCount;

	fBuffer = new char[fBufferSize];
}


/**
 * @brief Constructs an uninitialised BPushGameSound for subclass use.
 *
 * The circular buffer and page parameters are left at zero / NULL. Subclasses
 * must allocate fBuffer and set the page geometry before the object is used.
 *
 * @param device The BGameSoundDevice to use, or NULL for the process default.
 */
BPushGameSound::BPushGameSound(BGameSoundDevice * device)
		:	BStreamingGameSound(device),
			fLockPos(0),
			fPlayPos(0),
			fBuffer(NULL),
			fPageSize(0),
			fPageCount(0),
			fBufferSize(0)
{
	fPageLocked = new BList;
}


/**
 * @brief Destroys the BPushGameSound, freeing the circular buffer and page list.
 */
BPushGameSound::~BPushGameSound()
{
	delete [] fBuffer;
	delete fPageLocked;
}


/**
 * @brief Locks the next available page in the circular buffer for writing.
 *
 * Advances the lock position (fLockPos) by one page and adds the page
 * pointer to the locked-page list. The lock fails if all but one page
 * are already locked, or if the next page to lock overlaps the current
 * play position.
 *
 * @param out_pagePtr  Output parameter set to the start address of the locked page.
 * @param out_pageSize Output parameter set to fPageSize (bytes per page).
 * @return lock_ok on success; lock_failed if the page cannot be locked.
 */
BPushGameSound::lock_status
BPushGameSound::LockNextPage(void **out_pagePtr, size_t *out_pageSize)
{
	// the user can not lock every page
	if (fPageLocked->CountItems() > fPageCount - 1)
		return lock_failed;

	// the user can't lock a page being played
	if (fLockPos < fPlayPos
		&& fLockPos + fPageSize > fPlayPos)
		return lock_failed;

	// lock the page
	char * lockPage = &fBuffer[fLockPos];
	fPageLocked->AddItem(lockPage);

	// move the locker to the next page
	fLockPos += fPageSize;
	if (fLockPos >= fBufferSize)
		fLockPos = 0;

	*out_pagePtr = lockPage;
	*out_pageSize = fPageSize;

	return lock_ok;
}


/**
 * @brief Unlocks a previously locked page so that FillBuffer() can read it.
 *
 * Removes \a in_pagePtr from the locked-page list. The page pointer must
 * have been returned by a prior call to LockNextPage().
 *
 * @param in_pagePtr Pointer to the start of the page to unlock.
 * @return B_OK if the page was found and removed; B_ERROR if not found.
 */
status_t
BPushGameSound::UnlockPage(void *in_pagePtr)
{
	return (fPageLocked->RemoveItem(in_pagePtr)) ? B_OK : B_ERROR;
}


/**
 * @brief Locks the entire circular buffer for direct cyclic write access.
 *
 * Returns a pointer to the base of fBuffer and its total size. The caller
 * may write anywhere in the buffer and must call UnlockCyclic() when done.
 * This mode does not interact with the per-page lock list.
 *
 * @param out_basePtr Output parameter set to fBuffer.
 * @param out_size    Output parameter set to fBufferSize.
 * @return Always lock_ok.
 */
BPushGameSound::lock_status
BPushGameSound::LockForCyclic(void **out_basePtr, size_t *out_size)
{
	*out_basePtr = fBuffer;
	*out_size = fBufferSize;
	return lock_ok;
}


/**
 * @brief Releases the cyclic whole-buffer lock.
 *
 * This is a no-op because LockForCyclic() does not modify any internal
 * lock state; it exists for API symmetry.
 *
 * @return Always B_OK.
 */
status_t
BPushGameSound::UnlockCyclic()
{
	return B_OK;
}


/**
 * @brief Returns the current playback position within the circular buffer.
 *
 * fPlayPos advances each time FillBuffer() copies data to the mixer.
 * The value wraps at fBufferSize.
 *
 * @return Byte offset of the current play position within fBuffer.
 */
size_t
BPushGameSound::CurrentPosition()
{
	return fPlayPos;
}


/**
 * @brief Creates an independent copy of this BPushGameSound.
 *
 * The clone has the same page geometry (frame count per page, page count)
 * and audio format, but starts with an empty circular buffer.
 *
 * @return Pointer to a new BPushGameSound, or NULL if the format cannot be read.
 */
BGameSound *
BPushGameSound::Clone() const
{
	gs_audio_format format = Format();
	size_t frameSize = get_sample_size(format.format) * format.channel_count;
	size_t bufferFrameCount = fPageSize / frameSize;

	return new BPushGameSound(bufferFrameCount, &format, fPageCount, Device());
}


/**
 * @brief Forwards to BStreamingGameSound::Perform().
 * @param selector Selector identifying the desired operation.
 * @param data     Pointer to operation-specific data.
 * @return Result of BStreamingGameSound::Perform().
 */
status_t
BPushGameSound::Perform(int32 selector, void *data)
{
	return BStreamingGameSound::Perform(selector, data);
}


/**
 * @brief Not supported by BPushGameSound.
 *
 * Buffer geometry is fixed at construction time; runtime changes via
 * SetParameters() are not allowed.
 *
 * @return Always B_UNSUPPORTED.
 */
status_t
BPushGameSound::SetParameters(size_t inBufferFrameCount,
	const gs_audio_format *format, size_t inBufferCount)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Not supported by BPushGameSound.
 *
 * BPushGameSound uses per-page locking rather than a stream hook callback.
 *
 * @return Always B_UNSUPPORTED.
 */
status_t
BPushGameSound::SetStreamHook(void (*hook)(void * inCookie, void * inBuffer,
	size_t inByteCount, BStreamingGameSound * me), void * cookie)
{
	return B_UNSUPPORTED;
}


/**
 * @brief Copies ready (unlocked) pages from the circular buffer to the mixer.
 *
 * Called by the Media Kit's buffer-production thread. The method:
 *   1. Asks BytesReady() how many contiguous bytes starting at fPlayPos are
 *      available (i.e. not locked by the application).
 *   2. Copies those bytes into \a inBuffer, wrapping around the end of
 *      fBuffer if necessary.
 *   3. Advances fPlayPos accordingly.
 *   4. Calls BStreamingGameSound::FillBuffer() so that any installed stream
 *      hook also runs.
 *
 * @param inBuffer    Destination buffer provided by the Media Kit.
 * @param inByteCount Number of bytes the mixer needs.
 */
void
BPushGameSound::FillBuffer(void *inBuffer, size_t inByteCount)
{
	size_t bytes = inByteCount;

	if (!BytesReady(&bytes))
		return;

	if (fPlayPos + bytes > fBufferSize) {
		size_t remainder = fBufferSize - fPlayPos;
			// Space left in buffer
		char * buffer = (char*)inBuffer;

		// fill the buffer with the samples left at the end of our buffer
		memcpy(buffer, &fBuffer[fPlayPos], remainder);
		fPlayPos = 0;

		// fill the remainder of the buffer by looping to the start
		// of the buffer if it isn't locked
		bytes -= remainder;
		if (BytesReady(&bytes)) {
			memcpy(&buffer[remainder], fBuffer, bytes);
			fPlayPos += bytes;
		}
	} else {
		memcpy(inBuffer, &fBuffer[fPlayPos], bytes);
		fPlayPos += bytes;
	}

	BStreamingGameSound::FillBuffer(inBuffer, inByteCount);
}


/**
 * @brief Determines how many contiguous bytes from fPlayPos are ready to copy.
 *
 * Scans the locked-page list to find the first locked page that the play
 * position would cross before consuming \a *bytes bytes. If the current
 * page is locked, returns false immediately. Otherwise, \a *bytes is
 * reduced to the number of bytes available before the first locked page, and
 * true is returned.
 *
 * @param bytes In/out parameter: on entry the number of bytes requested;
 *              on exit, the number of contiguous unlocked bytes available
 *              (may be less than the input value).
 * @return \c false if the current play page is locked (no data available);
 *         \c true otherwise (even if \a *bytes was reduced).
 */
bool
BPushGameSound::BytesReady(size_t * bytes)
{
	if (fPageLocked->CountItems() <= 0)
		return true;

	size_t start = fPlayPos;
	size_t ready = fPlayPos;
	int32 page = int32(start / fPageSize);

	// return if there is nothing to do
	if (fPageLocked->HasItem(&fBuffer[page * fPageSize]))
		return false;

	while (ready < *bytes) {
		ready += fPageSize;
		page = int32(ready / fPageSize);

		if (fPageLocked->HasItem(&fBuffer[page * fPageSize])) {
			// we have found a locked page
			*bytes = ready - start - (ready - page * fPageSize);
			return true;
		}
	}

	// all of the bytes are ready
	return true;
}


/* unimplemented for protection of the user:
 *
 * BPushGameSound::BPushGameSound()
 * BPushGameSound::BPushGameSound(const BPushGameSound &)
 * BPushGameSound &BPushGameSound::operator=(const BPushGameSound &)
 */


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_0(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_1(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_2(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_3(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_4(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_5(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_6(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_7(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_8(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_9(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_10(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_11(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_12(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_13(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_14(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_15(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_16(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_17(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_18(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_19(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_20(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_21(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_22(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BPushGameSound::_Reserved_BPushGameSound_23(int32 arg, ...)
{
	return B_ERROR;
}
