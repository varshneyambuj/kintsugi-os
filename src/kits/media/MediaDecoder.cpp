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
 *   AUTHOR: Andrew Bachmann, Marcus Overhagen
 *   FILE: MediaDecoder.cpp
 */

/** @file MediaDecoder.cpp
 *  @brief Implementation of BMediaDecoder and BMediaBufferDecoder for decoding media data.
 */

#include <MediaDecoder.h>
#include <DecoderPlugin.h>
#include <new>
#include "PluginManager.h"
#include "MediaDebug.h"

/*************************************************************
 * public BMediaDecoder
 *************************************************************/

/** @brief Default constructor; decoder must be set up via SetTo() before use. */
BMediaDecoder::BMediaDecoder()
 :	fDecoder(NULL),
 	fInitStatus(B_NO_INIT)
{
}


/** @brief Constructs a BMediaDecoder and sets it up for the given input format.
 *  @param in_format  Pointer to the input media_format.
 *  @param info       Pointer to optional codec setup info data.
 *  @param info_size  Size of the info data in bytes.
 */
BMediaDecoder::BMediaDecoder(const media_format *in_format,
							 const void *info,
							 size_t info_size)
 :	fDecoder(NULL),
 	fInitStatus(B_NO_INIT)
{
	SetTo(in_format, info, info_size);
}


/** @brief Constructs a BMediaDecoder using a specific codec identified by media_codec_info.
 *  @param mci Pointer to the desired media_codec_info.
 */
BMediaDecoder::BMediaDecoder(const media_codec_info *mci)
 :	fDecoder(NULL),
 	fInitStatus(B_NO_INIT)
{
	SetTo(mci);
}


/** @brief Destructor; destroys the underlying decoder plugin instance. */
/* virtual */
BMediaDecoder::~BMediaDecoder()
{
	gPluginManager.DestroyDecoder(fDecoder);
}


/** @brief Returns the initialisation status of the decoder.
 *  @return B_OK if ready, B_NO_INIT if not yet configured, or an error code.
 */
status_t
BMediaDecoder::InitCheck() const
{
	return fInitStatus;
}


/** @brief Selects a decoder codec for the given input format and attaches to it.
 *  @param in_format  Pointer to the input media_format.
 *  @param info       Pointer to optional codec setup info data.
 *  @param info_size  Size of the info data in bytes.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaDecoder::SetTo(const media_format *in_format,
					 const void *info,
					 size_t info_size)
{
	gPluginManager.DestroyDecoder(fDecoder);
	fDecoder = NULL;

	status_t err = gPluginManager.CreateDecoder(&fDecoder, *in_format);
	if (err < B_OK)
		goto fail;

	err = AttachToDecoder();
	if (err < B_OK)
		goto fail;

	err = SetInputFormat(in_format, info, info_size);
	if (err < B_OK)
		goto fail;

	fInitStatus = B_OK;
	return B_OK;

fail:
	gPluginManager.DestroyDecoder(fDecoder);
	fDecoder = NULL;
	fInitStatus = B_NO_INIT;
	return err;
}


/** @brief Selects a decoder codec identified by the given media_codec_info.
 *  @param mci Pointer to the desired media_codec_info.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaDecoder::SetTo(const media_codec_info *mci)
{
	gPluginManager.DestroyDecoder(fDecoder);
	fDecoder = NULL;

	status_t err = gPluginManager.CreateDecoder(&fDecoder, *mci);
	if (err < B_OK)
		goto fail;

	err = AttachToDecoder();
	if (err < B_OK)
		goto fail;

	fInitStatus = B_OK;
	return B_OK;

fail:
	gPluginManager.DestroyDecoder(fDecoder);
	fDecoder = NULL;
	fInitStatus = B_NO_INIT;
	return err;
}


/**	SetInputFormat() sets the input data format to in_format.
 *	Unlike SetTo(), the SetInputFormat() function does not
 *	select a codec, so the currently-selected codec will
 *	continue to be used.  You should only use SetInputFormat()
 *	to refine the format settings if it will not require the
 *	use of a different decoder.
 *  @param in_format Pointer to the updated input media_format.
 *  @param in_info   Pointer to optional codec info data.
 *  @param in_size   Size of the info data in bytes.
 *  @return B_OK on success, B_NO_INIT if no decoder is active.
 */

status_t
BMediaDecoder::SetInputFormat(const media_format *in_format,
							  const void *in_info,
							  size_t in_size)
{
	if (!fDecoder)
		return B_NO_INIT;

	media_format format = *in_format;
	return fDecoder->Setup(&format, in_info, in_size);
}


/**	SetOutputFormat() sets the format the decoder should output.
 *	On return, the output_format is changed to match the actual
 *	format that will be output; this can be different if you
 *	specified any wildcards.
 *  @param output_format In/out pointer to the desired output media_format.
 *  @return B_OK on success, B_NO_INIT if no decoder is active.
 */

status_t
BMediaDecoder::SetOutputFormat(media_format *output_format)
{
	if (!fDecoder)
		return B_NO_INIT;

	return fDecoder->NegotiateOutputFormat(output_format);
}


