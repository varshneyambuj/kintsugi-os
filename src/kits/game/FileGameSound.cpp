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
 *   Copyright 2001-2012 Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 *       Jérôme Duval
 */

/**
 * @file FileGameSound.cpp
 * @brief BFileGameSound: streaming audio playback directly from a file or BDataIO.
 *
 * BFileGameSound opens an audio file (or any BDataIO) via BMediaFile /
 * BMediaTrack, decodes it into an intermediate buffer on demand, and streams
 * the decoded PCM data to the system audio mixer through the GameKit's
 * StreamingSoundBuffer / GameProducer pipeline.
 *
 * Key features:
 *   - Supports looping: seeks back to frame 0 on EOF.
 *   - Pause/resume with optional gain ramping (fade-out / fade-in).
 *   - The intermediate decode buffer (fBuffer) is sized to the larger of the
 *     codec's natural buffer size and the mixer's preferred buffer size.
 *   - FillBuffer() splices or combines codec buffers into whatever size the
 *     mixer requests, filling any leftover space with silence.
 *
 * Construction paths:
 *   - entry_ref: opens a BFile internally.
 *   - char*: resolves to entry_ref then opens a BFile.
 *   - BDataIO*: used directly (ownership is transferred).
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Entry.h>
#include <File.h>
#include <FileGameSound.h>
#include <MediaFile.h>
#include <MediaTrack.h>
#include <scheduler.h>

#include "GameSoundDevice.h"
#include "GSUtility.h"


/**
 * @brief Internal structure tracking the BMediaFile/BMediaTrack state.
 *
 * Bundles together the BMediaFile wrapper, the first audio BMediaTrack,
 * the total frame count, and a byte-level position counter used to track
 * progress through the decode buffer.
 */
struct _gs_media_tracker {
	BMediaFile*	file;
	BMediaTrack*	stream;
	int64		frames;
	size_t		position;
};


// Local utility functions -----------------------------------------------

/**
 * @brief Applies a gain ramp to a typed sample buffer in-place.
 *
 * Reads samples from \a src, multiplies each by the current ramp gain, and
 * writes clamped results to \a dest. After each sample, ChangeRamp() is
 * called to advance the ramp by one step. If the ramp completes before all
 * samples are processed, \a *bytes is updated to reflect how many bytes were
 * actually written and the function returns true.
 *
 * This template is instantiated for uint8, int16, int32, and float to cover
 * all four gs_audio_format sample types.
 *
 * @tparam T       Sample type.
 * @tparam min     Minimum representable value for T.
 * @tparam middle  Mid-point of T's range (0 for signed; 128 for unsigned).
 * @tparam max     Maximum representable value for T.
 * @param  ramp    Active gain ramp driving the fade.
 * @param  dest    Destination buffer for the processed samples.
 * @param  src     Source buffer containing the raw decoded samples.
 * @param  bytes   In/out: on entry, total byte count; on early completion,
 *                 set to the number of bytes actually written.
 * @return \c true if the ramp finished before all bytes were consumed;
 *         \c false if all bytes were processed without the ramp completing.
 */
template<typename T, int32 min, int32 middle, int32 max>
bool
FillBuffer(_gs_ramp* ramp, T* dest, const T* src, size_t* bytes)
{
	size_t samples = *bytes / sizeof(T);

	for (size_t sample = 0; sample < samples; sample++) {
		float gain = *ramp->value;
		dest[sample] = clamp<T, min, max>(float(src[sample] - middle) * gain
			+ middle);

		if (ChangeRamp(ramp)) {
			*bytes = sample * sizeof(T);
			return true;
		}
	}

	return false;
}


// BFileGameSound -------------------------------------------------------

/**
 * @brief Constructs a BFileGameSound from a file identified by entry_ref.
 *
 * Opens the file as a read-only BFile and delegates initialisation to
 * Init(BDataIO*). Ownership of the internally-created BFile is transferred
 * to the object.
 *
 * @param file    entry_ref of the audio file to stream.
 * @param looping \c true to loop the file when EOF is reached.
 * @param device  The BGameSoundDevice to use, or NULL for the process default.
 */
