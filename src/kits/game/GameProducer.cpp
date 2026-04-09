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
 *   Copyright 2002-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Christopher ML Zumwalt May (zummy@users.sf.net)
 */

/**
 * @file GameProducer.cpp
 * @brief BMediaNode/BBufferProducer/BMediaEventLooper that mixes GameKit sounds
 *        and delivers them to the system audio mixer.
 *
 * GameProducer acts as a Media Kit producer node for a single GameSoundBuffer.
 * It negotiates a raw-audio connection with the system mixer, manages a
 * BBufferGroup, schedules buffer-production events through the BMediaEventLooper
 * event queue, and calls back into the associated GameSoundBuffer::Play() to
 * fill each outgoing BBuffer with mixed, pan/gain-adjusted audio.
 */


/*	A MediaKit producer node which mixes sound from the GameKit
	and sends them to the audio mixer
*/


#include "GameProducer.h"

#include <string.h>
#include <stdio.h>

#include <Buffer.h>
#include <BufferGroup.h>
#include <ByteOrder.h>
#include <List.h>
#include <MediaDefs.h>
#include <TimeSource.h>

#include "GameSoundBuffer.h"
#include "GameSoundDevice.h"
#include "GSUtility.h"


/**
 * @brief Internal linked-list node used to track sounds scheduled for playback.
 *
 * Each _gs_play record associates a gs_id with a boolean flag pointer
 * that signals whether the sound is still active. The \a next and
 * \a previous pointers form a doubly-linked list.
 */
struct _gs_play  {
	gs_id		sound;
	bool*		hook;

	_gs_play*	next;
	_gs_play*	previous;
};


/**
 * @brief Constructs a GameProducer for the given GameSoundBuffer and audio format.
 *
 * Initialises the BMediaNode, BBufferProducer, and BMediaEventLooper base
 * classes. Sets up the preferred media format from the gs_audio_format
 * supplied, leaves the buffer size as a wildcard so the downstream consumer
 * can negotiate it, and stores a reference to the owning GameSoundBuffer.
 * The output destination is set to null until Connect() is called.
 *
 * @param object Pointer to the GameSoundBuffer that will supply audio data
 *               via its Play() method.
 * @param format Audio format (sample type, channel count, frame rate,
 *               byte order) advertised by this producer.
 */
GameProducer::GameProducer(GameSoundBuffer* object,
	const gs_audio_format* format)
	:
	BMediaNode("GameProducer.h"),
	BBufferProducer(B_MEDIA_RAW_AUDIO),
	BMediaEventLooper(),
	fBufferGroup(NULL),
	fLatency(0),
	fInternalLatency(0),
	fOutputEnabled(true)
{
	// initialize our preferred format object
	fPreferredFormat.type = B_MEDIA_RAW_AUDIO;
	fPreferredFormat.u.raw_audio.format = format->format;
	fPreferredFormat.u.raw_audio.channel_count = format->channel_count;
	fPreferredFormat.u.raw_audio.frame_rate = format->frame_rate; // Hertz
	fPreferredFormat.u.raw_audio.byte_order = format->byte_order;
//	fPreferredFormat.u.raw_audio.channel_mask
//		= B_CHANNEL_LEFT | B_CHANNEL_RIGHT;
//	fPreferredFormat.u.raw_audio.valid_bits = 32;
//	fPreferredFormat.u.raw_audio.matrix_mask = B_MATRIX_AMBISONIC_WXYZ;

	// we'll use the consumer's preferred buffer size, if any
	fPreferredFormat.u.raw_audio.buffer_size
		= media_raw_audio_format::wildcard.buffer_size;

	// we're not connected yet
	fOutput.destination = media_destination::null;
	fOutput.format = fPreferredFormat;

	fFrameSize = get_sample_size(format->format) * format->channel_count;
	fObject = object;
}


/**
 * @brief Destroys the GameProducer and stops the BMediaEventLooper thread.
 */
GameProducer::~GameProducer()
{
	// Stop the BMediaEventLooper thread
	Quit();
}


// BMediaNode methods

/**
 * @brief Returns the BMediaAddOn that instantiated this node, or NULL.
 *
 * GameProducer is not hosted by an add-on, so this always returns NULL.
 *
 * @param internal_id Unused add-on internal identifier output parameter.
 * @return Always NULL.
 */
