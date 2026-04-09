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
 * Incorporates work from the Haiku project, originally copyrighted:
 *   Copyright 2001-2002, Haiku. All rights reserved.
 *   Author: Christopher ML Zumwalt May (zummy@users.sf.net)
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file GameSoundBuffer.h
 *  @brief Internal audio buffer abstractions used by the Game Sound Kit mixer.
 */

#ifndef _GAMESOUNDBUFFER_H
#define _GAMESOUNDBUFFER_H


#include <GameSoundDefs.h>
#include <MediaDefs.h>
#include <MediaNode.h>

/** @brief Private attribute index reserved for the paused state flag. */
const int32 kPausedAttribute = B_GS_FIRST_PRIVATE_ATTRIBUTE;

class GameProducer;
struct _gs_ramp;

/** @brief Describes the media graph connection used by a GameSoundBuffer. */
struct Connection
{
	media_node producer;      /**< The producer node feeding this connection. */
	media_node consumer;      /**< The consumer node receiving audio. */
	media_source source;      /**< Source endpoint of the connection. */
	media_destination destination; /**< Destination endpoint of the connection. */
	media_format format;      /**< Negotiated audio format for this connection. */
	media_node timeSource;    /**< Time source node used for synchronisation. */
};


/** @brief Abstract base class for all internal game sound buffers; manages gain, pan, looping, and mixing. */
class GameSoundBuffer {
public:

	/** @brief Constructs a GameSoundBuffer with the given audio format.
	 *  @param format Pointer to the desired gs_audio_format.
	 */
									GameSoundBuffer(const gs_audio_format* format);

	/** @brief Destroys the buffer and disconnects from the media graph if needed. */
	virtual							~GameSoundBuffer();

	/** @brief Connects this buffer to a media consumer node.
	 *  @param consumer Pointer to the destination media node.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t				Connect(media_node * consumer);

	/** @brief Starts playback through the media graph connection.
	 *  @return B_OK on success, or an error code.
	 */
			status_t				StartPlaying();

	/** @brief Stops playback through the media graph connection.
	 *  @return B_OK on success, or an error code.
	 */
			status_t				StopPlaying();

	/** @brief Returns whether the buffer is currently playing.
	 *  @return True if playing, false otherwise.
	 */
			bool					IsPlaying();

	/** @brief Mixes the specified number of frames into the given output buffer.
	 *  @param data   Destination buffer for mixed PCM data.
	 *  @param frames Number of frames to mix.
	 */
			void					Play(void * data, int64 frames);

	/** @brief Advances all active gain/pan ramps by one step. */
			void					UpdateMods();

	/** @brief Resets playback position and state to the beginning. */
	virtual void					Reset();

	/** @brief Returns a pointer to the raw audio data, or NULL if not applicable.
	 *  @return Pointer to raw PCM data, or NULL.
	 */
	virtual void * 					Data() { return NULL; }

	/** @brief Returns the audio format used by this buffer.
	 *  @return Reference to the gs_audio_format descriptor.
	 */
			const gs_audio_format &	Format() const;

	/** @brief Returns whether looped playback is enabled.
	 *  @return True if looping, false otherwise.
	 */
			bool					IsLooping() const;

	/** @brief Enables or disables looped playback.
	 *  @param loop True to enable looping, false to disable.
	 */
			void					SetLooping(bool loop);

	/** @brief Returns the current gain value.
	 *  @return Current gain in the range [0.0, 1.0].
	 */
			float					Gain() const;

	/** @brief Sets the gain, optionally ramped over time.
	 *  @param gain     Target gain value.
	 *  @param duration Ramp duration in microseconds; 0 applies immediately.
	 *  @return B_OK on success, or an error code.
	 */
			status_t				SetGain(float gain, bigtime_t duration);

	/** @brief Returns the current stereo pan value.
	 *  @return Pan value in the range [-1.0, 1.0].
	 */
			float					Pan() const;

	/** @brief Sets the stereo pan, optionally ramped over time.
	 *  @param pan      Target pan value (-1.0 = left, 0.0 = center, 1.0 = right).
	 *  @param duration Ramp duration in microseconds; 0 applies immediately.
	 *  @return B_OK on success, or an error code.
	 */
			status_t				SetPan(float pan, bigtime_t duration);

