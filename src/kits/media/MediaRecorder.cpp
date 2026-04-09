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
 *   Copyright 2015, Hamish Morrison <hamishm53@gmail.com>
 *   Copyright 2014-2016, Dario Casalinuovo
 *   Copyright 1999, Be Incorporated
 *   All Rights Reserved.
 *   This file may be used under the terms of the Be Sample Code License.
 */

/** @file MediaRecorder.cpp
 *  @brief Implementation of BMediaRecorder for capturing live media from nodes.
 */


#include <MediaRecorder.h>

#include <MediaAddOn.h>
#include <MediaRoster.h>
#include <TimeSource.h>

#include "MediaDebug.h"
#include "MediaRecorderNode.h"


/** @brief Constructs a BMediaRecorder and registers its internal node with the media roster.
 *  @param name The name of the recorder (used for the internal BMediaNode).
 *  @param type The media type to accept (e.g. B_MEDIA_RAW_AUDIO).
 */
BMediaRecorder::BMediaRecorder(const char* name, media_type type)
	:
	fInitErr(B_OK),
	fConnected(false),
	fRunning(false),
	fReleaseOutputNode(false),
	fRecordHook(NULL),
	fNotifyHook(NULL),
	fNode(NULL),
	fBufferCookie(NULL)
{
	CALLED();

	BMediaRoster::Roster(&fInitErr);

	if (fInitErr == B_OK) {
		fNode = new(std::nothrow) BMediaRecorderNode(name, this, type);
		if (fNode == NULL)
			fInitErr = B_NO_MEMORY;

		fInitErr = BMediaRoster::CurrentRoster()->RegisterNode(fNode);
	}
}


/** @brief Destructor; stops recording, disconnects, and releases the internal node. */
BMediaRecorder::~BMediaRecorder()
{
	CALLED();

	if (fNode != NULL) {
		Stop();
		Disconnect();
		fNode->Release();
	}
}


/** @brief Returns the initialisation status of the recorder.
 *  @return B_OK if ready, or an error code.
 */
status_t
BMediaRecorder::InitCheck() const
{
	CALLED();

	return fInitErr;
}


/** @brief Sets the media format the internal node will accept from producers.
 *  @param format The media_format to accept.
 */
void
BMediaRecorder::SetAcceptedFormat(const media_format& format)
{
	CALLED();

	fNode->SetAcceptedFormat(format);
}


/** @brief Returns the currently accepted media format.
 *  @return Const reference to the accepted media_format.
 */
const media_format&
BMediaRecorder::AcceptedFormat() const
{
	CALLED();

	return fNode->AcceptedFormat();
}


/** @brief Installs the callback hooks for recording and notification events.
 *  @param recordFunc Callback invoked for each received buffer.
 *  @param notifyFunc Callback invoked for lifecycle notifications (start/stop/seek).
 *  @param cookie     User-supplied cookie passed to both callbacks.
 *  @return B_OK on success.
 */
status_t
BMediaRecorder::SetHooks(ProcessFunc recordFunc, NotifyFunc notifyFunc,
	void* cookie)
{
	CALLED();

	fRecordHook = recordFunc;
	fNotifyHook = notifyFunc;
	fBufferCookie = cookie;

	return B_OK;
}


/** @brief Called by the internal node when a buffer is received; invokes the record hook.
 *  @param buffer Pointer to the raw buffer data.
 *  @param size   Size of the buffer in bytes.
 *  @param header The media_header associated with the buffer.
 */
void
BMediaRecorder::BufferReceived(void* buffer, size_t size,
	const media_header& header)
{
	CALLED();

	if (fRecordHook) {
		(*fRecordHook)(fBufferCookie, header.start_time,
			buffer, size, Format());
	}
}


/** @brief Connects to a suitable default producer for the given format type.
 *         Selects the audio mixer for raw audio, or the video input for video.
 *  @param format The desired input media_format.
 *  @return B_OK on success, B_MEDIA_ALREADY_CONNECTED if already connected,
 *          B_MEDIA_BAD_FORMAT for unsupported types, or an error code.
 */
