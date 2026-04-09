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
 *   Copyright (c) 2001-2002, Haiku
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 */

/**
 * @file GameSoundBuffer.cpp
 * @brief Internal sound buffer classes managed by the GameSoundDevice.
 *
 * Provides three concrete internal buffer types used by the GameKit:
 *   - GameSoundBuffer: abstract base that owns a GameProducer media node,
 *     applies per-frame gain and stereo pan (with optional ramping), and
 *     connects the producer to the system audio mixer.
 *   - SimpleSoundBuffer: holds a fixed block of PCM data and supports
 *     looping playback.
 *   - StreamingSoundBuffer: delegates FillBuffer() calls to the
 *     BStreamingGameSound hook object.
 */


#include "GameSoundBuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <MediaRoster.h>
#include <MediaAddOn.h>
#include <MediaTheme.h>
#include <TimeSource.h>
#include <BufferGroup.h>

#include "GameProducer.h"
#include "GameSoundDevice.h"
#include "StreamingGameSound.h"
#include "GSUtility.h"

// Sound Buffer Utility functions ----------------------------------------

/**
 * @brief Applies stereo pan and gain to a single interleaved stereo frame.
 *
 * The template parameters define the numeric type and its value range so
 * that the same logic works for U8, S16, S32, and float sample formats.
 * The \a middle value is subtracted before scaling so that the centre point
 * of the type's range maps to silence, which is required for unsigned types.
 *
 * @tparam T       Sample type (uint8, int16, int32, or float).
 * @tparam min     Minimum representable value for type T.
 * @tparam middle  Mid-point of the value range (0 for signed, 128 for U8).
 * @tparam max     Maximum representable value for type T.
 * @param  data    Pointer to the interleaved stereo sample buffer.
 * @param  index   Frame index (each frame occupies two consecutive samples).
 * @param  pan     Two-element array: pan[0] = right channel weight,
 *                 pan[1] = left channel weight (both incorporate gain).
 */
template<typename T, int32 min, int32 middle, int32 max>
static inline void
ApplyMod(T* data, int64 index, float* pan)
{
	data[index * 2] = clamp<T, min, max>(float(data[index * 2] - middle)
		* pan[0] + middle);
	data[index * 2 + 1] = clamp<T, min, max>(float(data[index * 2 + 1] - middle)
		* pan[1] + middle);
}


// GameSoundBuffer -------------------------------------------------------

/**
 * @brief Constructs a GameSoundBuffer and its associated GameProducer node.
 *
 * Initialises gain to 1.0 (full) and pan to 0.0 (centre). No ramps are
 * active at construction time. The GameProducer node is created but not
 * yet registered with the Media Roster; call Connect() to do that.
 *
 * @param format Audio format (sample type, channel count, frame rate,
 *               byte order) that the producer node will advertise.
 */
GameSoundBuffer::GameSoundBuffer(const gs_audio_format * format)
	:
	fLooping(false),
	fIsConnected(false),
	fIsPlaying(false),
	fGain(1.0),
	fPan(0.0),
	fPanLeft(1.0),
	fPanRight(1.0),
	fGainRamp(NULL),
	fPanRamp(NULL)
{
	fConnection = new Connection;
	fNode = new GameProducer(this, format);

	fFrameSize = get_sample_size(format->format) * format->channel_count;

	fFormat = *format;
}


/**
 * @brief Destroys the GameSoundBuffer, disconnecting and releasing media nodes.
 *
 * If the producer is connected to the system mixer, the connection is torn
 * down and both the producer and consumer node references are released.
 * Any active gain or pan ramps are also deleted.
 *
 * @note Playback must be stopped before the destructor runs; otherwise a
 *       fatal error may occur if the buffer is still referenced by a subclass.
 */
