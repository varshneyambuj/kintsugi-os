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
 *   Copyright (c) 2001-2002, Haiku. All rights reserved.
 *   Author: Christopher ML Zumwalt May (zummy@users.sf.net)
 *   Distributed under the terms of the MIT License.
 *
 * Author (Kintsugi OS): Ambuj Varshney <ambuj@kintsugi-os.org>
 */

/** @file GameProducer.h
 *  @brief GameProducer: a MediaKit BBufferProducer node that mixes GameKit sounds and feeds the audio mixer.
 */

#ifndef _GAME_PRODUCER_H
#define _GAME_PRODUCER_H


#include <media/BufferProducer.h>
#include <media/MediaEventLooper.h>
#include <GameSoundDefs.h>


class GameSoundBuffer;


/** @brief A BBufferProducer/BMediaEventLooper node that pulls audio from GameSoundBuffer objects and delivers mixed buffers to the audio mixer. */
class GameProducer : public BBufferProducer, public BMediaEventLooper {
public:
	/** @brief Constructs a GameProducer tied to a specific GameSoundBuffer and format.
	 *  @param object Pointer to the GameSoundBuffer that owns this producer.
	 *  @param format Pointer to the audio format this producer will output.
	 */
								GameProducer(GameSoundBuffer* object,
									const gs_audio_format * format);

	/** @brief Destroys the GameProducer and disconnects from the media graph. */
								~GameProducer();

	// BMediaNode methods

	/** @brief Returns the add-on that instantiated this node, if any.
	 *  @param internal_id Set to the internal node ID within the add-on.
	 *  @return NULL if not instantiated from an add-on.
	 */
			BMediaAddOn*		AddOn(int32* internal_id) const;

	// BBufferProducer methods

	/** @brief Suggests a suitable media format for the given type and quality.
	 *  @param type    The desired media type.
	 *  @param quality Quality level hint.
	 *  @param format  Filled with the suggested format.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			FormatSuggestionRequested(media_type type,
									int32 quality, media_format* format);

	/** @brief Validates or adjusts a proposed format for the given output.
	 *  @param output Identifies which output source is being negotiated.
	 *  @param format In/out format to validate or adjust.
	 *  @return B_OK if the format is acceptable, or an error code.
	 */
			status_t			FormatProposal(const media_source& output,
									media_format* format);

	/** @brief Handles a request to change the format of an active connection.
	 *  @param source      The media source being changed.
	 *  @param destination The connected media destination.
	 *  @param io_format   In/out format being negotiated.
	 *  @param _deprecated_ Deprecated parameter; ignore.
	 *  @return B_OK on success, or an error code.
	 */
			status_t	 		FormatChangeRequested(const media_source& source,
									const media_destination& destination,
									media_format* io_format,
									int32* _deprecated_);

	/** @brief Iterates over available outputs.
	 *  @param cookie   Iteration state; start at 0.
	 *  @param _output  Filled with information about the next output.
	 *  @return B_OK while more outputs exist, B_BAD_INDEX when done.
	 */
			status_t			GetNextOutput(int32* cookie,
									media_output* _output);

	/** @brief Releases resources associated with an output iteration cookie.
	 *  @param cookie Cookie previously passed to GetNextOutput().
	 *  @return B_OK on success.
	 */
			status_t			DisposeOutputCookie(int32 cookie);

	/** @brief Assigns a BBufferGroup for this output to use.
	 *  @param forSource The source whose buffer group is being set.
	 *  @param group     The new buffer group.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetBufferGroup(const media_source& forSource,
									BBufferGroup* group);

	/** @brief Returns the total processing latency introduced by this node.
	 *  @param _latency Filled with latency in microseconds.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetLatency(bigtime_t* _latency);

	/** @brief Verifies and finalises a connection before Connect() is called.
	 *  @param what     Source being connected.
	 *  @param where    Destination being connected.
	 *  @param format   In/out negotiated format.
	 *  @param _source  Filled with the confirmed source identifier.
	 *  @param out_name Buffer to receive the output's name string.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			PrepareToConnect(const media_source& what,
									const media_destination& where,
									media_format* format,
									media_source* _source,
									char* out_name);

	/** @brief Called by the media kit to complete a connection.
	 *  @param error       B_OK if connection succeeded, error code otherwise.
	 *  @param source      The connected source.
	 *  @param destination The connected destination.
	 *  @param format      The negotiated format.
	 *  @param ioName      Buffer containing the assigned connection name.
	 */
			void				Connect(status_t error,
									const media_source& source,
									const media_destination& destination,
									const media_format& format,
									char* ioName);

