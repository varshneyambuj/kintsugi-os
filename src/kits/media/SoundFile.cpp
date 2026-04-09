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
 *   AUTHOR: Marcus Overhagen
 *     FILE: SoundFile.cpp
 */

/** @file SoundFile.cpp
 *  @brief Implements BSoundFile, providing access to audio file metadata
 *         and frame-level I/O via the Media Kit.
 */

#include <MediaFile.h>
#include <MediaTrack.h>
#include <SoundFile.h>

#include <string.h>

#include "MediaDebug.h"

/*************************************************************
 * public BSoundFile
 *************************************************************/

/**
 * @brief Default constructor. Initialises all fields to default values.
 */
BSoundFile::BSoundFile()
{
	_init_raw_stats();
}


/**
 * @brief Construct a BSoundFile and open the specified file entry.
 *
 * @param ref        Entry ref identifying the audio file.
 * @param open_mode  File open mode (e.g. B_READ_ONLY).
 */
BSoundFile::BSoundFile(const entry_ref *ref,
					   uint32 open_mode)
{
	_init_raw_stats();
	SetTo(ref,open_mode);
}

/**
 * @brief Destructor. Releases the media track and closes the underlying files.
 */
/* virtual */
BSoundFile::~BSoundFile()
{
	delete fSoundFile;
	delete fMediaFile;
		// fMediaTrack will be deleted by the BMediaFile destructor
}


/**
 * @brief Return the initialisation status of this object.
 *
 * @return B_OK if properly initialised, B_NO_INIT if no file has been opened,
 *         or a file-system error code.
 */
status_t
BSoundFile::InitCheck() const
{
	if (!fSoundFile) {
		return B_NO_INIT;
	}
	return fSoundFile->InitCheck();
}


/**
 * @brief Open a new audio file, replacing any previously opened file.
 *
 * @param ref        Entry ref identifying the new audio file.
 * @param open_mode  File open mode; currently only B_READ_ONLY is supported.
 * @return B_OK on success, B_ERROR if write mode is requested (unimplemented),
 *         or a file-system/media error code.
 */
status_t
BSoundFile::SetTo(const entry_ref *ref,
				  uint32 open_mode)
{
	if (fMediaTrack) {
		BMediaTrack * track = fMediaTrack;
		fMediaTrack = 0;
		fMediaFile->ReleaseTrack(track);
	}
	if (fMediaFile) {
		BMediaFile * file = fMediaFile;
		fMediaFile = 0;
		delete file;
	}
	if (fSoundFile) {
		BFile * file = fSoundFile;
		fSoundFile = 0;
		delete file;
	}
	if (open_mode == B_READ_ONLY) {
		return _ref_to_file(ref);
	} else {
		UNIMPLEMENTED();
		return B_ERROR;
	}
}


/**
 * @brief Return the file format identifier (e.g. B_AIFF_FILE, B_WAVE_FILE).
 *
 * @return Integer file format constant.
 */
int32
BSoundFile::FileFormat() const
{
	return fFileFormat;
}


/**
 * @brief Return the sampling rate of the audio data in frames per second.
 *
 * @return Sampling rate as an integer.
 */
int32
BSoundFile::SamplingRate() const
{
	return fSamplingRate;
}


/**
 * @brief Return the number of audio channels.
 *
 * @return Channel count.
 */
int32
BSoundFile::CountChannels() const
{
	return fChannelCount;
}


/**
 * @brief Return the number of bytes per sample.
 *
 * @return Sample size in bytes.
 */
int32
BSoundFile::SampleSize() const
{
	return fSampleSize;
}


/**
 * @brief Return the byte order of the audio samples.
 *
 * @return Byte order constant (e.g. B_BIG_ENDIAN).
 */
int32
BSoundFile::ByteOrder() const
{
	return fByteOrder;
}


/**
 * @brief Return the sample format (e.g. B_LINEAR_SAMPLES, B_FLOAT_SAMPLES).
 *
 * @return Sample format constant.
 */
int32
BSoundFile::SampleFormat() const
{
	return fSampleFormat;
}


/**
 * @brief Return the size of one audio frame in bytes.
 *
 * One frame = SampleSize() * CountChannels().
 *
 * @return Frame size in bytes.
 */
int32
BSoundFile::FrameSize() const
{
	return fSampleSize * fChannelCount;
}


/**
 * @brief Return the total number of frames in the file.
 *
 * @return Frame count.
 */
off_t
BSoundFile::CountFrames() const
{
	return fFrameCount;
}


/**
 * @brief Return whether the audio data is compressed.
 *
 * @return true if compressed, false otherwise.
 */
bool
BSoundFile::IsCompressed() const
{
	return fIsCompressed;
}


/**
 * @brief Return the compression type identifier.
 *
 * @return Compression type constant, or -1 if uncompressed.
 */
int32
BSoundFile::CompressionType() const
{
	return fCompressionType;
}


/**
 * @brief Return the human-readable name of the compression algorithm.
 *
 * @return Pointer to compression name string, or NULL.
 */