// Play must stop before the distructor is called; otherwise, a fatal
// error occures if the playback is in a subclass.
GameSoundBuffer::~GameSoundBuffer()
{
	BMediaRoster* roster = BMediaRoster::Roster();

	if (fIsConnected) {
		// Ordinarily we'd stop *all* of the nodes in the chain at this point.
		// However, one of the nodes is the System Mixer, and stopping the Mixer
		// is a Bad Idea (tm). So, we just disconnect from it, and release our
		// references to the nodes that we're using.  We *are* supposed to do
		// that even for global nodes like the Mixer.
		roster->Disconnect(fConnection->producer.node, fConnection->source,
			fConnection->consumer.node, fConnection->destination);

		roster->ReleaseNode(fConnection->producer);
		roster->ReleaseNode(fConnection->consumer);
	}

	delete fGainRamp;
	delete fPanRamp;

	delete fConnection;
	delete fNode;
}


/**
 * @brief Returns the audio format used by this buffer.
 * @return A const reference to the gs_audio_format struct stored at
 *         construction time.
 */
const gs_audio_format &
GameSoundBuffer::Format() const
{
	return fFormat;
}


/**
 * @brief Returns whether this buffer loops on completion.
 * @return \c true if the sound restarts when it reaches the end of its data.
 */
bool
GameSoundBuffer::IsLooping() const
{
	return fLooping;
}


/**
 * @brief Enables or disables looped playback.
 * @param looping Pass \c true to loop, \c false to play once and stop.
 */
void
GameSoundBuffer::SetLooping(bool looping)
{
	fLooping = looping;
}


/**
 * @brief Returns the current playback gain.
 * @return Gain value in the range [0.0, 1.0].
 */
float
GameSoundBuffer::Gain() const
{
	return fGain;
}


/**
 * @brief Sets the playback gain, optionally ramping to the target over time.
 *
 * If \a duration is longer than 100 ms a linear ramp is created so that
 * the gain changes gradually; otherwise the gain is set immediately.
 * Any previously active gain ramp is cancelled first.
 *
 * @param gain     Target gain in the range [0.0, 1.0].
 * @param duration Time in microseconds over which to ramp to the new gain.
 *                 Values <= 100000 result in an immediate change.
 * @return B_OK on success; B_BAD_VALUE if \a gain is outside [0.0, 1.0].
 */
status_t
GameSoundBuffer::SetGain(float gain, bigtime_t duration)
{
	if (gain < 0.0 || gain > 1.0)
		return B_BAD_VALUE;

	delete fGainRamp;
	fGainRamp = NULL;

	if (duration > 100000)
		fGainRamp  = InitRamp(&fGain, gain, fFormat.frame_rate, duration);
	else
		fGain = gain;

	return B_OK;
}


/**
 * @brief Returns the current stereo pan position.
 * @return Pan value in the range [-1.0, 1.0]; -1 is full left, +1 is full right.
 */
float
GameSoundBuffer::Pan() const
{
	return fPan;
}


/**
 * @brief Sets the stereo pan position, optionally ramping to the target over time.
 *
 * If \a duration is longer than 100 ms a linear ramp is created; otherwise
 * the pan is applied immediately, recalculating the per-channel weights
 * (fPanLeft / fPanRight) inline. Any previously active pan ramp is cancelled.
 *
 * @param pan      Target pan in the range [-1.0, 1.0].
 * @param duration Time in microseconds to ramp. Values <= 100000 are immediate.
 * @return B_OK on success; B_BAD_VALUE if \a pan is outside [-1.0, 1.0].
 */
status_t
GameSoundBuffer::SetPan(float pan, bigtime_t duration)
{
	if (pan < -1.0 || pan > 1.0)
		return B_BAD_VALUE;

	delete fPanRamp;
	fPanRamp = NULL;

	if (duration < 100000) {
		fPan = pan;

		if (fPan < 0.0) {
			fPanLeft = 1.0;
			fPanRight = 1.0 + fPan;
		} else {
			fPanRight = 1.0;
			fPanLeft = 1.0 - fPan;
		}
	} else
		fPanRamp = InitRamp(&fPan, pan, fFormat.frame_rate, duration);

	return B_OK;
}


