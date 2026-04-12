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
 *   Copyright 2015, Dario Casalinuovo. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MediaClientNode.cpp
 * @brief Internal BMediaNode implementation backing BMediaClient.
 *
 * BMediaClientNode multiplexes BBufferProducer, BBufferConsumer, and
 * BMediaEventLooper roles on behalf of its owning BMediaClient. It translates
 * media graph callbacks (format negotiation, connection, buffer events) into
 * the higher-level BMediaClient/BMediaConnection API.
 *
 * @see BMediaClient, BMediaConnection
 */


#include "MediaClientNode.h"

#include <MediaClient.h>
#include <MediaConnection.h>
#include <MediaRoster.h>
#include <scheduler.h>
#include <TimeSource.h>

#include <string.h>

#include "MediaDebug.h"

#define B_NEW_BUFFER (BTimedEventQueue::B_USER_EVENT + 1)


/**
 * @brief Constructs the BMediaClientNode for the given owner and media type.
 *
 * Registers B_BUFFER_PRODUCER, B_BUFFER_CONSUMER, and/or B_CONTROLLABLE node
 * kinds based on the owner's media_client_kinds.
 *
 * @param name   Human-readable name forwarded to BMediaNode.
 * @param owner  The BMediaClient that owns and receives callbacks from this node.
 * @param type   Media type used for both BBufferConsumer and BBufferProducer.
 */
BMediaClientNode::BMediaClientNode(const char* name,
	BMediaClient* owner, media_type type)
	:
	BMediaNode(name),
	BBufferConsumer(type),
	BBufferProducer(type),
	BMediaEventLooper(),
	fOwner(owner)
{
	CALLED();

	// Configure the node to do the requested jobs
	if (fOwner->Kinds() & B_MEDIA_PLAYER)
		AddNodeKind(B_BUFFER_PRODUCER);
	if (fOwner->Kinds() & B_MEDIA_RECORDER)
		AddNodeKind(B_BUFFER_CONSUMER);
	if (fOwner->Kinds() & B_MEDIA_CONTROLLABLE)
		AddNodeKind(B_CONTROLLABLE);
}


/**
 * @brief Sends a buffer from a local output connection to its destination.
 *
 * Delegates to BBufferProducer::SendBuffer() using the connection's
 * source and destination endpoints.
 *
 * @param buffer  The BBuffer to send.
 * @param conn    The BMediaConnection identifying the source/destination pair.
 * @return B_OK on success, or an error code from BBufferProducer::SendBuffer.
 */
status_t
BMediaClientNode::SendBuffer(BBuffer* buffer, BMediaConnection* conn)
{
	return BBufferProducer::SendBuffer(buffer, conn->_Source(), conn->_Destination());
}


/**
 * @brief Returns the media add-on that hosts this node, if any.
 *
 * Forwards to the owner's AddOn() implementation.
 *
 * @param id  Output pointer for the add-on-assigned node ID.
 * @return Pointer to the BMediaAddOn, or NULL if not add-on hosted.
 */
BMediaAddOn*
BMediaClientNode::AddOn(int32* id) const
{
	CALLED();

	return fOwner->AddOn(id);
}


/**
 * @brief Called by the Media Roster after the node is successfully registered.
 *
 * Notifies the owner via ClientRegistered() and starts the event looper thread.
 */
void
BMediaClientNode::NodeRegistered()
{
	CALLED();

	fOwner->ClientRegistered();

	Run();
}


/**
 * @brief Sets the thread scheduling priority appropriate for the run mode and media type.
 *
 * Maps run mode and consumer media type to a Media Kit scheduling priority
 * constant, then calls suggest_thread_priority() before forwarding to
 * BMediaNode::SetRunMode().
 *
 * @param mode  The requested BMediaNode::run_mode.
 */
