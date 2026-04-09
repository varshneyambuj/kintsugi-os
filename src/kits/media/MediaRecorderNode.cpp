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
 *   Copyright 2014-2016, Dario Casalinuovo
 *   Copyright 1999, Be Incorporated
 *   All Rights Reserved.
 *   This file may be used under the terms of the Be Sample Code License.
 */

/** @file MediaRecorderNode.cpp
 *  @brief Internal BMediaNode/BBufferConsumer node backing BMediaRecorder.
 */


#include "MediaRecorderNode.h"

#include <Buffer.h>
#include <scheduler.h>
#include <MediaRoster.h>
#include <MediaRosterEx.h>
#include <TimedEventQueue.h>
#include <TimeSource.h>

#include "MediaDebug.h"


/** @brief Constructs the BMediaRecorderNode and initialises its single input.
 *  @param name     Name for the node (also used as input name prefix).
 *  @param recorder Pointer to the owning BMediaRecorder.
 *  @param type     The media_type this node will consume.
 */
BMediaRecorderNode::BMediaRecorderNode(const char* name,
	BMediaRecorder* recorder, media_type type)
	:
	BMediaNode(name),
	BMediaEventLooper(),
	BBufferConsumer(type),
	fRecorder(recorder),
	fConnectMode(true)
{
	CALLED();

	fInput.node = Node();
	fInput.destination.id = 1;
	fInput.destination.port = ControlPort();

	fName.SetTo(name);

	BString str(name);
	str << " Input";
	strcpy(fInput.name, str.String());
}


/** @brief Destructor. */
BMediaRecorderNode::~BMediaRecorderNode()
{
	CALLED();
}


/** @brief Returns NULL because this node is not part of an add-on.
 *  @param id Output set to -1.
 *  @return NULL.
 */
BMediaAddOn*
BMediaRecorderNode::AddOn(int32* id) const
{
	CALLED();

	if (id)
		*id = -1;

	return NULL;
}


/** @brief Called by the media kit after the node is registered; starts the event loop. */
void
BMediaRecorderNode::NodeRegistered()
{
	CALLED();
	Run();
}


/** @brief Sets the scheduling priority appropriate for the current run mode and consumer type.
 *  @param mode The new run_mode.
 */
void
BMediaRecorderNode::SetRunMode(run_mode mode)
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


/** @brief Updates the accepted format on the internal input and the OK format.
 *  @param format The new accepted media_format.
 */
void
BMediaRecorderNode::SetAcceptedFormat(const media_format& format)
{
	CALLED();

	fInput.format = format;
	fOKFormat = format;
}


/** @brief Returns the currently accepted media format.
 *  @return Const reference to the accepted media_format.
 */
const media_format&
BMediaRecorderNode::AcceptedFormat() const
{
	CALLED();

	return fInput.format;
}


/** @brief Fills in the caller's media_input with the node's single input descriptor.
 *  @param outInput Pointer to a media_input struct to fill in.
 */
void
BMediaRecorderNode::GetInput(media_input* outInput)
{
	CALLED();

	fInput.node = Node();
	*outInput = fInput;
}


/** @brief Enables or disables data flow through the connected input.
 *  @param enabled true to unmute (allow data), false to mute.
 */
void
BMediaRecorderNode::SetDataEnabled(bool enabled)
{
	CALLED();

	int32 tag;

	SetOutputEnabled(fInput.source,
		fInput.destination, enabled, NULL, &tag);
}


/** @brief Controls whether the node should resolve the producer node internally on connect.
 *  @param connectMode true to resolve internally, false to skip.
 */
void
BMediaRecorderNode::ActivateInternalConnect(bool connectMode)
{
	fConnectMode = connectMode;
}


/** @brief Event handler; all events are silently ignored.
 *  @param event       Pointer to the timed media event.
 *  @param lateness    How late the event is in microseconds.
 *  @param realTimeEvent Whether this is a real-time event.
 */
void
BMediaRecorderNode::HandleEvent(const media_timed_event* event,
	bigtime_t lateness, bool realTimeEvent)
{
	CALLED();

	// we ignore them all!
}