/**
 * @brief Reads a set of sound attributes (gain, pan, looping).
 *
 * Iterates over the supplied attribute array and fills in the \c value and
 * \c duration fields for each recognised attribute type. Unrecognised
 * attributes receive a value of 0.0 and a duration of 0.
 *
 * @param attributes     Array of gs_attribute structs to fill in.
 * @param attributeCount Number of entries in \a attributes.
 * @return Always B_OK.
 */
status_t
GameSoundBuffer::GetAttributes(gs_attribute * attributes,
	size_t attributeCount)
{
	for (size_t i = 0; i < attributeCount; i++) {
		switch (attributes[i].attribute) {
			case B_GS_GAIN:
				attributes[i].value = fGain;
				if (fGainRamp)
					attributes[i].duration = fGainRamp->duration;
				break;

			case B_GS_PAN:
				attributes[i].value = fPan;
				if (fPanRamp)
					attributes[i].duration = fPanRamp->duration;
				break;

			case B_GS_LOOPING:
				attributes[i].value = (fLooping) ? -1.0 : 0.0;
				attributes[i].duration = bigtime_t(0);
				break;

			default:
				attributes[i].value = 0.0;
				attributes[i].duration = bigtime_t(0);
				break;
		}
	}

	return B_OK;
}


/**
 * @brief Applies a set of sound attributes (gain, pan, looping).
 *
 * Iterates over the supplied attribute array and calls the appropriate
 * setter for each recognised attribute type. Unrecognised attributes are
 * silently ignored. The return value reflects the last error encountered.
 *
 * @param attributes     Array of gs_attribute structs to apply.
 * @param attributeCount Number of entries in \a attributes.
 * @return B_OK on full success; the error code from the last failing setter
 *         otherwise.
 */
status_t
GameSoundBuffer::SetAttributes(gs_attribute * attributes,
	size_t attributeCount)
{
	status_t error = B_OK;

	for (size_t i = 0; i < attributeCount; i++) {
		switch (attributes[i].attribute) {
			case B_GS_GAIN:
				error = SetGain(attributes[i].value, attributes[i].duration);
				break;

			case B_GS_PAN:
				error = SetPan(attributes[i].value, attributes[i].duration);
				break;

			case B_GS_LOOPING:
				fLooping = bool(attributes[i].value);
				break;

			default:
				break;
		}
	}

	return error;
}


/**
 * @brief Fills \a data with audio frames and applies per-frame pan and gain.
 *
 * Calls the virtual FillBuffer() to obtain raw PCM data, then walks each
 * frame applying stereo pan and gain via the ApplyMod template for the
 * current sample format. UpdateMods() is called after each frame so that
 * active ramps advance one frame at a time. Mono sounds skip the per-frame
 * processing (pan and gain are not currently applied to mono output).
 *
 * @param data   Destination buffer of at least \a frames * frame-size bytes.
 * @param frames Number of audio frames to produce.
 */