void
BMediaClientNode::SetRunMode(run_mode mode)
{
	CALLED();

	int32 priority;
	if (mode == BMediaNode::B_OFFLINE)
		priority = B_OFFLINE_PROCESSING;
	else {
		switch(ConsumerType()) {
			case B_MEDIA_RAW_AUDIO:
			case B_MEDIA_ENCODED_AUDIO:
				priority = B_AUDIO_RECORDING;
				break;

			case B_MEDIA_RAW_VIDEO:
			case B_MEDIA_ENCODED_VIDEO:
				priority = B_VIDEO_RECORDING;
				break;

			default:
				priority = B_DEFAULT_MEDIA_PRIORITY;
		}
	}

	SetPriority(suggest_thread_priority(priority));
	BMediaNode::SetRunMode(mode);
}


/**
 * @brief Schedules the start event in the event looper queue.
 *
 * @param performanceTime  Performance timestamp at which to start.
 */
void
BMediaClientNode::Start(bigtime_t performanceTime)
{
	CALLED();

	BMediaEventLooper::Start(performanceTime);
}


/**
 * @brief Schedules the stop event in the event looper queue.
 *
 * @param performanceTime  Performance timestamp at which to stop.
 * @param immediate        If true, stop as soon as possible.
 */
void
BMediaClientNode::Stop(bigtime_t performanceTime, bool immediate)
{
	CALLED();

	BMediaEventLooper::Stop(performanceTime, immediate);
}


/**
 * @brief Schedules a seek event in the event looper queue.
 *
 * @param mediaTime        The media time to seek to.
 * @param performanceTime  Performance timestamp at which to execute the seek.
 */
void
BMediaClientNode::Seek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	CALLED();

	BMediaEventLooper::Seek(mediaTime, performanceTime);
}


/**
 * @brief Schedules a time-warp event in the event looper queue.
 *
 * @param realTime         Real time of the warp event.
 * @param performanceTime  New performance time mapping.
 */
void
BMediaClientNode::TimeWarp(bigtime_t realTime, bigtime_t performanceTime)
{
	CALLED();

	BMediaEventLooper::TimeWarp(realTime, performanceTime);
}


/**
 * @brief Handles an inbound node message (currently unimplemented).
 *
 * @param message  The message opcode.
 * @param data     Message payload.
 * @param size     Size of the payload.
 * @return B_ERROR always.
 */
status_t
BMediaClientNode::HandleMessage(int32 message,
	const void* data, size_t size)
{
	CALLED();

	return B_ERROR;
}


/**
 * @brief Validates a proposed format for a consumer input destination.
 *
 * Delegates to the BMediaInput found for \a dest.
 *
 * @param dest    The destination endpoint being queried.
 * @param format  In/out format to accept or modify.
 * @return B_OK if accepted, B_MEDIA_BAD_DESTINATION if not found, or the
 *         connection's AcceptFormat() result.
 */
status_t
BMediaClientNode::AcceptFormat(const media_destination& dest,
	media_format* format)
{
	CALLED();

	BMediaInput* conn = fOwner->_FindInput(dest);
	if (conn == NULL)
		return B_MEDIA_BAD_DESTINATION;

	return conn->AcceptFormat(format);
}


/**
 * @brief Iterates over registered inputs, filling \a input for each cookie value.
 *
 * @param cookie  In/out iteration cookie; starts at 0 and advances by 1 each call.
 * @param input   Output pointer filled with the next media_input descriptor.
 * @return B_OK if an input was returned, B_BAD_INDEX when iteration is complete.
 */
status_t
BMediaClientNode::GetNextInput(int32* cookie,
	media_input* input)
{
	CALLED();

	if (fOwner->CountInputs() == 0)
		return B_BAD_INDEX;

	if (*cookie < 0 || *cookie >= fOwner->CountInputs()) {
		*cookie = -1;
		input = NULL;
	} else {
		BMediaInput* conn = fOwner->InputAt(*cookie);
		if (conn != NULL) {
			*input = conn->fConnection._BuildMediaInput();
			*cookie += 1;
			return B_OK;
		}
	}
	return B_BAD_INDEX;
}


/**
 * @brief Releases any resources allocated for an input iteration cookie.
 *
 * The default implementation does nothing; cookies are plain integers.
 *
 * @param cookie  The cookie value returned by a prior GetNextInput() call.
 */
void
BMediaClientNode::DisposeInputCookie(int32 cookie)
{
	CALLED();
}


/**
 * @brief Enqueues a received buffer as a B_HANDLE_BUFFER timed event.
 *
 * @param buffer  The incoming BBuffer whose start_time schedules the event.
 */
