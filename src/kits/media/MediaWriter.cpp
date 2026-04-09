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
 *   All rights reserved. Distributed under the terms of the MIT license.
 */

/** @file MediaWriter.cpp
 *  @brief Internal media writer that multiplexes encoded streams into a container file.
 */


#include "MediaWriter.h"

#include <new>

#include <stdio.h>
#include <string.h>

#include <Autolock.h>

#include "MediaDebug.h"

#include "PluginManager.h"



/** @brief Internal ChunkWriter that routes WriteChunk() calls to the parent MediaWriter
 *         for a specific stream index.
 */
class MediaExtractorChunkWriter : public ChunkWriter {
public:
	/** @brief Constructs the chunk writer for the given stream index.
	 *  @param writer      Pointer to the owning MediaWriter.
	 *  @param streamIndex Zero-based stream index.
	 */
	MediaExtractorChunkWriter(MediaWriter* writer, int32 streamIndex)
		:
		fWriter(writer),
		fStreamIndex(streamIndex)
	{
	}

	/** @brief Writes an encoded chunk to the underlying writer plugin.
	 *  @param chunkBuffer Pointer to the encoded data.
	 *  @param chunkSize   Size of the encoded data in bytes.
	 *  @param encodeInfo  Pointer to media_encode_info for the chunk.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t WriteChunk(const void* chunkBuffer, size_t chunkSize,
		media_encode_info* encodeInfo)
	{
		return fWriter->WriteChunk(fStreamIndex, chunkBuffer, chunkSize,
			encodeInfo);
	}

private:
	MediaWriter*	fWriter;
	int32			fStreamIndex;
};


// #pragma mark -


/** @brief Constructs a MediaWriter and creates the writer plugin for the given format.
 *  @param target     Pointer to the BDataIO output target.
 *  @param fileFormat The desired container file format.
 */
MediaWriter::MediaWriter(BDataIO* target, const media_file_format& fileFormat)
	:
	fTarget(target),
	fWriter(NULL),
	fStreamInfos(),
	fFileFormat(fileFormat)
{
	CALLED();

	gPluginManager.CreateWriter(&fWriter, fFileFormat, fTarget);
}


/** @brief Destructor; frees all stream cookies and destroys the writer plugin. */
MediaWriter::~MediaWriter()
{
	CALLED();

	if (fWriter != NULL) {
		// free all stream cookies
		// and chunk caches
		StreamInfo* info;
		for (fStreamInfos.Rewind(); fStreamInfos.GetNext(&info);)
			fWriter->FreeCookie(info->cookie);

		gPluginManager.DestroyWriter(fWriter);
	}

	// fTarget is owned by the BMediaFile
}


/** @brief Initialises the writer plugin with the file format information.
 *  @return B_OK on success, B_NO_INIT if no writer plugin was created.
 */
status_t
MediaWriter::InitCheck()
{
	CALLED();

	return fWriter != NULL ? fWriter->Init(&fFileFormat) : B_NO_INIT;
}


/** @brief Returns the BDataIO output target used by this writer.
 *  @return Pointer to the BDataIO target.
 */
BDataIO*
MediaWriter::Target() const
{
	return fTarget;
}


/** @brief Fills in a media_file_format struct with the current file format info.
 *  @param _fileFormat Pointer to a media_file_format struct to fill; ignored if NULL.
 */
void
MediaWriter::GetFileFormatInfo(media_file_format* _fileFormat) const
{
	CALLED();

	if (_fileFormat != NULL)
		*_fileFormat = fFileFormat;
}


/** @brief Creates an encoder for the given codec and allocates a new stream cookie.
 *  @param _encoder  Output pointer to the created Encoder.
 *  @param codecInfo Pointer to the desired codec info.
 *  @param format    Pointer to the stream's media_format.
 *  @param flags     Optional encoder creation flags.
 *  @return B_OK on success, or an error code.
 */
status_t
MediaWriter::CreateEncoder(Encoder** _encoder,
	const media_codec_info* codecInfo, media_format* format, uint32 flags)
{
	CALLED();

	if (fWriter == NULL)
		return B_NO_INIT;

	// TODO: Here we should work out a way so that if there is a setup
	// failure we can try the next encoder.
	Encoder* encoder;
	status_t ret = gPluginManager.CreateEncoder(&encoder, codecInfo, flags);
	if (ret != B_OK) {
		ERROR("MediaWriter::CreateEncoder gPluginManager.CreateEncoder "
			"failed, codec: %s\n", codecInfo->pretty_name);
		return ret;
	}

	StreamInfo info;
	ret = fWriter->AllocateCookie(&info.cookie, format, codecInfo);
	if (ret != B_OK) {
		gPluginManager.DestroyEncoder(encoder);
		return ret;
	}

	int32 streamIndex = fStreamInfos.CountItems();

	if (!fStreamInfos.Insert(info)) {
		gPluginManager.DestroyEncoder(encoder);
		ERROR("MediaWriter::CreateEncoder can't create StreamInfo "
			"for stream %" B_PRId32 "\n", streamIndex);
		return B_NO_MEMORY;
	}

	ChunkWriter* chunkWriter = new(std::nothrow) MediaExtractorChunkWriter(
		this, streamIndex);
	if (chunkWriter == NULL) {
		gPluginManager.DestroyEncoder(encoder);
		ERROR("MediaWriter::CreateEncoder can't create ChunkWriter "
			"for stream %" B_PRId32 "\n", streamIndex);
		return B_NO_MEMORY;
	}

	encoder->SetChunkWriter(chunkWriter);
	*_encoder = encoder;

	return B_OK;
}


/** @brief Sets the global copyright string for the output file.
 *  @param copyright Null-terminated copyright string.
 *  @return B_OK on success, B_NO_INIT if no writer is active.
 */
status_t
MediaWriter::SetCopyright(const char* copyright)
{
	if (fWriter == NULL)
		return B_NO_INIT;

	return fWriter->SetCopyright(copyright);
}


/** @brief Sets the copyright string for a specific stream.
 *  @param streamIndex Zero-based stream index.
 *  @param copyright   Null-terminated copyright string.
 *  @return B_OK on success, B_NO_INIT if no writer is active, B_BAD_INDEX if out of range.
 */
status_t
MediaWriter::SetCopyright(int32 streamIndex, const char* copyright)
{
	if (fWriter == NULL)
		return B_NO_INIT;

	StreamInfo* info;
	if (!fStreamInfos.Get(streamIndex, &info))
		return B_BAD_INDEX;

	return fWriter->SetCopyright(info->cookie, copyright);
}


/** @brief Commits the file header after all streams have been added.
 *  @return B_OK on success, B_NO_INIT if no writer is active.
 */
status_t
MediaWriter::CommitHeader()
{
	if (fWriter == NULL)
		return B_NO_INIT;

	return fWriter->CommitHeader();
}


/** @brief Flushes any buffered encoded data to the target.
 *  @return B_OK on success, B_NO_INIT if no writer is active.
 */
status_t
MediaWriter::Flush()
{
	if (fWriter == NULL)
		return B_NO_INIT;

	return fWriter->Flush();
}


/** @brief Closes the output file and finalises the container format.
 *  @return B_OK on success, B_NO_INIT if no writer is active.
 */
status_t
MediaWriter::Close()
{
	if (fWriter == NULL)
		return B_NO_INIT;

	return fWriter->Close();
}


/** @brief Adds track-level info to the specified stream.
 *  @param streamIndex Zero-based stream index.
 *  @param code        Code identifying the info type.
 *  @param data        Pointer to the info data.
 *  @param size        Size of the info data in bytes.
 *  @param flags       Optional flags.
 *  @return B_OK on success, B_NO_INIT if no writer is active, B_BAD_INDEX if out of range.
 */
status_t
MediaWriter::AddTrackInfo(int32 streamIndex, uint32 code,
	const void* data, size_t size, uint32 flags)
{
	if (fWriter == NULL)
		return B_NO_INIT;

	StreamInfo* info;
	if (!fStreamInfos.Get(streamIndex, &info))
		return B_BAD_INDEX;

	return fWriter->AddTrackInfo(info->cookie, code, data, size, flags);
}


/** @brief Writes an encoded chunk to the specified stream in the container.
 *  @param streamIndex Zero-based stream index.
 *  @param chunkBuffer Pointer to the encoded data.
 *  @param chunkSize   Size of the encoded data in bytes.
 *  @param encodeInfo  Pointer to media_encode_info for the chunk.
 *  @return B_OK on success, B_NO_INIT if no writer is active, B_BAD_INDEX if out of range.
 */
status_t
MediaWriter::WriteChunk(int32 streamIndex, const void* chunkBuffer,
	size_t chunkSize, media_encode_info* encodeInfo)
{
	if (fWriter == NULL)
		return B_NO_INIT;

	StreamInfo* info;
	if (!fStreamInfos.Get(streamIndex, &info))
		return B_BAD_INDEX;

	return fWriter->WriteChunk(info->cookie, chunkBuffer, chunkSize,
		encodeInfo);
}