BMediaAddOn*
GameProducer::AddOn(int32* internal_id) const
{
	return NULL;
}


// BBufferProducer methods

/**
 * @brief Enumerates the producer's outputs one at a time.
 *
 * This node has exactly one output. The cookie is used as an iteration
 * index; when it is non-zero the enumeration is complete.
 *
 * @param cookie  Iteration cookie; must point to 0 on the first call.
 * @param _output Filled with the single media_output descriptor on success.
 * @return B_OK on the first call; B_BAD_INDEX when the cookie is non-zero
 *         (enumeration exhausted).
 */
status_t
GameProducer::GetNextOutput(int32* cookie, media_output* _output)
{
	// we currently support only one output
	if (0 != *cookie)
		return B_BAD_INDEX;

	*_output = fOutput;
	*cookie += 1;
	return B_OK;
}


/**
 * @brief Releases resources associated with an output enumeration cookie.
 *
 * Because the cookie is only an integer counter no cleanup is needed.
 *
 * @param cookie The cookie value returned by GetNextOutput().
 * @return Always B_OK.
 */
status_t
GameProducer::DisposeOutputCookie(int32 cookie)
{
	// do nothing because our cookie is only an integer
	return B_OK;
}


/**
 * @brief Enables or disables delivery of buffers on the specified output.
 *
 * When the output matches fOutput.source the fOutputEnabled flag is updated.
 * Buffers are still produced when disabled, but are recycled instead of being
 * sent downstream.
 *
 * @param what        The media_source identifying the output to control.
 * @param enabled     \c true to enable buffer delivery; \c false to suppress it.
 * @param _deprecated_ Ignored (deprecated parameter from the API).
 */
void
GameProducer::EnableOutput(const media_source& what, bool enabled,
	int32* _deprecated_)
{
	// If I had more than one output, I'd have to walk my list of output records
	// to see which one matched the given source, and then enable/disable that
	// one.  But this node only has one output,  so I just make sure the given
	// source matches, then set the enable state accordingly.
	if (what == fOutput.source)
	{
		fOutputEnabled = enabled;
	}
}


/**
 * @brief Returns the preferred media format when a consumer requests a suggestion.
 *
 * Returns fPreferredFormat for any wildcard or raw-audio type request.
 * Non-audio type requests are rejected.
 *
 * @param type    The requested media type (B_MEDIA_UNKNOWN_TYPE is accepted).
 * @param format  Output parameter filled with the preferred raw-audio format.
 * @return B_OK if the type is acceptable; B_MEDIA_BAD_FORMAT if \a type is
 *         not B_MEDIA_UNKNOWN_TYPE or B_MEDIA_RAW_AUDIO; B_BAD_VALUE if
 *         \a format is NULL.
 */
status_t
GameProducer::FormatSuggestionRequested(media_type type, int32 /*quality*/,
	media_format* format)
{
	// insure that we received a format
	if (!format)
		return B_BAD_VALUE;

	// returning our preferred format
	*format = fPreferredFormat;

	// our format is supported
	if (type == B_MEDIA_UNKNOWN_TYPE)
		return B_OK;

	// we only support raw audo
	return (type != B_MEDIA_RAW_AUDIO) ? B_MEDIA_BAD_FORMAT : B_OK;
}


/**
 * @brief Evaluates a format proposed by a consumer during connection negotiation.
 *
 * Verifies that \a output matches the single output source. If so, the
 * preferred format is written back and B_OK is returned. Non-audio types
 * are rejected.
 *
 * @param output The media_source of the output being negotiated.
 * @param format In/out parameter; replaced with fPreferredFormat on success.
 * @return B_OK if the proposal is acceptable; B_MEDIA_BAD_SOURCE if
 *         \a output does not match; B_MEDIA_BAD_FORMAT for non-audio types.
 */