status_t
BMediaRecorder::Connect(const media_format& format)
{
	CALLED();

	if (fInitErr != B_OK)
		return fInitErr;

	if (fConnected)
		return B_MEDIA_ALREADY_CONNECTED;

	status_t err = B_OK;
	media_node node;

	switch (format.type) {
		// switch on format for default
		case B_MEDIA_RAW_AUDIO:
			err = BMediaRoster::Roster()->GetAudioMixer(&node);
			break;
		case B_MEDIA_RAW_VIDEO:
		case B_MEDIA_ENCODED_VIDEO:
			err = BMediaRoster::Roster()->GetVideoInput(&node);
			break;
		// give up?
		default:
			return B_MEDIA_BAD_FORMAT;
	}

	if (err != B_OK)
		return err;

	fReleaseOutputNode = true;

	err = _Connect(node, NULL, format);

	if (err != B_OK) {
		BMediaRoster::Roster()->ReleaseNode(node);
		fReleaseOutputNode = false;
	}

	return err;
}


/** @brief Connects to a producer identified by a dormant node info.
 *  @param dormantNode Info identifying the dormant node to instantiate.
 *  @param format      The desired input media_format.
 *  @return B_OK on success, B_MEDIA_ALREADY_CONNECTED if already connected,
 *          or an error code.
 */
status_t
BMediaRecorder::Connect(const dormant_node_info& dormantNode,
	const media_format& format)
{
	CALLED();

	if (fInitErr != B_OK)
		return fInitErr;

	if (fConnected)
		return B_MEDIA_ALREADY_CONNECTED;

	media_node node;
	status_t err = BMediaRoster::Roster()->InstantiateDormantNode(dormantNode,
		&node, B_FLAVOR_IS_GLOBAL);

	if (err != B_OK)
		return err;

	fReleaseOutputNode = true;

	err = _Connect(node, NULL, format);

	if (err != B_OK) {
		BMediaRoster::Roster()->ReleaseNode(node);
		fReleaseOutputNode = false;
	}

	return err;
}


/** @brief Connects to a specific media node and optional output.
 *  @param node   The producer media_node to connect from.
 *  @param output Pointer to a specific media_output, or NULL to find a free one.
 *  @param format Pointer to the desired media_format, or NULL to use the output's format.
 *  @return B_OK on success, B_MEDIA_ALREADY_CONNECTED if already connected,
 *          or an error code.
 */
status_t
BMediaRecorder::Connect(const media_node& node,
	const media_output* output, const media_format* format)
{
	CALLED();

	if (fInitErr != B_OK)
		return fInitErr;

	if (fConnected)
		return B_MEDIA_ALREADY_CONNECTED;

	if (format == NULL && output != NULL)
		format = &output->format;

	return _Connect(node, output, *format);
}


/** @brief Disconnects from the producer node.
 *         Stops recording first if currently running.
 *  @return B_OK on success, B_MEDIA_NOT_CONNECTED if not connected,
 *          or an error code.
 */
status_t
BMediaRecorder::Disconnect()
{
	CALLED();

	status_t err = B_OK;

	if (fInitErr != B_OK)
		return fInitErr;

	if (!fConnected)
		return B_MEDIA_NOT_CONNECTED;

	if (!fNode)
		return B_ERROR;

	if (fRunning)
		err = Stop();

	if (err != B_OK)
		return err;

	media_input ourInput;
	fNode->GetInput(&ourInput);

	// do the disconnect
	err = BMediaRoster::CurrentRoster()->Disconnect(
		fOutputNode.node, fOutputSource,
			fNode->Node().node, ourInput.destination);

	if (fReleaseOutputNode) {
		BMediaRoster::Roster()->ReleaseNode(fOutputNode);
		fReleaseOutputNode = false;
	}

	fConnected = false;
	fRunning = false;

	return err;
}


/** @brief Starts the producer node and unmutes the data flow.
 *  @param force If true, start even if already running.
 *  @return B_OK on success, B_MEDIA_NOT_CONNECTED if not connected,
 *          EALREADY if already running (and @p force is false), or an error code.
 */
status_t
BMediaRecorder::Start(bool force)
{
	CALLED();

	if (fInitErr != B_OK)
		return fInitErr;

	if (!fConnected)
		return B_MEDIA_NOT_CONNECTED;

	if (fRunning && !force)
		return EALREADY;

	if (!fNode)
		return B_ERROR;

	// start node here
	status_t err = B_OK;

	if ((fOutputNode.kind & B_TIME_SOURCE) != 0)
		err = BMediaRoster::CurrentRoster()->StartTimeSource(
			fOutputNode, BTimeSource::RealTime());
	else
		err = BMediaRoster::CurrentRoster()->StartNode(
			fOutputNode, fNode->TimeSource()->Now());

	// then un-mute it
	if (err == B_OK) {
		fNode->SetDataEnabled(true);
		fRunning = true;
	} else
		fRunning = false;

	return err;
}


