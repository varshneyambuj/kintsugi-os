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
 * Copyright 2009, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file Buffer.h
 *  @brief Defines BBuffer and BSmallBuffer, the fundamental media data-transfer containers.
 */

#ifndef _BUFFER_H
#define _BUFFER_H


#include <MediaDefs.h>


namespace BPrivate {
	class BufferCache;
	class SharedBufferList;
}


/** @brief Describes the information needed to clone an existing media buffer. */
struct buffer_clone_info {
								buffer_clone_info();
								~buffer_clone_info();

			media_buffer_id		buffer;  /**< ID of the source buffer to clone. */
			area_id				area;    /**< Memory area containing the buffer. */
			size_t				offset;  /**< Byte offset within the area. */
			size_t				size;    /**< Size of the buffer in bytes. */
			int32				flags;   /**< Buffer flags. */

private:
			uint32				_reserved_[4];
};


/** @brief A reference-counted block of shared memory used to pass media data between nodes.
 *
 *  BBuffer objects are created and owned by BBufferGroup. Producers fill them
 *  with data and send them to consumers; consumers call Recycle() when done.
 */
class BBuffer {
public:
	/** @brief Buffer type flags. */
	enum {
		B_F1_BUFFER		= 0x1,         /**< Interlaced field-1 buffer. */
		B_F2_BUFFER		= 0x2,         /**< Interlaced field-2 buffer. */
		B_SMALL_BUFFER	= 0x80000000   /**< Buffer belongs to BSmallBuffer. */
	};

	/** @brief Returns a pointer to the raw data area of this buffer.
	 *  @return Pointer to the buffer's data.
	 */
			void*				Data();

	/** @brief Returns the total number of bytes available in this buffer.
	 *  @return Available size in bytes.
	 */
			size_t				SizeAvailable();

	/** @brief Returns the number of bytes actually used by the current payload.
	 *  @return Used size in bytes.
	 */
			size_t				SizeUsed();

	/** @brief Sets the number of bytes in use within this buffer.
	 *  @param used Number of bytes of valid data.
	 */
			void				SetSizeUsed(size_t used);

	/** @brief Returns the flags associated with this buffer.
	 *  @return Buffer flags bitmask.
	 */
			uint32				Flags();

	/** @brief Returns this buffer to its owning group for reuse. */
			void				Recycle();

	/** @brief Returns the clone-info structure describing this buffer's memory.
	 *  @return A buffer_clone_info copy.
	 */
			buffer_clone_info	CloneInfo() const;

	/** @brief Returns the system-wide unique identifier for this buffer.
	 *  @return The media_buffer_id.
	 */
			media_buffer_id		ID();

	/** @brief Returns the media type of the data stored in this buffer.
	 *  @return The media_type value from the buffer header.
	 */
			media_type			Type();

	/** @brief Returns a pointer to the generic media header.
	 *  @return Pointer to the buffer's media_header.
	 */
			media_header*		Header();

	/** @brief Returns a pointer to the audio-specific portion of the header.
	 *  @return Pointer to the media_audio_header sub-structure.
	 */
			media_audio_header*	AudioHeader();

	/** @brief Returns a pointer to the video-specific portion of the header.
	 *  @return Pointer to the media_video_header sub-structure.
	 */
			media_video_header*	VideoHeader();

	/** @brief Returns the total allocated size of this buffer in bytes.
	 *  @return Allocated buffer size.
	 */
			size_t				Size();

private:
	friend class BPrivate::BufferCache;
	friend class BPrivate::SharedBufferList;
	friend class BMediaRoster;
	friend class BBufferProducer;
	friend class BBufferConsumer;
	friend class BBufferGroup;
	friend class BSmallBuffer;

	explicit					BBuffer(const buffer_clone_info& info);
								~BBuffer();

								BBuffer();
								BBuffer(const BBuffer& other);
			BBuffer&			operator=(const BBuffer& other);
									// not implemented

			void				SetHeader(const media_header* header);

			media_header		fMediaHeader;
			BPrivate::SharedBufferList* fBufferList;
			area_id				fArea;
			void*				fData;
			size_t				fOffset;
			size_t				fSize;
			int32				fFlags;

			uint32				_reserved[12];
};


/** @brief A lightweight buffer subclass optimized for small payloads.
 *
 *  BSmallBuffer avoids per-buffer area allocation overhead for small data
 *  transfers. Use SmallBufferSizeLimit() to determine the maximum payload.
 */
class BSmallBuffer : public BBuffer {
public:
	/** @brief Default constructor. */
								BSmallBuffer();

	/** @brief Returns the maximum payload size supported by a small buffer.
	 *  @return Maximum payload size in bytes.
	 */
	static	size_t				SmallBufferSizeLimit();
};


#endif	// _BUFFER_H