/**	Decodes a chunk of media data into the output buffer specified
 *	by out_buffer.  On return, out_frameCount is set to indicate how
 *	many frames of data were decoded, and out_mh is the header for
 *	the decoded buffer.  The media_decode_info structure info is used
 *	on input to specify decoding parameters.
 *
 *	The amount of data decoded is part of the format determined by
 *	SetTo() or SetInputFormat().  For audio, it's the buffer_size.
 *	For video, it's one frame, which is height*row_bytes.  The data
 *	to be decoded will be fetched from the source by the decoder
 *	add-on calling the derived class' GetNextChunk() function.
 *  @param out_buffer     Pointer to the output buffer for decoded data.
 *  @param out_frameCount Output number of decoded frames.
 *  @param out_mh         Output media_header for the decoded buffer.
 *  @param info           Pointer to media_decode_info for decoding parameters.
 *  @return B_OK on success, B_NO_INIT if no decoder is active.
 */

status_t
BMediaDecoder::Decode(void *out_buffer,
					  int64 *out_frameCount,
					  media_header *out_mh,
					  media_decode_info *info)
{
	if (!fDecoder)
		return B_NO_INIT;

	return fDecoder->Decode(out_buffer, out_frameCount, out_mh, info);
}


/** @brief Retrieves codec information for the active decoder.
 *  @param out_info Pointer to a media_codec_info struct to fill in.
 *  @return B_OK on success, B_NO_INIT if no decoder is active.
 */
status_t
BMediaDecoder::GetDecoderInfo(media_codec_info *out_info) const
{
	if (!fDecoder)
		return B_NO_INIT;

	return gPluginManager.GetDecoderInfo(fDecoder, out_info);
}


/*************************************************************
 * protected BMediaDecoder
 *************************************************************/


/*************************************************************
 * private BMediaDecoder
 *************************************************************/

/*
// unimplemented
BMediaDecoder::BMediaDecoder(const BMediaDecoder &);
BMediaDecoder::BMediaDecoder & operator=(const BMediaDecoder &);
*/

/** @brief Creates and attaches a ChunkProvider that routes GetNextChunk() calls
 *         through this BMediaDecoder instance to the underlying decoder plugin.
 *  @return B_OK on success, B_NO_MEMORY if the provider could not be allocated.
 */
status_t
BMediaDecoder::AttachToDecoder()
{
	class MediaDecoderChunkProvider : public ChunkProvider {
	private:
		BMediaDecoder * fDecoder;
	public:
		MediaDecoderChunkProvider(BMediaDecoder * decoder) {
			fDecoder = decoder;
		}
		virtual status_t GetNextChunk(const void **chunkBuffer, size_t *chunkSize,
		                              media_header *mediaHeader) {
			return fDecoder->GetNextChunk(chunkBuffer, chunkSize, mediaHeader);
		}
	} * provider = new(std::nothrow) MediaDecoderChunkProvider(this);

	if (!provider)
		return B_NO_MEMORY;

	fDecoder->SetChunkProvider(provider);
	return B_OK;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_0(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_1(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_2(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_3(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_4(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_5(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_6(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_7(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_8(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_9(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_10(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_11(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_12(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_13(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_14(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaDecoder::_Reserved_BMediaDecoder_15(int32 arg, ...) { return B_ERROR; }

/*************************************************************
 * public BMediaBufferDecoder
 *************************************************************/

/** @brief Default constructor; creates an uninitialised BMediaBufferDecoder. */
BMediaBufferDecoder::BMediaBufferDecoder()
 :	BMediaDecoder()
 ,	fBufferSize(0)
{
}


/** @brief Constructs a BMediaBufferDecoder and sets it up for the given format.
 *  @param in_format  Pointer to the input media_format.
 *  @param info       Pointer to optional codec setup info data.
 *  @param info_size  Size of the info data in bytes.
 */
BMediaBufferDecoder::BMediaBufferDecoder(const media_format *in_format,
										 const void *info,
										 size_t info_size)
 :	BMediaDecoder(in_format, info, info_size)
 ,	fBufferSize(0)
{
}


/** @brief Constructs a BMediaBufferDecoder using a specific codec.
 *  @param mci Pointer to the desired media_codec_info.
 */
BMediaBufferDecoder::BMediaBufferDecoder(const media_codec_info *mci)
 :	BMediaDecoder(mci)
 ,	fBufferSize(0)
{
}


/** @brief Decodes a caller-supplied in-memory buffer into the output buffer.
 *  @param input_buffer   Pointer to the encoded input data.
 *  @param input_size     Size of the encoded input data in bytes.
 *  @param out_buffer     Pointer to the output buffer for decoded frames.
 *  @param out_frameCount Output number of decoded frames.
 *  @param out_mh         Output media_header for the decoded buffer.
 *  @param info           Pointer to media_decode_info for decoding parameters.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaBufferDecoder::DecodeBuffer(const void *input_buffer,
								  size_t input_size,
								  void *out_buffer,
								  int64 *out_frameCount,
								  media_header *out_mh,
								  media_decode_info *info)
{
	fBuffer = input_buffer;
	fBufferSize = input_size;
	return Decode(out_buffer, out_frameCount, out_mh,info);
}


/*************************************************************
 * protected BMediaBufferDecoder
 *************************************************************/

/** @brief Returns the stored input buffer as the next chunk; clears the buffer after one call.
 *  @param chunkData Output pointer to the chunk data.
 *  @param chunkLen  Output size of the chunk in bytes.
 *  @param mh        Output media_header (unused).
 *  @return B_OK on success, B_LAST_BUFFER_ERROR if the buffer has already been consumed.
 */
/* virtual */
status_t
BMediaBufferDecoder::GetNextChunk(const void **chunkData,
								  size_t *chunkLen,
                                  media_header *mh)
{
	if (!fBufferSize)
		return B_LAST_BUFFER_ERROR;

	*chunkData = fBuffer;
	*chunkLen = fBufferSize;
	fBufferSize = 0;
	return B_OK;
}
