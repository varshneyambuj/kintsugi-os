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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2002-2009, Haiku.
 * Original authors: Marcus Overhagen, Jérôme Duval.
 */

/** @file SoundPlayNode.h
    @brief Internal BBufferProducer node that drives BSoundPlayer audio output. */

#ifndef _SOUND_PLAY_NODE_H
#define _SOUND_PLAY_NODE_H


#include <Buffer.h>
#include <BufferGroup.h>
#include <BufferProducer.h>
#include <MediaEventLooper.h>
#include <SoundPlayer.h>


namespace BPrivate {


/** @brief Media node that produces audio buffers on behalf of a BSoundPlayer instance. */
class SoundPlayNode : public BBufferProducer, public BMediaEventLooper {
public:
								SoundPlayNode(const char* name,
									BSoundPlayer* player);
	virtual						~SoundPlayNode();

			/** @brief Returns true if the node is currently playing audio.
			    @return true if playback is active. */
			bool				IsPlaying();

			/** @brief Returns the current media performance time.
			    @return Current time in microseconds. */
			bigtime_t			CurrentTime();

	// BMediaNode methods

	/** @brief Returns the add-on that instantiated this node, if any.
	    @param _internalID Output internal ID within the add-on.
	    @return NULL for internal nodes not associated with an add-on. */
	virtual	BMediaAddOn*		AddOn(int32* _internalID) const;

protected:
	virtual	void				Preroll();

public:
	/** @brief Handles an incoming message directed at this node.
	    @param message Message code.
	    @param data Pointer to message payload.
	    @param size Size of the payload in bytes.
	    @return B_OK if handled, or an error code. */
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

protected:
	virtual	void				NodeRegistered();
	virtual	status_t			RequestCompleted(
									const media_request_info& info);
	virtual	void				SetTimeSource(BTimeSource* timeSource);
	virtual void				SetRunMode(run_mode mode);

	// BBufferProducer methods

	/** @brief Suggests a suitable output format for the given media type and quality.
	    @param type Requested media type.
	    @param quality Quality hint.
	    @param format In/out format; filled with the suggestion on success.
	    @return B_OK on success, or an error code. */
	virtual status_t 			FormatSuggestionRequested(media_type type,
									int32 quality, media_format* format);

	/** @brief Evaluates a format proposal from a downstream consumer.
	    @param output Source output this proposal concerns.
	    @param format In/out format; may be adjusted to a compatible format.
	    @return B_OK if acceptable, or an error code. */
	virtual status_t		 	FormatProposal(const media_source& output,
									media_format* format);

	/** @brief Handles a downstream request to change the output format.
	    @param source The media source.
	    @param destination The media destination.
	    @param format In/out new format.
	    @param _deprecated_ Unused deprecated parameter.
	    @return B_OK on success, or an error code. */
	virtual status_t 			FormatChangeRequested(
									const media_source& source,
									const media_destination& destination,
									media_format* format, int32* _deprecated_);

	/** @brief Iterates over all outputs this node exposes.
	    @param cookie Iterator state; start at 0.
	    @param _output Output media_output descriptor.
	    @return B_OK while more outputs exist, B_BAD_INDEX when done. */
	virtual status_t 			GetNextOutput(int32* cookie,
									media_output* _output);

	/** @brief Releases resources associated with an output cookie.
	    @param cookie The cookie returned by GetNextOutput.
	    @return B_OK on success, or an error code. */
	virtual status_t		 	DisposeOutputCookie(int32 cookie);

	/** @brief Assigns a buffer group to be used for the given source.
	    @param forSource The source whose buffer group should be set.
	    @param group The new buffer group, or NULL to use the default.
	    @return B_OK on success, or an error code. */
	virtual	status_t 			SetBufferGroup(const media_source& forSource,
									BBufferGroup* group);