BFileGameSound::BFileGameSound(const entry_ref* file, bool looping,
	BGameSoundDevice* device)
	:
	BStreamingGameSound(device),
	fAudioStream(NULL),
	fStopping(false),
	fLooping(looping),
	fBuffer(NULL),
	fPlayPosition(0),
	fPausing(NULL),
	fPaused(false),
	fPauseGain(1.0)
{
	if (InitCheck() == B_OK)
		SetInitError(Init(new(std::nothrow) BFile(file, B_READ_ONLY)));
}


/**
 * @brief Constructs a BFileGameSound from a filesystem path string.
 *
 * Resolves the path to an entry_ref, opens a read-only BFile, and
 * delegates to Init(BDataIO*). Sets B_ENTRY_NOT_FOUND if the path cannot
 * be resolved.
 *
 * @param file    Null-terminated filesystem path to the audio file.
 * @param looping \c true to loop the file when EOF is reached.
 * @param device  The BGameSoundDevice to use, or NULL for the process default.
 */
BFileGameSound::BFileGameSound(const char* file, bool looping,
	BGameSoundDevice* device)
	:
	BStreamingGameSound(device),
	fAudioStream(NULL),
	fStopping(false),
	fLooping(looping),
	fBuffer(NULL),
	fPlayPosition(0),
	fPausing(NULL),
	fPaused(false),
	fPauseGain(1.0)
{
	if (InitCheck() == B_OK) {
		entry_ref node;

		if (get_ref_for_path(file, &node) != B_OK)
			SetInitError(B_ENTRY_NOT_FOUND);
		else {
			BFile* file = new(std::nothrow) BFile(&node, B_READ_ONLY);
			SetInitError(Init(file));
		}
	}
}


/**
 * @brief Constructs a BFileGameSound from an arbitrary BDataIO stream.
 *
 * Ownership of \a data is transferred to the object; it will be deleted in
 * the destructor.
 *
 * @param data    BDataIO providing audio data (e.g. a BFile or BMemoryIO).
 * @param looping \c true to loop when the stream reaches EOF.
 * @param device  The BGameSoundDevice to use, or NULL for the process default.
 */
BFileGameSound::BFileGameSound(BDataIO* data, bool looping,
	BGameSoundDevice* device)
	:
	BStreamingGameSound(device),
	fAudioStream(NULL),
	fStopping(false),
	fLooping(looping),
	fBuffer(NULL),
	fPlayPosition(0),
	fPausing(NULL),
	fPaused(false),
	fPauseGain(1.0)
{
	if (InitCheck() == B_OK)
		SetInitError(Init(data));
}


/**
 * @brief Destroys the BFileGameSound, releasing the media track and decode buffer.
 *
 * Releases the BMediaTrack via the BMediaFile, deletes the BMediaFile and
 * the _gs_media_tracker struct, frees the decode buffer, and deletes the
 * BDataIO source.
 */
BFileGameSound::~BFileGameSound()
{
	if (fAudioStream != NULL) {
		if (fAudioStream->stream != NULL)
			fAudioStream->file->ReleaseTrack(fAudioStream->stream);

		delete fAudioStream->file;
	}

	delete [] fBuffer;
	delete fAudioStream;
	delete fDataSource;
}


/**
 * @brief Cloning is not supported for file-based streaming sounds.
 *
 * File streams represent a single sequential position and cannot be trivially
 * duplicated.
 *
 * @return Always NULL.
 */
BGameSound*
BFileGameSound::Clone() const
{
	return NULL;
}


/**
 * @brief Starts streaming playback from the beginning of the file.
 *
 * If already playing, StopPlaying() is called first to reset the stream
 * position. Then delegates to BStreamingGameSound::StartPlaying().
 *
 * @return B_OK on success; an error code from BStreamingGameSound::StartPlaying()
 *         on failure.
 */
status_t
BFileGameSound::StartPlaying()
{
	// restart playback if needed
	if (IsPlaying())
		StopPlaying();

	// start playing the file
	return BStreamingGameSound::StartPlaying();
}


/**
 * @brief Stops streaming playback and rewinds the stream to the beginning.
 *
 * Calls BStreamingGameSound::StopPlaying(), then seeks the audio track back
 * to frame 0 and resets all position counters so that the next StartPlaying()
 * begins from the start of the file.
 *
 * @return B_OK on success; the error from BStreamingGameSound::StopPlaying()
 *         otherwise.
 */