void
BMediaClientNode::BufferReceived(BBuffer* buffer)
{
	CALLED();

	EventQueue()->AddEvent(media_timed_event(buffer->Header()->start_time,
		BTimedEventQueue::B_HANDLE_BUFFER, buffer,
		BTimedEventQueue::B_RECYCLE_BUFFER));
}


/**
 * @brief Queries the downstream latency for a given consumer destination.
 *
 * @param dest        The consumer destination to query.
 * @param latency     Output pointer for the latency in microseconds.
 * @param timesource  Output pointer for the time source node ID.
 * @return B_OK on success, B_MEDIA_BAD_DESTINATION if not found.
 */
status_t
BMediaClientNode::GetLatencyFor(const media_destination& dest,
	bigtime_t* latency, media_node_id* timesource)
{
	CALLED();

	BMediaInput* conn = fOwner->_FindInput(dest);
	if (conn == NULL)
		return B_MEDIA_BAD_DESTINATION;

	//*latency = conn->fLatency;
	*timesource = TimeSource()->ID();
	return B_OK;
}


/**
 * @brief Called by the Media Roster when an input connection is established.
 *
 * Updates the connection's source, format, and remote node information, then
 * calls BMediaInput::Connected() to notify the application.
 *
 * @param source    The connected upstream media_source.
 * @param dest      The local media_destination that was connected.
 * @param format    The negotiated media_format.
 * @param outInput  Output parameter filled with the final media_input.
 * @return B_OK on success, B_MEDIA_BAD_DESTINATION if not found.
 */
status_t
BMediaClientNode::Connected(const media_source& source,
	const media_destination& dest, const media_format& format,
	media_input* outInput)
{
	CALLED();

	BMediaInput* conn = fOwner->_FindInput(dest);
	if (conn == NULL)
		return B_MEDIA_BAD_DESTINATION;

	conn->fConnection.source = source;
	conn->fConnection.format = format;

	// Retrieve the node without using GetNodeFor that's pretty inefficient.
	// Unfortunately we don't have an alternative which doesn't require us
	// to release the cloned node.
	// However, our node will not have flags set. Keep in mind this.
	conn->fConnection.remote_node.node
		= BMediaRoster::CurrentRoster()->NodeIDFor(source.port);
	conn->fConnection.remote_node.port = source.port;

	conn->Connected(format);

	*outInput = conn->fConnection._BuildMediaInput();
	return B_OK;
}


/**
 * @brief Called by the Media Roster when an input connection is broken.
 *
 * Resets the connection's source, format, and remote node, then calls
 * BMediaInput::Disconnected().
 *
 * @param source  The upstream media_source that disconnected.
 * @param dest    The local media_destination that was disconnected.
 */
void
BMediaClientNode::Disconnected(const media_source& source,
	const media_destination& dest)
{
	CALLED();

	BMediaInput* conn = fOwner->_FindInput(dest);
	if (conn == NULL)
		return;

	if (conn->_Source() == source) {
		// Cleanup the connection
		conn->fConnection.source = media_source::null;
		conn->fConnection.format = media_format();

		conn->fConnection.remote_node.node = -1;
		conn->fConnection.remote_node.port = -1;

		conn->Disconnected();
	}
}


/**
 * @brief Called when the upstream producer changes its output format.
 *
 * Not yet implemented; returns B_ERROR.
 *
 * @param source  The upstream source whose format changed.
 * @param dest    The local destination affected.
 * @param tag     Opaque tag identifying this format-change transaction.
 * @param format  The new proposed media_format.
 * @return B_ERROR always.
 */
status_t
BMediaClientNode::FormatChanged(const media_source& source,
	const media_destination& dest,
	int32 tag, const media_format& format)
{
	CALLED();
	return B_ERROR;
}


/**
 * @brief Returns a suitable output format for the requested media type and quality.
 *
 * First tries the owner's FormatSuggestion() hook; if that fails, returns a
 * generic format of the owner's media type.
 *
 * @param type     The requested media type.
 * @param quality  Hint about the desired quality level.
 * @param format   Output pointer filled with the suggested format.
 * @return B_OK on success, B_MEDIA_BAD_FORMAT if the type is unsupported.
 */
