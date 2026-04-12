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
 * @file MediaClient.cpp
 * @brief Implementation of BMediaClient, the high-level media node wrapper.
 *
 * BMediaClient simplifies use of the Media Kit by hiding BMediaNode internals
 * behind a producer/consumer API. It owns a BMediaClientNode that handles
 * BBufferProducer/BBufferConsumer duties, and exposes connection management,
 * transport control, and format negotiation hooks for subclasses.
 *
 * @see BMediaClientNode, BMediaConnection, BMediaRoster
 */


#include "MediaClient.h"

#include <MediaConnection.h>

#include <MediaRoster.h>
#include <TimeSource.h>

#include "MediaClientNode.h"

#include "MediaDebug.h"


namespace BPrivate { namespace media {


/**
 * @brief RAII helper that calls Release() on a BMediaConnection when destroyed.
 *
 * Used by the input/output lists to automatically release connection objects
 * when they are removed from the list.
 */
class ConnReleaser {
public:
	/**
	 * @brief Constructs a ConnReleaser holding the given connection.
	 *
	 * @param conn  The BMediaConnection to manage.
	 */
	ConnReleaser(BMediaConnection* conn)
		:
		fConn(conn) {}

	/**
	 * @brief Releases the managed connection.
	 */
	virtual ~ConnReleaser()
	{
		fConn->Release();
	}

	/**
	 * @brief Equality operator comparing the wrapped connection pointer.
	 *
	 * @param c1  The other ConnReleaser to compare against.
	 * @return true if both wrap the same BMediaConnection pointer.
	 */
	bool operator== (const ConnReleaser &c1)
	{
		return c1.fConn == this->fConn;
	}

protected:
	/**
	 * @brief Returns the wrapped BMediaConnection pointer.
	 *
	 * @return Pointer to the managed connection.
	 */
	BMediaConnection* Obj() const
	{
		return fConn;
	}

private:
	BMediaConnection* fConn;
};


/**
 * @brief ConnReleaser specialisation for input connections.
 */
class InputReleaser : public ConnReleaser {
public:
	/**
	 * @brief Constructs an InputReleaser for the given BMediaInput.
	 *
	 * @param input  The BMediaInput to manage.
	 */
	InputReleaser(BMediaInput* input)
		:
		ConnReleaser(input) {}

	/**
	 * @brief Returns the wrapped connection cast to BMediaInput.
	 *
	 * @return Pointer to the managed BMediaInput, or NULL on bad cast.
	 */
	BMediaInput* Obj() const
	{
		return dynamic_cast<BMediaInput*>(ConnReleaser::Obj());
	}
};


/**
 * @brief ConnReleaser specialisation for output connections.
 */
class OutputReleaser : public ConnReleaser {
public:
	/**
	 * @brief Constructs an OutputReleaser for the given BMediaOutput.
	 *
	 * @param output  The BMediaOutput to manage.
	 */
	OutputReleaser(BMediaOutput* output)
		:
		ConnReleaser(output) {}

	/**
	 * @brief Returns the wrapped connection cast to BMediaOutput.
	 *
	 * @return Pointer to the managed BMediaOutput, or NULL on bad cast.
	 */
	BMediaOutput* Obj() const
	{
		return dynamic_cast<BMediaOutput*>(ConnReleaser::Obj());
	}
};


}
}


/**
 * @brief Constructs a BMediaClient, registering it with the Media Roster.
 *
 * Creates an internal BMediaClientNode and registers it so the client
 * becomes visible in the media graph.
 *
 * @param name   Human-readable name for the node visible in the Media Roster.
 * @param type   Primary media type (e.g. B_MEDIA_RAW_AUDIO) consumed or produced.
 * @param kinds  Bitmask of media_client_kinds indicating producer/consumer roles.
 */
BMediaClient::BMediaClient(const char* name,
	media_type type, media_client_kinds kinds)
	:
	fLastID(-1)
{
	CALLED();

	fNode = new BMediaClientNode(name, this, type);
	_Init();

	fClient.node = fNode->Node();
	fClient.kinds = kinds;
}


