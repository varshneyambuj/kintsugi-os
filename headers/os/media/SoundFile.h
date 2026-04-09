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

/** @file SoundFile.h
 *  @brief Defines BSoundFile, a simple interface for reading and writing sound files.
 */

#ifndef _SOUND_FILE_H
#define _SOUND_FILE_H


#include <Entry.h>
#include <File.h>
#include <MediaDefs.h>


/** @brief File format identifiers returned by BSoundFile::FileFormat(). */
// file formats
enum {
	B_UNKNOWN_FILE,  /**< Unknown or unsupported file format. */
	B_AIFF_FILE,     /**< AIFF sound file format. */
	B_WAVE_FILE,     /**< WAV (RIFF WAVE) sound file format. */
	B_UNIX_FILE      /**< Raw or AU (Sun/Next) sound file format. */
};


/** @brief Provides a simplified interface for reading and writing uncompressed audio files.
 *
 *  BSoundFile abstracts AIFF, WAV, and similar formats, exposing frame-level
 *  read/write operations and format metadata queries.
 */
class BSoundFile  {
public:
	/** @brief Default constructor; call SetTo() before using. */
								BSoundFile();

	/** @brief Constructs and opens the given file.
	 *  @param ref Entry reference identifying the sound file.
	 *  @param openMode B_READ_ONLY, B_WRITE_ONLY, or B_READ_WRITE.
	 */
								BSoundFile(const entry_ref* ref,
									uint32 openMode);
	virtual						~BSoundFile();

	/** @brief Returns the initialization status.
	 *  @return B_OK if the file is open and ready, or an error code.
	 */
			status_t			InitCheck() const;

	/** @brief Opens a sound file for reading or writing.
	 *  @param ref Entry reference identifying the sound file.
	 *  @param openMode B_READ_ONLY, B_WRITE_ONLY, or B_READ_WRITE.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetTo(const entry_ref* ref, uint32 openMode);

	/** @brief Returns the file format identifier (e.g. B_AIFF_FILE).
	 *  @return One of the file format enum values.
	 */
			int32				FileFormat() const;

	/** @brief Returns the audio sampling rate in frames per second.
	 *  @return Sampling rate.
	 */
			int32				SamplingRate() const;

	/** @brief Returns the number of interleaved audio channels.
	 *  @return Channel count.
	 */
			int32				CountChannels() const;

	/** @brief Returns the size of one sample in bytes.
	 *  @return Sample size in bytes.
	 */
			int32				SampleSize() const;

	/** @brief Returns the byte order of the sample data.
	 *  @return B_BIG_ENDIAN or B_LITTLE_ENDIAN.
	 */
			int32				ByteOrder() const;

	/** @brief Returns the sample format (e.g. B_LINEAR_SAMPLES).
	 *  @return Sample format identifier.
	 */
			int32				SampleFormat() const;

	/** @brief Returns the size of one multi-channel frame in bytes.
	 *  @return Frame size in bytes.
	 */
			int32				FrameSize() const;

	/** @brief Returns the total number of frames in the file.
	 *  @return Frame count.
	 */
			off_t				CountFrames() const;

	/** @brief Returns true if the audio data is compressed.
	 *  @return True if compressed.
	 */
			bool				IsCompressed() const;

	/** @brief Returns the compression type code.
	 *  @return Compression type identifier.
	 */
			int32				CompressionType() const;

	/** @brief Returns a human-readable description of the compression type.
	 *  @return Compression name string.
	 */
			char*				CompressionName() const;

	/** @brief Sets the file format.
	 *  @param format One of the file format enum values.
	 *  @return The new format value.
	 */
	virtual	int32				SetFileFormat(int32 format);

	/** @brief Sets the sampling rate.
	 *  @param fps The new sampling rate in frames per second.
	 *  @return The new sampling rate.
	 */
	virtual	int32				SetSamplingRate(int32 fps);

	/** @brief Sets the number of channels.
	 *  @param samplesPerFrame The new channel count.
	 *  @return The new channel count.
	 */
	virtual	int32				SetChannelCount(int32 samplesPerFrame);

	/** @brief Sets the sample size.
	 *  @param bytesPerSample The new sample size in bytes.
	 *  @return The new sample size.
	 */
	virtual	int32				SetSampleSize(int32 bytesPerSample);

	/** @brief Sets the byte order of the sample data.
	 *  @param byteOrder B_BIG_ENDIAN or B_LITTLE_ENDIAN.
	 *  @return The new byte order.
	 */
	virtual	int32				SetByteOrder(int32 byteOrder);

	/** @brief Sets the sample format.
	 *  @param format The new sample format identifier.
	 *  @return The new format.
	 */
	virtual	int32				SetSampleFormat(int32 format);

	/** @brief Sets the compression type.
	 *  @param type The new compression type identifier.
	 *  @return The new type.
	 */
	virtual	int32				SetCompressionType(int32 type);

	/** @brief Sets the compression name string.
	 *  @param name The new compression name.
	 *  @return The new name pointer.
	 */
	virtual	char*				SetCompressionName(char* name);

	/** @brief Sets whether the audio data is compressed.
	 *  @param compressed True if compressed, false otherwise.
	 *  @return The new compressed flag.
	 */
	virtual	bool				SetIsCompressed(bool compressed);

	/** @brief Sets the byte offset to the first sample in the file.
	 *  @param offset Byte offset from the start of the file.
	 *  @return The new offset.
	 */
	virtual	off_t				SetDataLocation(off_t offset);

	/** @brief Sets the total frame count.
	 *  @param count The new frame count.
	 *  @return The new frame count.
	 */
	virtual	off_t				SetFrameCount(off_t count);

	/** @brief Reads the next frames from the file into a buffer.
	 *  @param buffer Destination buffer.
	 *  @param count Number of frames to read.
	 *  @return Number of frames actually read.
	 */
			size_t				ReadFrames(char* buffer, size_t count);

	/** @brief Writes frames from a buffer to the file.
	 *  @param buffer Source buffer.
	 *  @param count Number of frames to write.
	 *  @return Number of frames actually written.
	 */
			size_t				WriteFrames(char* buffer, size_t count);

	/** @brief Seeks to the given frame index.
	 *  @param index The target frame index.
	 *  @return The actual frame position after seeking.
	 */
	virtual	off_t				SeekToFrame(off_t index);

	/** @brief Returns the current frame read/write position.
	 *  @return Current frame index.
	 */
			off_t 				FrameIndex() const;

	/** @brief Returns the number of frames remaining from the current position.
	 *  @return Remaining frame count.
	 */
			off_t				FramesRemaining() const;

			BFile*				fSoundFile;  /**< The underlying BFile object. */

private:

	virtual	void				_ReservedSoundFile1();
	virtual	void				_ReservedSoundFile2();
	virtual	void				_ReservedSoundFile3();

			void				_init_raw_stats();
			status_t			_ref_to_file(const entry_ref* ref);

			int32				fFileFormat;
			int32				fSamplingRate;
			int32 				fChannelCount;
			int32				fSampleSize;
			int32				fByteOrder;
			int32				fSampleFormat;

			off_t				fByteOffset;
									// offset to first sample

			off_t				fFrameCount;
			off_t				fFrameIndex;

			bool				fIsCompressed;
			int32				fCompressionType;
			char*				fCompressionName;
			status_t			fCStatus;
			BMediaFile*			fMediaFile;
			BMediaTrack*		fMediaTrack;

			uint32			_reserved[2];
};

#endif	// _SOUND_FILE_H
