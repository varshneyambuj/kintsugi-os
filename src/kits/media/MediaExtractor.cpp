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
 *   Copyright 2004-2007, Marcus Overhagen. All rights reserved.
 *   Copyright 2008, Maurice Kalinowski. All rights reserved.
 *   Copyright 2009-2012, Axel Dörfler, axeld@pinc-software.de.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file MediaExtractor.cpp
 *  @brief Internal media extractor that demuxes streams and manages per-stream chunk caches.
 */


#include "MediaExtractor.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <Autolock.h>
#include <InterfacePrivate.h>

#include "ChunkCache.h"
#include "MediaDebug.h"
#include "MediaMisc.h"
#include "PluginManager.h"


// should be 0, to disable the chunk cache set it to 1
#define DISABLE_CHUNK_CACHE 0


/** @brief ChunkProvider adapter that feeds chunks from a MediaExtractor stream. */
class MediaExtractorChunkProvider : public ChunkProvider {
public:
	/** @brief Constructs the provider for the given extractor and stream index.
	 *  @param extractor Pointer to the owning MediaExtractor.
	 *  @param stream    Zero-based stream index.
	 */
	MediaExtractorChunkProvider(MediaExtractor* extractor, int32 stream)
		:
		fExtractor(extractor),
		fStream(stream)
	{
	}

