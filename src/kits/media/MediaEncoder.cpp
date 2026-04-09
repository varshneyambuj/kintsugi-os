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
 *   Copyright 2015, Dario Casalinuovo
 *   Copyright 2010, Oleg Krysenkov, beos344@mail.ru.
 *   Copyright 2012, Fredrik Modéen, [firstname]@[lastname].se.
 *   Copyright 2004-2007, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file MediaEncoder.cpp
 *  @brief Implementation of BMediaEncoder and BMediaBufferEncoder for encoding media data.
 */


#include <MediaEncoder.h>

#include <EncoderPlugin.h>
#include <PluginManager.h>

#include <new>

#include "MediaDebug.h"

/*************************************************************
 * public BMediaEncoder
 *************************************************************/

/** @brief Default constructor; the encoder must be configured via SetTo() before use. */
BMediaEncoder::BMediaEncoder()
	:
	fEncoder(NULL),
	fInitStatus(B_NO_INIT)
{
	CALLED();
}


/** @brief Constructs a BMediaEncoder and configures it for the given output format.
 *  @param outputFormat Pointer to the desired output media_format.
 */
BMediaEncoder::BMediaEncoder(const media_format* outputFormat)
	:
	fEncoder(NULL),
	fInitStatus(B_NO_INIT)
{
	CALLED();
	SetTo(outputFormat);
}


/** @brief Constructs a BMediaEncoder using the codec identified by the given codec info.
 *  @param mci Pointer to the desired media_codec_info.
 */
BMediaEncoder::BMediaEncoder(const media_codec_info* mci)
	:
	fEncoder(NULL),
	fInitStatus(B_NO_INIT)
{
	CALLED();
	SetTo(mci);
}


/** @brief Destructor; releases the underlying encoder plugin instance. */
/* virtual */
BMediaEncoder::~BMediaEncoder()
{
	CALLED();
	ReleaseEncoder();
}


/** @brief Returns the initialisation status of the encoder.
 *  @return B_OK if ready, B_NO_INIT if not yet configured, or an error code.
 */
status_t
BMediaEncoder::InitCheck() const
{
	return fInitStatus;
}


/** @brief Selects an encoder codec for the given output format and attaches to it.
 *  @param outputFormat Pointer to the desired output media_format.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaEncoder::SetTo(const media_format* outputFormat)
{
	CALLED();

	status_t err = B_ERROR;
	ReleaseEncoder();

	if (outputFormat == NULL)
		return fInitStatus;

	media_format format = *outputFormat;
	err = gPluginManager.CreateEncoder(&fEncoder, format);
	if (fEncoder != NULL && err == B_OK) {
		err = _AttachToEncoder();
		if (err == B_OK)
			return err;
	}
	ReleaseEncoder();
	fInitStatus = err;
	return err;
}


/** @brief Selects an encoder codec identified by the given media_codec_info.
 *  @param mci Pointer to the desired media_codec_info.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaEncoder::SetTo(const media_codec_info* mci)
{
	CALLED();

	ReleaseEncoder();
	status_t err = gPluginManager.CreateEncoder(&fEncoder, mci, 0);
	if (fEncoder != NULL && err == B_OK) {
		err = _AttachToEncoder();
		if (err == B_OK) {
			fInitStatus = B_OK;
			return B_OK;
		}
	}

	ReleaseEncoder();
	fInitStatus = err;
	return err;
}


/** @brief Sets the input and output formats for the encoder.
 *  @param inputFormat  Pointer to the input media_format.
 *  @param outputFormat Pointer to the output media_format (may be NULL).
 *  @param mfi          Pointer to the media_file_format (currently unused).
 *  @return B_OK on success, B_NO_INIT if no encoder is active.
 */
status_t
BMediaEncoder::SetFormat(media_format* inputFormat,
	media_format* outputFormat, media_file_format* mfi)
{
	CALLED();
	TRACE("BMediaEncoder::SetFormat. Input = %d, Output = %d\n",
		inputFormat->type, outputFormat->type);

	if (!fEncoder)
		return B_NO_INIT;

	if (outputFormat != NULL)
		SetTo(outputFormat);

	//TODO: How we support mfi?
	return fEncoder->SetUp(inputFormat);
}


