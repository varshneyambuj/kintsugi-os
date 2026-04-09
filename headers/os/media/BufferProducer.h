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

/** @file BufferProducer.h
 *  @brief Defines BBufferProducer, the base class for media nodes that send data buffers.
 */

#ifndef _BUFFER_PRODUCER_H
#define _BUFFER_PRODUCER_H


#include <MediaNode.h>


class BBuffer;
class BBufferGroup;
class BRegion;


namespace BPrivate {
	namespace media {
		class BMediaRosterEx;
	}
}


/** @brief Abstract base class for media nodes that produce (send) data buffers.
 *
 *  Derive from BBufferProducer to build a node that generates media data and
 *  distributes it to connected consumer nodes via SendBuffer().
 */
class BBufferProducer : public virtual BMediaNode {
protected:
	// NOTE: This has to be at the top to force a vtable.
	virtual						~BBufferProducer();

public:

	/** @brief Supported formats for low-level clipping data. */
	enum {
		B_CLIP_SHORT_RUNS = 1  /**< Clipping data is encoded as int16 run pairs. */
	};

	/** @brief Converts run-length-encoded clip data to a BRegion.
	 *  @param format The clip data format identifier (e.g. B_CLIP_SHORT_RUNS).
	 *  @param size Size of the clip data in bytes.
	 *  @param data Pointer to the encoded clip data.
	 *  @param region On return, the decoded clipping region.
	 *  @return B_OK on success, or an error code.
	 */
	static	status_t			ClipDataToRegion(int32 format, int32 size,
									const void* data,  BRegion* region);

	/** @brief Returns the media type this producer outputs.
	 *  @return The producer's media_type.
	 */
			media_type			ProducerType();

protected:
	/** @brief Constructs a BBufferProducer of the given media type.
	 *  @param producer_type The media type this producer will generate.
	 */
	explicit					BBufferProducer(media_type producer_type
									/* = B_MEDIA_UNKNOWN_TYPE */);

	/** @brief Quality hint values for FormatSuggestionRequested(). */
	enum suggestion_quality {
		B_ANY_QUALITY		= 0,    /**< Accept any quality level. */
		B_LOW_QUALITY		= 10,   /**< Prefer low quality / small data. */
		B_MEDIUM_QUALITY	= 50,   /**< Prefer medium quality. */
		B_HIGH_QUALITY		= 100   /**< Prefer highest quality. */
	};

	/** @brief Called to ask the producer for a suggested output format.
	 *  @param type The desired media type.
	 *  @param quality One of the suggestion_quality values.
	 *  @param format On return, the suggested format.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			FormatSuggestionRequested(media_type type,
									int32 quality, media_format* format) = 0;

	/** @brief Called to verify or adjust the format for a specific output.
	 *  @param output The output whose format is being proposed.
	 *  @param ioFormat In/out: the proposed format; may be adjusted.
	 *  @return B_OK if the format is acceptable, or an error code.
	 */
	virtual	status_t			FormatProposal(const media_source& output,
									media_format* ioFormat) = 0;

	/** @brief Called when a downstream consumer requests a format change.
	 *  @param source The output source whose format should change.
	 *  @param destination The consumer destination requesting the change.
	 *  @param ioFormat In/out: the proposed format; adjust to a supported format.
	 *  @param _deprecated_ Ignored; for binary compatibility only.
	 *  @return B_OK if the format can be changed, or an error code.
	 */
	virtual	status_t			FormatChangeRequested(
									const media_source& source,
									const media_destination& destination,
									media_format* ioFormat,
									int32* _deprecated_) = 0;

	/** @brief Iterates over all outputs provided by this producer.
	 *  @param ioCookie In/out iteration cookie; initialize to 0 before first call.
	 *  @param _output On return, filled with the next output description.
	 *  @return B_OK while more outputs exist, B_BAD_INDEX when done.
	 */
	virtual	status_t			GetNextOutput(
									int32* ioCookie,
									media_output* _output) = 0;

	/** @brief Releases any resources associated with an output iteration cookie.
	 *  @param cookie The cookie returned by GetNextOutput().
	 *  @return B_OK on success.
	 */
	virtual	status_t			DisposeOutputCookie(int32 cookie) = 0;