/** @brief Stops the data flow by muting the node and stopping it.
 *  @param force If true, stop even if not currently running.
 *  @return B_OK on success, EALREADY if not running (and @p force is false),
 *          or an error code.
 */
status_t
BMediaRecorder::Stop(bool force)
{
	CALLED();

	if (fInitErr != B_OK)
		return fInitErr;

	if (!fRunning && !force)
		return EALREADY;

	if (!fNode)
		return B_ERROR;

	// should have the Node mute the output here
	fNode->SetDataEnabled(false);

	fRunning = false;

	return BMediaRoster::CurrentRoster()->StopNode(fNode->Node(), 0);
}


/** @brief Returns whether the recorder is currently running.
 *  @return true if running, false otherwise.
 */
bool
BMediaRecorder::IsRunning() const
{
	CALLED();

	return fRunning;
}


/** @brief Returns whether the recorder is currently connected to a producer.
 *  @return true if connected, false otherwise.
 */
bool
BMediaRecorder::IsConnected() const
{
	CALLED();

	return fConnected;
}


/** @brief Returns the media_source of the connected producer output.
 *  @return Const reference to the producer's media_source.
 */
const media_source&
BMediaRecorder::MediaSource() const
{
	CALLED();

	return fOutputSource;
}


/** @brief Returns the media_input of the internal recorder node.
 *  @return The current media_input.
 */
const media_input
BMediaRecorder::MediaInput() const
{
	CALLED();

	media_input input;
	fNode->GetInput(&input);
	return input;
}


/** @brief Returns the currently accepted media format.
 *  @return Const reference to the accepted media_format.
 */
const media_format&
BMediaRecorder::Format() const
{
	CALLED();

	return fNode->AcceptedFormat();
}


/** @brief Stores the producer's output source and configures time-source synchronisation.
 *         Called internally once the media connection is established.
 *  @param outputSource The media_source of the connected producer output.
 *  @return B_OK on success, or an error code from the roster.
 */
status_t
BMediaRecorder::SetUpConnection(media_source outputSource)
{
	fOutputSource = outputSource;

	// Perform the connection
	media_node timeSource;
	if ((fOutputNode.kind & B_TIME_SOURCE) != 0)
		timeSource = fOutputNode;
	else
		BMediaRoster::Roster()->GetTimeSource(&timeSource);

	// Set time source
	return BMediaRoster::Roster()->SetTimeSourceFor(fNode->Node().node,
		timeSource.node);
}


/** @brief Internal helper that performs the actual media graph connection.
 *  @param node   The producer media_node.
 *  @param output Pointer to a specific output, or NULL to search for a free one.
 *  @param format The desired media_format.
 *  @return B_OK on success, B_MEDIA_BAD_SOURCE if no compatible output is found,
 *          or an error code.
 */
status_t
BMediaRecorder::_Connect(const media_node& node,
	const media_output* output, const media_format& format)
{
	CALLED();

	status_t err = B_OK;
	media_format ourFormat = format;
	media_output ourOutput;

	if (fNode == NULL)
		return B_ERROR;

	fNode->SetAcceptedFormat(ourFormat);

	fOutputNode = node;

	// Figure out the output provided
	if (output != NULL) {
		ourOutput = *output;
	} else if (err == B_OK) {
		media_output outputs[10];
		int32 count = 10;

		err = BMediaRoster::Roster()->GetFreeOutputsFor(fOutputNode,
			outputs, count, &count, ourFormat.type);

		if (err != B_OK)
			return err;

		for (int i = 0; i < count; i++) {
			if (format_is_compatible(outputs[i].format, ourFormat)) {
				ourOutput = outputs[i];
				ourFormat = outputs[i].format;
				break;
			}
		}
	}

	if (ourOutput.source == media_source::null)
		return B_MEDIA_BAD_SOURCE;

	// Find our Node's free input
	media_input ourInput;
	fNode->GetInput(&ourInput);

	// Acknowledge the node that we already know
	// who is our producer node.
	fNode->ActivateInternalConnect(false);

	return BMediaRoster::CurrentRoster()->Connect(ourOutput.source,
		ourInput.destination, &ourFormat, &ourOutput, &ourInput,
		BMediaRoster::B_CONNECT_MUTED);
}


/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder0() { }
/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder1() { }
/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder2() { }
/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder3() { }
/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder4() { }
/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder5() { }
/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder6() { }
/** @brief Reserved for future binary compatibility. */
void BMediaRecorder::_ReservedMediaRecorder7() { }

