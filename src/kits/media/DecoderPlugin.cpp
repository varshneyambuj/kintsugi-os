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
 *   Copyright 2004, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file DecoderPlugin.cpp
 *  @brief Implements the Decoder base class and DecoderPlugin interface for media decoding plug-ins. */

#include "DecoderPlugin.h"

#include <stdio.h>
#include <string.h>

#include <MediaFormats.h>


/** @brief Default constructor. Initialises the chunk provider and media plugin pointers to NULL. */
Decoder::Decoder()
	:
	fChunkProvider(NULL),
	fMediaPlugin(NULL)
{
}


/** @brief Destructor. Releases the owned ChunkProvider instance. */
Decoder::~Decoder()
{
	delete fChunkProvider;
}


/** @brief Retrieves the next encoded chunk from the underlying chunk provider.
 *  @param chunkBuffer  Output pointer set to the buffer containing the chunk data.
 *  @param chunkSize    Output pointer set to the size of the returned chunk in bytes.
 *  @param mediaHeader  Output pointer filled with the media header for this chunk.
 *  @return B_OK on success, or an error code if the chunk could not be retrieved. */
status_t
Decoder::GetNextChunk(const void **chunkBuffer, size_t *chunkSize,
					  media_header *mediaHeader)
{
	return fChunkProvider->GetNextChunk(chunkBuffer, chunkSize, mediaHeader);
}


/** @brief Sets the chunk provider used to supply raw encoded data to this decoder.
 *  @param provider  Pointer to the new ChunkProvider; ownership is transferred and
 *                   the previous provider (if any) is deleted. */
void
Decoder::SetChunkProvider(ChunkProvider *provider)
{
	delete fChunkProvider;
	fChunkProvider = provider;
}


/** @brief Hook for performing implementation-specific actions identified by a perform code.
 *  @param code   An opaque code identifying the action to perform.
 *  @param _data  Pointer to action-specific data; may be NULL.
 *  @return B_OK always (base implementation is a no-op). */
status_t
Decoder::Perform(perform_code code, void* _data)
{
	return B_OK;
}

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Decoder::_ReservedDecoder1() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Decoder::_ReservedDecoder2() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Decoder::_ReservedDecoder3() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Decoder::_ReservedDecoder4() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Decoder::_ReservedDecoder5() {}

//	#pragma mark -


/** @brief Default constructor for the DecoderPlugin factory. */
DecoderPlugin::DecoderPlugin()
{
}