char *
BSoundFile::CompressionName() const
{
	return fCompressionName;
}


/**
 * @brief Set the file format identifier.
 *
 * @param format  New file format constant.
 * @return The newly set format value.
 */
/* virtual */ int32
BSoundFile::SetFileFormat(int32 format)
{
	fFileFormat = format;
	return fFileFormat;
}


/**
 * @brief Set the sampling rate in frames per second.
 *
 * @param fps  New sampling rate.
 * @return The newly set sampling rate.
 */
/* virtual */ int32
BSoundFile::SetSamplingRate(int32 fps)
{
	fSamplingRate = fps;
	return fSamplingRate;
}


/**
 * @brief Set the number of audio channels.
 *
 * @param spf  New channel count.
 * @return The newly set channel count.
 */
/* virtual */ int32
BSoundFile::SetChannelCount(int32 spf)
{
	fChannelCount = spf;
	return fChannelCount;
}


/**
 * @brief Set the sample size in bytes.
 *
 * @param bps  New sample size.
 * @return The newly set sample size.
 */
/* virtual */ int32
BSoundFile::SetSampleSize(int32 bps)
{
	fSampleSize = bps;
	return fSampleSize;
}


/**
 * @brief Set the byte order for audio samples.
 *
 * @param bord  New byte order constant.
 * @return The newly set byte order.
 */
/* virtual */ int32
BSoundFile::SetByteOrder(int32 bord)
{
	fByteOrder = bord;
	return fByteOrder;
}


/**
 * @brief Set the sample format.
 *
 * @param fmt  New sample format constant.
 * @return The newly set sample format.
 */
/* virtual */ int32
BSoundFile::SetSampleFormat(int32 fmt)
{
	fSampleFormat = fmt;
	return fSampleFormat;
}


/**
 * @brief Set the compression type (not implemented).
 *
 * @param type  Compression type constant (ignored).
 * @return Always 0.
 */
/* virtual */ int32
BSoundFile::SetCompressionType(int32 type)
{
	return 0;
}


/**
 * @brief Set the compression name (not implemented).
 *
 * @param name  Compression name string (ignored).
 * @return Always NULL.
 */
/* virtual */ char *
BSoundFile::SetCompressionName(char *name)
{
	return NULL;
}


/**
 * @brief Set the compression flag (not implemented).
 *
 * @param tf  New compression flag (ignored).
 * @return Always false.
 */
/* virtual */ bool
BSoundFile::SetIsCompressed(bool tf)
{
	return false;
}


/**
 * @brief Set the data location offset (not yet implemented).
 *
 * @param offset  Byte offset for data start (ignored).
 * @return Always 0.
 */
/* virtual */ off_t
BSoundFile::SetDataLocation(off_t offset)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Set the total frame count.
 *
 * @param count  New frame count.
 * @return The newly set frame count.
 */
/* virtual */ off_t
BSoundFile::SetFrameCount(off_t count)
{
	fFrameCount = count;
	return fFrameCount;
}


/**
 * @brief Read a number of audio frames into a buffer.
 *
 * Reads frames sequentially from the current position, advancing
 * the internal frame index.
 *
 * @param buf    Destination buffer for the decoded audio data.
 * @param count  Number of frames to read.
 * @return Number of frames actually read, or a negative error code if the
 *         first read attempt fails entirely.
 */
size_t
BSoundFile::ReadFrames(char *buf,
					   size_t count)
{
	size_t frameRead = 0;
	int64 frames = count;
	while (count > 0) {
		status_t status = fMediaTrack->ReadFrames(
				reinterpret_cast<void *>(buf), &frames);
		count -= frames;
		frameRead += frames;
		buf += fSampleSize * fChannelCount * frames;
		if (status != B_OK) {
			if (frameRead > 0)
				break;
			return status;
		}
	}
	return frameRead;
}


/**
 * @brief Write a number of audio frames from a buffer to the file.
 *
 * @param buf    Source buffer containing the audio data to write.
 * @param count  Number of frames to write.
 * @return Number of frames written.
 */
size_t
BSoundFile::WriteFrames(char *buf,
						size_t count)
{
	return fMediaTrack->WriteFrames(
			reinterpret_cast<void *>(buf), count);
}


/**
 * @brief Seek the current read/write position to the specified frame.
 *
 * @param n  Target frame index.
 * @return The frame index seeked to on success, or a negative error code.
 */
/* virtual */ off_t
BSoundFile::SeekToFrame(off_t n)
{
	int64 frames = n;
	status_t status = fMediaTrack->SeekToFrame(&frames);

	if (status != B_OK)
		return status;

	return frames;
}


/**
 * @brief Return the current frame index (read position).
 *
 * @return Current frame index.
 */
off_t
BSoundFile::FrameIndex() const
{
	return fFrameIndex;
}


/**
 * @brief Return the number of frames remaining from the current position.
 *
 * @return CountFrames() - FrameIndex().
 */
off_t
BSoundFile::FramesRemaining() const
{
	return fFrameCount - FrameIndex();
}