	/** @brief Retrieves the next encoded chunk from the stream.
	 *  @param _chunkBuffer Output pointer to the chunk data.
	 *  @param _chunkSize   Output size of the chunk in bytes.
	 *  @param mediaHeader  Output media_header for the chunk.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t GetNextChunk(const void** _chunkBuffer, size_t* _chunkSize,
		media_header *mediaHeader)
	{
		return fExtractor->GetNextChunk(fStream, _chunkBuffer, _chunkSize,
			mediaHeader);
	}

private:
	MediaExtractor*	fExtractor;
	int32			fStream;
};


// #pragma mark -


/** @brief Constructs a MediaExtractor and begins asynchronous chunk caching.
 *  @param source Pointer to the BDataIO data source.
 *  @param flags  Reader flags controlling behaviour.
 */
MediaExtractor::MediaExtractor(BDataIO* source, int32 flags)
	:
	fExtractorThread(-1),
	fReader(NULL),
	fStreamInfo(NULL),
	fStreamCount(0)
{
	_Init(source, flags);
}


/** @brief Initialises the reader plugin, allocates stream info structures,
 *         and starts the background extractor thread.
 *  @param source Pointer to the BDataIO data source.
 *  @param flags  Reader flags controlling behaviour.
 */
void
MediaExtractor::_Init(BDataIO* source, int32 flags)
{
	CALLED();

	fSource = source;

#if !DISABLE_CHUNK_CACHE
	// start extractor thread
	fExtractorWaitSem = create_sem(1, "media extractor thread sem");
	if (fExtractorWaitSem < 0) {
		fInitStatus = fExtractorWaitSem;
		return;
	}
#endif

	fInitStatus = gPluginManager.CreateReader(&fReader, &fStreamCount,
		&fFileFormat, source);
	if (fInitStatus != B_OK)
		return;

	fStreamInfo = new stream_info[fStreamCount];

	// initialize stream infos
	for (int32 i = 0; i < fStreamCount; i++) {
		fStreamInfo[i].status = B_OK;
		fStreamInfo[i].cookie = 0;
		fStreamInfo[i].hasCookie = false;
		fStreamInfo[i].infoBuffer = 0;
		fStreamInfo[i].infoBufferSize = 0;
		fStreamInfo[i].lastChunk = NULL;
		fStreamInfo[i].chunkCache = NULL;
		fStreamInfo[i].encodedFormat.Clear();
	}

	// create all stream cookies
	for (int32 i = 0; i < fStreamCount; i++) {
		if (fReader->AllocateCookie(i, &fStreamInfo[i].cookie) != B_OK) {
			fStreamInfo[i].cookie = 0;
			fStreamInfo[i].hasCookie = false;
			fStreamInfo[i].status = B_ERROR;
			ERROR("MediaExtractor::MediaExtractor: AllocateCookie for stream %"
				B_PRId32 " failed\n", i);
		} else
			fStreamInfo[i].hasCookie = true;
	}

	// get info for all streams
	for (int32 i = 0; i < fStreamCount; i++) {
		if (fStreamInfo[i].status != B_OK)
			continue;

		int64 frameCount;
		bigtime_t duration;
		if (fReader->GetStreamInfo(fStreamInfo[i].cookie, &frameCount,
				&duration, &fStreamInfo[i].encodedFormat,
				&fStreamInfo[i].infoBuffer, &fStreamInfo[i].infoBufferSize)
					!= B_OK) {
			fStreamInfo[i].status = B_ERROR;
			ERROR("MediaExtractor::MediaExtractor: GetStreamInfo for "
				"stream %" B_PRId32 " failed\n", i);
		}

#if !DISABLE_CHUNK_CACHE
		// Allocate our ChunkCache
		size_t chunkCacheMaxBytes = _CalculateChunkBuffer(i);
		fStreamInfo[i].chunkCache
			= new ChunkCache(fExtractorWaitSem, chunkCacheMaxBytes);
		if (fStreamInfo[i].chunkCache->InitCheck() != B_OK) {
			fInitStatus = B_NO_MEMORY;
			return;
		}
#endif
	}

#if !DISABLE_CHUNK_CACHE
	// start extractor thread
	fExtractorThread = spawn_thread(_ExtractorEntry, "media extractor thread",
		B_NORMAL_PRIORITY + 4, this);
	resume_thread(fExtractorThread);
#endif
}


/** @brief Destructor; stops background processing, frees cookies and chunk caches,
 *         and destroys the reader plugin.
 */
MediaExtractor::~MediaExtractor()
{
	CALLED();

	// stop the extractor thread, if still running
	StopProcessing();

	// free all stream cookies
	// and chunk caches
	for (int32 i = 0; i < fStreamCount; i++) {
		if (fStreamInfo[i].hasCookie)
			fReader->FreeCookie(fStreamInfo[i].cookie);

		delete fStreamInfo[i].chunkCache;
	}

	gPluginManager.DestroyReader(fReader);

	delete[] fStreamInfo;
	// fSource is owned by the BMediaFile
}


/** @brief Returns the initialisation status of the extractor.
 *  @return B_OK if initialised successfully, or an error code.
 */
status_t
MediaExtractor::InitCheck()
{
	CALLED();
	return fInitStatus;
}


/** @brief Fills in the media_file_format struct with the detected file format.
 *  @param fileFormat Pointer to a media_file_format struct to fill in.
 */
void
MediaExtractor::GetFileFormatInfo(media_file_format* fileFormat) const
{
	CALLED();
	*fileFormat = fFileFormat;
}


/** @brief Retrieves global metadata from the source file into a BMessage.
 *  @param _data Pointer to a BMessage to fill with metadata.
 *  @return B_OK on success, or an error code from the reader plugin.
 */
status_t
MediaExtractor::GetMetaData(BMessage* _data) const
{
	CALLED();
	return fReader->GetMetaData(_data);
}


/** @brief Returns the number of streams found in the source.
 *  @return The stream count.
 */
int32
MediaExtractor::StreamCount()
{
	CALLED();
	return fStreamCount;
}


/** @brief Returns the copyright string from the source file.
 *  @return A pointer to the copyright string, or NULL if none.
 */
const char*
MediaExtractor::Copyright()
{
	return fReader->Copyright();
}


/** @brief Returns a pointer to the encoded media_format for the given stream.
 *  @param stream Zero-based stream index.
 *  @return Pointer to the stream's encoded media_format.
 */
const media_format*
MediaExtractor::EncodedFormat(int32 stream)
{
	return &fStreamInfo[stream].encodedFormat;
}


/** @brief Returns the total frame count for the given stream.
 *  @param stream Zero-based stream index.
 *  @return Frame count, or 0 if the stream is in an error state.
 */
int64
MediaExtractor::CountFrames(int32 stream) const
{
	CALLED();
	if (fStreamInfo[stream].status != B_OK)
		return 0LL;

	int64 frameCount;
	bigtime_t duration;
	media_format format;
	const void* infoBuffer;
	size_t infoSize;

	fReader->GetStreamInfo(fStreamInfo[stream].cookie, &frameCount, &duration,
		&format, &infoBuffer, &infoSize);

	return frameCount;
}


/** @brief Returns the total duration of the given stream in microseconds.
 *  @param stream Zero-based stream index.
 *  @return Duration in microseconds, or 0 if the stream is in an error state.
 */
bigtime_t
MediaExtractor::Duration(int32 stream) const
{
	CALLED();

	if (fStreamInfo[stream].status != B_OK)
		return 0LL;

	int64 frameCount;
	bigtime_t duration;
	media_format format;
	const void* infoBuffer;
	size_t infoSize;

	fReader->GetStreamInfo(fStreamInfo[stream].cookie, &frameCount, &duration,
		&format, &infoBuffer, &infoSize);

	return duration;
}


/** @brief Seeks the given stream to the position specified by flags.
 *         Clears the chunk cache after a successful seek.
 *  @param stream  Zero-based stream index.
 *  @param seekTo  Seek mode flags (B_MEDIA_SEEK_TO_TIME, etc.).
 *  @param _frame  In/out frame position.
 *  @param _time   In/out time position in microseconds.
 *  @return B_OK on success, or an error code.
 */
status_t
MediaExtractor::Seek(int32 stream, uint32 seekTo, int64* _frame,
	bigtime_t* _time)
{
	CALLED();

	stream_info& info = fStreamInfo[stream];
	if (info.status != B_OK)
		return info.status;

#if !DISABLE_CHUNK_CACHE
	BAutolock _(info.chunkCache);
#endif

	status_t status = fReader->Seek(info.cookie, seekTo, _frame, _time);
	if (status != B_OK)
		return status;

#if !DISABLE_CHUNK_CACHE
	// clear buffered chunks after seek
	info.chunkCache->MakeEmpty();
#endif

	return B_OK;
}


/** @brief Finds the nearest key frame to the requested position without seeking.
 *  @param stream  Zero-based stream index.
 *  @param seekTo  Seek mode flags.
 *  @param _frame  In/out frame position.
 *  @param _time   In/out time position in microseconds.
 *  @return B_OK on success, or an error code.
 */
status_t
MediaExtractor::FindKeyFrame(int32 stream, uint32 seekTo, int64* _frame,
	bigtime_t* _time) const
{
	CALLED();

	stream_info& info = fStreamInfo[stream];
	if (info.status != B_OK)
		return info.status;

	return fReader->FindKeyFrame(info.cookie, seekTo, _frame, _time);
}


/** @brief Retrieves the next encoded chunk from the stream, using the chunk cache.
 *  @param stream        Zero-based stream index.
 *  @param _chunkBuffer  Output pointer to the chunk data.
 *  @param _chunkSize    Output size of the chunk in bytes.
 *  @param mediaHeader   Output media_header for the chunk.
 *  @return B_OK on success, or an error code.
 */
status_t
MediaExtractor::GetNextChunk(int32 stream, const void** _chunkBuffer,
	size_t* _chunkSize, media_header* mediaHeader)
{
	stream_info& info = fStreamInfo[stream];

	if (info.status != B_OK)
		return info.status;

#if DISABLE_CHUNK_CACHE
	return fReader->GetNextChunk(fStreamInfo[stream].cookie, _chunkBuffer,
		_chunkSize, mediaHeader);
#else
	BAutolock _(info.chunkCache);

	_RecycleLastChunk(info);

	// Retrieve next chunk - read it directly, if the cache is drained
	chunk_buffer* chunk = info.chunkCache->NextChunk(fReader, info.cookie);

	if (chunk == NULL)
		return B_NO_MEMORY;

	info.lastChunk = chunk;

	*_chunkBuffer = chunk->buffer;
	*_chunkSize = chunk->size;
	*mediaHeader = chunk->header;

	return chunk->status;
#endif
}


/** @brief Creates and initialises a Decoder for the given stream.
 *  @param stream     Zero-based stream index.
 *  @param _decoder   Output pointer to the created Decoder.
 *  @param codecInfo  Output pointer to receive codec information.
 *  @return B_OK on success, or an error code.
 */
status_t
MediaExtractor::CreateDecoder(int32 stream, Decoder** _decoder,
	media_codec_info* codecInfo)
{
	CALLED();

	status_t status = fStreamInfo[stream].status;
	if (status != B_OK) {
		ERROR("MediaExtractor::CreateDecoder can't create decoder for "
			"stream %" B_PRId32 ": %s\n", stream, strerror(status));
		return status;
	}

	// TODO: Here we should work out a way so that if there is a setup
	// failure we can try the next decoder
	Decoder* decoder;
	status = gPluginManager.CreateDecoder(&decoder,
		fStreamInfo[stream].encodedFormat);
	if (status != B_OK) {
#if DEBUG
		char formatString[256];
		string_for_format(fStreamInfo[stream].encodedFormat, formatString,
			sizeof(formatString));

		ERROR("MediaExtractor::CreateDecoder gPluginManager.CreateDecoder "
			"failed for stream %" B_PRId32 ", format: %s: %s\n", stream,
			formatString, strerror(status));
#endif
		return status;
	}

	ChunkProvider* chunkProvider
		= new(std::nothrow) MediaExtractorChunkProvider(this, stream);
	if (chunkProvider == NULL) {
		gPluginManager.DestroyDecoder(decoder);
		ERROR("MediaExtractor::CreateDecoder can't create chunk provider "
			"for stream %" B_PRId32 "\n", stream);
		return B_NO_MEMORY;
	}

	decoder->SetChunkProvider(chunkProvider);

	status = decoder->Setup(&fStreamInfo[stream].encodedFormat,
		fStreamInfo[stream].infoBuffer, fStreamInfo[stream].infoBufferSize);
	if (status != B_OK) {
		gPluginManager.DestroyDecoder(decoder);
		ERROR("MediaExtractor::CreateDecoder Setup failed for stream %" B_PRId32
			": %s\n", stream, strerror(status));
		return status;
	}

	status = gPluginManager.GetDecoderInfo(decoder, codecInfo);
	if (status != B_OK) {
		gPluginManager.DestroyDecoder(decoder);
		ERROR("MediaExtractor::CreateDecoder GetCodecInfo failed for stream %"
			B_PRId32 ": %s\n", stream, strerror(status));
		return status;
	}

	*_decoder = decoder;
	return B_OK;
}


/** @brief Retrieves per-stream metadata into a BMessage.
 *  @param stream Zero-based stream index.
 *  @param _data  Pointer to a BMessage to fill with stream metadata.
 *  @return B_OK on success, or an error code.
 */
status_t
MediaExtractor::GetStreamMetaData(int32 stream, BMessage* _data) const
{
	const stream_info& info = fStreamInfo[stream];

	if (info.status != B_OK)
		return info.status;

	return fReader->GetStreamMetaData(fStreamInfo[stream].cookie, _data);
}


/** @brief Stops the background extractor thread and releases its semaphore. */
void
MediaExtractor::StopProcessing()
{
#if !DISABLE_CHUNK_CACHE
	if (fExtractorWaitSem > -1) {
		// terminate extractor thread
		delete_sem(fExtractorWaitSem);
		fExtractorWaitSem = -1;

		status_t status;
		wait_for_thread(fExtractorThread, &status);
	}
#endif
}


/** @brief Returns the last cached chunk of the given stream to the cache for reuse.
 *  @param info Reference to the stream_info whose last chunk is to be recycled.
 */
void
MediaExtractor::_RecycleLastChunk(stream_info& info)
{
	if (info.lastChunk != NULL) {
		info.chunkCache->RecycleChunk(info.lastChunk);
		info.lastChunk = NULL;
	}
}


/** @brief Static thread entry point; calls _ExtractorThread() on the extractor instance.
 *  @param extractor Pointer to the MediaExtractor instance (cast from void*).
 *  @return B_OK.
 */
status_t
MediaExtractor::_ExtractorEntry(void* extractor)
{
	static_cast<MediaExtractor*>(extractor)->_ExtractorThread();
	return B_OK;
}


/** @brief Calculates an appropriate chunk cache size for the given stream.
 *         Uses a heuristic based on video frame dimensions when available.
 *  @param stream Zero-based stream index.
 *  @return Recommended cache size in bytes (page-aligned).
 */
size_t
MediaExtractor::_CalculateChunkBuffer(int32 stream)
{
	// WARNING: magic
	// Your A/V may skip frames, chunks or not play at all if the cache size
	// is insufficient. Unfortunately there's currently no safe way to
	// calculate it.

	size_t cacheSize = 3 * 1024 * 1024;

	const media_format* format = EncodedFormat(stream);
	if (format->IsVideo()) {
		// For video, have space for at least two frames
		int32 rowSize = BPrivate::get_bytes_per_row(format->ColorSpace(),
			format->Width());
		if (rowSize > 0) {
			cacheSize = max_c(cacheSize, rowSize * format->Height() * 2);
		}
	}
	return ROUND_UP_TO_PAGE(cacheSize);
}


/** @brief Background thread body; continuously fills stream chunk caches
 *         until all streams are saturated, then waits for the semaphore signal.
 */
void
MediaExtractor::_ExtractorThread()
{
	while (true) {
		status_t status;
		do {
			status = acquire_sem(fExtractorWaitSem);
		} while (status == B_INTERRUPTED);

		if (status != B_OK) {
			// we were asked to quit
			return;
		}

		// Iterate over all streams until they are all filled

		int32 streamsFilled;
		do {
			streamsFilled = 0;

			for (int32 stream = 0; stream < fStreamCount; stream++) {
				stream_info& info = fStreamInfo[stream];
				if (info.status != B_OK) {
					streamsFilled++;
					continue;
				}

				BAutolock _(info.chunkCache);

				if (!info.chunkCache->SpaceLeft()
					|| !info.chunkCache->ReadNextChunk(fReader, info.cookie))
					streamsFilled++;
			}
		} while (streamsFilled < fStreamCount);
	}
}
