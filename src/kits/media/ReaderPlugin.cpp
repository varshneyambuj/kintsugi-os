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
 *   Copyright 2009-2010, Stephan Aßmus <superstippi@gmx.de>.
 *   Distributed under the terms of the MIT License.
 *
 *   Copyright 2004, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file ReaderPlugin.cpp
 *  @brief Implements the Reader base class and ReaderPlugin interface for media container demuxing plug-ins. */

#include "ReaderPlugin.h"

#include <stdio.h>


/** @brief Default constructor. Initialises the data source and media plugin pointers to NULL. */
Reader::Reader()
	:
	fSource(NULL),
	fMediaPlugin(NULL)
{
}


/** @brief Destructor. */
Reader::~Reader()
{
}


/** @brief Retrieves file-level metadata for the media container (optional override).
 *  @param _data  Output BMessage to be populated with metadata key/value pairs.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Reader::GetMetaData(BMessage* _data)
{
	return B_NOT_SUPPORTED;
}


/** @brief Seeks within a stream to the position closest to the requested frame or time.
 *  @param cookie  Opaque per-stream cookie returned by TrackInfo().
 *  @param flags   Seek flags controlling direction and rounding behaviour.
 *  @param frame   In/out: requested frame number; set to the actual frame seeked to.
 *  @param time    In/out: requested time in microseconds; set to the actual time seeked to.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Reader::Seek(void* cookie, uint32 flags, int64* frame, bigtime_t* time)
{
	return B_NOT_SUPPORTED;
}


/** @brief Finds the nearest key frame to the requested position within a stream.
 *  @param cookie  Opaque per-stream cookie returned by TrackInfo().
 *  @param flags   Flags controlling search direction and rounding.
 *  @param frame   In/out: requested frame number; set to the key-frame position found.
 *  @param time    In/out: requested time in microseconds; set to the key-frame time found.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Reader::FindKeyFrame(void* cookie, uint32 flags, int64* frame, bigtime_t* time)
{
	return B_NOT_SUPPORTED;
}


/** @brief Retrieves per-stream metadata for the stream identified by @p cookie.
 *  @param cookie  Opaque per-stream cookie returned by TrackInfo().
 *  @param _data   Output BMessage to be populated with stream-level metadata.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Reader::GetStreamMetaData(void* cookie, BMessage* _data)
{
	return B_NOT_SUPPORTED;
}


/** @brief Returns the data source this reader is operating on.
 *  @return Pointer to the BDataIO source; may be NULL if Setup() has not been called. */
BDataIO*
Reader::Source() const
{
	return fSource;
}


/** @brief Associates a data source with this reader.
 *  @param source  Pointer to the BDataIO object to read from; ownership is NOT transferred. */
void
Reader::Setup(BDataIO *source)
{
	fSource = source;
}


/** @brief Hook for performing implementation-specific actions identified by a perform code.
 *  @param code   An opaque code identifying the action to perform.
 *  @param _data  Pointer to action-specific data; may be NULL.
 *  @return B_OK always (base implementation is a no-op). */
status_t
Reader::Perform(perform_code code, void* _data)
{
	return B_OK;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Reader::_ReservedReader1() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Reader::_ReservedReader2() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Reader::_ReservedReader3() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Reader::_ReservedReader4() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Reader::_ReservedReader5() {}