status_t
GameProducer::FormatProposal(const media_source& output, media_format* format)
{
	// doest the proposed output match our output?
	if (output != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	// return our preferred format
	*format = fPreferredFormat;

	// we will reject the proposal if the format is not audio
	media_type requestedType = format->type;
	if ((requestedType != B_MEDIA_UNKNOWN_TYPE)
		&& (requestedType != B_MEDIA_RAW_AUDIO)) {
		return B_MEDIA_BAD_FORMAT;
	}

	return B_OK;		// raw audio or wildcard type, either is okay by us
}


/**
 * @brief Finalises format negotiation just before the connection is established.
 *
 * Called after the consumer has processed the format proposal. Verifies that
 * the source matches and that the node is not already connected. Any wildcard
 * buffer size is resolved to 4096 bytes. On success the connection is reserved
 * by updating fOutput and the source/name output parameters are set.
 *
 * @param what     The media_source identifying our output.
 * @param where    The media_destination of the consuming node.
 * @param format   In/out format that may contain wildcards to be resolved.
 * @param _source  Output parameter set to the confirmed media_source.
 * @param out_name Output buffer (B_MEDIA_NAME_LENGTH bytes) for the output name.
 * @return B_OK on success; B_MEDIA_BAD_SOURCE if \a what does not match;
 *         B_MEDIA_ALREADY_CONNECTED if already connected;
 *         B_MEDIA_BAD_FORMAT if the format type or sample format is wrong.
 */
status_t
GameProducer::PrepareToConnect(const media_source& what,
	const media_destination& where, media_format* format,
	media_source* _source, char* out_name)
{
	// The format has been processed by the consumer at this point. We need
	// to insure the format is still acceptable and any wild care are filled in.

	// trying to connect something that isn't our source?
	if (what != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	// are we already connected?
	if (fOutput.destination != media_destination::null)
		return B_MEDIA_ALREADY_CONNECTED;

	// the format may not yet be fully specialized (the consumer might have
	// passed back some wildcards).  Finish specializing it now, and return an
	// error if we don't support the requested format.
	if (format->type != B_MEDIA_RAW_AUDIO)
		return B_MEDIA_BAD_FORMAT;

	if (format->u.raw_audio.format != fPreferredFormat.u.raw_audio.format)
		return B_MEDIA_BAD_FORMAT;

	// check the buffer size, which may still be wildcarded
	if (format->u.raw_audio.buffer_size
		== media_raw_audio_format::wildcard.buffer_size) {
		format->u.raw_audio.buffer_size = 4096;
			// pick something comfortable to suggest
	}

	// Now reserve the connection, and return information about it
	fOutput.destination = where;
	fOutput.format = *format;
	*_source = fOutput.source;
	strlcpy(out_name, fOutput.name, B_MEDIA_NAME_LENGTH);
	return B_OK;
}


/**
 * @brief Completes connection setup after the Media Roster confirms the link.
 *
 * Records the negotiated destination and format. Measures the internal
 * buffer-fill latency with a dry run and sets the total event latency
 * (downstream + internal). Creates a BBufferGroup sized for the confirmed
 * buffer size if one was not already supplied via SetBufferGroup().
 *
 * @param error       Non-zero if an earlier step failed; connection is aborted.
 * @param source      The confirmed media_source for our output.
 * @param destination The confirmed media_destination of the consumer.
 * @param format      The negotiated media_format agreed upon by both parties.
 * @param ioName      In/out buffer for the connection name; updated with our name.
 */
void
GameProducer::Connect(status_t error, const media_source& source,
	const media_destination& destination, const media_format& format,
	char* ioName)
{
	// If something earlier failed, Connect() might still be called, but with a
	// non-zero error code.  When that happens we simply unreserve the
	// connection and do nothing else.
	if (error) {
		fOutput.destination = media_destination::null;
		fOutput.format = fPreferredFormat;
		return;
	}

	// Okay, the connection has been confirmed.  Record the destination and
	// format that we agreed on, and report our connection name again.
	fOutput.destination = destination;
	fOutput.format = format;
	strlcpy(ioName, fOutput.name, B_MEDIA_NAME_LENGTH);

	// Now that we're connected, we can determine our downstream latency.
	// Do so, then make sure we get our events early enough.
	media_node_id id;
	FindLatencyFor(fOutput.destination, &fLatency, &id);

	if (!fBufferGroup)
		fBufferSize = fOutput.format.u.raw_audio.buffer_size;
			// Have to set it before latency calculating

	// Use a dry run to see how long it takes me to fill a buffer of data

	// The first step to setup the buffer
	bigtime_t start, produceLatency;
	int32 frames = int32(fBufferSize / fFrameSize);
	float* data = new float[frames * 2];

	// Second, fill the buffer
	start = ::system_time();
	for (int32 i = 0; i < frames; i++) {
		data[i * 2] = 0.8 * float(i / frames);
		data[i * 2 + 1] = 0.8 * float(i / frames);
	}
	produceLatency = ::system_time();

	// Third, calculate the latency
	fInternalLatency = produceLatency - start;
	SetEventLatency(fLatency + fInternalLatency);

	// Finaily, clean up
	delete [] data;

	// reset our buffer duration, etc. to avoid later calculations
	bigtime_t duration = bigtime_t(1000000) * frames
		/ bigtime_t(fOutput.format.u.raw_audio.frame_rate);
	SetBufferDuration(duration);

	// Set up the buffer group for our connection, as long as nobody handed us a
	// buffer group (via SetBufferGroup()) prior to this.
	if (!fBufferGroup) {
		int32 count = int32(fLatency / BufferDuration() + 2);
		fBufferGroup = new BBufferGroup(fBufferSize, count);
	}
}


/**
 * @brief Tears down the connection to the consumer and releases the buffer group.
 *
 * Resets the output destination to null and restores the preferred format.
 * The BBufferGroup is deleted (which waits for all outstanding buffers to be
 * recycled) and the pointer is set to NULL.
 *
 * @param what  The media_source of the connection being torn down.
 * @param where The media_destination of the connection being torn down.
 */
void
GameProducer::Disconnect(const media_source& what,
	const media_destination& where)
{
	// Make sure that our connection is the one being disconnected
	if ((where == fOutput.destination) && (what == fOutput.source)) {
		fOutput.destination = media_destination::null;
		fOutput.format = fPreferredFormat;
		delete fBufferGroup;
		fBufferGroup = NULL;
	}
}


/**
 * @brief Rejects all format change requests from the consumer.
 *
 * GameProducer does not support dynamic format changes; the format is fixed
 * at connection time.
 *
 * @param source       Source side of the connection requesting the change.
 * @param destination  Destination side of the connection.
 * @param io_format    The proposed new format (ignored).
 * @param _deprecated_ Ignored deprecated parameter.
 * @return Always B_ERROR.
 */
status_t
GameProducer::FormatChangeRequested(const media_source& source,
	const media_destination& destination, media_format* io_format,
	int32* _deprecated_)
{
	// we don't support any other formats, so we just reject any format changes.
	return B_ERROR;
}


/**
 * @brief Switches the buffer group used by the producer.
 *
 * Deletes the current BBufferGroup (waiting for outstanding buffers to
 * recycle) and adopts \a newGroup. If \a newGroup is NULL a new group is
 * created from the negotiated buffer size and downstream latency. The buffer
 * size is updated from the first buffer of the new group when a non-NULL
 * group is supplied.
 *
 * @param forSource The media_source the new group applies to.
 * @param newGroup  The replacement BBufferGroup, or NULL to auto-create one.
 * @return B_OK on success; B_MEDIA_BAD_SOURCE if \a forSource does not match;
 *         B_BAD_VALUE if the first buffer cannot be retrieved from \a newGroup.
 */
status_t
GameProducer::SetBufferGroup(const media_source& forSource,
	BBufferGroup* newGroup)
{
	// verify that we didn't get bogus arguments before we proceed
	if (forSource != fOutput.source)
		return B_MEDIA_BAD_SOURCE;

	// Are we being passed the buffer group we're already using?
	if (newGroup == fBufferGroup)
		return B_OK;

	// Ahh, someone wants us to use a different buffer group.  At this point we
	// delete the one we are using and use the specified one instead. If the
	// specified group is NULL, we need to recreate one ourselves, and use
	// *that*. Note that if we're caching a BBuffer that we requested earlier,
	// we have to Recycle() that buffer *before* deleting the buffer group,
	// otherwise we'll deadlock waiting for that buffer to be recycled!
	delete fBufferGroup;		// waits for all buffers to recycle
	if (newGroup != NULL) {
		// we were given a valid group; just use that one from now on
		fBufferGroup = newGroup;

		// get buffer length from the first buffer
		BBuffer* buffers[1];
		if (newGroup->GetBufferList(1, buffers) != B_OK)
			return B_BAD_VALUE;
		fBufferSize = buffers[0]->SizeAvailable();
	} else {
		// we were passed a NULL group pointer; that means we construct
		// our own buffer group to use from now on
		fBufferSize = fOutput.format.u.raw_audio.buffer_size;
		int32 count = int32(fLatency / BufferDuration() + 2);
		fBufferGroup = new BBufferGroup(fBufferSize, count);
	}

	return B_OK;
}


/**
 * @brief Reports the total latency of this node (internal plus downstream).
 *
 * The total latency is the event latency (downstream + internal) plus
 * the current scheduling latency of the BMediaEventLooper thread.
 *
 * @param _latency Output parameter set to the total latency in microseconds.
 * @return Always B_OK.
 */
status_t
GameProducer::GetLatency(bigtime_t* _latency)
{
	// report our *total* latency:  internal plus downstream plus scheduling
	*_latency = EventLatency() + SchedulingLatency();
	return B_OK;
}


/**
 * @brief Responds to a late-notice from the Media Roster.
 *
 * The action taken depends on the current run mode:
 *   - B_RECORDING: no action (hardware capture cannot adjust timing).
 *   - B_INCREASE_LATENCY: increase the internal latency estimate and
 *     update the event latency accordingly.
 *   - Other modes: skip one buffer's worth of frames to catch up.
 *
 * @param what                The media_source that is running late.
 * @param howMuch             How late the node is, in microseconds.
 * @param performanceDuration Performance time of the late buffer.
 */
void
GameProducer::LateNoticeReceived(const media_source& what, bigtime_t howMuch,
	bigtime_t performanceDuration)
{
	// If we're late, we need to catch up.  Respond in a manner appropriate to
	// our current run mode.
	if (what == fOutput.source) {
		if (RunMode() == B_RECORDING) {
			// A hardware capture node can't adjust; it simply emits buffers at
			// appropriate points.  We (partially) simulate this by not
			// adjusting our behavior upon receiving late notices -- after all,
			// the hardware can't choose to capture "sooner"...
		} else if (RunMode() == B_INCREASE_LATENCY) {
			// We're late, and our run mode dictates that we try to produce
			// buffers earlier in order to catch up. This argues that the
			// downstream nodes are not properly reporting their latency, but
			// there's not much we can do about that at the moment, so we try
			// to start producing buffers earlier to compensate.
			fInternalLatency += howMuch;
			SetEventLatency(fLatency + fInternalLatency);
		} else {
			// The other run modes dictate various strategies for sacrificing
			// data quality in the interests of timely data delivery. The way we
			// do this is to skip a buffer, which catches us up in time by one
			// buffer duration.
			size_t nSamples = fBufferSize / fFrameSize;
			fFramesSent += nSamples;
		}
	}
}


/**
 * @brief Notifies the producer that downstream latency has changed.
 *
 * Updates fLatency and recalculates the total event latency when the
 * changed connection matches our output.
 *
 * @param source      The source side of the connection whose latency changed.
 * @param destination The destination side of the connection.
 * @param new_latency The new downstream latency in microseconds.
 * @param flags       Flags associated with the latency change (currently unused).
 */
void
GameProducer::LatencyChanged(const media_source& source,
	const media_destination& destination, bigtime_t new_latency, uint32 flags)
{
	// something downstream changed latency, so we need to start producing
	// buffers earlier (or later) than we were previously.  Make sure that the
	// connection that changed is ours, and adjust to the new downstream
	// latency if so.
	if ((source == fOutput.source) && (destination == fOutput.destination)) {
		fLatency = new_latency;
		SetEventLatency(fLatency + fInternalLatency);
	}
}


/**
 * @brief Rejects requests to change the play rate.
 *
 * Play rate control is not supported by GameProducer.
 *
 * @param numerator   Numerator of the requested rate fraction.
 * @param denominator Denominator of the requested rate fraction.
 * @return Always B_ERROR.
 */
status_t
GameProducer::SetPlayRate(int32 numerator, int32 denominator)
{
	// Play rates are weird.  We don't support them
	return B_ERROR;
}


/**
 * @brief Handles private messages sent to this node's control port.
 *
 * GameProducer does not define any private messages; all messages are rejected.
 *
 * @param message The message type code.
 * @param data    Pointer to the message data.
 * @param size    Size of \a data in bytes.
 * @return Always B_ERROR.
 */
status_t
GameProducer::HandleMessage(int32 message, const void* data, size_t size)
{
	// We currently do not handle private messages
	return B_ERROR;
}


/**
 * @brief Ignores requests for additional buffers in offline mode.
 *
 * Offline mode is not supported; this method is a no-op.
 *
 * @param source      The output source requesting an additional buffer.
 * @param prev_buffer ID of the previous buffer in the sequence.
 * @param prev_time   Performance time of the previous buffer.
 * @param prev_tag    Optional seek tag from the previous buffer.
 */
void
GameProducer::AdditionalBufferRequested(const media_source& source,
	media_buffer_id prev_buffer, bigtime_t prev_time,
	const media_seek_tag* prev_tag)
{
	// we don't support offline mode (yet...)
	return;
}


// BMediaEventLooper methods

/**
 * @brief Completes node initialisation after registration with the Media Roster.
 *
 * Assigns the control port and node identity to fOutput, names the output
 * "GameProducer Output", sets the thread priority to B_REAL_TIME_PRIORITY,
 * and starts the BMediaEventLooper's event-processing thread.
 */
void
GameProducer::NodeRegistered()
{
	// set up as much information about our output as we can
	fOutput.source.port = ControlPort();
	fOutput.source.id = 0;
	fOutput.node = Node();
	strlcpy(fOutput.name, "GameProducer Output", B_MEDIA_NAME_LENGTH);

	// Start the BMediaEventLooper thread
	SetPriority(B_REAL_TIME_PRIORITY);
	Run();
}


/**
 * @brief Intercepts run-mode changes to report unsupported offline mode.
 *
 * B_OFFLINE mode is not supported. ReportError() is called to notify the
 * Media Roster, though the run mode change itself cannot be prevented.
 *
 * @param mode The requested run mode.
 */
void
GameProducer::SetRunMode(run_mode mode)
{
	// We don't support offline run mode, so broadcast an error if we're set to
	// B_OFFLINE.  Unfortunately, we can't actually reject the mode change...
	if (B_OFFLINE == mode) {
		ReportError(B_NODE_FAILED_SET_RUN_MODE);
	}
}


/**
 * @brief Processes timed events dispatched by the BMediaEventLooper.
 *
 * Handles three event types:
 *   - B_START: initialises frame bookkeeping and enqueues the first
 *     B_HANDLE_BUFFER event.
 *   - B_STOP: flushes all pending B_HANDLE_BUFFER events from the queue.
 *   - B_HANDLE_BUFFER: requests a filled BBuffer from FillNextBuffer(),
 *     sends it downstream (or recycles it if output is disabled), and
 *     schedules the next buffer event based on the cumulative frame count.
 *
 * @param event         The timed event to process.
 * @param lateness      How late this event is relative to its scheduled time.
 * @param realTimeEvent Whether this event is a real-time event.
 */
void
GameProducer::HandleEvent(const media_timed_event* event, bigtime_t lateness,
	bool realTimeEvent)
{
//	FPRINTF(stderr, "ToneProducer::HandleEvent\n");
	switch (event->type)
	{
	case BTimedEventQueue::B_START:
		// don't do anything if we're already running
		if (RunState() != B_STARTED) {
			// Going to start sending buffers so setup the needed bookkeeping
			fFramesSent = 0;
			fStartTime = event->event_time;
			media_timed_event firstBufferEvent(fStartTime,
				BTimedEventQueue::B_HANDLE_BUFFER);

			// Alternatively, we could call HandleEvent() directly with this
			// event, to avoid a trip through the event queue like this:
			//		this->HandleEvent(&firstBufferEvent, 0, false);
			EventQueue()->AddEvent(firstBufferEvent);
		}
		break;

	case BTimedEventQueue::B_STOP:
		// When we handle a stop, we must ensure that downstream consumers don't
		// get any more buffers from us.  This means we have to flush any
		// pending buffer-producing events from the queue.
		EventQueue()->FlushEvents(0, BTimedEventQueue::B_ALWAYS, true,
			BTimedEventQueue::B_HANDLE_BUFFER);
		break;

	case BTimedEventQueue::B_HANDLE_BUFFER:
		{
			// Ensure we're both started and connected before delivering buffer
			if ((RunState() == BMediaEventLooper::B_STARTED)
				&& (fOutput.destination != media_destination::null)) {
				// Get the next buffer of data
				BBuffer* buffer = FillNextBuffer(event->event_time);
				if (buffer) {
					// Send the buffer downstream if output is enabled
					status_t err = B_ERROR;
					if (fOutputEnabled) {
						err = SendBuffer(buffer, fOutput.source,
							fOutput.destination);
					}
					if (err) {
						// we need to recycle the buffer ourselves if output is
						// disabled or if the call to SendBuffer() fails
						buffer->Recycle();
					}
				}

				// track how much media we've delivered so far
				size_t nFrames = fBufferSize / fFrameSize;
				fFramesSent += nFrames;

				// The buffer is on its way; now schedule the next one to go
				bigtime_t nextEvent = fStartTime + bigtime_t(double(fFramesSent)
					/ double(fOutput.format.u.raw_audio.frame_rate)
					* 1000000.0);
				media_timed_event nextBufferEvent(nextEvent,
					BTimedEventQueue::B_HANDLE_BUFFER);
				EventQueue()->AddEvent(nextBufferEvent);
			}
		}
		break;

	default:
		break;
	}
}


/**
 * @brief Obtains a BBuffer from the group and fills it with mixed audio data.
 *
 * Requests a buffer from fBufferGroup with a timeout of BufferDuration().
 * If the request fails, NULL is returned (the current buffer-production
 * cycle is skipped). The buffer is zeroed, then GameSoundBuffer::Play() is
 * called to fill it with gain/pan-adjusted audio. The buffer header is
 * stamped with the appropriate performance time: the raw event_time in
 * B_RECORDING mode, or a recalculated performance time in live modes.
 *
 * @param event_time The scheduled performance time for this buffer.
 * @return Pointer to a filled BBuffer ready to send downstream, or NULL if
 *         a buffer could not be obtained from the group.
 */
BBuffer*
GameProducer::FillNextBuffer(bigtime_t event_time)
{
	// get a buffer from our buffer group
	BBuffer* buf = fBufferGroup->RequestBuffer(fBufferSize, BufferDuration());

	// if we fail to get a buffer (for example, if the request times out), we
	// skip this buffer and go on to the next, to avoid locking up the control
	// thread.
	if (!buf)
		return NULL;

	// we need to discribe the buffer
	int64 frames = int64(fBufferSize / fFrameSize);
	memset(buf->Data(), 0, fBufferSize);

	// now fill the buffer with data, continuing where the last buffer left off
	fObject->Play(buf->Data(), frames);

	// fill in the buffer header
	media_header* hdr = buf->Header();
	hdr->type = B_MEDIA_RAW_AUDIO;
	hdr->size_used = fBufferSize;
	hdr->time_source = TimeSource()->ID();

	bigtime_t stamp;
	if (RunMode() == B_RECORDING) {
		// In B_RECORDING mode, we stamp with the capture time.  We're not
		// really a hardware capture node, but we simulate it by using the
		// (precalculated) time at which this buffer "should" have been created.
		stamp = event_time;
	} else {
		// okay, we're in one of the "live" performance run modes.  in these
		// modes, we stamp the buffer with the time at which the buffer should
		// be rendered to the output, not with the capture time. fStartTime is
		// the cached value of the first buffer's performance time; we calculate
		// this buffer's performance time as an offset from that time, based on
		// the amount of media we've created so far.
		// Recalculating every buffer like this avoids accumulation of error.
		stamp = fStartTime + bigtime_t(double(fFramesSent)
			/ double(fOutput.format.u.raw_audio.frame_rate) * 1000000.0);
	}
	hdr->start_time = stamp;

	return buf;
}