	/** @brief Called by the media kit to tear down a connection.
	 *  @param what  The source being disconnected.
	 *  @param where The destination being disconnected.
	 */
			void				Disconnect(const media_source& what,
									const media_destination& where);

	/** @brief Notifies the node that a buffer arrived late.
	 *  @param what                The late source.
	 *  @param howMuch             How late the buffer was in microseconds.
	 *  @param performanceDuration Scheduled performance duration.
	 */
			void				LateNoticeReceived(const media_source& what,
									bigtime_t howMuch,
									bigtime_t performanceDuration);

	/** @brief Enables or disables output from a specific source.
	 *  @param what       The source to enable or disable.
	 *  @param enabled    True to enable, false to disable.
	 *  @param _deprecated_ Deprecated; ignore.
	 */
			void				EnableOutput(const media_source & what,
									bool enabled, int32* _deprecated_);

	/** @brief Sets the play rate as a ratio of numerator to denominator.
	 *  @param numerator   Numerator of the desired rate.
	 *  @param denominator Denominator of the desired rate.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetPlayRate(int32 numerator, int32 denominator);

	/** @brief Handles a raw message from the media kit.
	 *  @param message Message code.
	 *  @param data    Pointer to message data.
	 *  @param size    Size of message data in bytes.
	 *  @return B_OK if handled, B_ERROR otherwise.
	 */
			status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	/** @brief Called when an additional buffer is needed (e.g. for seek).
	 *  @param source    The requesting source.
	 *  @param prev_buffer Previous buffer ID.
	 *  @param prev_time   Previous performance time.
	 *  @param prev_tag    Previous seek tag.
	 */
			void				AdditionalBufferRequested(const media_source& source,
									media_buffer_id prev_buffer,
									bigtime_t prev_time,
									const media_seek_tag* prev_tag);

	/** @brief Called when the downstream latency of a connection changes.
	 *  @param source      The affected source.
	 *  @param destination The affected destination.
	 *  @param new_latency New latency in microseconds.
	 *  @param flags       Latency change flags.
	 */
			void 				LatencyChanged(const media_source& source,
									const media_destination& destination,
									bigtime_t new_latency,
									uint32 flags);

	// BMediaEventLooper methods

	/** @brief Called once the node has been registered with the media kit; starts output. */
			void 				NodeRegistered();

	/** @brief Responds to run-mode changes imposed by the media kit.
	 *  @param mode New run mode.
	 */
			void 				SetRunMode(run_mode mode);

	/** @brief Processes a timed media event from the event queue.
	 *  @param event         The event to process.
	 *  @param lateness      How late (in µs) this event is being handled.
	 *  @param realTimeEvent True if the event is a real-time event.
	 */
			void 				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);

	// GameProducer

	/** @brief Registers a GameSoundBuffer for mixing into the output stream.
	 *  @param sound The GameSoundBuffer to start mixing.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StartPlaying(GameSoundBuffer* sound);

	/** @brief Removes a GameSoundBuffer from the mix.
	 *  @param sound The GameSoundBuffer to stop mixing.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			StopPlaying(GameSoundBuffer* sound);

	/** @brief Returns whether the given GameSoundBuffer is currently being mixed.
	 *  @param sound The GameSoundBuffer to query.
	 *  @return True if the sound is currently playing.
	 */
			bool				IsPlaying(GameSoundBuffer* sound) const;

	/** @brief Returns the total number of sounds currently registered with this producer.
	 *  @return Count of registered GameSoundBuffer objects.
	 */
			int32				SoundCount() const;

private:
			BBuffer* 			FillNextBuffer(bigtime_t event_time);

			BBufferGroup*	 	fBufferGroup;
			bigtime_t 			fLatency;
			bigtime_t			fInternalLatency;
			media_output	 	fOutput;
			bool 				fOutputEnabled;
			media_format 		fPreferredFormat;

			bigtime_t			fStartTime;
			size_t				fFrameSize;
			int64				fFramesSent;
			GameSoundBuffer*	fObject;
			size_t				fBufferSize;
};


#endif	// _GAME_PRODUCER_H
