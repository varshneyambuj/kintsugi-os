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
 *   Copyright 2009, Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 *   Copyright 2004, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file EncoderPlugin.cpp
 *  @brief Implements the Encoder base class and EncoderPlugin interface for media encoding plug-ins. */

#include "EncoderPlugin.h"

#include <stdio.h>
#include <string.h>

#include <MediaFormats.h>


/** @brief Default constructor. Initialises the chunk writer and media plugin pointers to NULL. */
Encoder::Encoder()
	:
	fChunkWriter(NULL),
	fMediaPlugin(NULL)
{
}


/** @brief Destructor. Releases the owned ChunkWriter instance. */
Encoder::~Encoder()
{
	delete fChunkWriter;
}


// #pragma mark - Convenience stubs


/** @brief Adds track-level metadata to the encoded output (optional override).
 *  @param code   A code identifying the type of track information.
 *  @param data   Pointer to the track information data.
 *  @param size   Size of the data in bytes.
 *  @param flags  Flags modifying the behaviour of the call.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Encoder::AddTrackInfo(uint32 code, const void* data, size_t size, uint32 flags)
{
	return B_NOT_SUPPORTED;
}


/** @brief Returns a BView that exposes encoder parameters (optional override).
 *  @return NULL (base implementation; subclasses may override). */
BView*
Encoder::ParameterView()
{
	return NULL;
}


/** @brief Returns a BParameterWeb describing the encoder's controllable parameters (optional override).
 *  @return NULL (base implementation; subclasses may override). */
BParameterWeb*
Encoder::ParameterWeb()
{
	return NULL;
}


/** @brief Retrieves the current value of the encoder parameter identified by @p id.
 *  @param id    Identifier of the parameter to query.
 *  @param value Output buffer to receive the parameter value.
 *  @param size  In/out: capacity of @p value on entry; actual size written on exit.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Encoder::GetParameterValue(int32 id, void* value, size_t* size) const
{
	return B_NOT_SUPPORTED;
}


/** @brief Sets the encoder parameter identified by @p id to @p value.
 *  @param id    Identifier of the parameter to modify.
 *  @param value Pointer to the new parameter value.
 *  @param size  Size of the value data in bytes.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Encoder::SetParameterValue(int32 id, const void* value, size_t size)
{
	return B_NOT_SUPPORTED;
}


/** @brief Retrieves the current encoding quality/rate-control parameters.
 *  @param parameters  Output structure filled with the current encode parameters.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Encoder::GetEncodeParameters(encode_parameters* parameters) const
{
	return B_NOT_SUPPORTED;
}


/** @brief Applies new encoding quality/rate-control parameters.
 *  @param parameters  Pointer to the new encode parameters to apply.
 *  @return B_NOT_SUPPORTED (base implementation; subclasses may override). */
status_t
Encoder::SetEncodeParameters(encode_parameters* parameters)
{
	return B_NOT_SUPPORTED;
}


// #pragma mark -


/** @brief Writes a single encoded chunk to the underlying chunk writer.
 *  @param chunkBuffer  Pointer to the buffer containing the encoded chunk data.
 *  @param chunkSize    Size of the chunk in bytes.
 *  @param encodeInfo   Pointer to encoding metadata associated with this chunk.
 *  @return B_OK on success, or an error code propagated from the ChunkWriter. */
status_t
Encoder::WriteChunk(const void* chunkBuffer, size_t chunkSize,
	media_encode_info* encodeInfo)
{
	return fChunkWriter->WriteChunk(chunkBuffer, chunkSize, encodeInfo);
}


/** @brief Sets the chunk writer used to receive encoded output from this encoder.
 *  @param writer  Pointer to the new ChunkWriter; ownership is transferred and
 *                 the previous writer (if any) is deleted. */
void
Encoder::SetChunkWriter(ChunkWriter* writer)
{
	delete fChunkWriter;
	fChunkWriter = writer;
}


// #pragma mark - FBC padding


/** @brief Hook for performing implementation-specific actions identified by a perform code.
 *  @param code  An opaque code identifying the action to perform.
 *  @param data  Pointer to action-specific data; may be NULL.
 *  @return B_OK always (base implementation is a no-op). */
status_t
Encoder::Perform(perform_code code, void* data)
{
	return B_OK;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder1() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder2() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder3() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder4() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder5() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder6() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder7() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder8() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder9() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder10() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder11() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder12() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder13() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder14() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder15() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder16() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder17() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder18() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder19() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Encoder::_ReservedEncoder20() {}


//	#pragma mark - EncoderPlugin


/** @brief Default constructor for the EncoderPlugin factory. */
EncoderPlugin::EncoderPlugin()
{
}
