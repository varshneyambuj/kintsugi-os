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
 * Copyright 2009, Haiku Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file BufferConsumer.h
 *  @brief Defines BBufferConsumer, the base class for media nodes that receive data buffers.
 */

#ifndef _BUFFER_CONSUMER_H
#define _BUFFER_CONSUMER_H


#include <MediaDefs.h>
#include <MediaNode.h>


class BBuffer;
class BBufferGroup;
class BRegion;

namespace BPrivate {
	class BufferCache;
	namespace media {
		class BMediaRosterEx;
	}
}


/** @brief Abstract base class for media nodes that consume (receive) data buffers.
 *
 *  Derive from BBufferConsumer (typically alongside BBufferProducer and/or
 *  BMediaEventLooper) to build a node that accepts incoming media data from a
 *  connected producer.
 */
class BBufferConsumer : public virtual BMediaNode {
protected:
	virtual						~BBufferConsumer();

public:
	/** @brief Returns the media type this consumer accepts.
	 *  @return The media_type passed to the constructor.
	 */
			media_type			ConsumerType();

	/** @brief Converts a BRegion to the compact run-length-encoded clip format.
	 *  @param region The clipping region to convert.
	 *  @param format On return, the clip data format identifier.
	 *  @param size On return, the size in bytes of the encoded data.
	 *  @param data Buffer that receives the encoded clip data.
	 *  @return B_OK on success, or an error code.
	 */
	static	status_t			RegionToClipData(const BRegion* region,
									int32* format, int32* size, void* data);

protected:
	/** @brief Constructs a BBufferConsumer of the given media type.
	 *  @param type The media type this consumer will accept.
	 */
	explicit					BBufferConsumer(media_type type);

	/** @brief Notifies an upstream producer that it is running late.
	 *  @param whatSource The source that is producing late data.
	 *  @param howMuch How late the data is, in microseconds.
	 *  @param performanceTime The performance time at which lateness was detected.
	 */
	static	void				NotifyLateProducer(
									const media_source& whatSource,
									bigtime_t howMuch,
									bigtime_t performanceTime);

	/** @brief Requests that a producer apply video clipping to its output.
	 *  @param output The source whose clipping should change.
	 *  @param destination The destination on this consumer.
	 *  @param shorts Pointer to the run-length-encoded clipping data.
	 *  @param shortCount Number of int16 values in the clipping array.
	 *  @param display Video display information.
	 *  @param userData Arbitrary user data forwarded to the producer.
	 *  @param changeTag On return, the change tag assigned to this request.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetVideoClippingFor(const media_source& output,
									const media_destination& destination,
									const int16* shorts, int32 shortCount,
									const media_video_display_info& display,
									void* userData, int32* changeTag,
									void* _reserved = NULL);

	/** @brief Enables or disables a producer output.
	 *  @param source The source to enable or disable.
	 *  @param destination The destination on this consumer.
	 *  @param enabled True to enable, false to disable.
	 *  @param userData Arbitrary user data forwarded to the producer.
	 *  @param changeTag On return, the change tag for this request.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetOutputEnabled(const media_source& source,
									const media_destination& destination,
									bool enabled, void* userData,
									int32* changeTag, void* _reserved = NULL);

	/** @brief Asks the producer to change to a different format.
	 *  @param source The source whose format should change.
	 *  @param destination The destination on this consumer.
	 *  @param toFormat The desired new format.
	 *  @param userData Arbitrary user data forwarded to the producer.
	 *  @param changeTag On return, the change tag for this request.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			RequestFormatChange(const media_source& source,
									const media_destination& destination,
									const media_format& toFormat,
									void* userData, int32* changeTag,
									void* _reserved = NULL);

	/** @brief Requests an additional buffer from the producer, keyed by the previous buffer.
	 *  @param source The source to request the buffer from.
	 *  @param previousBuffer The previously received buffer.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			RequestAdditionalBuffer(
									const media_source& source,
									BBuffer* previousBuffer,
									void* _reserved = NULL);

	/** @brief Requests an additional buffer from the producer at a given start time.
	 *  @param source The source to request the buffer from.
	 *  @param startTime The performance time at which the buffer should start.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			RequestAdditionalBuffer(
									const media_source& source,
									bigtime_t startTime,
									void* _reserved = NULL);

	/** @brief Instructs the producer to use a specific buffer group for output.
	 *  @param source The producer source.
	 *  @param destination The destination on this consumer.
	 *  @param group The buffer group to use, or NULL to revert to defaults.
	 *  @param userData Arbitrary user data forwarded to the producer.
	 *  @param changeTag On return, the change tag for this request.
	 *  @param willReclaim True if the consumer will reclaim the group later.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetOutputBuffersFor(const media_source& source,
									const media_destination& destination,
									BBufferGroup* group, void* userData,
									int32* changeTag, bool willReclaim = false,
									void* _reserved = NULL);

	/** @brief Notifies the producer of a change in this consumer's latency.
	 *  @param source The producer source.
	 *  @param destination The destination on this consumer.
	 *  @param newLatency The updated latency value in microseconds.
	 *  @param flags Optional flags (currently unused; pass 0).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SendLatencyChange(const media_source& source,
									const media_destination& destination,
									bigtime_t newLatency, uint32 flags = 0);

protected:
	/** @brief Dispatches an incoming port message to the appropriate handler.
	 *  @param message The message code.
	 *  @param data Pointer to the message payload.
	 *  @param size Size of the payload in bytes.
	 *  @return B_OK if handled, B_ERROR if unknown.
	 */
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	/** @brief Called by the Media Kit to verify that a proposed format is acceptable.
	 *  @param destination The destination being negotiated.
	 *  @param format In/out: the format to check; may be specialized by the callee.
	 *  @return B_OK if acceptable, or an error code.
	 */
	virtual	status_t			AcceptFormat(
									const media_destination& destination,
									media_format* format) = 0;

