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
 * @file SimpleMediaClient.cpp
 * @brief Implementation of BSimpleMediaClient and its hook-driven connection types.
 *
 * BSimpleMediaClient provides a callback-based layer on top of BMediaClient,
 * allowing applications to participate in the media graph without subclassing.
 * BSimpleMediaInput and BSimpleMediaOutput extend BSimpleMediaConnection with
 * consumer and producer roles and delegate all events to registered hook functions.
 *
 * @see BMediaClient, BMediaConnection
 */


#include <SimpleMediaClient.h>

#include <MediaDebug.h>


/**
 * @brief Constructs a BSimpleMediaClient with no hooks installed.
 *
 * @param name   Human-readable node name forwarded to BMediaClient.
 * @param type   Primary media type consumed or produced.
 * @param kinds  Bitmask of media_client_kinds for producer/consumer roles.
 */
BSimpleMediaClient::BSimpleMediaClient(const char* name,
	media_type type, media_client_kinds kinds)
	:
	BMediaClient(name, type, kinds),
	fNotifyHook(NULL),
	fNotifyCookie(NULL)
{
	CALLED();
}


/**
 * @brief Destroys the BSimpleMediaClient.
 */
BSimpleMediaClient::~BSimpleMediaClient()
{
	CALLED();
}


/**
 * @brief Creates a new BSimpleMediaInput and registers it with this client.
 *
 * @return Pointer to the newly allocated and registered BSimpleMediaInput.
 */
BSimpleMediaInput*
BSimpleMediaClient::BeginInput()
{
	CALLED();

	BSimpleMediaInput* input = new BSimpleMediaInput();
	RegisterInput(input);
	return input;
}


/**
 * @brief Creates a new BSimpleMediaOutput and registers it with this client.
 *
 * @return Pointer to the newly allocated and registered BSimpleMediaOutput.
 */
BSimpleMediaOutput*
BSimpleMediaClient::BeginOutput()
{
	CALLED();

	BSimpleMediaOutput* output = new BSimpleMediaOutput();
	RegisterOutput(output);
	return output;
}


/**
 * @brief Installs a notification hook called on transport and format events.
 *
 * @param notifyHook  Function pointer invoked with variadic event arguments.
 * @param cookie      Opaque user data passed as the first argument to the hook.
 */
void
BSimpleMediaClient::SetHook(notify_hook notifyHook, void* cookie)
{
	CALLED();

	fNotifyHook = notifyHook;
	fNotifyCookie = cookie;
}


/**
 * @brief Hook called when the node is about to start; forwards to fNotifyHook.
 *
 * @param performanceTime  Performance timestamp of the start event.
 */
void
BSimpleMediaClient::HandleStart(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL) {
		(*fNotifyHook)(BSimpleMediaClient::fNotifyCookie,
			BSimpleMediaClient::B_WILL_START,
			performanceTime);
	}
}


/**
 * @brief Hook called when the node is about to stop; forwards to fNotifyHook.
 *
 * @param performanceTime  Performance timestamp of the stop event.
 */
void
BSimpleMediaClient::HandleStop(bigtime_t performanceTime)
{
	if (fNotifyHook != NULL) {
		(*fNotifyHook)(BSimpleMediaClient::fNotifyCookie,
			BSimpleMediaClient::B_WILL_STOP,
			performanceTime);
	}
}


/**
 * @brief Hook called when a seek event is received; forwards to fNotifyHook.
 *
 * @param mediaTime        The media time being sought to.
 * @param performanceTime  Performance timestamp of the seek event.
 */
void
BSimpleMediaClient::HandleSeek(bigtime_t mediaTime, bigtime_t performanceTime)
{
	if (fNotifyHook != NULL) {
		(*fNotifyHook)(BSimpleMediaClient::fNotifyCookie,
			BSimpleMediaClient::B_WILL_SEEK,
			performanceTime, mediaTime);
	}
}


/**
 * @brief Hook called to suggest a media format; delegates to fNotifyHook.
 *
 * If no hook is installed, returns B_ERROR indicating no format preference.
 *
 * @param type     The requested media type.
 * @param quality  Quality hint from the Media Kit.
 * @param format   Output pointer for the suggested media_format.
 * @return The status code returned by the hook, or B_ERROR if no hook is set.
 */