/** @brief Notifies the owner recorder and sets the running flag when the node starts.
 *  @param performanceTime The performance time at which the start takes effect.
 */
void
BMediaRecorderNode::Start(bigtime_t performanceTime)
{
	CALLED();

	if (fRecorder->fNotifyHook)
		(*fRecorder->fNotifyHook)(fRecorder->fBufferCookie,
			BMediaRecorder::B_WILL_START, performanceTime);

	fRecorder->fRunning = true;
}


/** @brief Notifies the owner recorder and clears the running flag when the node stops.
 *  @param performanceTime The performance time at which the stop takes effect.
 *  @param immediate       If true, stop should happen as soon as possible.
 */
void
BMediaRecorderNode::Stop(bigtime_t performanceTime, bool immediate)
{
	CALLED();

	if (fRecorder->fNotifyHook)
		(*fRecorder->fNotifyHook)(fRecorder->fBufferCookie,
			BMediaRecorder::B_WILL_STOP, performanceTime, immediate);

	fRecorder->fRunning = false;
}


/** @brief Notifies the owner recorder of an impending seek operation.
 *  @param mediaTime       The media time being sought to.
 *  @param performanceTime The performance time at which the seek takes effect.
 */
void
BMediaRecorderNode::Seek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	CALLED();

	if (fRecorder->fNotifyHook)
		(*fRecorder->fNotifyHook)(fRecorder->fBufferCookie,
			BMediaRecorder::B_WILL_SEEK, performanceTime, mediaTime);
}


/** @brief Notifies the owner recorder of a time-warp event.
 *         Since buffers arrive pre-time-stamped, the warp itself is otherwise ignored.
 *  @param realTime        The real time of the warp.
 *  @param performanceTime The performance time of the warp.
 */
void
BMediaRecorderNode::TimeWarp(bigtime_t realTime, bigtime_t performanceTime)
{
	CALLED();

	// Since buffers will come pre-time-stamped, we only need to look
	// at them, so we can ignore the time warp as a consumer.
	if (fRecorder->fNotifyHook)
		(*fRecorder->fNotifyHook)(fRecorder->fBufferCookie,
			BMediaRecorder::B_WILL_TIMEWARP, realTime, performanceTime);
}


/** @brief Dispatches an incoming message to the appropriate BMediaNode base class handler.
 *  @param message The message code.
 *  @param data    Pointer to message data.
 *  @param size    Size of the message data in bytes.
 *  @return B_OK on success, B_ERROR if no handler claimed the message.
 */
status_t
BMediaRecorderNode::HandleMessage(int32 message,
	const void* data, size_t size)
{
	CALLED();

	if (BBufferConsumer::HandleMessage(message, data, size) < 0
		&& BMediaEventLooper::HandleMessage(message, data, size) < 0
		&& BMediaNode::HandleMessage(message, data, size) < 0) {
		HandleBadMessage(message, data, size);
		return B_ERROR;
	}
	return B_OK;
}


/** @brief Accepts the given format if compatible with the node's OK format;
 *         otherwise fills @p format with the acceptable format and returns B_MEDIA_BAD_FORMAT.
 *  @param dest   The destination this format is proposed for.
 *  @param format In/out pointer to the proposed media_format.
 *  @return B_OK if compatible, B_MEDIA_BAD_FORMAT otherwise.
 */
status_t
BMediaRecorderNode::AcceptFormat(const media_destination& dest,
	media_format* format)
{
	CALLED();

	if (format_is_compatible(*format, fOKFormat))
		return B_OK;

	*format = fOKFormat;

	return B_MEDIA_BAD_FORMAT;
}


/** @brief Iterates over available inputs; this node exposes a single input.
 *  @param cookie   In/out iteration cookie (set to 0 before first call).
 *  @param outInput Pointer to a media_input struct to fill in.
 *  @return B_OK for the first call, B_BAD_INDEX on subsequent calls.
 */
status_t
BMediaRecorderNode::GetNextInput(int32* cookie, media_input* outInput)
{
	CALLED();

	if (*cookie == 0) {
		*cookie = -1;
		*outInput = fInput;
		return B_OK;
	}

	return B_BAD_INDEX;
}


