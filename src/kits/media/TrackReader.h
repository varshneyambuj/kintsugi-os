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
 * Incorporates work originally by Marcus Overhagen.
 * The undocumented BTrackReader class, used by BSound and the GameSound classes.
 */

/** @file TrackReader.h
    @brief Undocumented BTrackReader helper used by BSound and GameSound classes. */

#if !defined(_TRACK_READER_H_)
#define _TRACK_READER_H_

#include <MediaTrack.h>

class BMediaFile;

namespace BPrivate
{

/** @brief Provides sequential raw-audio frame access over a BMediaTrack,
           used internally by BSound and the GameSound classes. */
class BTrackReader
{
public:
	/** @brief Constructs a reader wrapping an existing media track.
	    @param track The BMediaTrack to read from.
	    @param format The desired raw audio output format. */
	BTrackReader(BMediaTrack *, media_raw_audio_format const &);

	/** @brief Constructs a reader by opening the first audio track in a file.
	    @param file The BFile to open as a media file.
	    @param format The desired raw audio output format. */
	BTrackReader(BFile *, media_raw_audio_format const &);

	~BTrackReader();

	/** @brief Returns the initialisation status of this reader.
	    @return B_OK if the reader is ready, or an error code. */
	status_t 	InitCheck();

	/** @brief Returns the total number of audio frames in the track.
	    @return Frame count as a 64-bit integer. */
	int64 		CountFrames(void);

	/** @brief Returns the size in bytes of a single audio frame.
	    @return Frame size in bytes. */
	int32 		FrameSize(void);

	/** @brief Reads a number of decoded audio frames into a caller-supplied buffer.
	    @param in_buffer Destination buffer; must be at least frame_count * FrameSize() bytes.
	    @param frame_count Number of frames to decode and copy.
	    @return B_OK on success, or an error code. */
	status_t 	ReadFrames(void *in_buffer, int32 frame_count);

	/** @brief Seeks the read position to a specific frame, adjusting to the nearest keyframe.
	    @param in_out_frame In/out frame index; updated to the actual frame seeked to.
	    @return B_OK on success, or an error code. */
	status_t 	SeekToFrame(int64 *in_out_frame);

	/** @brief Returns the underlying BMediaTrack being read.
	    @return Pointer to the BMediaTrack. */
	BMediaTrack * 					Track(void);

	/** @brief Returns the raw audio format this reader decodes into.
	    @return Reference to the media_raw_audio_format. */
	const media_raw_audio_format & 	Format(void) const;

private:
	void SetToTrack(BMediaTrack *track);

private:
	int32	fFrameSize;
	uint8 *	fBuffer;
	int32 	fBufferOffset;
	int32	fBufferUsedSize;
	BMediaFile *fMediaFile;
	BMediaTrack *fMediaTrack;
	media_raw_audio_format fFormat;
};

}; //namespace BPrivate

#endif