/** @brief Encodes @p frameCount frames from @p buffer using the active encoder.
 *  @param buffer     Pointer to the raw input data.
 *  @param frameCount Number of frames to encode.
 *  @param info       Pointer to media_encode_info controlling encoding.
 *  @return B_OK on success, B_NO_INIT if no encoder is active.
 */
status_t
BMediaEncoder::Encode(const void* buffer,
	int64 frameCount, media_encode_info* info)
{
	CALLED();

	if (!fEncoder)
		return B_NO_INIT;

	return fEncoder->Encode(buffer, frameCount, info);
}


/** @brief Retrieves the current encode parameters from the active encoder.
 *  @param parameters Pointer to an encode_parameters struct to fill in.
 *  @return B_OK on success, B_NO_INIT if no encoder is active.
 */
status_t
BMediaEncoder::GetEncodeParameters(encode_parameters* parameters) const
{
	CALLED();

	if (fEncoder == NULL)
		return B_NO_INIT;

	return fEncoder->GetEncodeParameters(parameters);
}


/** @brief Sets the encode parameters on the active encoder.
 *  @param parameters Pointer to the new encode_parameters.
 *  @return B_OK on success, B_NO_INIT if no encoder is active.
 */
status_t
BMediaEncoder::SetEncodeParameters(encode_parameters* parameters)
{
	CALLED();

	if (fEncoder == NULL)
		return B_NO_INIT;

	return fEncoder->SetEncodeParameters(parameters);
}


/*************************************************************
 * protected BMediaEncoder
 *************************************************************/

/** @brief Adds track-level info to the encoded output.
 *  @param code Code identifying the type of track info.
 *  @param data Pointer to the info data.
 *  @param size Size of the info data in bytes.
 *  @return B_OK on success, B_NO_INIT if no encoder is active.
 */
/* virtual */ status_t
BMediaEncoder::AddTrackInfo(uint32 code, const char* data, size_t size)
{
	CALLED();

	if (fEncoder == NULL)
		return B_NO_INIT;

	return fEncoder->AddTrackInfo(code, data, size);
}


/*************************************************************
 * private BMediaEncoder
 *************************************************************/

/*
//	unimplemented
BMediaEncoder::BMediaEncoder(const BMediaEncoder &);
BMediaEncoder::BMediaEncoder & operator=(const BMediaEncoder &);
*/

/** @brief Static write-chunk callback forwarded to WriteChunk() on the encoder instance.
 *  @param classptr  Pointer to the BMediaEncoder instance (cast from void*).
 *  @param chunk_data Pointer to the encoded chunk data.
 *  @param chunk_len  Size of the encoded chunk in bytes.
 *  @param info       Pointer to media_encode_info for the chunk.
 *  @return B_OK on success, B_BAD_VALUE if @p classptr is NULL.
 */
/* static */ status_t
BMediaEncoder::write_chunk(void* classptr, const void* chunk_data,
	size_t chunk_len, media_encode_info* info)
{
	CALLED();

	BMediaEncoder* encoder = static_cast<BMediaEncoder*>(classptr);
	if (encoder == NULL)
		return B_BAD_VALUE;
	return encoder->WriteChunk(chunk_data, chunk_len, info);
}


/** @brief Placeholder initialisation hook (currently unimplemented). */
void
BMediaEncoder::Init()
{
	UNIMPLEMENTED();
}


/** @brief Destroys the active encoder plugin instance and resets the init status. */
void
BMediaEncoder::ReleaseEncoder()
{
	CALLED();
	if (fEncoder != NULL) {
		gPluginManager.DestroyEncoder(fEncoder);
		fEncoder = NULL;
	}
	fInitStatus = B_NO_INIT;
}


/** @brief Creates and attaches a ChunkWriter that routes WriteChunk() calls
 *         through this BMediaEncoder instance to the underlying encoder plugin.
 *  @return B_OK on success, B_NO_MEMORY if the writer could not be allocated.
 */