	/** @brief Iterates over all inputs provided by this consumer.
	 *  @param cookie In/out iteration cookie; initialize to 0 before first call.
	 *  @param _input On return, filled with the next input description.
	 *  @return B_OK while more inputs exist, B_BAD_INDEX when done.
	 */
	virtual	status_t			GetNextInput(int32* cookie,
									media_input* _input) = 0;

	/** @brief Releases any resources associated with an input iteration cookie.
	 *  @param cookie The cookie value returned by GetNextInput().
	 */
	virtual	void				DisposeInputCookie(int32 cookie) = 0;

	/** @brief Called when a buffer has been delivered to this consumer.
	 *  @param buffer The received buffer; call Recycle() when done.
	 */
	virtual	void				BufferReceived(BBuffer* buffer) = 0;

	/** @brief Notifies the consumer of a change in the upstream producer's data status.
	 *  @param forWhom The destination affected.
	 *  @param status One of the media_producer_status values.
	 *  @param atPerformanceTime The performance time of the status change.
	 */
	virtual	void				ProducerDataStatus(
									const media_destination& forWhom,
									int32 status,
									bigtime_t atPerformanceTime) = 0;

	/** @brief Queries this consumer for its total input latency.
	 *  @param forWhom The destination whose latency is being queried.
	 *  @param _latency On return, the latency in microseconds.
	 *  @param _timesource On return, the ID of the governing time source.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetLatencyFor(const media_destination& forWhom,
									bigtime_t* _latency,
									media_node_id* _timesource) = 0;

	/** @brief Called when a connection to this consumer has been established.
	 *  @param producer The source that was connected.
	 *  @param where The destination on this consumer.
	 *  @param withFormat The negotiated format for the connection.
	 *  @param _input On return, the media_input describing the connection.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Connected(const media_source& producer,
									const media_destination& where,
									const media_format& withFormat,
									media_input* _input) = 0;

	/** @brief Called when a connection to this consumer has been broken.
	 *  @param producer The source that was disconnected.
	 *  @param where The destination that is now free.
	 */
	virtual	void				Disconnected(const media_source& producer,
									const media_destination& where) = 0;

	/** @brief Notifies the consumer that the upstream format has changed.
	 *  @param producer The source whose format changed.
	 *  @param consumer The destination that is affected.
	 *  @param changeTag The tag previously assigned to the format-change request.
	 *  @param format The new format.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			FormatChanged(const media_source& producer,
									const media_destination& consumer,
									int32 changeTag,
									const media_format& format) = 0;

	/** @brief Optionally override to provide a seek tag for the requested time.
	 *  @param destination The destination making the request.
	 *  @param targetTime The performance time to seek to.
	 *  @param flags Seek flags.
	 *  @param _seekTag On return, the seek tag for the nearest key frame.
	 *  @param _taggedTime On return, the actual tagged time.
	 *  @param _flags On return, updated flags.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SeekTagRequested(
									const media_destination& destination,
									bigtime_t targetTime, uint32 flags,
									media_seek_tag* _seekTag,
									bigtime_t* _taggedTime, uint32* _flags);

private:
	friend class BMediaNode;
	friend class BBufferProducer;
	friend class BMediaRoster;
	friend class BPrivate::media::BMediaRosterEx;

								BBufferConsumer();
								BBufferConsumer(const BBufferConsumer& other);
			BBufferConsumer&	operator=(const BBufferConsumer& other);

	// deprecated methods following
	static	status_t			SetVideoClippingFor(const media_source& output,
									const int16* shorts, int32 shortCount,
									const media_video_display_info& display,
									int32* changeTag);
	static	status_t			RequestFormatChange(const media_source& source,
									const media_destination& destination,
									media_format* toFormat, int32* changeTag);
	static	status_t			SetOutputEnabled(const media_source& source,
									bool enabled, int32* changeTag);

			status_t			_Reserved_BufferConsumer_0(void*);
									// used for SeekTagRequested()
	virtual	status_t			_Reserved_BufferConsumer_1(void*);
	virtual	status_t			_Reserved_BufferConsumer_2(void*);
	virtual	status_t			_Reserved_BufferConsumer_3(void*);
	virtual	status_t			_Reserved_BufferConsumer_4(void*);
	virtual	status_t			_Reserved_BufferConsumer_5(void*);
	virtual	status_t			_Reserved_BufferConsumer_6(void*);
	virtual	status_t			_Reserved_BufferConsumer_7(void*);
	virtual	status_t			_Reserved_BufferConsumer_8(void*);
	virtual	status_t			_Reserved_BufferConsumer_9(void*);
	virtual	status_t			_Reserved_BufferConsumer_10(void*);
	virtual	status_t			_Reserved_BufferConsumer_11(void*);
	virtual	status_t			_Reserved_BufferConsumer_12(void*);
	virtual	status_t			_Reserved_BufferConsumer_13(void*);
	virtual	status_t			_Reserved_BufferConsumer_14(void*);
	virtual	status_t			_Reserved_BufferConsumer_15(void*);

private:
			media_type			fConsumerType;
			BPrivate::BufferCache* fBufferCache;
			BBufferGroup*		fDeleteBufferGroup;
			uint32				_reserved[14];
};


#endif	// _BUFFER_CONSUMER_H