void
GameSoundBuffer::Play(void * data, int64 frames)
{
	// Mh... should we add some locking?
	if (!fIsPlaying)
		return;

	if (fFormat.channel_count == 2) {
		float pan[2];
		pan[0] = fPanRight * fGain;
		pan[1] = fPanLeft * fGain;

		FillBuffer(data, frames);

		switch (fFormat.format) {
			case gs_audio_format::B_GS_U8:
			{
				for (int64 i = 0; i < frames; i++) {
					ApplyMod<uint8, 0, 128, UINT8_MAX>((uint8*)data, i, pan);
					UpdateMods();
				}

				break;
			}

			case gs_audio_format::B_GS_S16:
			{
				for (int64 i = 0; i < frames; i++) {
					ApplyMod<int16, INT16_MIN, 0, INT16_MAX>((int16*)data, i,
						pan);
					UpdateMods();
				}

				break;
			}

			case gs_audio_format::B_GS_S32:
			{
				for (int64 i = 0; i < frames; i++) {
					ApplyMod<int32, INT32_MIN, 0, INT32_MAX>((int32*)data, i,
						pan);
					UpdateMods();
				}

				break;
			}

			case gs_audio_format::B_GS_F:
			{
				for (int64 i = 0; i < frames; i++) {
					ApplyMod<float, -1, 0, 1>((float*)data, i, pan);
					UpdateMods();
				}

				break;
			}
		}
	} else if (fFormat.channel_count == 1) {
		// FIXME the output should be stereo, and we could pan mono sounds
		// here. But currently the output has the same number of channels as
		// the sound and we can't do this.
		// FIXME also, we don't handle the gain here.
		FillBuffer(data, frames);
	} else
		debugger("Invalid number of channels.");

}


/**
 * @brief Advances active gain and pan ramps by one frame.
 *
 * Called once per audio frame from Play(). When a ramp finishes
 * (ChangeRamp() returns true), the ramp object is deleted and the
 * corresponding field is set to NULL. For the pan ramp the per-channel
 * left/right weights are also recalculated after each step.
 */
void
GameSoundBuffer::UpdateMods()
{
	// adjust the gain if needed
	if (fGainRamp) {
		if (ChangeRamp(fGainRamp)) {
			delete fGainRamp;
			fGainRamp = NULL;
		}
	}

	// adjust the ramp if needed
	if (fPanRamp) {
		if (ChangeRamp(fPanRamp)) {
			delete fPanRamp;
			fPanRamp = NULL;
		} else {
			if (fPan < 0.0) {
				fPanLeft = 1.0;
				fPanRight = 1.0 + fPan;
			} else {
				fPanRight = 1.0;
				fPanLeft = 1.0 - fPan;
			}
		}
	}
}


/**
 * @brief Resets gain, pan, and looping to their default values.
 *
 * Sets gain to 1.0, pan to 0.0 (centre), and clears the looping flag.
 * Any active gain or pan ramps are deleted.
 */
void
GameSoundBuffer::Reset()
{
	fGain = 1.0;
	delete fGainRamp;
	fGainRamp = NULL;

	fPan = 0.0;
	fPanLeft = 1.0;
	fPanRight = 1.0;

	delete fPanRamp;
	fPanRamp = NULL;

	fLooping = false;
}


/**
 * @brief Registers the GameProducer node and connects it to the given consumer.
 *
 * Registers fNode with the Media Roster, obtains a shared reference to the
 * producer, sets the time source, enumerates free outputs and inputs, then
 * calls BMediaRoster::Connect() to establish a raw-audio connection to the
 * supplied consumer (typically the system mixer).
 *
 * @param consumer Pointer to the media_node that will consume the audio
 *                 (usually obtained via BMediaRoster::GetAudioMixer()).
 * @return B_OK on success; a Media Kit error code otherwise.
 */