status_t
BMediaEncoder::_AttachToEncoder()
{
	class MediaEncoderChunkWriter : public ChunkWriter {
		public:
			MediaEncoderChunkWriter(BMediaEncoder* encoder)
			{
				fEncoder = encoder;
			}
			virtual status_t WriteChunk(const void* chunkBuffer,
				size_t chunkSize, media_encode_info* encodeInfo)
			{
				return fEncoder->WriteChunk(chunkBuffer, chunkSize, encodeInfo);
			}
		private:
			BMediaEncoder* fEncoder;
	} *writer = new(std::nothrow) MediaEncoderChunkWriter(this);

	if (!writer)
		return B_NO_MEMORY;

	fEncoder->SetChunkWriter(writer);
	return B_OK;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_0(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_1(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_2(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_3(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_4(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_5(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_6(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_7(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_8(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_9(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_10(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_11(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_12(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_13(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_14(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEncoder::_Reserved_BMediaEncoder_15(int32 arg, ...) { return B_ERROR; }

/*************************************************************
 * public BMediaBufferEncoder
 *************************************************************/

/** @brief Default constructor; creates an uninitialised BMediaBufferEncoder. */
BMediaBufferEncoder::BMediaBufferEncoder()
	:
	BMediaEncoder(),
	fBuffer(NULL)
{
	CALLED();
}


/** @brief Constructs a BMediaBufferEncoder for the given output format.
 *  @param outputFormat Pointer to the desired output media_format.
 */
BMediaBufferEncoder::BMediaBufferEncoder(const media_format* outputFormat)
	:
	BMediaEncoder(outputFormat),
	fBuffer(NULL)
{
	CALLED();
}


/** @brief Constructs a BMediaBufferEncoder using the given codec.
 *  @param mci Pointer to the desired media_codec_info.
 */
BMediaBufferEncoder::BMediaBufferEncoder(const media_codec_info* mci)
	:
	BMediaEncoder(mci),
	fBuffer(NULL)
{
	CALLED();
}


/** @brief Encodes frames from @p inputBuffer directly into the caller-supplied @p outputBuffer.
 *  @param outputBuffer Pointer to the buffer that receives encoded output.
 *  @param outputSize   In: capacity of @p outputBuffer; out: bytes actually written.
 *  @param inputBuffer  Pointer to the raw input frames.
 *  @param frameCount   Number of frames to encode.
 *  @param info         Pointer to media_encode_info controlling encoding.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaBufferEncoder::EncodeToBuffer(void* outputBuffer,
	size_t* outputSize, const void* inputBuffer,
	int64 frameCount, media_encode_info* info)
{
	CALLED();

	status_t error;
	fBuffer = outputBuffer;
	fBufferSize = *outputSize;
	error = Encode(inputBuffer, frameCount, info);
	if (fBuffer) {
		fBuffer = NULL;
		*outputSize = 0;
	} else {
		*outputSize = fBufferSize;
	}
	return error;
}


/*************************************************************
 * public BMediaBufferEncoder
 *************************************************************/

/** @brief Writes the encoded chunk into the pre-set output buffer.
 *         If the chunk is larger than the buffer capacity, as much data as
 *         fits is copied and B_DEVICE_FULL is returned.
 *  @param chunkData Pointer to the encoded chunk data.
 *  @param chunkLen  Size of the encoded chunk in bytes.
 *  @param info      Pointer to media_encode_info (unused).
 *  @return B_OK on success, B_ENTRY_NOT_FOUND if no buffer is set,
 *          B_DEVICE_FULL if the chunk exceeds the buffer capacity.
 */
status_t
BMediaBufferEncoder::WriteChunk(const void* chunkData,
	size_t chunkLen, media_encode_info* info)
{
	CALLED();

	if (fBuffer == NULL)
		return B_ENTRY_NOT_FOUND;

	if (chunkLen > (size_t)fBufferSize) {
		memcpy(fBuffer, chunkData, fBufferSize);
		fBuffer = NULL;
		return B_DEVICE_FULL;
	}

	memcpy(fBuffer, chunkData, chunkLen);
	fBufferSize = chunkLen;
	fBuffer = NULL;
	return B_NO_ERROR;
}