	/** @brief Called to set the buffer group for a specific output source.
	 *  @param forSource The output whose buffer group should be changed.
	 *  @param group The new buffer group to use (may be NULL).
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SetBufferGroup(const media_source& forSource,
									BBufferGroup* group) = 0;

	/** @brief Called when video clipping changes for a specific output.
	 *  @param forSource The output source affected.
	 *  @param numShorts Number of int16 values in the clip data array.
	 *  @param clipData The encoded clipping data.
	 *  @param display Updated video display info; non-zero fields indicate changes.
	 *  @param _deprecated_ Ignored; for binary compatibility only.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			VideoClippingChanged(
									const media_source& forSource,
									int16 numShorts,
									int16* clipData,
									const media_video_display_info& display,
									int32 * _deprecated_);

	/** @brief Iterates over all outputs to compute the maximum downstream latency.
	 *  @param _lantency On return, the maximum latency found in microseconds.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetLatency(bigtime_t* _lantency);

	/** @brief Called just before a connection is established; verify/adjust the format.
	 *  @param what The output source being connected.
	 *  @param where The consumer destination.
	 *  @param format In/out: the negotiated format.
	 *  @param _source On return, the actual source that will be used.
	 *  @param _name Buffer to receive a descriptive name for the connection.
	 *  @return B_OK if the producer can accept the connection, or an error code.
	 */
	virtual	status_t			PrepareToConnect(const media_source& what,
									const media_destination& where,
									media_format* format,
									media_source* _source,
									char* _name) = 0;

	/** @brief Called after a connection has been established.
	 *  @param error B_OK if the connection succeeded, or an error code.
	 *  @param source The source that was connected.
	 *  @param destination The consumer destination.
	 *  @param format The format agreed upon for this connection.
	 *  @param ioName In/out: the connection name; may be modified.
	 */
	virtual	void				Connect(status_t error,
									const media_source& source,
									const media_destination& destination,
									const media_format& format,
									char* ioName) = 0;

	/** @brief Called when a connection to a consumer has been broken.
	 *  @param what The source that was disconnected.
	 *  @param where The consumer destination that is now free.
	 */
	virtual	void				Disconnect(const media_source& what,
									const media_destination& where) = 0;

	/** @brief Called to inform the producer that a consumer received a buffer late.
	 *  @param what The source whose buffer was late.
	 *  @param howMuch How late the buffer was, in microseconds.
	 *  @param performanceTime The performance time at which lateness was detected.
	 */
	virtual	void				LateNoticeReceived(const media_source& what,
									bigtime_t howMuch,
									bigtime_t performanceTime) = 0;

	/** @brief Called to enable or disable a specific output.
	 *  @param what The source to enable or disable.
	 *  @param enabled True to enable the output, false to disable it.
	 *  @param _deprecated_ Ignored; for binary compatibility only.
	 */
	virtual	void				EnableOutput(const media_source& what,
									bool enabled, int32* _deprecated_) = 0;

	/** @brief Sets the playback rate for this producer.
	 *  @param numer Numerator of the rate fraction.
	 *  @param denom Denominator of the rate fraction.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SetPlayRate(int32 numer, int32 denom);

	/** @brief Dispatches an incoming port message; call from the listener thread.
	 *  @param message The message code.
	 *  @param data Pointer to the message payload.
	 *  @param size Size of the payload in bytes.
	 *  @return B_OK if handled, or an error code.
	 */
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	/** @brief Called when a consumer requests an additional buffer from this producer.
	 *  @param source The source from which the buffer is requested.
	 *  @param previousBuffer ID of the most recently sent buffer.
	 *  @param previousTime Performance time of the previous buffer.
	 *  @param previousTag Seek tag of the previous buffer, or NULL.
	 */
	virtual	void				AdditionalBufferRequested(
									const media_source& source,
									media_buffer_id previousBuffer,
									bigtime_t previousTime,
									const media_seek_tag* previousTag
										/* = NULL */);

	/** @brief Called when a consumer's latency has changed.
	 *  @param source The source connected to the consumer.
	 *  @param destination The consumer destination whose latency changed.
	 *  @param newLatency The new latency value in microseconds.
	 *  @param flags Flags describing the nature of the change.
	 */
	virtual	void				LatencyChanged(const media_source& source,
									const media_destination& destination,
									bigtime_t newLatency, uint32 flags);

