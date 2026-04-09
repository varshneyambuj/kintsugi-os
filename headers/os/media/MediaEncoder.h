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

/** @file MediaEncoder.h
 *  @brief Defines BMediaEncoder and BMediaBufferEncoder for codec-based media encoding.
 */

#ifndef _MEDIA_ENCODER_H
#define _MEDIA_ENCODER_H


#include <MediaFormats.h>

namespace BPrivate {
	namespace media {
		class Encoder;
		class EncoderPlugin;
	}
}

using namespace BPrivate::media;


/** @brief Abstract base class that encodes raw media data into a compressed format.
 *
 *  Subclass BMediaEncoder and implement WriteChunk() to consume encoded output;
 *  then call Encode() to feed raw frames into the codec.
 */
class BMediaEncoder {
public:
	/** @brief Default constructor; call SetTo() before encoding. */
								BMediaEncoder();

	/** @brief Constructs and initializes the encoder for the given output format.
	 *  @param outputFormat The desired compressed output format.
	 */
								BMediaEncoder(
									const media_format* outputFormat);

	/** @brief Constructs the encoder for a specific codec.
	 *  @param info Codec information identifying the encoder to use.
	 */
								BMediaEncoder(const media_codec_info* info);

	virtual						~BMediaEncoder();

	/** @brief Returns the initialization status.
	 *  @return B_OK if the encoder is ready, or an error code.
	 */
			status_t			InitCheck() const;

	/** @brief Initializes or re-initializes the encoder for an output format.
	 *  @param outputFormat The desired compressed output format.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetTo(const media_format* outputFormat);

	/** @brief Initializes the encoder for a specific codec.
	 *  @param info Codec information identifying the encoder to use.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetTo(const media_codec_info* info);

	/** @brief Sets both the raw input and compressed output formats.
	 *  @param inputFormat The raw format of data supplied to Encode().
	 *  @param outputFormat The compressed output format.
	 *  @param fileFormat Optional file format context; may be NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetFormat(media_format* inputFormat,
									media_format* outputFormat,
									media_file_format* fileFormat = NULL);

	/** @brief Encodes frameCount raw frames from buffer.
	 *  @param buffer Pointer to the raw input data.
	 *  @param frameCount Number of frames (or samples for audio) to encode.
	 *  @param info Encoding control parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Encode(const void* buffer, int64 frameCount,
									media_encode_info* info);

	/** @brief Retrieves the current encoding quality parameters.
	 *  @param parameters On return, filled with the current encode_parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetEncodeParameters(
									encode_parameters* parameters) const;

	/** @brief Applies new encoding quality parameters.
	 *  @param parameters The new encode_parameters to use.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetEncodeParameters(
									encode_parameters* parameters);

protected:
	/** @brief Called by the framework with each encoded output chunk.
	 *  @param buffer Pointer to the encoded data.
	 *  @param size Size of the encoded data in bytes.
	 *  @param info Encoding metadata for this chunk.
	 *  @return B_OK to continue encoding, or an error code to stop.
	 */
	virtual	status_t			WriteChunk(const void* buffer, size_t size,
									media_encode_info* info) = 0;

	/** @brief Optionally adds codec-specific track metadata.
	 *  @param code A format-defined metadata code.
	 *  @param data The metadata value.
	 *  @param size Size of data in bytes.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			AddTrackInfo(uint32 code, const char* data,
									size_t size);

	// TODO: Needs Perform() method for FBC!

private:
	// FBC padding and forbidden methods
	virtual	status_t			_Reserved_BMediaEncoder_0(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_1(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_2(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_3(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_4(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_5(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_6(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_7(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_8(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_9(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_10(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_11(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_12(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_13(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_14(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaEncoder_15(int32 arg, ...);

								BMediaEncoder(const BMediaEncoder& other);
			BMediaEncoder&		operator=(const BMediaEncoder& other);

private:
			status_t			_AttachToEncoder();

	static	status_t			write_chunk(void* classPtr,
									const void* buffer, size_t size,
									media_encode_info* info);

			void				Init();
			void				ReleaseEncoder();

			uint32				_reserved_was_fEncoderMgr;
			Encoder*			fEncoder;

			int32				fEncoderID;
			bool				fFormatValid;
			bool				fEncoderStarted;
			status_t			fInitStatus;

			uint32				_reserved_BMediaEncoder_[32];
};


/** @brief Convenience subclass of BMediaEncoder that captures encoded output in memory.
 *
 *  Call EncodeToBuffer() to encode raw frames and receive the compressed result
 *  in a caller-supplied memory buffer without implementing WriteChunk().
 */
class BMediaBufferEncoder : public BMediaEncoder {
public:
	/** @brief Default constructor. */
								BMediaBufferEncoder();

	/** @brief Constructs from the given compressed output format.
	 *  @param outputFormat The desired compressed output format.
	 */
								BMediaBufferEncoder(
									const media_format* outputFormat);

	/** @brief Constructs from a specific codec info structure.
	 *  @param info Codec information identifying the encoder.
	 */
								BMediaBufferEncoder(
									const media_codec_info* info);

	/** @brief Encodes raw frames and writes the result to a memory buffer.
	 *  @param outputBuffer Destination buffer for the encoded output.
	 *  @param _size In: buffer capacity; Out: bytes actually written.
	 *  @param inputBuffer Pointer to the raw input data.
	 *  @param frameCount Number of frames to encode.
	 *  @param info Encoding control parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			EncodeToBuffer(void* outputBuffer,
									size_t* _size, const void* inputBuffer,
									int64 frameCount, media_encode_info* info);

protected:
	virtual	status_t			WriteChunk(const void* buffer, size_t size,
									media_encode_info* info);

protected:
			void*				fBuffer;
			int32				fBufferSize;
};

#endif // _MEDIA_ENCODER_H

