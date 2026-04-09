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
 * Incorporates work from Haiku, Inc. Distributed under the terms of the MIT License.
 */

/** @file MediaDecoder.h
 *  @brief Defines BMediaDecoder and BMediaBufferDecoder for codec-based media decoding.
 */

#ifndef MEDIADECODER_H
#define MEDIADECODER_H

#include <MediaDefs.h>
#include <MediaFormats.h>

namespace BPrivate {
	class Decoder;
}
namespace BPrivate {
	namespace media {
		class Decoder;
		class DecoderPlugin;
	}
}

/** @brief Abstract base class that decodes compressed media data into raw frames.
 *
 *  Subclass BMediaDecoder and implement GetNextChunk() to supply compressed input
 *  data; then call Decode() to retrieve decoded raw frames.
 */
class BMediaDecoder {
	public:
		/** @brief Default constructor; call SetTo() before using the decoder. */
		BMediaDecoder();

		/** @brief Constructs and initializes the decoder for the given input format.
		 *  @param in_format The compressed input format.
		 *  @param info Optional codec-specific initialization data.
		 *  @param info_size Size of the info buffer in bytes.
		 */
		BMediaDecoder(const media_format *in_format,
		              const void *info = NULL, size_t info_size = 0);

		/** @brief Constructs the decoder for a specific codec.
		 *  @param mci Codec information identifying the decoder to use.
		 */
		BMediaDecoder(const media_codec_info *mci);

		virtual ~BMediaDecoder();

		/** @brief Returns the initialization status.
		 *  @return B_OK if the decoder is ready, or an error code.
		 */
		status_t InitCheck() const;

		/** @brief Initializes or re-initializes the decoder for the given format.
		 *  @param in_format The compressed input format.
		 *  @param info Optional codec-specific initialization data.
		 *  @param info_size Size of the info buffer in bytes.
		 *  @return B_OK on success, or an error code.
		 */
		status_t SetTo(const media_format *in_format,
		               const void *info = NULL, size_t info_size = 0);

		/** @brief Initializes the decoder for a specific codec.
		 *  @param mci Codec information identifying the decoder to use.
		 *  @return B_OK on success, or an error code.
		 */
		status_t SetTo(const media_codec_info *mci);

		/** @brief Sets the compressed input format for the decoder.
		 *  @param in_format The compressed input format.
		 *  @param in_info Optional codec-specific initialization data.
		 *  @param in_size Size of in_info in bytes.
		 *  @return B_OK on success, or an error code.
		 */
		status_t SetInputFormat(const media_format *in_format,
		                        const void *in_info = NULL, size_t in_size = 0);

		/** @brief Negotiates and sets the raw output format.
		 *  @param output_format In/out: desired output format; set to the format actually used.
		 *  @return B_OK on success, or an error code.
		 */
		status_t SetOutputFormat(media_format *output_format);

		/** @brief Decodes the next chunk of data into the output buffer.
		 *  @param out_buffer Destination buffer for decoded raw data.
		 *  @param out_frameCount On return, the number of frames written.
		 *  @param out_mh On return, the media header for the decoded chunk.
		 *  @param info Optional decode control parameters.
		 *  @return B_OK on success, or an error code.
		 */
		status_t Decode(void *out_buffer, int64 *out_frameCount,
		                media_header *out_mh, media_decode_info *info);

		/** @brief Returns information about the active decoder codec.
		 *  @param out_info On return, filled with codec information.
		 *  @return B_OK on success, or an error code.
		 */
		status_t GetDecoderInfo(media_codec_info *out_info) const;

	protected:
		/** @brief Called by the framework to supply the next chunk of compressed data.
		 *  @param chunkData On return, pointer to the compressed data.
		 *  @param chunkLen On return, length of the compressed data in bytes.
		 *  @param mh On return, the media header for this chunk.
		 *  @return B_OK to continue, or an error code to stop decoding.
		 */
		virtual status_t GetNextChunk(const void **chunkData, size_t *chunkLen,
		                              media_header *mh) = 0;

	private:

		//	unimplemented
		BMediaDecoder(const BMediaDecoder &);
		BMediaDecoder & operator=(const BMediaDecoder &);

		status_t AttachToDecoder();

		BPrivate::media::Decoder	*fDecoder;
		status_t			fInitStatus;

		/* fbc data and virtuals */

		uint32 _reserved_BMediaDecoder_[33];

		virtual	status_t _Reserved_BMediaDecoder_0(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_1(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_2(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_3(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_4(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_5(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_6(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_7(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_8(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_9(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_10(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_11(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_12(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_13(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_14(int32 arg, ...);
		virtual	status_t _Reserved_BMediaDecoder_15(int32 arg, ...);
};

/** @brief Convenience subclass of BMediaDecoder that decodes from an in-memory buffer.
 *
 *  Instead of overriding GetNextChunk(), call DecodeBuffer() with the compressed
 *  data and it will handle the chunk delivery internally.
 */
class BMediaBufferDecoder : public BMediaDecoder {
	public:
		/** @brief Default constructor. */
		BMediaBufferDecoder();

		/** @brief Constructs and initializes from the given format.
		 *  @param in_format The compressed input format.
		 *  @param info Optional codec-specific initialization data.
		 *  @param info_size Size of info in bytes.
		 */
		BMediaBufferDecoder(const media_format *in_format,
		                    const void *info = NULL, size_t info_size = 0);

		/** @brief Constructs from a specific codec info structure.
		 *  @param mci Codec information identifying the decoder.
		 */
		BMediaBufferDecoder(const media_codec_info *mci);

		/** @brief Decodes a single compressed buffer into raw output.
		 *  @param input_buffer Pointer to the compressed input data.
		 *  @param input_size Size of the compressed input in bytes.
		 *  @param out_buffer Destination for the decoded raw data.
		 *  @param out_frameCount On return, number of decoded frames.
		 *  @param out_mh On return, the media header for the decoded data.
		 *  @param info Optional decode control parameters.
		 *  @return B_OK on success, or an error code.
		 */
		status_t DecodeBuffer(const void *input_buffer, size_t input_size,
		                      void *out_buffer, int64 *out_frameCount,
		                      media_header *out_mh,
		                      media_decode_info *info = NULL);
	protected:
		virtual status_t GetNextChunk(const void **chunkData, size_t *chunkLen,
		                              media_header *mh);
		const void *fBuffer;
		int32 fBufferSize;
};

#endif