status_t
BFileGameSound::StopPlaying()
{
	status_t error = BStreamingGameSound::StopPlaying();

	if (fAudioStream == NULL || fAudioStream->stream == NULL)
		return B_OK;

	// start reading next time from the start of the file
	int64 frame = 0;
	fAudioStream->stream->SeekToFrame(&frame);

	fStopping = false;
	fAudioStream->position = 0;
	fPlayPosition = 0;

	return error;
}


/**
 * @brief Pre-loads the first decode buffer without starting playback.
 *
 * Useful to warm up the decoder before StartPlaying() is called so that
 * the first buffer is immediately available to the mixer.
 *
 * @return Always B_OK.
 */
status_t
BFileGameSound::Preload()
{
	if (!IsPlaying())
		Load();

	return B_OK;
}


/**
 * @brief Fills the mixer's output buffer with decoded audio, applying any
 *        active pause gain ramp.
 *
 * Loops until \a inByteCount bytes have been produced or the sound is fully
 * paused. On each iteration:
 *   - Calls Load() if the decode buffer is exhausted.
 *   - If a pause ramp is active (fPausing != NULL), applies the
 *     typed FillBuffer template to blend towards silence (or back to full
 *     gain). The ramp is deleted when it completes.
 *   - Otherwise performs a direct memcpy from the decode buffer.
 * Any bytes not filled (e.g. because the sound stopped) are set to silence.
 *
 * @param inBuffer    Destination buffer for the mixer; must be at least
 *                    \a inByteCount bytes.
 * @param inByteCount Number of bytes the mixer needs.
 */
void
BFileGameSound::FillBuffer(void* inBuffer, size_t inByteCount)
{
	// Split or combine decoder buffers into mixer buffers
	// fPlayPosition is where we got up to in the input buffer after last call

	char* buffer = (char*)inBuffer;
	size_t out_offset = 0;

	while (inByteCount > 0 && (!fPaused || fPausing != NULL)) {
		if (fPlayPosition == 0 || fPlayPosition >= fBufferSize) {
			if (!Load())
				break;
		}

		size_t bytes = fBufferSize - fPlayPosition;

		if (bytes > inByteCount)
			bytes = inByteCount;

		if (fPausing != NULL) {
			Lock();

			bool rampDone = false;

			switch(Format().format) {
				case gs_audio_format::B_GS_U8:
					rampDone = ::FillBuffer<uint8, 0, 128, UINT8_MAX>(
						fPausing, (uint8*)&buffer[out_offset],
						(uint8*)&fBuffer[fPlayPosition], &bytes);
					break;

				case gs_audio_format::B_GS_S16:
					rampDone = ::FillBuffer<int16, INT16_MIN, 0, INT16_MAX>(
						fPausing, (int16*)&buffer[out_offset],
						(int16*)&fBuffer[fPlayPosition], &bytes);
					break;

				case gs_audio_format::B_GS_S32:
					rampDone = ::FillBuffer<int32, INT32_MIN, 0, INT32_MAX>(
						fPausing, (int32*)&buffer[out_offset],
						(int32*)&fBuffer[fPlayPosition], &bytes);
					break;

				case gs_audio_format::B_GS_F:
					rampDone = ::FillBuffer<float, -1, 0, 1>(
						fPausing, (float*)&buffer[out_offset],
						(float*)&fBuffer[fPlayPosition], &bytes);
					break;
			}

			if (rampDone) {
				delete fPausing;
				fPausing = NULL;
			}

			Unlock();
		} else
			memcpy(&buffer[out_offset], &fBuffer[fPlayPosition], bytes);

		inByteCount -= bytes;
		out_offset += bytes;
		fPlayPosition += bytes;
	}

	// Fill the rest with silence
	if (inByteCount > 0) {
		int middle = 0;
		if (Format().format == gs_audio_format::B_GS_U8)
			middle = 128;
		memset(&buffer[out_offset], middle, inByteCount);
	}
}


/**
 * @brief Reserved virtual dispatch hook; not implemented.
 * @param selector Selector identifying the desired operation.
 * @param data     Pointer to operation-specific data.
 * @return Always B_ERROR.
 */