status_t
BMediaClientNode::FormatSuggestionRequested(media_type type,
	int32 quality, media_format* format)
{
	CALLED();

	if (type != ConsumerType()
			&& type != ProducerType()) {
		return B_MEDIA_BAD_FORMAT;
	}

	status_t ret = fOwner->FormatSuggestion(type, quality, format);
	if (ret != B_OK) {
		// In that case we return just a very generic format.
		media_format outFormat;
		outFormat.type = fOwner->MediaType();
		*format = outFormat;
		return B_OK;
	}

	return ret;
}


/**
 * @brief Validates a format proposed for a producer output source.
 *
 * Delegates to the BMediaOutput found for \a source.
 *
 * @param source  The local source endpoint.
 * @param format  In/out format to accept or modify.
 * @return B_OK if accepted, B_MEDIA_BAD_DESTINATION if the source is not found,
 *         or the connection's FormatProposal() result.
 */
status_t
BMediaClientNode::FormatProposal(const media_source& source,
	media_format* format)
{
	CALLED();

	BMediaOutput* conn = fOwner->_FindOutput(source);
	if (conn == NULL)
		return B_MEDIA_BAD_DESTINATION;

	return conn->FormatProposal(format);
}


/**
 * @brief Handles a downstream request to change the output format (not implemented).
 *
 * @param source       The local source whose format is requested to change.
 * @param dest         The downstream destination requesting the change.
 * @param format       In/out format descriptor.
 * @param _deprecated_ Unused legacy parameter.
 * @return B_ERROR always.
 */
status_t
BMediaClientNode::FormatChangeRequested(const media_source& source,
	const media_destination& dest, media_format* format,
	int32* _deprecated_)
{
	CALLED();

	return B_ERROR;
}


/**
 * @brief Notifies the producer that a downstream consumer is running late.
 *
 * The default implementation does nothing.
 *
 * @param source  The affected output source.
 * @param late    How late the last buffer arrived, in microseconds.
 * @param when    The performance time at which lateness was detected.
 */
void
BMediaClientNode::LateNoticeReceived(const media_source& source,
	bigtime_t late, bigtime_t when)
{
	CALLED();

}


/**
 * @brief Iterates over registered outputs, filling \a output for each cookie value.
 *
 * @param cookie  In/out iteration cookie; starts at 0 and advances by 1 each call.
 * @param output  Output pointer filled with the next media_output descriptor.
 * @return B_OK if an output was returned, B_BAD_INDEX when iteration is complete.
 */
status_t
BMediaClientNode::GetNextOutput(int32* cookie, media_output* output)
{
	CALLED();

	if (fOwner->CountOutputs() == 0)
		return B_BAD_INDEX;

	if (*cookie < 0 || *cookie >= fOwner->CountOutputs()) {
		*cookie = -1;
		output = NULL;
	} else {
		BMediaOutput* conn = fOwner->OutputAt(*cookie);
		if (conn != NULL) {
			*output = conn->fConnection._BuildMediaOutput();
			*cookie += 1;
			return B_OK;
		}
	}
	return B_BAD_INDEX;
}


/**
 * @brief Releases any resources allocated for an output iteration cookie.
 *
 * @param cookie  The cookie value to dispose.
 * @return B_OK always.
 */
status_t
BMediaClientNode::DisposeOutputCookie(int32 cookie)
{
	CALLED();

	return B_OK;
}


/**
 * @brief Assigns a new buffer group to a producer output source.
 *
 * Replaces the connection's current BBufferGroup. If \a group is NULL,
 * a new default group of three buffers is created.
 *
 * @param source  The source endpoint whose buffer group is being set.
 * @param group   New BBufferGroup to use, or NULL to allocate a default one.
 * @return B_OK on success, B_MEDIA_BAD_SOURCE if not found, or B_NO_MEMORY.
 */
