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
 *   Copyright (c) 2002, 2003 Marcus Overhagen <Marcus@Overhagen.de>
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files or portions
 *   thereof (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software,
 *   and to permit persons to whom the Software is furnished to do so, subject
 *   to the following conditions:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above copyright notice
 *      in the  binary, as well as this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided with
 *      the distribution.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *   THE SOFTWARE.
 */

/** @file TrackReader.cpp
 *  @brief Implementation of the undocumented BTrackReader class used by BSound and GameSound.
 */

/*
 * The undocumented BTrackReader class,
 * used by BSound and the GameSound classes
 */

#include <File.h>
#include <MediaTrack.h>
#include <MediaFile.h>
#include <string.h>
#include "TrackReader.h"
#include "MediaDebug.h"

namespace BPrivate
{

/** @brief Constructs a BTrackReader from an existing BMediaTrack object.
 *         Attempts to negotiate a compatible raw audio output format.
 *  @param track  Pointer to an already-opened BMediaTrack.
 *  @param format The desired raw audio output format.
 */
BTrackReader::BTrackReader(BMediaTrack *track, media_raw_audio_format const &format) :
	fFrameSize(0),
	fBuffer(0),
	fBufferOffset(0),
	fBufferUsedSize(0),
	fMediaFile(0),
	fMediaTrack(0),
	fFormat(format)
{
	CALLED();
	if (track == NULL)
		return;
	if (track->InitCheck() != B_OK)
		return;

	SetToTrack(track);

	// if the track was not set abort now
	if (fMediaTrack == 0)
		return;

	fBuffer = new uint8[fFormat.buffer_size];
	fFrameSize = fFormat.channel_count * (fFormat.format & media_raw_audio_format::B_AUDIO_SIZE_MASK);

	TRACE("BTrackReader::BTrackReader successful\n");
}


/** @brief Constructs a BTrackReader by opening a BFile and locating the first audio track.
 *  @param file   Pointer to an already-opened BFile.
 *  @param format The desired raw audio output format.
 */
BTrackReader::BTrackReader(BFile *file, media_raw_audio_format const &format) :
	fFrameSize(0),
	fBuffer(0),
	fBufferOffset(0),
	fBufferUsedSize(0),
	fMediaFile(0),
	fMediaTrack(0),
	fFormat(format)
{
	CALLED();
	if (file == NULL)
		return;
	if (file->InitCheck() != B_OK)
		return;

	fMediaFile = new BMediaFile(file);
	if (fMediaFile->InitCheck() != B_OK)
		return;

	int count = fMediaFile->CountTracks();
	if (count == 0) {
		ERROR("BTrackReader: no tracks in file\n");
		return;
	}

	// find the first audio track
	BMediaTrack *track;
	BMediaTrack *audiotrack = 0;
	for (int tr = 0; tr < count; tr++) {
		track = fMediaFile->TrackAt(tr);
		if (track == 0 || track->InitCheck() != B_OK)
			continue;
		media_format fmt;
		if (track->DecodedFormat(&fmt) != B_OK)
			continue;
		if (fmt.type == B_MEDIA_RAW_AUDIO) {
			audiotrack = track;
			break;
		}
		fMediaFile->ReleaseTrack(track);
	}
	if (audiotrack == 0) {
		ERROR("BTrackReader: no audio track in file\n");
		return;
	}

	SetToTrack(audiotrack);

	// if the track was not set, release it
	if (fMediaTrack == 0) {
		fMediaFile->ReleaseTrack(audiotrack);
		return;
	}

	fBuffer = new uint8[fFormat.buffer_size];
	fFrameSize = fFormat.channel_count * (fFormat.format & media_raw_audio_format::B_AUDIO_SIZE_MASK);

	TRACE("BTrackReader::BTrackReader successful\n");
}


/** @brief Attempts to negotiate a raw audio output format with the given track.
 *         Tries progressively simpler formats if the first negotiation fails.
 *  @param track Pointer to the BMediaTrack to negotiate with.
 */
void
BTrackReader::SetToTrack(BMediaTrack *track)
{
	media_format fmt;
	fmt.Clear(); //wildcard
	memcpy(&fmt.u.raw_audio, &fFormat, sizeof(fFormat));
	fmt.type = B_MEDIA_RAW_AUDIO;
	//try to find a output format
	if (track->DecodedFormat(&fmt) == B_OK) {
		memcpy(&fFormat, &fmt.u.raw_audio, sizeof(fFormat));
		fMediaTrack = track;
		return;
	}

	//try again
	fmt.u.raw_audio.buffer_size = 2 * 4096;
	fmt.u.raw_audio.format = media_raw_audio_format::B_AUDIO_FLOAT;
	if (track->DecodedFormat(&fmt) == B_OK) {
		memcpy(&fFormat, &fmt.u.raw_audio, sizeof(fFormat));
		fMediaTrack = track;
		return;
	}

	//try again
	fmt.u.raw_audio.buffer_size = 4096;
	fmt.u.raw_audio.format = media_raw_audio_format::B_AUDIO_SHORT;
	if (track->DecodedFormat(&fmt) == B_OK) {
		memcpy(&fFormat, &fmt.u.raw_audio, sizeof(fFormat));
		fMediaTrack = track;
		return;
	}

	//we have failed
	ERROR("BTrackReader::SetToTrack failed\n");
}


/** @brief Destructor; releases the media track and frees the decode buffer. */
BTrackReader::~BTrackReader()
{
	CALLED();
	if (fMediaFile && fMediaTrack)
		fMediaFile->ReleaseTrack(fMediaTrack);
	delete fMediaFile;
	delete[] fBuffer;
}


/** @brief Returns the initialisation status of the reader.
 *  @return B_OK if ready, B_ERROR if no track was found.
 */
status_t
BTrackReader::InitCheck()
{
	CALLED();
	return fMediaTrack ? fMediaTrack->InitCheck() : B_ERROR;
}


/** @brief Returns the total number of audio frames available.
 *  @return Frame count, or 0 if no track is set.
 */
int64
BTrackReader::CountFrames(void)
{
	CALLED();
	return fMediaTrack ? fMediaTrack->CountFrames() : 0;
}


/** @brief Returns the negotiated raw audio format.
 *  @return Const reference to the current media_raw_audio_format.
 */
const media_raw_audio_format &
BTrackReader::Format(void) const
{
	CALLED();
	return fFormat;
}


/** @brief Returns the size of a single audio frame in bytes.
 *  @return Frame size in bytes (channels * bytes-per-sample).
 */
int32
BTrackReader::FrameSize(void)
{
	CALLED();
	return fFrameSize;
}


/** @brief Reads @p frame_count audio frames into the caller-supplied buffer.
 *         Silences any frames that could not be read due to end-of-stream.
 *  @param in_buffer   Pointer to the output buffer.
 *  @param frame_count Number of frames to read.
 *  @return B_OK on success, B_LAST_BUFFER_ERROR if the stream ended before all
 *          frames could be read.
 */
status_t
BTrackReader::ReadFrames(void* in_buffer, int32 frame_count)
{
	CALLED();

	uint8* buffer = static_cast<uint8*>(in_buffer);
	int32 bytes_to_read = frame_count * fFrameSize;

	status_t last_status = B_OK;
	while (bytes_to_read > 0) {
		int32 bytes_to_copy = min_c(fBufferUsedSize, bytes_to_read);
		if (bytes_to_copy > 0) {
			memcpy(buffer, fBuffer + fBufferOffset, bytes_to_copy);
			buffer += bytes_to_copy;
			bytes_to_read -= bytes_to_copy;
			fBufferOffset += bytes_to_copy;
			fBufferUsedSize -= bytes_to_copy;
		}
		if (last_status != B_OK)
			break;
		if (fBufferUsedSize == 0) {
			int64 outFrameCount;
			last_status = fMediaTrack->ReadFrames(fBuffer,
				&outFrameCount);
			fBufferOffset = 0;
			fBufferUsedSize = outFrameCount * fFrameSize;
		}
	}
	if (bytes_to_read > 0) {
		memset(buffer, 0, bytes_to_read);
		return B_LAST_BUFFER_ERROR;
	}
	return B_OK;
}


/** @brief Seeks to the given frame position, rounding to the nearest key frame.
 *         Also clears the internal read buffer.
 *  @param in_out_frame In/out pointer to the desired frame position.
 *  @return B_OK on success, or an error code from the underlying track seek.
 */
status_t
BTrackReader::SeekToFrame(int64* in_out_frame)
{
	CALLED();
	status_t s = fMediaTrack->SeekToFrame(in_out_frame, B_MEDIA_SEEK_CLOSEST_BACKWARD);
	if (s != B_OK)
		return s;
	fBufferUsedSize = 0;
	fBufferOffset = 0;
	return B_OK;
}


/** @brief Returns a pointer to the underlying BMediaTrack.
 *  @return Pointer to the BMediaTrack, or NULL if not initialised.
 */
BMediaTrack*
BTrackReader::Track(void)
{
	CALLED();
	return fMediaTrack;
}

}; //namespace BPrivate

