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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file Sound.h
 *  @brief Defines BSound, a reference-counted raw audio data container for BSoundPlayer.
 */

#ifndef _SOUND_H
#define _SOUND_H


#include <MediaDefs.h>

class BFile;
class BSoundPlayer;
struct entry_ref;

namespace BPrivate {
	class BTrackReader;
};


/** @brief A reference-counted container for raw audio data used with BSoundPlayer.
 *
 *  BSound can wrap either an in-memory audio buffer or an audio file.
 *  Manage its lifetime with AcquireRef() and ReleaseRef().
 */
class BSound {
public:
	/** @brief Constructs a BSound wrapping a caller-supplied memory buffer.
	 *  @param data Pointer to the raw audio data.
	 *  @param size Size of the data in bytes.
	 *  @param format The raw audio format of the data.
	 *  @param freeWhenDone If true, the BSound will free the buffer when destroyed.
	 */
								BSound(void* data, size_t size,
									const media_raw_audio_format& format,
									bool freeWhenDone = false);

	/** @brief Constructs a BSound backed by an audio file.
	 *  @param soundFile Entry reference to the audio file.
	 *  @param loadIntoMemory If true, load the entire file into memory immediately.
	 */
								BSound(const entry_ref* soundFile,
									bool loadIntoMemory = false);

	/** @brief Returns the initialization status.
	 *  @return B_OK if ready, or an error code.
	 */
			status_t			InitCheck();

	/** @brief Increments the reference count and returns this object.
	 *  @return This BSound pointer.
	 */
			BSound* 			AcquireRef();

	/** @brief Decrements the reference count; destroys the object when it reaches zero.
	 *  @return True if the object was destroyed.
	 */
			bool				ReleaseRef();

	/** @brief Returns the current reference count (unreliable in multi-threaded use).
	 *  @return Reference count.
	 */
			int32				RefCount() const; // unreliable!

	/** @brief Returns the duration of the audio data in microseconds.
	 *  @return Duration in microseconds.
	 */
	virtual	bigtime_t			Duration() const;

	/** @brief Returns the raw audio format of this sound.
	 *  @return Reference to the media_raw_audio_format.
	 */
	virtual	const media_raw_audio_format &Format() const;

	/** @brief Returns a pointer to the in-memory audio data, or NULL for file-backed sounds.
	 *  @return Pointer to audio data, or NULL.
	 */
	virtual	const void*			Data() const; // returns NULL for files

	/** @brief Returns the total size of the audio data in bytes.
	 *  @return Size in bytes.
	 */
	virtual	off_t				Size() const;

	/** @brief Copies audio data starting at the given byte offset into a buffer.
	 *  @param offset Byte offset into the audio data.
	 *  @param intoBuffer Destination buffer.
	 *  @param bufferSize Capacity of the destination buffer.
	 *  @param outUsed On return, the number of bytes actually copied.
	 *  @return True if data was copied, false if no more data is available.
	 */
	virtual	bool				GetDataAt(off_t offset,
									void* intoBuffer, size_t bufferSize,
									size_t* outUsed);

protected:
	/** @brief Constructs a BSound with only a format; for use by subclasses.
	 *  @param format The raw audio format.
	 */
								BSound(const media_raw_audio_format& format);

	/** @brief Extensibility hook for subclasses.
	 *  @param code Selector code.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Perform(int32 code, ...);

private:
			friend	class DummyFriend;
	virtual						~BSound();

public:
	/** @brief Called by BSoundPlayer when it begins using this sound.
	 *  @param player The BSoundPlayer that is binding to this sound.
	 *  @param format The raw audio format the player will use.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			BindTo(BSoundPlayer* player,
									const media_raw_audio_format& format);

	/** @brief Called by BSoundPlayer when it is done using this sound.
	 *  @param player The BSoundPlayer that is unbinding.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			UnbindFrom(BSoundPlayer* player);

private:
			status_t			_Reserved_Sound_0(void*);	// BindTo
			status_t			_Reserved_Sound_1(void*);	// UnbindFrom
	virtual	status_t			_Reserved_Sound_2(void*);
	virtual	status_t			_Reserved_Sound_3(void*);
	virtual	status_t			_Reserved_Sound_4(void*);
	virtual	status_t			_Reserved_Sound_5(void*);

private:
			void*				fData;
			size_t				fDataSize;
			BFile*				fFile;
			int32				fRefCount;
			status_t			fStatus;
			media_raw_audio_format fFormat;

			bool				fFreeWhenDone;
			bool				fReserved[3];

			BPrivate::BTrackReader* fTrackReader;
			uint32				fReserved2[18];
};

#endif // _SOUND_H