/**
 * @brief Destroys the BMediaClient, stopping and disconnecting all connections.
 */
BMediaClient::~BMediaClient()
{
	CALLED();

	_Deinit();
}


/**
 * @brief Returns the media_client descriptor for this client.
 *
 * @return Reference to the internal media_client structure.
 */
const media_client&
BMediaClient::Client() const
{
	return fClient;
}


/**
 * @brief Returns the initialization status of this client.
 *
 * @return B_OK if the node was registered successfully, or an error code.
 */
status_t
BMediaClient::InitCheck() const
{
	CALLED();

	return fInitErr;
}


/**
 * @brief Returns the roles this client was constructed with.
 *
 * @return Bitmask of media_client_kinds (producer, consumer, etc.).
 */
media_client_kinds
BMediaClient::Kinds() const
{
	CALLED();

	return fClient.Kinds();
}


/**
 * @brief Returns the primary media type of this client.
 *
 * @return The media_type set at construction time.
 */
media_type
BMediaClient::MediaType() const
{
	CALLED();

	// Right now ConsumerType() and ProducerType() are the same.
	return fNode->ConsumerType();
}


/**
 * @brief Registers an input connection with this client and assigns it an ID.
 *
 * @param input  The BMediaInput to register; must not already be registered.
 * @return B_OK on success.
 */
status_t
BMediaClient::RegisterInput(BMediaInput* input)
{
	input->_ConnectionRegistered(this, ++fLastID);
	_AddInput(input);
	return B_OK;
}


/**
 * @brief Registers an output connection with this client and assigns it an ID.
 *
 * @param output  The BMediaOutput to register; must not already be registered.
 * @return B_OK on success.
 */
status_t
BMediaClient::RegisterOutput(BMediaOutput* output)
{
	output->_ConnectionRegistered(this, ++fLastID);
	_AddOutput(output);
	return B_OK;
}


/**
 * @brief Binds an input to an output so that received buffers are forwarded.
 *
 * Both connections must belong to this client and must not already be bound.
 *
 * @param input   The local BMediaInput to bind.
 * @param output  The local BMediaOutput to forward buffers to.
 * @return B_OK on success, B_ERROR if either pointer is NULL, if the
 *         connections do not belong to this client, or if either is
 *         already bound.
 */
status_t
BMediaClient::Bind(BMediaInput* input, BMediaOutput* output)
{
	CALLED();

	if (input == NULL
		|| output == NULL)
		return B_ERROR;

	if (input->fOwner != this || output->fOwner != this)
		return B_ERROR;

	// TODO: Implement binding one input to more outputs.
	if (input->fBind != NULL
		|| output->fBind != NULL)
		return B_ERROR;

	input->fBind = output;
	output->fBind = input;
	return B_OK;
}


/**
 * @brief Removes a previously established binding between an input and output.
 *
 * @param input   The BMediaInput to unbind.
 * @param output  The BMediaOutput to unbind.
 * @return B_OK on success, B_ERROR if either pointer is NULL or if the
 *         connections do not belong to this client.
 */
status_t
BMediaClient::Unbind(BMediaInput* input, BMediaOutput* output)
{
	CALLED();

	if (input == NULL || output == NULL)
		return B_ERROR;

	if (input->fOwner != this || output->fOwner != this)
		return B_ERROR;

	input->fBind = NULL;
	output->fBind = NULL;
	return B_OK;
}


/**
 * @brief Connects a local connection to a remote connection object.
 *
 * @param ourConnection    The local BMediaConnection (input or output).
 * @param theirConnection  The remote BMediaConnection to connect to.
 * @return B_OK on success, or an error code from the Media Roster.
 */
status_t
BMediaClient::Connect(BMediaConnection* ourConnection,
	BMediaConnection* theirConnection)
{
	CALLED();

	return Connect(ourConnection, theirConnection->Connection());
}