status_t
BSimpleMediaClient::FormatSuggestion(media_type type, int32 quality,
	media_format* format)
{
	if (fNotifyHook != NULL) {
		status_t result = B_ERROR;
		(*fNotifyHook)(BSimpleMediaClient::fNotifyCookie,
			BSimpleMediaClient::B_FORMAT_SUGGESTION,
			type, quality, format, &result);
		return result;
	}
	return B_ERROR;
}


void BSimpleMediaClient::_ReservedSimpleMediaClient0() {}
void BSimpleMediaClient::_ReservedSimpleMediaClient1() {}
void BSimpleMediaClient::_ReservedSimpleMediaClient2() {}
void BSimpleMediaClient::_ReservedSimpleMediaClient3() {}
void BSimpleMediaClient::_ReservedSimpleMediaClient4() {}
void BSimpleMediaClient::_ReservedSimpleMediaClient5() {}


/**
 * @brief Constructs a BSimpleMediaConnection with no hooks installed.
 *
 * @param kinds  B_MEDIA_INPUT or B_MEDIA_OUTPUT direction.
 */
BSimpleMediaConnection::BSimpleMediaConnection(media_connection_kinds kinds)
	:
	BMediaConnection(kinds),
	fProcessHook(NULL),
	fNotifyHook(NULL),
	fBufferCookie(NULL)
{
}


/**
 * @brief Destroys the BSimpleMediaConnection.
 */
BSimpleMediaConnection::~BSimpleMediaConnection()
{
	CALLED();
}


/**
 * @brief Installs buffer-processing and notification hooks on this connection.
 *
 * @param processHook  Called for each received/produced buffer.
 * @param notifyHook   Called for connection and format events.
 * @param cookie       Opaque user data forwarded to both hook functions.
 */
void
BSimpleMediaConnection::SetHooks(process_hook processHook,
	notify_hook notifyHook, void* cookie)
{
	CALLED();

	fProcessHook = processHook;
	fNotifyHook = notifyHook;
	fBufferCookie = cookie;
}


/**
 * @brief Returns the opaque cookie passed to the hooks.
 *
 * @return The cookie pointer set via SetHooks().
 */
void*
BSimpleMediaConnection::Cookie() const
{
	CALLED();

	return fBufferCookie;
}


/**
 * @brief Returns the preferred buffer size for this connection.
 *
 * @return Buffer size in bytes.
 */
size_t
BSimpleMediaConnection::BufferSize() const
{
	return fBufferSize;
}


/**
 * @brief Sets the preferred buffer size for this connection.
 *
 * @param bufferSize  Desired buffer size in bytes.
 */
void
BSimpleMediaConnection::SetBufferSize(size_t bufferSize)
{
	fBufferSize = bufferSize;
}


/**
 * @brief Stores the media format accepted or produced by this connection.
 *
 * @param format  The media_format to record.
 */
void
BSimpleMediaConnection::SetAcceptedFormat(const media_format& format)
{
	CALLED();

	fAcceptedFormat = format;
}


/**
 * @brief Returns the media format accepted or produced by this connection.
 *
 * @return Reference to the stored accepted media_format.
 */
const media_format&
BSimpleMediaConnection::AcceptedFormat() const
{
	CALLED();

	return fAcceptedFormat;
}


/**
 * @brief Constructs a BSimpleMediaInput inheriting both connection base classes.
 */
BSimpleMediaInput::BSimpleMediaInput()
	:
	BMediaConnection(B_MEDIA_INPUT),
	BSimpleMediaConnection(B_MEDIA_INPUT),
	BMediaInput()
{
}


/**
 * @brief Destroys the BSimpleMediaInput.
 */
BSimpleMediaInput::~BSimpleMediaInput()
{
	CALLED();
}


/**
 * @brief Validates a format proposed by an upstream producer.
 *
 * Accepts the format if it is compatible with AcceptedFormat(); otherwise
 * sets \a format to AcceptedFormat() and returns B_MEDIA_BAD_FORMAT.
 *
 * @param format  In/out format to validate or replace.
 * @return B_OK if compatible, B_MEDIA_BAD_FORMAT otherwise.
 */