	/** @brief Returns the total output latency of this node.
	    @param _latency Output latency in microseconds.
	    @return B_OK on success, or an error code. */
	virtual	status_t 			GetLatency(bigtime_t* _latency);

	/** @brief Prepares a connection from this output to a consumer destination.
	    @param what The source output.
	    @param where The destination input.
	    @param format In/out negotiated format.
	    @param _source Output actual source used.
	    @param _name Output human-readable name for the connection.
	    @return B_OK on success, or an error code. */
	virtual status_t 			PrepareToConnect(const media_source& what,
									const media_destination& where,
									media_format* format, media_source* _source,
									char* _name);

	/** @brief Called after a connection is fully established.
	    @param error B_OK if the connection succeeded, error otherwise.
	    @param source The connected source.
	    @param destination The connected destination.
	    @param format The agreed format.
	    @param name Human-readable connection name. */
	virtual void 				Connect(status_t error,
									const media_source& source,
									const media_destination& destination,
									const media_format& format,
									char* name);

	/** @brief Called when an existing connection is torn down.
	    @param what The source that was connected.
	    @param where The destination that was connected. */
	virtual void 				Disconnect(const media_source& what,
									const media_destination& where);

	/** @brief Notifies the producer that a buffer arrived late.
	    @param what The affected source.
	    @param howMuch How many microseconds late.
	    @param performanceTime Performance time of the late buffer. */
	virtual void 				LateNoticeReceived(const media_source& what,
									bigtime_t howMuch,
									bigtime_t performanceTime);

	/** @brief Enables or disables output on a given source.
	    @param what The source to control.
	    @param enabled true to enable output.
	    @param _deprecated_ Unused deprecated parameter. */
	virtual void 				EnableOutput(const media_source& what,
									bool enabled, int32* _deprecated_);

	/** @brief Requests that an additional buffer be produced beyond the scheduled ones.
	    @param source The source to produce from.
	    @param previousBuffer ID of the previously sent buffer.
	    @param previousTime Performance time of the previous buffer.
	    @param previousTag Seek tag of the previous buffer. */
	virtual void 				AdditionalBufferRequested(
									const media_source& source,
									media_buffer_id previousBuffer,
									bigtime_t previousTime,
									const media_seek_tag* previousTag);

	/** @brief Notifies of a latency change on a connected path.
	    @param source The source whose path changed.
	    @param destination The destination of the path.
	    @param newLatency Updated latency in microseconds.
	    @param flags Change flags. */
	virtual void 				LatencyChanged(const media_source& source,
									const media_destination& destination,
									bigtime_t newLatency, uint32 flags);

	// BMediaEventLooper methods

protected:
	virtual void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
public:
			/** @brief Returns the current audio output format.
			    @return The media_multi_audio_format in use. */
			media_multi_audio_format Format() const;

protected:
	virtual status_t			HandleStart(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
	virtual status_t			HandleSeek(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
	virtual status_t			HandleWarp(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
	virtual status_t			HandleStop(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
	virtual status_t			SendNewBuffer(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
	virtual status_t			HandleDataStatus(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);
	virtual status_t			HandleParameter(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);

			/** @brief Allocates the output buffer group.
			    @return B_OK on success, or an error code. */
			status_t 			AllocateBuffers();

			/** @brief Fills the next output buffer with audio data for the given event time.
			    @param eventTime Performance time of the buffer.
			    @return Pointer to the filled BBuffer, or NULL on failure. */
			BBuffer*			FillNextBuffer(bigtime_t eventTime);

private:
			BSoundPlayer*		fPlayer;

			status_t 			fInitStatus;
			bool 				fOutputEnabled;
			media_output		fOutput;
			BBufferGroup*		fBufferGroup;
			bigtime_t 			fLatency;
			bigtime_t 			fInternalLatency;
			bigtime_t 			fStartTime;
			uint64 				fFramesSent;
			int32				fTooEarlyCount;
};


}	// namespace BPrivate


#endif	// _SOUND_PLAY_NODE_H