/**
 * @brief Connects a local connection to a remote connection descriptor.
 *
 * Determines direction from the types of ourConnection and theirConnection,
 * then delegates to _ConnectInput() or _ConnectOutput().
 *
 * @param ourConnection    The local BMediaConnection (input or output).
 * @param theirConnection  Descriptor of the remote connection endpoint.
 * @return B_OK on success, B_ERROR if the direction combination is invalid,
 *         or an error code from the Media Roster.
 */
status_t
BMediaClient::Connect(BMediaConnection* ourConnection,
	const media_connection& theirConnection)
{
	CALLED();

	BMediaOutput* output = dynamic_cast<BMediaOutput*>(ourConnection);
	if (output != NULL && theirConnection.IsInput())
		return _ConnectInput(output, theirConnection);

	BMediaInput* input = dynamic_cast<BMediaInput*>(ourConnection);
	if (input != NULL && theirConnection.IsOutput())
		return _ConnectOutput(input, theirConnection);

	return B_ERROR;
}


/**
 * @brief Connects a local connection to a client (unimplemented).
 *
 * @param connection  The local BMediaConnection.
 * @param client      The target media_client descriptor.
 * @return B_ERROR always (not yet implemented).
 */
status_t
BMediaClient::Connect(BMediaConnection* connection,
	const media_client& client)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Disconnects all registered input and output connections.
 *
 * @return B_OK always (individual connection errors are not propagated).
 */
status_t
BMediaClient::Disconnect()
{
	CALLED();

	for (int32 i = 0; i < CountInputs(); i++)
		InputAt(i)->Disconnect();

	for (int32 i = 0; i < CountOutputs(); i++)
		OutputAt(i)->Disconnect();

	return B_OK;
}


/**
 * @brief Returns the number of registered input connections.
 *
 * @return Count of inputs currently owned by this client.
 */
int32
BMediaClient::CountInputs() const
{
	CALLED();

	return fInputs.CountItems();
}


/**
 * @brief Returns the number of registered output connections.
 *
 * @return Count of outputs currently owned by this client.
 */
int32
BMediaClient::CountOutputs() const
{
	CALLED();

	return fOutputs.CountItems();
}


/**
 * @brief Returns the input connection at a given index.
 *
 * @param index  Zero-based index into the input list.
 * @return Pointer to the BMediaInput, or NULL if out of range.
 */
BMediaInput*
BMediaClient::InputAt(int32 index) const
{
	CALLED();

	return fInputs.ItemAt(index)->Obj();
}


/**
 * @brief Returns the output connection at a given index.
 *
 * @param index  Zero-based index into the output list.
 * @return Pointer to the BMediaOutput, or NULL if out of range.
 */
BMediaOutput*
BMediaClient::OutputAt(int32 index) const
{
	CALLED();

	return fOutputs.ItemAt(index)->Obj();
}


/**
 * @brief Finds an input connection by its media_connection descriptor.
 *
 * @param input  Descriptor identifying the input (must satisfy IsInput()).
 * @return Pointer to the matching BMediaInput, or NULL if not found.
 */
BMediaInput*
BMediaClient::FindInput(const media_connection& input) const
{
	CALLED();

	if (!input.IsInput())
		return NULL;

	return _FindInput(input.destination);
}


/**
 * @brief Finds an output connection by its media_connection descriptor.
 *
 * @param output  Descriptor identifying the output (must satisfy IsOutput()).
 * @return Pointer to the matching BMediaOutput, or NULL if not found.
 */
BMediaOutput*
BMediaClient::FindOutput(const media_connection& output) const
{
	CALLED();

	if (!output.IsOutput())
		return NULL;

	return _FindOutput(output.source);
}


/**
 * @brief Returns whether the node is currently running.
 *
 * @return true if Start() has been called and the node is in the started state.
 */
bool
BMediaClient::IsStarted() const
{
	CALLED();

	return fRunning;
}


/**
 * @brief Override hook called after the node is registered with the Media Roster.
 *
 * The default implementation does nothing; subclasses may perform
 * post-registration setup here.
 */