status_t
BSimpleMediaInput::AcceptFormat(media_format* format)
{
	CALLED();

	// TODO: Add hooks

	if (format_is_compatible(*format, AcceptedFormat()))
		return B_OK;

	*format = AcceptedFormat();

	return B_MEDIA_BAD_FORMAT;
}


/**
 * @brief Called when the upstream producer connects; notifies the hook and records the format.
 *
 * Fires fNotifyHook with B_INPUT_CONNECTED, stores the negotiated format,
 * and calls BMediaInput::Connected().
 *
 * @param format  The negotiated media_format.
 */
void
BSimpleMediaInput::Connected(const media_format& format)
{
	if (fNotifyHook != NULL)
		(*fNotifyHook)(this, BSimpleMediaConnection::B_INPUT_CONNECTED);

	SetAcceptedFormat(format);

	BMediaInput::Connected(format);
}


/**
 * @brief Called when the upstream producer disconnects; notifies the hook.
 *
 * Fires fNotifyHook with B_INPUT_DISCONNECTED, then calls
 * BMediaInput::Disconnected().
 */
void
BSimpleMediaInput::Disconnected()
{
	if (fNotifyHook != NULL)
		(*fNotifyHook)(this, BSimpleMediaConnection::B_INPUT_DISCONNECTED);

	BMediaInput::Disconnected();
}


/**
 * @brief Delivers a received buffer to the process hook.
 *
 * @param buffer  The incoming BBuffer; ownership is not transferred.
 */
void
BSimpleMediaInput::HandleBuffer(BBuffer* buffer)
{
	CALLED();

	if (fProcessHook != NULL)
		(*fProcessHook)(this, buffer);
}


/**
 * @brief Constructs a BSimpleMediaOutput inheriting both connection base classes.
 */
BSimpleMediaOutput::BSimpleMediaOutput()
	:
	BMediaConnection(B_MEDIA_OUTPUT),
	BSimpleMediaConnection(B_MEDIA_OUTPUT),
	BMediaOutput()
{
}


/**
 * @brief Destroys the BSimpleMediaOutput.
 */
BSimpleMediaOutput::~BSimpleMediaOutput()
{
	CALLED();
}


/**
 * @brief Validates this output's format against a downstream consumer's proposal.
 *
 * @param format  In/out format proposed by the downstream node.
 * @return B_OK if compatible, B_ERROR otherwise.
 */
status_t
BSimpleMediaOutput::PrepareToConnect(media_format* format)
{
	// TODO: Add hooks

	if (!format_is_compatible(AcceptedFormat(), *format))
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Responds to a format proposal from a downstream consumer.
 *
 * If fNotifyHook is set, calls it with B_FORMAT_PROPOSAL and returns its
 * result; otherwise copies AcceptedFormat() into \a format.
 *
 * @param format  In/out format descriptor to fill or validate.
 * @return B_OK on success, or the hook's return value.
 */
status_t
BSimpleMediaOutput::FormatProposal(media_format* format)
{
	if (fNotifyHook != NULL) {
		return (*fNotifyHook)(this,
			BSimpleMediaConnection::B_FORMAT_PROPOSAL, format);
	} else
		*format = AcceptedFormat();

	return B_OK;
}


/**
 * @brief Called when a downstream consumer connects; notifies the hook and records the format.
 *
 * Fires fNotifyHook with B_OUTPUT_CONNECTED, stores the negotiated format,
 * and calls BMediaOutput::Connected().
 *
 * @param format  The negotiated media_format.
 */
void
BSimpleMediaOutput::Connected(const media_format& format)
{
	if (fNotifyHook != NULL)
		(*fNotifyHook)(this, BSimpleMediaConnection::B_OUTPUT_CONNECTED);

	SetAcceptedFormat(format);

	BMediaOutput::Connected(format);
}


/**
 * @brief Called when the downstream consumer disconnects; notifies the hook.
 *
 * Fires fNotifyHook with B_OUTPUT_DISCONNECTED, then calls
 * BMediaOutput::Disconnected().
 */
void
BSimpleMediaOutput::Disconnected()
{
	if (fNotifyHook != NULL)
		(*fNotifyHook)(this, BSimpleMediaConnection::B_OUTPUT_DISCONNECTED);

	BMediaOutput::Disconnected();
}