status_t
BMediaClientNode::SetBufferGroup(const media_source& source, BBufferGroup* group)
{
	CALLED();

	BMediaOutput* conn = fOwner->_FindOutput(source);
	if (conn == NULL)
		return B_MEDIA_BAD_SOURCE;

	if (group == conn->fBufferGroup)
		return B_OK;

	delete conn->fBufferGroup;

	if (group != NULL) {
		conn->fBufferGroup = group;
		return B_OK;
	}

	conn->fBufferGroup = new BBufferGroup(conn->BufferSize(), 3);
	if (conn->fBufferGroup == NULL)
		return B_NO_MEMORY;

	return conn->fBufferGroup->InitCheck();
}


/**
 * @brief Called just before a connection is established to finalise the format.
 *
 * Validates the proposed format against the owner's media type, stores the
 * destination, calls PrepareToConnect() on the connection, and fills
 * \a out_source and \a name.
 *
 * @param source      The local source endpoint.
 * @param dest        The remote destination endpoint.
 * @param format      In/out negotiated media_format.
 * @param out_source  Output pointer set to the confirmed source endpoint.
 * @param name        Output buffer filled with the connection's name.
 * @return B_OK on success, B_MEDIA_BAD_SOURCE if not found,
 *         B_MEDIA_ALREADY_CONNECTED, or B_MEDIA_BAD_FORMAT.
 */