/*************************************************************
 * private BSoundFile
 *************************************************************/

/** @brief Reserved for future binary compatibility. */
void BSoundFile::_ReservedSoundFile1() {}
/** @brief Reserved for future binary compatibility. */
void BSoundFile::_ReservedSoundFile2() {}
/** @brief Reserved for future binary compatibility. */
void BSoundFile::_ReservedSoundFile3() {}

/**
 * @brief Initialise all member variables to safe default values.
 */
void
BSoundFile::_init_raw_stats()
{
	fSoundFile = 0;
	fMediaFile = 0;
	fMediaTrack = 0;
	fFileFormat = B_UNKNOWN_FILE;
	fSamplingRate = 44100;
	fChannelCount = 2;
	fSampleSize = 2;
	fByteOrder = B_BIG_ENDIAN;
	fSampleFormat = B_LINEAR_SAMPLES;
	fFrameCount = 0;
	fFrameIndex = 0;
	fIsCompressed = false;
	fCompressionType = -1;
	fCompressionName = NULL;
}


/**
 * @brief Map a MIME type string to a BSoundFile format constant.
 *
 * @param mime_type  MIME type string (e.g. "audio/x-aiff").
 * @return B_AIFF_FILE, B_WAVE_FILE, or B_UNKNOWN_FILE.
 */
static int32
_ParseMimeType(char *mime_type)
{
	if (strcmp(mime_type, "audio/x-aiff") == 0)
		return B_AIFF_FILE;
	if (strcmp(mime_type, "audio/x-wav") == 0)
		return B_WAVE_FILE;
	return B_UNKNOWN_FILE;
}


/**
 * @brief Open the audio file referenced by @p ref and populate internal state.
 *
 * Opens the BFile, wraps it in a BMediaFile, locates the first audio track,
 * and extracts format information such as sampling rate, channel count, and
 * sample size.
 *
 * @param ref  Entry ref of the audio file to open.
 * @return B_OK on success, or an error code if the file cannot be opened or
 *         does not contain a usable audio track.
 */
status_t
BSoundFile::_ref_to_file(const entry_ref *ref)
{
	status_t status;
	BFile * file = new BFile(ref, B_READ_ONLY);
	status = file->InitCheck();
	if (status != B_OK) {
		fSoundFile = file;
		return status;
	}
	BMediaFile * media = new BMediaFile(file);
	status = media->InitCheck();
	if (status != B_OK) {
		delete media;
		delete file;
		return status;
	}
	media_file_format mfi;
	media->GetFileFormatInfo(&mfi);
	switch (mfi.family) {
		case B_AIFF_FORMAT_FAMILY: fFileFormat = B_AIFF_FILE; break;
		case B_WAV_FORMAT_FAMILY:  fFileFormat = B_WAVE_FILE; break;
		default: fFileFormat = _ParseMimeType(mfi.mime_type); break;
	}
	int trackNum = 0;
	BMediaTrack * track = 0;
	media_format mf;
	while (trackNum < media->CountTracks()) {
		track = media->TrackAt(trackNum);
		status = track->DecodedFormat(&mf);
		if (status != B_OK) {
			media->ReleaseTrack(track);
			delete media;
			delete file;
			return status;
		}
		if (mf.IsAudio()) {
			break;
		}
		media->ReleaseTrack(track);
		track = 0;
	}
	if (track == 0) {
		delete media;
		delete file;
		return B_ERROR;
	}
	media_raw_audio_format * raw = 0;
	if (mf.type == B_MEDIA_ENCODED_AUDIO) {
		raw = &mf.u.encoded_audio.output;
	}
	if (mf.type == B_MEDIA_RAW_AUDIO) {
		raw = &mf.u.raw_audio;
	}

	if (raw == NULL) {
		delete media;
		delete file;
		return B_ERROR;
	}

	fSamplingRate = (int)raw->frame_rate;
	fChannelCount = raw->channel_count;
	fSampleSize = raw->format & 0xf;
	fByteOrder = raw->byte_order;
	switch (raw->format) {
		case media_raw_audio_format::B_AUDIO_FLOAT:
			fSampleFormat = B_FLOAT_SAMPLES;
			break;
		case media_raw_audio_format::B_AUDIO_INT:
		case media_raw_audio_format::B_AUDIO_SHORT:
		case media_raw_audio_format::B_AUDIO_UCHAR:
		case media_raw_audio_format::B_AUDIO_CHAR:
			fSampleFormat = B_LINEAR_SAMPLES;
			break;
		default:
			fSampleFormat = B_UNDEFINED_SAMPLES;
	}
	fByteOffset = 0;
	fFrameCount = track->CountFrames();
	fFrameIndex = 0;
	if (mf.type == B_MEDIA_ENCODED_AUDIO) {
		fIsCompressed = true;
		fCompressionType = mf.u.encoded_audio.encoding;
	}
	fMediaFile = media;
	fMediaTrack = track;
	fSoundFile = file;
	return B_OK;
}