void
BMediaClient::ClientRegistered()
{
	CALLED();
}


/**
 * @brief Starts the media node and all connected remote nodes.
 *
 * Iterates over outputs and starts each connected remote node (or time source),
 * then starts this client's own node via the Media Roster.
 *
 * @return B_OK on success, or the last error code from StartNode/StartTimeSource.
 */
status_t
BMediaClient::Start()
{
	CALLED();

	status_t err = B_OK;
	for (int32 i = 0; i < CountOutputs(); i++) {
		media_node remoteNode = OutputAt(i)->Connection().remote_node;
		if (remoteNode.kind & B_TIME_SOURCE)
			err = BMediaRoster::CurrentRoster()->StartTimeSource(
				remoteNode, BTimeSource::RealTime());
		else
			err = BMediaRoster::CurrentRoster()->StartNode(
				remoteNode, fNode->TimeSource()->Now());
	}

	return BMediaRoster::CurrentRoster()->StartNode(
		fNode->Node(), fNode->TimeSource()->Now());
}


/**
 * @brief Stops the media node via the Media Roster.
 *
 * @return B_OK on success, or an error code from StopNode.
 */
status_t
BMediaClient::Stop()
{
	CALLED();

	return BMediaRoster::CurrentRoster()->StopNode(
		fNode->Node(), fNode->TimeSource()->Now());
}


/**
 * @brief Seeks the media node to the given media time.
 *
 * @param mediaTime        Target position in media time.
 * @param performanceTime  Performance timestamp at which to execute the seek.
 * @return B_OK on success, or an error code from SeekNode.
 */
status_t
BMediaClient::Seek(bigtime_t mediaTime,
	bigtime_t performanceTime)
{
	CALLED();

	return BMediaRoster::CurrentRoster()->SeekNode(fNode->Node(),
		mediaTime, performanceTime);
}


/**
 * @brief Schedules a combined start/stop/seek operation on the node.
 *
 * @param start  Performance time at which to start.
 * @param stop   Performance time at which to stop.
 * @param seek   Media time to seek to at start.
 * @return B_OK on success, or an error code from RollNode.
 */
status_t
BMediaClient::Roll(bigtime_t start, bigtime_t stop, bigtime_t seek)
{
	CALLED();

	return BMediaRoster::CurrentRoster()->RollNode(fNode->Node(),
		start, stop, seek);
}


/**
 * @brief Returns the current media time tracked by this client.
 *
 * @return The last recorded media time in microseconds.
 */
bigtime_t
BMediaClient::CurrentTime() const
{
	CALLED();

	return fCurrentTime;
}


/**
 * @brief Returns the media add-on that created this node, if any.
 *
 * The default implementation returns NULL; subclasses instantiated by an
 * add-on should override this.
 *
 * @param id  Output pointer for the add-on-assigned node ID.
 * @return NULL always in the base implementation.
 */
BMediaAddOn*
BMediaClient::AddOn(int32* id) const
{
	CALLED();

	return NULL;
}


/**
 * @brief Hook called when the node transitions to the started state.
 *
 * Sets fRunning to true. Subclasses should call the base implementation.
 *
 * @param performanceTime  Performance timestamp of the start event.
 */
void
BMediaClient::HandleStart(bigtime_t performanceTime)
{
	fRunning = true;
}


/**
 * @brief Hook called when the node transitions to the stopped state.
 *
 * Sets fRunning to false. Subclasses should call the base implementation.
 *
 * @param performanceTime  Performance timestamp of the stop event.
 */
void
BMediaClient::HandleStop(bigtime_t performanceTime)
{
	fRunning = false;
}


/**
 * @brief Hook called when a seek event is delivered to the node.
 *
 * The default implementation does nothing.
 *
 * @param mediaTime        The media time sought to.
 * @param performanceTime  Performance timestamp of the seek event.
 */
void
BMediaClient::HandleSeek(bigtime_t mediaTime, bigtime_t performanceTime)
{
}