status_t
BFileGameSound::Perform(int32 selector, void* data)
{
	return B_ERROR;
}


/**
 * @brief Pauses or resumes playback, optionally with a gain ramp.
 *
 * If \a rampTime > 100 ms, a linear gain ramp is created that fades to 0.0
 * (pause) or back to 1.0 (resume) over \a rampTime microseconds. During the
 * ramp, FillBuffer() continues to run but applies the decreasing/increasing
 * gain. Any previously active ramp is cancelled first.
 *
 * The Lock()/Unlock() pair ensures that FillBuffer() (which runs on the
 * Media Kit thread) does not read fPausing while it is being replaced.
 *
 * @param isPaused \c true to pause; \c false to resume.
 * @param rampTime Duration of the gain ramp in microseconds.
 *                 Values <= 100000 result in an immediate state change.
 * @return B_OK on success; EALREADY if the sound is already in the
 *         requested state.
 */
status_t
BFileGameSound::SetPaused(bool isPaused, bigtime_t rampTime)
{
	if (fPaused == isPaused)
		return EALREADY;

	Lock();

	// Clear any old ramping
	delete fPausing;
	fPausing = NULL;

	if (rampTime > 100000) {
		// Setup for ramping
		if (isPaused) {
			fPausing = InitRamp(&fPauseGain, 0.0,
					Format().frame_rate, rampTime);
		} else {
			fPausing = InitRamp(&fPauseGain, 1.0,
					Format().frame_rate, rampTime);
		}
	}

	fPaused = isPaused;
	Unlock();

	return B_OK;
}


/**
 * @brief Returns the current pause state of the sound.
 *
 * Distinguishes between fully paused, in the middle of a pause/resume ramp,
 * and fully playing.
 *
 * @return B_PAUSED if the sound is paused and no ramp is active;
 *         B_PAUSE_IN_PROGRESS if a gain ramp is currently running;
 *         B_NOT_PAUSED if the sound is playing normally.
 */
int32
BFileGameSound::IsPaused()
{
	if (fPausing)
		return B_PAUSE_IN_PROGRESS;

	if (fPaused)
		return B_PAUSED;

	return B_NOT_PAUSED;
}


/**
 * @brief Opens the data source, sets up BMediaFile/BMediaTrack, and registers
 *        the streaming buffer with the device.
 *
 * Steps performed:
 *   1. Stores \a data as fDataSource; allocates a _gs_media_tracker.
 *   2. Opens a BMediaFile on the data source and retrieves track 0.
 *   3. Verifies that the track contains audio.
 *   4. Negotiates a raw-audio decoded format with the track.
 *   5. Translates the media format to a gs_audio_format.
 *   6. Allocates fBuffer at 2x the larger of the codec buffer size and the
 *      device's preferred buffer size, pre-filled with silence.
 *   7. Calls BGameSoundDevice::CreateBuffer() to connect the streaming buffer
 *      to the mixer, then BGameSound::Init() to record the gs_id.
 *
 * @param data BDataIO providing audio data; ownership is transferred.
 * @return B_OK on success; B_NO_MEMORY if allocation fails; B_MEDIA_BAD_FORMAT
 *         if the track is not audio or decoding negotiation fails; another
 *         Media Kit error code on other failures.
 */