	/** @brief Retrieves one or more sound attributes.
	 *  @param attributes     Array of gs_attribute descriptors to fill.
	 *  @param attributeCount Number of elements in the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t				GetAttributes(gs_attribute * attributes,
												  size_t attributeCount);

	/** @brief Sets one or more sound attributes.
	 *  @param attributes     Array of gs_attribute descriptors.
	 *  @param attributeCount Number of elements in the array.
	 *  @return B_OK on success, or an error code.
	 */
	virtual status_t				SetAttributes(gs_attribute * attributes,
												  size_t attributeCount);
protected:

	/** @brief Subclass hook: fill the output buffer with the requested number of frames.
	 *  @param data   Destination buffer for PCM data.
	 *  @param frames Number of frames to produce.
	 */
	virtual void					FillBuffer(void * data, int64 frames) = 0;

			gs_audio_format			fFormat;    /**< Audio format descriptor for this buffer. */
			bool					fLooping;   /**< Whether looped playback is active. */

			size_t					fFrameSize; /**< Bytes per audio frame (channels * sample size). */

			Connection *			fConnection; /**< Media graph connection state. */
			GameProducer *			fNode;       /**< Associated GameProducer node. */

private:

			bool					fIsConnected; /**< True if a media graph connection is established. */
			bool					fIsPlaying;   /**< True if audio is currently playing. */

			float					fGain;
			float					fPan, fPanLeft, fPanRight;
			_gs_ramp*				fGainRamp;
			_gs_ramp*				fPanRamp;
};


/** @brief A GameSoundBuffer backed by a static in-memory PCM sample. */
class SimpleSoundBuffer : public GameSoundBuffer {
public:
	/** @brief Constructs a SimpleSoundBuffer from a raw PCM data block.
	 *  @param format Pointer to the audio format descriptor.
	 *  @param data   Pointer to the raw PCM sample data.
	 *  @param frames Number of audio frames in the data block.
	 */
								SimpleSoundBuffer(const gs_audio_format* format,
													const void * data,
													int64 frames = 0);

	/** @brief Destroys the SimpleSoundBuffer and frees the internal copy of the data. */
	virtual						~SimpleSoundBuffer();

	/** @brief Returns a pointer to the internal PCM data buffer.
	 *  @return Pointer to the raw PCM data.
	 */
	virtual void *				Data() { return fBuffer; }

	/** @brief Resets the playback position to the start of the buffer. */
	virtual void				Reset();

protected:

	/** @brief Fills the output with samples from the internal buffer, advancing the position.
	 *  @param data   Destination buffer for PCM data.
	 *  @param frames Number of frames to copy.
	 */
	virtual	void				FillBuffer(void * data, int64 frames);

private:
			char *				fBuffer;
			size_t				fBufferSize;
			size_t				fPosition;
};


/** @brief A GameSoundBuffer backed by a streaming hook function. */
class StreamingSoundBuffer : public GameSoundBuffer {
public:
	/** @brief Constructs a StreamingSoundBuffer with the given streaming hook.
	 *  @param format             Pointer to the audio format descriptor.
	 *  @param streamHook         Pointer to the streaming hook (cast to void*).
	 *  @param inBufferFrameCount Number of frames per fill call.
	 *  @param inBufferCount      Number of ring-buffer pages.
	 */
								StreamingSoundBuffer(const gs_audio_format * format,
													 const void * streamHook,
													 size_t inBufferFrameCount,
													 size_t inBufferCount);

	/** @brief Destroys the StreamingSoundBuffer. */
	virtual						~StreamingSoundBuffer();

protected:

	/** @brief Fills the output by invoking the registered streaming hook.
	 *  @param data   Destination buffer for PCM data.
	 *  @param frames Number of frames requested.
	 */
	virtual void				FillBuffer(void * data, int64 frames);

private:

			void *				fStreamHook; /**< Opaque pointer to the stream fill hook. */
};

#endif