/**
 * @brief Override hook for format negotiation during connection setup.
 *
 * The default implementation returns B_ERROR, indicating no preference.
 * Subclasses should fill \a format with an acceptable format.
 *
 * @param type     The requested media type.
 * @param quality  Hint about the desired quality level.
 * @param format   Output parameter to fill with the suggested format.
 * @return B_OK if \a format was filled, B_ERROR otherwise.
 */
status_t
BMediaClient::FormatSuggestion(media_type type, int32 quality,
	media_format* format)
{
	return B_ERROR;
}


/**
 * @brief Initialises the client by registering the node with the Media Roster.
 *
 * Sets fInitErr to reflect the outcome of node registration.
 */
void
BMediaClient::_Init()
{
	CALLED();

	BMediaRoster* roster = BMediaRoster::Roster(&fInitErr);
	if (fInitErr == B_OK && roster != NULL)
		fInitErr = roster->RegisterNode(fNode);
}


/**
 * @brief Tears down the client: stops, disconnects, releases connections and the node.
 */
void
BMediaClient::_Deinit()
{
	CALLED();

	if (IsStarted())
		Stop();

	Disconnect();

	// This will release the connections too.
	fInputs.MakeEmpty(true);
	fOutputs.MakeEmpty(true);

	fNode->Release();
}


/**
 * @brief Adds an input connection to the internal input list.
 *
 * @param input  The BMediaInput to add.
 */
void
BMediaClient::_AddInput(BMediaInput* input)
{
	CALLED();

	fInputs.AddItem(new InputReleaser(input));
}


/**
 * @brief Adds an output connection to the internal output list.
 *
 * @param output  The BMediaOutput to add.
 */
void
BMediaClient::_AddOutput(BMediaOutput* output)
{
	CALLED();

	fOutputs.AddItem(new OutputReleaser(output));
}


/**
 * @brief Finds an input by matching its media_destination.
 *
 * @param dest  The destination descriptor to search for.
 * @return Pointer to the matching BMediaInput, or NULL if not found.
 */
BMediaInput*
BMediaClient::_FindInput(const media_destination& dest) const
{
	CALLED();

	for (int32 i = 0; i < CountInputs(); i++) {
		if (dest.id == InputAt(i)->_Destination().id)
			return InputAt(i);
	}
	return NULL;
}


/**
 * @brief Finds an output by matching its media_source.
 *
 * @param source  The source descriptor to search for.
 * @return Pointer to the matching BMediaOutput, or NULL if not found.
 */
BMediaOutput*
BMediaClient::_FindOutput(const media_source& source) const
{
	CALLED();

	for (int32 i = 0; i < CountOutputs(); i++) {
		if (source.id == OutputAt(i)->_Source().id)
			return OutputAt(i);
	}
	return NULL;
}


/**
 * @brief Asks the Media Roster to connect a local output to a remote input.
 *
 * Builds the output/input descriptors and calls BMediaRoster::Connect() in
 * muted mode so that the connection is set up but data flow does not start
 * until Start() is called.
 *
 * @param output  The local BMediaOutput to connect from.
 * @param input   Descriptor of the remote media_connection to connect to.
 * @return B_OK on success, B_MEDIA_BAD_DESTINATION if the destination is
 *         null, or an error code from the Media Roster.
 */
status_t
BMediaClient::_ConnectInput(BMediaOutput* output,
	const media_connection& input)
{
	CALLED();

	if (input.destination == media_destination::null)
		return B_MEDIA_BAD_DESTINATION;

	media_output ourOutput = output->Connection()._BuildMediaOutput();
	media_input theirInput = input._BuildMediaInput();
	media_format format;

	// NOTE: We want to set this data in the callbacks if possible.
	// The correct format should have been set in BMediaConnection::Connected.
	// TODO: Perhaps add some check assert?

	status_t ret = BMediaRoster::CurrentRoster()->Connect(ourOutput.source,
		theirInput.destination, &format, &ourOutput, &theirInput,
		BMediaRoster::B_CONNECT_MUTED);

#if 0
	if (ret == B_OK)
		output->fConnection.format = format;
#endif

	return ret;
}