status_t
GameSoundBuffer::Connect(media_node * consumer)
{
	BMediaRoster* roster = BMediaRoster::Roster();
	status_t err = roster->RegisterNode(fNode);

	if (err != B_OK)
		return err;

	// make sure the Media Roster knows that we're using the node
	err = roster->GetNodeFor(fNode->Node().node, &fConnection->producer);

	if (err != B_OK)
		return err;

	// connect to the mixer
	fConnection->consumer = *consumer;

	// set the producer's time source to be the "default" time source, which
	// the Mixer uses too.
	err = roster->GetTimeSource(&fConnection->timeSource);
	if (err != B_OK)
		return err;

	err = roster->SetTimeSourceFor(fConnection->producer.node,
		fConnection->timeSource.node);
	if (err != B_OK)
		return err;
	// got the nodes; now we find the endpoints of the connection
	media_input mixerInput;
	media_output soundOutput;
	int32 count = 1;
	err = roster->GetFreeOutputsFor(fConnection->producer, &soundOutput, 1,
		&count);

	if (err != B_OK)
		return err;
	count = 1;
	err = roster->GetFreeInputsFor(fConnection->consumer, &mixerInput, 1,
		&count);
	if (err != B_OK)
		return err;

	// got the endpoints; now we connect it!
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	format.u.raw_audio = media_raw_audio_format::wildcard;
	err = roster->Connect(soundOutput.source, mixerInput.destination, &format,
		&soundOutput, &mixerInput);
	if (err != B_OK)
		return err;

	// the inputs and outputs might have been reassigned during the
	// nodes' negotiation of the Connect().  That's why we wait until
	// after Connect() finishes to save their contents.
	fConnection->format = format;
	fConnection->source = soundOutput.source;
	fConnection->destination = mixerInput.destination;

	fIsConnected = true;
	return B_OK;
}


/**
 * @brief Starts the GameProducer node running so that audio is delivered.
 *
 * Retrieves the current downstream latency and starts the producer node
 * at a future time that allows the pipeline to fill before playback begins.
 * Returns EALREADY if the buffer is already playing.
 *
 * @return B_OK on success; EALREADY if already playing; a Media Kit error
 *         code on failure.
 */
status_t
GameSoundBuffer::StartPlaying()
{
	if (fIsPlaying)
		return EALREADY;

	BMediaRoster* roster = BMediaRoster::Roster();
	BTimeSource* source = roster->MakeTimeSourceFor(fConnection->producer);

	// make sure we give the producer enough time to run buffers through
	// the node chain, otherwise it'll start up already late
	bigtime_t latency = 0;
	status_t status = roster->GetLatencyFor(fConnection->producer, &latency);
	if (status == B_OK) {
		status = roster->StartNode(fConnection->producer,
			source->Now() + latency);
	}
	source->Release();

	fIsPlaying = true;

	return status;
}


/**
 * @brief Stops the GameProducer node and resets buffer state.
 *
 * Issues a synchronous stop request to the producer, then calls Reset()
 * to clear gain/pan/looping state. Returns EALREADY if not currently playing.
 *
 * @return B_OK on success; EALREADY if not currently playing.
 */
status_t
GameSoundBuffer::StopPlaying()
{
	if (!fIsPlaying)
		return EALREADY;

	BMediaRoster* roster = BMediaRoster::Roster();
	roster->StopNode(fConnection->producer, 0, true);
		// synchronous stop

	Reset();
	fIsPlaying = false;

	return B_OK;
}


/**
 * @brief Returns whether the buffer is currently playing.
 * @return \c true if the producer node is running and sending audio.
 */
bool
GameSoundBuffer::IsPlaying()
{
	return fIsPlaying;
}


// SimpleSoundBuffer ------------------------------------------------------

/**
 * @brief Constructs a SimpleSoundBuffer from a pre-filled PCM data block.
 *
 * The buffer takes ownership of \a data (the caller must not free it).
 * The total byte size is computed as \a frames * frame_size, where frame_size
 * is derived from the format.
 *
 * @param format Audio format describing the sample type and channel count.
 * @param data   Pointer to the raw PCM audio data; ownership is transferred.
 * @param frames Number of audio frames contained in \a data.
 */
SimpleSoundBuffer::SimpleSoundBuffer(const gs_audio_format * format,
	const void * data, int64 frames)
	:
	GameSoundBuffer(format),
	fPosition(0)
{
	fBufferSize = frames * fFrameSize;
	fBuffer = (char*)data;
}


/**
 * @brief Destroys the SimpleSoundBuffer and frees the PCM data block.
 */
SimpleSoundBuffer::~SimpleSoundBuffer()
{
	delete [] fBuffer;
}