/** @brief Disposes of an input iteration cookie (no-op).
 *  @param cookie The cookie to dispose of.
 */
void
BMediaRecorderNode::DisposeInputCookie(int32 cookie)
{
	CALLED();
}


/** @brief Delivers a received buffer to the owner recorder and recycles the buffer.
 *  @param buffer Pointer to the incoming BBuffer.
 */
void
BMediaRecorderNode::BufferReceived(BBuffer* buffer)
{
	CALLED();

	fRecorder->BufferReceived(buffer->Data(), buffer->SizeUsed(),
		*buffer->Header());

	buffer->Recycle();
}


/** @brief Called when the producer's data status changes; currently a no-op.
 *  @param forWhom         The destination this status pertains to.
 *  @param status          The new data status code.
 *  @param performanceTime The performance time of the status change.
 */
void
BMediaRecorderNode::ProducerDataStatus(
	const media_destination& forWhom, int32 status,
	bigtime_t performanceTime)
{
	CALLED();
}


/** @brief Returns the latency of this consumer node (zero) and the time source ID.
 *  @param forWhom       The destination being queried.
 *  @param outLatency    Output latency in microseconds (set to 0).
 *  @param outTimesource Output time source node ID.
 *  @return B_OK.
 */
status_t
BMediaRecorderNode::GetLatencyFor(const media_destination& forWhom,
	bigtime_t* outLatency, media_node_id* outTimesource)
{
	CALLED();

	*outLatency = 0;
	*outTimesource = TimeSource()->ID();

	return B_OK;
}


/** @brief Called when a connection is established; records the producer and format,
 *         and notifies the owner BMediaRecorder.
 *  @param producer   The connected producer media_source.
 *  @param where      The destination this connection was made to.
 *  @param withFormat The negotiated media_format for the connection.
 *  @param outInput   Output pointer filled with the accepted media_input.
 *  @return B_OK on success, B_MEDIA_BAD_NODE if the node lookup fails.
 */
status_t
BMediaRecorderNode::Connected(const media_source &producer,
	const media_destination &where, const media_format &withFormat,
	media_input* outInput)
{
	CALLED();

	fInput.source = producer;
	fInput.format = withFormat;
	*outInput = fInput;

	if (fConnectMode == true) {
		// This is a workaround needed for us to get the node
		// so that our owner class can do it's operations.
		media_node node;
		BMediaRosterEx* roster = MediaRosterEx(BMediaRoster::CurrentRoster());
		if (roster->GetNodeFor(roster->NodeIDFor(producer.port), &node) != B_OK)
			return B_MEDIA_BAD_NODE;

		fRecorder->fOutputNode = node;
		fRecorder->fReleaseOutputNode = true;
	}
	fRecorder->SetUpConnection(producer);
	fRecorder->fConnected = true;

	return B_OK;
}


/** @brief Called when the connection is torn down; resets state in both the node
 *         and the owner BMediaRecorder.
 *  @param producer The disconnecting producer media_source.
 *  @param where    The destination that was disconnected.
 */
void
BMediaRecorderNode::Disconnected(const media_source& producer,
	const media_destination& where)
{
	CALLED();

	fInput.source = media_source::null;
	// Reset the connection mode
	fConnectMode = true;
	fRecorder->fConnected = false;
	fInput.format = fOKFormat;
}


/** @brief Handles a format change request from the producer.
 *         Accepts the change if the new format is compatible with the OK format.
 *  @param producer The producer requesting the change.
 *  @param consumer The destination receiving the change.
 *  @param tag      Change synchronisation tag.
 *  @param format   The proposed new media_format.
 *  @return B_OK if compatible, B_MEDIA_BAD_FORMAT otherwise.
 */
status_t
BMediaRecorderNode::FormatChanged(const media_source& producer,
	const media_destination& consumer, int32 tag,
	const media_format& format)
{
	CALLED();

	if (!format_is_compatible(format, fOKFormat))
		return B_MEDIA_BAD_FORMAT;

	fInput.format = format;

	return B_OK;
}