/**
 * @brief Asks the Media Roster to connect a remote output to a local input.
 *
 * Builds the input/output descriptors and calls BMediaRoster::Connect() in
 * muted mode.
 *
 * @param input   The local BMediaInput to connect to.
 * @param output  Descriptor of the remote media_connection to connect from.
 * @return B_OK on success, B_MEDIA_BAD_SOURCE if the source is null, or an
 *         error code from the Media Roster.
 */
status_t
BMediaClient::_ConnectOutput(BMediaInput* input,
	const media_connection& output)
{
	CALLED();

	if (output.source == media_source::null)
		return B_MEDIA_BAD_SOURCE;

	media_input ourInput = input->Connection()._BuildMediaInput();
	media_output theirOutput = output._BuildMediaOutput();
	media_format format;

	// NOTE: We want to set this data in the callbacks if possible.
	// The correct format should have been set in BMediaConnection::Connected.
	// TODO: Perhaps add some check assert?

	status_t ret = BMediaRoster::CurrentRoster()->Connect(theirOutput.source,
		ourInput.destination, &format, &theirOutput, &ourInput,
		BMediaRoster::B_CONNECT_MUTED);

#if 0
	if (ret == B_OK)
		input->fConnection.format = format;
#endif

	return ret;
}


/**
 * @brief Tears down the media graph connection for a given connection object.
 *
 * Calls BMediaRoster::Disconnect() with the correct source/destination order
 * depending on whether the connection is an input or output.
 *
 * @param conn  The BMediaConnection to disconnect.
 * @return B_OK on success, B_ERROR if \a conn does not belong to this client,
 *         or an error code from the Media Roster.
 */
status_t
BMediaClient::_DisconnectConnection(BMediaConnection* conn)
{
	CALLED();

	if (conn->Client() != this)
		return B_ERROR;

	const media_connection& handle = conn->Connection();
	if (handle.IsInput()) {
		return BMediaRoster::CurrentRoster()->Disconnect(
			handle.remote_node.node, handle.source,
			handle._Node().node, handle.destination);
	} else {
		return BMediaRoster::CurrentRoster()->Disconnect(
			handle._Node().node, handle.source,
			handle.remote_node.node, handle.destination);
	}

	return B_ERROR;
}


/**
 * @brief Removes a connection from this client's internal lists.
 *
 * Removes the InputReleaser or OutputReleaser wrapper without triggering
 * Release() on the connection (the wrapper's destructor is suppressed via
 * the false parameter to RemoveItem).
 *
 * @param conn  The BMediaConnection to remove.
 * @return B_OK on success, B_ERROR if \a conn does not belong to this client.
 */
status_t
BMediaClient::_ReleaseConnection(BMediaConnection* conn)
{
	if (conn->Client() != this)
		return B_ERROR;

	if (conn->Connection().IsInput()) {
		InputReleaser obj(dynamic_cast<BMediaInput*>(conn));
		fInputs.RemoveItem(&obj, false);
		return B_OK;
	} else {
		OutputReleaser obj(dynamic_cast<BMediaOutput*>(conn));
		fOutputs.RemoveItem(&obj, false);
		return B_OK;
	}

	return B_ERROR;
}


void BMediaClient::_ReservedMediaClient0() {}
void BMediaClient::_ReservedMediaClient1() {}
void BMediaClient::_ReservedMediaClient2() {}
void BMediaClient::_ReservedMediaClient3() {}
void BMediaClient::_ReservedMediaClient4() {}
void BMediaClient::_ReservedMediaClient5() {}
void BMediaClient::_ReservedMediaClient6() {}
void BMediaClient::_ReservedMediaClient7() {}
void BMediaClient::_ReservedMediaClient8() {}
void BMediaClient::_ReservedMediaClient9() {}
void BMediaClient::_ReservedMediaClient10() {}