status_t
BFileGameSound::Init(BDataIO* data)
{
	fDataSource = data;
	if (fDataSource == NULL)
		return B_NO_MEMORY;

	fAudioStream = new(std::nothrow) _gs_media_tracker;
	if (fAudioStream == NULL)
		return B_NO_MEMORY;

	memset(fAudioStream, 0, sizeof(_gs_media_tracker));
	fAudioStream->file = new(std::nothrow) BMediaFile(data);
	if (fAudioStream->file == NULL) {
		delete fAudioStream;
		fAudioStream = NULL;
		return B_NO_MEMORY;
	}

	status_t error = fAudioStream->file->InitCheck();
	if (error != B_OK)
		return error;

	fAudioStream->stream = fAudioStream->file->TrackAt(0);

	// is this is an audio file?
	media_format playFormat;
	if ((error = fAudioStream->stream->EncodedFormat(&playFormat)) != B_OK) {
		fAudioStream->file->ReleaseTrack(fAudioStream->stream);
		fAudioStream->stream = NULL;
		return error;
	}

	if (!playFormat.IsAudio()) {
		fAudioStream->file->ReleaseTrack(fAudioStream->stream);
		fAudioStream->stream = NULL;
		return B_MEDIA_BAD_FORMAT;
	}

	gs_audio_format dformat = Device()->Format();

	// request the format we want the sound
	playFormat.Clear();
	playFormat.type = B_MEDIA_RAW_AUDIO;
	if (fAudioStream->stream->DecodedFormat(&playFormat) != B_OK) {
		fAudioStream->file->ReleaseTrack(fAudioStream->stream);
		fAudioStream->stream = NULL;
		return B_MEDIA_BAD_FORMAT;
	}

	// translate the format into a "GameKit" friendly one
	gs_audio_format gsformat;
	media_to_gs_format(&gsformat, &playFormat.u.raw_audio);

	// Since the buffer sized read from the file is most likely differnt
	// then the buffer used by the audio mixer, we must allocate a buffer
	// large enough to hold the largest request.
	fBufferSize = gsformat.buffer_size;
	if (fBufferSize < dformat.buffer_size)
		fBufferSize = dformat.buffer_size;

	// create the buffer
	int middle = 0;
	if (gsformat.format == gs_audio_format::B_GS_U8)
		middle = 128;
	fBuffer = new char[fBufferSize * 2];
	memset(fBuffer, middle, fBufferSize * 2);

	fFrameSize = gsformat.channel_count * get_sample_size(gsformat.format);
	fAudioStream->frames = fAudioStream->stream->CountFrames();

	// Ask the device to attach our sound to it
	gs_id sound;
	error = Device()->CreateBuffer(&sound, this, &gsformat);
	if (error != B_OK)
		return error;

	return BGameSound::Init(sound);
}


/**
 * @brief Reads the next chunk of decoded audio frames into fBuffer.
 *
 * Calls BMediaTrack::ReadFrames() to decode the next batch of frames. If the
 * read returns zero bytes (EOF), the behaviour depends on the fLooping flag:
 *   - Looping: seeks back to frame 0 and returns true (caller should retry).
 *   - Not looping: calls StopPlaying() and returns false.
 *
 * fPlayPosition is reset to 0 after each successful load so that FillBuffer()
 * starts reading from the beginning of the newly decoded chunk.
 *
 * @return \c true if at least one frame was decoded (or a loop seek succeeded);
 *         \c false if playback should stop.
 */
bool
BFileGameSound::Load()
{
	if (fAudioStream == NULL || fAudioStream->stream == NULL)
		return false;

	// read a new buffer
	int64 frames = 0;
	fAudioStream->stream->ReadFrames(fBuffer, &frames);
	fBufferSize = frames * fFrameSize;
	fPlayPosition = 0;

	if (fBufferSize <= 0) {
		// EOF
		if (fLooping) {
			// start reading next time from the start of the file
			int64 frame = 0;
			fAudioStream->stream->SeekToFrame(&frame);
		} else {
			StopPlaying();
			return false;
		}
	}

	return true;
}


/**
 * @brief Stub read method; not implemented.
 *
 * Provided as an extension point for subclasses; always returns false.
 *
 * @param buffer Unused destination buffer.
 * @param bytes  Unused byte count.
 * @return Always \c false.
 */
bool
BFileGameSound::Read(void* buffer, size_t bytes)
{
	return false;
}


/* unimplemented for protection of the user:
 *
 * BFileGameSound::BFileGameSound()
 * BFileGameSound::BFileGameSound(const BFileGameSound &)
 * BFileGameSound &BFileGameSound::operator=(const BFileGameSound &)
 */


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_0(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_1(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_2(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_3(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_4(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_5(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_6(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_7(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_8(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_9(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_10(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_11(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_12(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_13(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_14(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_15(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_16(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_17(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_18(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_19(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_20(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_21(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_22(int32 arg, ...)
{
	return B_ERROR;
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t
BFileGameSound::_Reserved_BFileGameSound_23(int32 arg, ...)
{
	return B_ERROR;
}