	/** @brief Sends a filled buffer to the connected consumer.
	 *  @param buffer The buffer to send.
	 *  @param source The source this buffer originates from.
	 *  @param destination The consumer destination to deliver it to.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SendBuffer(BBuffer* buffer,
									const media_source& source,
									const media_destination& destination);

	/** @brief Sends a data-status notification to a consumer destination.
	 *  @param status One of the media_producer_status values.
	 *  @param destination The consumer destination to notify.
	 *  @param atTime The performance time of the status change.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SendDataStatus(int32 status,
									const media_destination& destination,
									bigtime_t atTime);

	/** @brief Asks a consumer whether it can accept the given format.
	 *  @param format In/out: the format to check; may be specialized.
	 *  @param forDestination The destination to query.
	 *  @return B_OK if the format is acceptable, or an error code.
	 */
			status_t			ProposeFormatChange(media_format* format,
									const media_destination& forDestination);

	/** @brief Requests that a connected consumer accept a new format.
	 *  @param forSource The output source whose format is changing.
	 *  @param forDestination The consumer destination to notify.
	 *  @param format The new format to apply.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ChangeFormat(const media_source& forSource,
									const media_destination& forDestination,
									media_format* format);

	/** @brief Queries the downstream graph for its total latency.
	 *  @param forDestination The consumer destination to query.
	 *  @param _latency On return, the downstream latency in microseconds.
	 *  @param _timesource On return, the ID of the governing time source.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			FindLatencyFor(
									const media_destination& forDestination,
									bigtime_t* _latency,
									media_node_id* _timesource);

	/** @brief Finds a seek tag near the given target time.
	 *  @param forDestination The consumer destination to query.
	 *  @param inTargetTime The performance time to seek to.
	 *  @param _tag On return, the seek tag for the nearest key frame.
	 *  @param _taggedTime On return, the actual time of the tagged frame.
	 *  @param _flags On return, updated flags.
	 *  @param flags Input flags controlling the search direction.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			FindSeekTag(
									const media_destination& forDestination,
									bigtime_t inTargetTime,
									media_seek_tag* _tag,
									bigtime_t* _taggedTime, uint32* _flags = 0,
									uint32 flags = 0);

	/** @brief Sets the initial startup latency for this producer.
	 *  @param inInitialLatency The additional latency in microseconds.
	 *  @param flags Optional flags (currently unused; pass 0).
	 */
			void				SetInitialLatency(bigtime_t inInitialLatency,
									uint32 flags = 0);

	// TODO: Needs a Perform() virtual method!

private:
	// FBC padding and forbidden methods
								BBufferProducer();
								BBufferProducer(const BBufferProducer& other);
			BBufferProducer&	operator=(const BBufferProducer& other);

			status_t			_Reserved_BufferProducer_0(void*);
				// was AdditionalBufferRequested()
			status_t			_Reserved_BufferProducer_1(void*);
				// was LatencyChanged()
	virtual	status_t			_Reserved_BufferProducer_2(void*);
	virtual	status_t			_Reserved_BufferProducer_3(void*);
	virtual	status_t			_Reserved_BufferProducer_4(void*);
	virtual	status_t			_Reserved_BufferProducer_5(void*);
	virtual	status_t			_Reserved_BufferProducer_6(void*);
	virtual	status_t			_Reserved_BufferProducer_7(void*);
	virtual	status_t			_Reserved_BufferProducer_8(void*);
	virtual	status_t			_Reserved_BufferProducer_9(void*);
	virtual	status_t			_Reserved_BufferProducer_10(void*);
	virtual	status_t			_Reserved_BufferProducer_11(void*);
	virtual	status_t			_Reserved_BufferProducer_12(void*);
	virtual	status_t			_Reserved_BufferProducer_13(void*);
	virtual	status_t			_Reserved_BufferProducer_14(void*);
	virtual	status_t			_Reserved_BufferProducer_15(void*);

	// deprecated calls
			status_t			SendBuffer(BBuffer* buffer,
									const media_destination& destination);

private:
			friend class BBufferConsumer;
			friend class BMediaNode;
			friend class BMediaRoster;
			friend class BPrivate::media::BMediaRosterEx;

	static	status_t			clip_shorts_to_region(const int16* data,
									int count, BRegion* output);
	static	status_t			clip_region_to_shorts(const BRegion* input,
									int16* data, int maxCount, int* _count);

private:
			media_type			fProducerType;
			bigtime_t			fInitialLatency;
			uint32				fInitialFlags;
			bigtime_t			fDelay;

			uint32				_reserved_buffer_producer_[12];
};

#endif // _BUFFER_PRODUCER_H