status_t
BMediaClientNode::PrepareToConnect(const media_source& source,
	const media_destination& dest, media_format* format,
	media_source* out_source, char *name)
{
	CALLED();

	BMediaOutput* conn = fOwner->_FindOutput(source);
	if (conn == NULL)
		return B_MEDIA_BAD_SOURCE;

	if (conn->_Destination() != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;

	if (fOwner->MediaType() != B_MEDIA_UNKNOWN_TYPE
			&& format->type != fOwner->MediaType()) {
		return B_MEDIA_BAD_FORMAT;
	}

	conn->fConnection.destination = dest;

	status_t err = conn->PrepareToConnect(format);
	if (err != B_OK)
		return err;

	*out_source = conn->_Source();
	strcpy(name, conn->Name());

	return B_OK;
}


/**
 * @brief Called after BMediaRoster::Connect() completes to finalise a connection.
 *
 * Updates the output connection's destination, format, and remote node
 * information, allocates the buffer group, and notifies the connection via
 * BMediaOutput::Connected().
 *
 * @param status  Result of the connection attempt; if non-B_OK, returns early.
 * @param source  The confirmed source endpoint.
 * @param dest    The confirmed destination endpoint.
 * @param format  The final negotiated media_format.
 * @param name    Output buffer filled with the connection's name.
 */
void
BMediaClientNode::Connect(status_t status, const media_source& source,
	const media_destination& dest, const media_format& format,
	char* name)
{
	CALLED();

	BMediaOutput* conn = fOwner->_FindOutput(source);
	if (conn == NULL)
		return;

	// Connection failed, return.
	if (status != B_OK)
		return;

	conn->fConnection.destination = dest;
	conn->fConnection.format = format;

	// Retrieve the node without using GetNodeFor that's pretty inefficient.
	// Unfortunately we don't have an alternative which doesn't require us
	// to release the cloned node.
	// However, our node will not have flags set. Keep in mind this.
	conn->fConnection.remote_node.node
		= BMediaRoster::CurrentRoster()->NodeIDFor(dest.port);
	conn->fConnection.remote_node.port = dest.port;

	strcpy(name, conn->Name());

	// TODO: add correct latency estimate
	SetEventLatency(1000);

	conn->fBufferGroup = new BBufferGroup(conn->BufferSize(), 3);
	if (conn->fBufferGroup == NULL)
		TRACE("Can't allocate the buffer group\n");

	conn->Connected(format);
}


/**
 * @brief Called when a producer-side connection is torn down.
 *
 * Frees the buffer group, resets destination and format fields, and notifies
 * the connection via BMediaOutput::Disconnected().
 *
 * @param source  The source endpoint that was disconnected.
 * @param dest    The remote destination that was disconnected.
 */
void
BMediaClientNode::Disconnect(const media_source& source,
	const media_destination& dest)
{
	CALLED();

	BMediaOutput* conn = fOwner->_FindOutput(source);
	if (conn == NULL)
		return;

	if (conn->_Destination() == dest) {
		// Cleanup the connection
		delete conn->fBufferGroup;
		conn->fBufferGroup = NULL;

		conn->fConnection.destination = media_destination::null;
		conn->fConnection.format = media_format();

		conn->fConnection.remote_node.node = -1;
		conn->fConnection.remote_node.port = -1;

		conn->Disconnected();
	}
}


/**
 * @brief Enables or disables data flow on a producer output source.
 *
 * @param source       The output source to modify.
 * @param enabled      true to enable, false to disable.
 * @param _deprecated_ Unused legacy parameter.
 */
void
BMediaClientNode::EnableOutput(const media_source& source,
	bool enabled, int32* _deprecated_)
{
	CALLED();

	BMediaOutput* conn = fOwner->_FindOutput(source);
	if (conn != NULL)
		conn->_SetEnabled(enabled);
}


/**
 * @brief Returns the total downstream latency of this producer.
 *
 * @param outLatency  Output pointer for the latency in microseconds.
 * @return B_OK on success, or an error code from BBufferProducer::GetLatency.
 */
status_t
BMediaClientNode::GetLatency(bigtime_t* outLatency)
{
	CALLED();

	return BBufferProducer::GetLatency(outLatency);
}


/**
 * @brief Notification that the downstream latency on a connection has changed.
 *
 * The default implementation does nothing.
 *
 * @param source   The source endpoint.
 * @param dest     The destination endpoint.
 * @param latency  New latency value in microseconds.
 * @param flags    Latency change flags.
 */
void
BMediaClientNode::LatencyChanged(const media_source& source,
	const media_destination& dest, bigtime_t latency, uint32 flags)
{
	CALLED();
}


/**
 * @brief Notifies the producer about data status changes at a destination.
 *
 * The default implementation does nothing.
 *
 * @param dest    The affected consumer destination.
 * @param status  Status code describing the change.
 * @param when    Performance time of the status change.
 */
void
BMediaClientNode::ProducerDataStatus(const media_destination& dest,
	int32 status, bigtime_t when)
{
	CALLED();
}


/**
 * @brief Dispatches timed events from the event looper to appropriate handlers.
 *
 * Handles B_HANDLE_BUFFER (consumer path), B_NEW_BUFFER (producer path),
 * B_START, B_STOP, B_SEEK, and B_WARP events.
 *
 * @param event          The timed event to process.
 * @param late           How late this event is being processed, in microseconds.
 * @param realTimeEvent  true if this is a real-time (non-performance-time) event.
 */
void
BMediaClientNode::HandleEvent(const media_timed_event* event,
	bigtime_t late, bool realTimeEvent)
{
	CALLED();

	switch (event->type) {
		// This event is used for inputs which consumes buffers
		// or binded connections which also send them to an output.
		case BTimedEventQueue::B_HANDLE_BUFFER:
			_HandleBuffer((BBuffer*)event->pointer);
			break;

		// This is used for connections which produce buffers only.
		case B_NEW_BUFFER:
			_ProduceNewBuffer(event, late);
			break;

		case BTimedEventQueue::B_START:
		{
			if (RunState() != B_STARTED)
				fOwner->HandleStart(event->event_time);

			fStartTime = event->event_time;

			_ScheduleConnections(event->event_time);
			break;
		}

		case BTimedEventQueue::B_STOP:
		{
			fOwner->HandleStop(event->event_time);

			EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
				BTimedEventQueue::B_HANDLE_BUFFER);
			break;
		}

		case BTimedEventQueue::B_SEEK:
			fOwner->HandleSeek(event->event_time, event->bigdata);
			break;

		case BTimedEventQueue::B_WARP:
			// NOTE: We have no need to handle it
			break;
	}
}


/**
 * @brief Destroys the node and quits the event looper thread.
 */
BMediaClientNode::~BMediaClientNode()
{
	CALLED();

	Quit();
}


/**
 * @brief Enqueues a B_NEW_BUFFER event for each unbound output at start time.
 *
 * Only outputs that are not bound to an input are scheduled here; bound
 * outputs forward received buffers rather than generating new ones.
 *
 * @param eventTime  The performance time of the start event.
 */