/**
 * @brief Resets gain/pan/looping state and rewinds the read position to zero.
 *
 * Delegates to GameSoundBuffer::Reset() and then resets fPosition so that
 * the next call to FillBuffer() starts from the beginning of the PCM data.
 */
void
SimpleSoundBuffer::Reset()
{
	GameSoundBuffer::Reset();
	fPosition = 0;
}


/**
 * @brief Copies PCM frames into \a data from the internal buffer.
 *
 * Reads sequentially through fBuffer, wrapping back to the start when
 * looping is enabled. If the buffer is exhausted and looping is disabled,
 * the remainder of \a data is filled with silence (0 for signed formats,
 * 128 for B_GS_U8).
 *
 * @param data   Destination buffer; must hold at least \a frames * frame-size bytes.
 * @param frames Number of audio frames to fill.
 */
void
SimpleSoundBuffer::FillBuffer(void * data, int64 frames)
{
	char * buffer = (char*)data;
	size_t bytes = fFrameSize * frames;

	if (fPosition + bytes >= fBufferSize) {
		if (fPosition < fBufferSize) {
			// copy the remaining frames
			size_t remainder = fBufferSize - fPosition;
			memcpy(buffer, &fBuffer[fPosition], remainder);

			bytes -= remainder;
			buffer += remainder;
		}

		if (fLooping) {
			// restart the sound from the beginning
			memcpy(buffer, fBuffer, bytes);
			fPosition = bytes;
			bytes = 0;
		} else {
			fPosition = fBufferSize;
		}

		if (bytes > 0) {
			// Fill the rest with silence
			int middle = 0;
			if (fFormat.format == gs_audio_format::B_GS_U8)
				middle = 128;
			memset(buffer, middle, bytes);
		}
	} else {
		memcpy(buffer, &fBuffer[fPosition], bytes);
		fPosition += bytes;
	}
}


// StreamingSoundBuffer ------------------------------------------------------

/**
 * @brief Constructs a StreamingSoundBuffer connected to a BStreamingGameSound.
 *
 * If both \a inBufferFrameCount and \a inBufferCount are non-zero, a
 * BBufferGroup is created with the specified geometry and handed to the
 * GameProducer node via SetBufferGroup().
 *
 * @param format              Audio format for the producer node.
 * @param streamHook          Pointer to the BStreamingGameSound object whose
 *                            FillBuffer() will be called to supply audio data.
 * @param inBufferFrameCount  Number of frames per buffer; 0 = use default.
 * @param inBufferCount       Number of buffers in the group; 0 = use default.
 */
StreamingSoundBuffer::StreamingSoundBuffer(const gs_audio_format * format,
	const void * streamHook, size_t inBufferFrameCount, size_t inBufferCount)
	:
	GameSoundBuffer(format),
	fStreamHook(const_cast<void *>(streamHook))
{
	if (inBufferFrameCount != 0 && inBufferCount  != 0) {
		BBufferGroup *bufferGroup
			= new BBufferGroup(inBufferFrameCount * fFrameSize, inBufferCount);
		fNode->SetBufferGroup(fConnection->source, bufferGroup);
	}
}


/**
 * @brief Destroys the StreamingSoundBuffer.
 */
StreamingSoundBuffer::~StreamingSoundBuffer()
{
}


/**
 * @brief Delegates buffer filling to the associated BStreamingGameSound object.
 *
 * Casts fStreamHook to BStreamingGameSound* and calls its FillBuffer()
 * method with the byte count derived from \a frames and the frame size.
 *
 * @param buffer Destination buffer to fill with audio data.
 * @param frames Number of audio frames requested.
 */
void
StreamingSoundBuffer::FillBuffer(void * buffer, int64 frames)
{
	BStreamingGameSound* object = (BStreamingGameSound*)fStreamHook;

	size_t bytes = fFrameSize * frames;
	object->FillBuffer(buffer, bytes);
}