void
BMediaClientNode::_ScheduleConnections(bigtime_t eventTime)
{
	for (int32 i = 0; i < fOwner->CountOutputs(); i++) {
		BMediaOutput* output = fOwner->OutputAt(i);

		if (output->HasBinding())
			continue;

		media_timed_event firstBufferEvent(eventTime,
			B_NEW_BUFFER);

		output->fFramesSent = 0;

		firstBufferEvent.pointer = (void*) output;
		EventQueue()->AddEvent(firstBufferEvent);
	}
}


/**
 * @brief Delivers a received buffer to the appropriate input and its binding.
 *
 * Looks up the BMediaInput by destination ID, calls HandleBuffer(), then if
 * the input is bound, forwards the buffer to the bound BMediaOutput.
 *
 * @param buffer  The incoming BBuffer to handle.
 */
void
BMediaClientNode::_HandleBuffer(BBuffer* buffer)
{
	CALLED();

	media_destination dest;
	dest.id = buffer->Header()->destination;
	BMediaInput* conn = fOwner->_FindInput(dest);

	if (conn != NULL)
		conn->HandleBuffer(buffer);

	// TODO: Investigate system level latency logging

	if (conn->HasBinding()) {
		BMediaOutput* output = dynamic_cast<BMediaOutput*>(conn->Binding());
		output->SendBuffer(buffer);
	}
}


/**
 * @brief Generates and sends a new buffer for a producing output connection.
 *
 * Requests a buffer from the output's group, fills the header, calls the
 * output's SendBuffer(), and schedules the next B_NEW_BUFFER event based on
 * the audio frame rate.
 *
 * @param event  The timed event carrying a pointer to the BMediaOutput.
 * @param late   How late the event is, in microseconds.
 */
void
BMediaClientNode::_ProduceNewBuffer(const media_timed_event* event,
	bigtime_t late)
{
	CALLED();

	if (RunState() != BMediaEventLooper::B_STARTED)
		return;

	// The connection is get through the event
	BMediaOutput* output
		= dynamic_cast<BMediaOutput*>((BMediaConnection*)event->pointer);
	if (output == NULL)
		return;

	if (output->_IsEnabled()) {
		BBuffer* buffer = _GetNextBuffer(output, event->event_time);

		if (buffer != NULL) {
			if (output->SendBuffer(buffer) != B_OK) {
				TRACE("BMediaClientNode: Failed to send buffer\n");
				// The output failed, let's recycle the buffer
				buffer->Recycle();
			}
		}
	}

	bigtime_t time = 0;
	media_format format = output->fConnection.format;
	if (format.IsAudio()) {
		size_t nFrames = format.u.raw_audio.buffer_size
			/ ((format.u.raw_audio.format
				& media_raw_audio_format::B_AUDIO_SIZE_MASK)
			* format.u.raw_audio.channel_count);
		output->fFramesSent += nFrames;

		time = fStartTime + bigtime_t((1000000LL * output->fFramesSent)
			/ (int32)format.u.raw_audio.frame_rate);
	}

	media_timed_event nextEvent(time, B_NEW_BUFFER);
	EventQueue()->AddEvent(nextEvent);
}


/**
 * @brief Allocates and initialises a buffer for a producing output connection.
 *
 * Requests a buffer from the output's BBufferGroup, fills the media_header
 * with the format type, size, time source, and start time.
 *
 * @param output     The BMediaOutput requesting a new buffer.
 * @param eventTime  Performance time stamp to write into the buffer header.
 * @return Pointer to the allocated BBuffer, or NULL if allocation fails.
 */
BBuffer*
BMediaClientNode::_GetNextBuffer(BMediaOutput* output, bigtime_t eventTime)
{
	CALLED();

	BBuffer* buffer
		= output->fBufferGroup->RequestBuffer(output->BufferSize(), 0);
	if (buffer == NULL) {
		TRACE("MediaClientNode:::_GetNextBuffer: Failed to get the buffer\n");
		return NULL;
	}

	media_header* header = buffer->Header();
	header->type = output->fConnection.format.type;
	header->size_used = output->BufferSize();
	header->time_source = TimeSource()->ID();
	header->start_time = eventTime;

	return buffer;
}
