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
 *   Copyright 2009-2011, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 */


/**
 * @file SocketMessenger.cpp
 * @brief Implementation of BSocketMessenger, a BMessage transport over TCP sockets.
 *
 * BSocketMessenger wraps a BSocket and a background reader thread to provide
 * asynchronous BMessage send/receive semantics similar to BMessenger but
 * over a raw TCP connection.  Request/reply correlation is handled via
 * embedded reply-ID fields in each message.
 *
 * @see BSocket, BMessenger
 */


#include <SocketMessenger.h>

#include <Message.h>
#include <MessageQueue.h>
#include <Messenger.h>

#include <AutoDeleter.h>
#include <AutoLocker.h>
#include <HashMap.h>


/** @brief Field name embedded in requests to carry the reply-correlation ID. */
static const char* kReplySenderIDField = "socket_messenger:sender_reply_id";

/** @brief Field name embedded in replies to route them to the correct waiter. */
static const char* kReplyReceiverIDField = "socket_messenger:reply_id";


// #pragma mark - BSocketMessenger::Private


struct BSocketMessenger::Private {
			typedef SynchronizedHashMap<HashKey64<int64>,
									BMessage> ReplyMessageMap;

								Private();
	virtual						~Private();

			void				ClearMessages();

			sem_id				fMessageWaiters;
			thread_id			fReplyReader;
			ReplyMessageMap		fReceivedReplies;
			BMessageQueue		fReceivedMessages;
			int64				fReplyIDCounter;
};


/**
 * @brief Construct the Private data struct with invalid handles.
 */
BSocketMessenger::Private::Private()
	:
	fMessageWaiters(-1),
	fReplyReader(-1),
	fReceivedReplies(),
	fReceivedMessages(),
	fReplyIDCounter(0)
{
}


/**
 * @brief Destructor — deletes the semaphore and waits for the reader thread.
 */
BSocketMessenger::Private::~Private()
{
	if (fMessageWaiters > 0)
		delete_sem(fMessageWaiters);
	if (fReplyReader > 0)
		wait_for_thread(fReplyReader, NULL);

	ClearMessages();
}


/**
 * @brief Discard all pending replies and unsolicited messages.
 */
void
BSocketMessenger::Private::ClearMessages()
{
	fReceivedReplies.Clear();
	AutoLocker<BMessageQueue> queueLocker(fReceivedMessages);
	while (!fReceivedMessages.IsEmpty())
		delete fReceivedMessages.NextMessage();
}


// #pragma mark - BSocketMessenger


/**
 * @brief Default constructor — creates an unconnected messenger.
 */
BSocketMessenger::BSocketMessenger()
	:
	fPrivateData(NULL),
	fSocket(),
	fInitStatus(B_NO_INIT)
{
	_Init();
}


/**
 * @brief Construct and connect to \a address within \a timeout microseconds.
 *
 * @param address  Remote address to connect to.
 * @param timeout  Connection timeout in microseconds, or B_INFINITE_TIMEOUT.
 */
BSocketMessenger::BSocketMessenger(const BNetworkAddress& address,
	bigtime_t timeout)
	:
	fPrivateData(NULL),
	fSocket(),
	fInitStatus(B_NO_INIT)
{
	_Init();
	SetTo(address, timeout);
}


/**
 * @brief Construct from an already-connected BSocket.
 *
 * Adopts the connected \a socket and starts the background reader thread.
 *
 * @param socket  An initialised, connected BSocket to wrap.
 */
BSocketMessenger::BSocketMessenger(const BSocket& socket)
	:
	fPrivateData(NULL),
	fSocket(socket),
	fInitStatus(B_NO_INIT)
{
	_Init();
	if (fPrivateData == NULL)
		return;

	fInitStatus = socket.InitCheck();
	if (fInitStatus != B_OK)
		return;

	fPrivateData->fReplyReader = spawn_thread(&_MessageReader,
		"Message Reader", B_NORMAL_PRIORITY, this);
	if (fPrivateData->fReplyReader < 0)
		fInitStatus = fPrivateData->fReplyReader;
	if (fInitStatus != B_OK) {
		exit_thread(fPrivateData->fReplyReader);
		fPrivateData->fReplyReader = -1;
		return;
	}

	fInitStatus = resume_thread(fPrivateData->fReplyReader);
}


/**
 * @brief Destructor — disconnects and frees private data.
 */
BSocketMessenger::~BSocketMessenger()
{
	Unset();

	delete fPrivateData;
}


/**
 * @brief Disconnect the socket and reset the messenger to an uninitialised state.
 */
void
BSocketMessenger::Unset()
{
	if (fPrivateData == NULL)
		return;

	fSocket.Disconnect();
	wait_for_thread(fPrivateData->fReplyReader, NULL);
	fPrivateData->fReplyReader = -1;
	fPrivateData->ClearMessages();

	release_sem_etc(fPrivateData->fMessageWaiters, 1, B_RELEASE_ALL);

	fInitStatus = B_NO_INIT;
}


/**
 * @brief Connect to \a address, starting the reader thread on success.
 *
 * If already connected, Unset() is called first.
 *
 * @param address  Remote address to connect to.
 * @param timeout  Connection timeout in microseconds.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSocketMessenger::SetTo(const BNetworkAddress& address, bigtime_t timeout)
{
	Unset();

	if (fPrivateData == NULL)
		return B_NO_MEMORY;

	fPrivateData->fReplyReader = spawn_thread(&_MessageReader,
		"Message Reader", B_NORMAL_PRIORITY, this);
	if (fPrivateData->fReplyReader < 0)
		return fPrivateData->fReplyReader;
	status_t error = fSocket.Connect(address, timeout);
	if (error != B_OK) {
		Unset();
		return error;
	}

	return fInitStatus = resume_thread(fPrivateData->fReplyReader);
}


/**
 * @brief Connect to the same address as another BSocketMessenger.
 *
 * @param target   The messenger whose address to connect to.
 * @param timeout  Connection timeout in microseconds.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSocketMessenger::SetTo(const BSocketMessenger& target, bigtime_t timeout)
{
	return SetTo(target.Address(), timeout);
}


/**
 * @brief Send a message to the remote peer without waiting for a reply.
 *
 * @param message  The message to transmit.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSocketMessenger::SendMessage(const BMessage& message)
{
	return _SendMessage(message);
}


/**
 * @brief Send a message and block until a reply arrives or the timeout expires.
 *
 * A unique reply-ID is embedded in the message so the reader thread can
 * route the response back to this call.
 *
 * @param message  The message to transmit.
 * @param _reply   Output parameter populated with the reply message.
 * @param timeout  Maximum time to wait for the reply in microseconds.
 * @return B_OK on success, or an error code on timeout or send failure.
 */
status_t
BSocketMessenger::SendMessage(const BMessage& message, BMessage& _reply,
	bigtime_t timeout)
{
	int64 replyID = atomic_add64(&fPrivateData->fReplyIDCounter, 1);
	BMessage temp(message);
	temp.AddInt64(kReplySenderIDField, replyID);
	status_t error = _SendMessage(temp);
	if (error != B_OK)
		return error;

	return _ReadReply(replyID, _reply, timeout);
}


/**
 * @brief Send a message, wait for its reply, then forward the reply to a BMessenger.
 *
 * @param message      The message to transmit.
 * @param replyTarget  BMessenger to which the reply will be forwarded.
 * @param timeout      Maximum time to wait for the remote reply.
 * @return B_OK on success, or an error code on failure.
 */
status_t
BSocketMessenger::SendMessage(const BMessage& message,
	BMessenger& replyTarget, bigtime_t timeout)
{
	BMessage reply;
	status_t error = SendMessage(message, reply, timeout);
	if (error != B_OK)
		return error;

	return replyTarget.SendMessage(&reply);
}


/**
 * @brief Send a reply to a previously received message.
 *
 * Extracts the reply-correlation ID from \a message and sends \a reply
 * tagged with the corresponding receiver ID.
 *
 * @param message  The incoming message to reply to (must contain a sender ID).
 * @param reply    The reply message to send.
 * @return B_OK on success, B_NOT_ALLOWED if \a message has no sender ID.
 */
status_t
BSocketMessenger::SendReply(const BMessage& message, const BMessage& reply)
{
	int64 replyID;
	if (message.FindInt64(kReplySenderIDField, &replyID) != B_OK)
		return B_NOT_ALLOWED;

	BMessage replyMessage(reply);
	replyMessage.AddInt64(kReplyReceiverIDField, replyID);
	return SendMessage(replyMessage);
}


/**
 * @brief Receive the next unsolicited message, blocking up to \a timeout microseconds.
 *
 * @param _message  Output parameter populated with the received message.
 * @param timeout   Maximum time to wait in microseconds.
 * @return B_OK on success, B_CANCELED if the socket disconnected, or a timeout error.
 */
status_t
BSocketMessenger::ReceiveMessage(BMessage& _message, bigtime_t timeout)
{
	status_t error = B_OK;
	AutoLocker<BMessageQueue> queueLocker(fPrivateData->fReceivedMessages);
	for (;;) {
		if (!fPrivateData->fReceivedMessages.IsEmpty()) {
			BMessage* nextMessage
				= fPrivateData->fReceivedMessages.NextMessage();
			_message = *nextMessage;
			delete nextMessage;
			break;
		}

		queueLocker.Unlock();
		error = _WaitForMessage(timeout);
		if (error != B_OK)
			break;
		if (!fSocket.IsConnected()) {
			error = B_CANCELED;
			break;
		}
		queueLocker.Lock();
	}

	return error;
}


/**
 * @brief Allocate and initialise the Private data struct and its semaphore.
 *
 * No-op if fPrivateData is already set.
 */
void
BSocketMessenger::_Init()
{
	if (fPrivateData != NULL)
		return;

	BSocketMessenger::Private* data
		= new(std::nothrow) BSocketMessenger::Private;

	if (data == NULL) {
		fInitStatus = B_NO_MEMORY;
		return;
	}

	data->fMessageWaiters = create_sem(0, "message waiters");
	if (data->fMessageWaiters < 0) {
		fInitStatus = data->fMessageWaiters;
		delete data;
		return;
	}

	fPrivateData = data;
}


/**
 * @brief Block the calling thread until a message arrives or \a timeout elapses.
 *
 * Handles EINTR automatically by adjusting the remaining timeout.
 *
 * @param timeout  Maximum wait time in microseconds; B_INFINITE_TIMEOUT to wait forever.
 * @return B_OK when a message is signalled, or an error code on timeout or cancellation.
 */
status_t
BSocketMessenger::_WaitForMessage(bigtime_t timeout)
{
	for (;;) {
		status_t error = acquire_sem_etc(fPrivateData->fMessageWaiters, 1,
			B_RELATIVE_TIMEOUT, timeout);
		if (error == B_INTERRUPTED) {
			if (timeout != B_INFINITE_TIMEOUT)
				timeout -= system_time();
			continue;
		}
		if (error != B_OK)
			return error;
		break;
	}

	return B_OK;
}


/**
 * @brief Serialise and transmit a BMessage over the socket.
 *
 * Prepends a ssize_t length field before the flattened message data.
 *
 * @param message  The message to send.
 * @return B_OK on success, or an error code on serialisation or write failure.
 */
status_t
BSocketMessenger::_SendMessage(const BMessage& message)
{
	ssize_t flatSize = message.FlattenedSize();
	ssize_t totalSize = flatSize + sizeof(ssize_t);

	char* buffer = new(std::nothrow) char[totalSize];
	if (buffer == NULL)
		return B_NO_MEMORY;

	ArrayDeleter<char> bufferDeleter(buffer);
	*(ssize_t*)buffer = flatSize;
	char* messageBuffer = buffer + sizeof(ssize_t);
	status_t error = message.Flatten(messageBuffer, flatSize);
	if (error != B_OK)
		return error;

	ssize_t size = fSocket.Write(buffer, totalSize);
	if (size < 0)
		return size;

	return B_OK;
}


/**
 * @brief Read the next length-prefixed BMessage from the socket.
 *
 * Blocks on the socket until data is available, reads the size header,
 * then reads that many bytes and unflattens them into \a _message.
 *
 * @param _message  Output parameter populated with the received message.
 * @return B_OK on success, or an error code on read or unflatten failure.
 */
status_t
BSocketMessenger::_ReadMessage(BMessage& _message)
{
	status_t error = fSocket.WaitForReadable(B_INFINITE_TIMEOUT);
	if (error != B_OK)
		return error;

	ssize_t size = 0;
	ssize_t readSize = fSocket.Read(&size, sizeof(ssize_t));
	if (readSize < 0)
		return readSize;

	if (readSize != sizeof(ssize_t))
		return B_BAD_VALUE;

	if (size <= 0)
		return B_MISMATCHED_VALUES;

	char* buffer = new(std::nothrow) char[size];
	if (buffer == NULL)
		return B_NO_MEMORY;

	ArrayDeleter<char> bufferDeleter(buffer);
	readSize = fSocket.Read(buffer, size);
	if (readSize < 0)
		return readSize;

	if (readSize != size)
		return B_MISMATCHED_VALUES;

	return _message.Unflatten(buffer);
}


/**
 * @brief Wait for and retrieve the reply associated with \a replyID.
 *
 * Spins on _WaitForMessage() until the reply map contains an entry for
 * \a replyID or an error occurs.
 *
 * @param replyID  Correlation ID of the reply to wait for.
 * @param reply    Output parameter populated with the matched reply.
 * @param timeout  Maximum wait time in microseconds.
 * @return B_OK on success, B_CANCELED if the socket disconnected, or a timeout error.
 */
status_t
BSocketMessenger::_ReadReply(const int64 replyID, BMessage& reply,
	bigtime_t timeout)
{
	status_t error = B_OK;
	for (;;) {
		if (fPrivateData->fReceivedReplies.ContainsKey(replyID)) {
			reply = fPrivateData->fReceivedReplies.Remove(replyID);
			break;
		}

		error = _WaitForMessage(timeout);
		if (error != B_OK)
			break;
		if (!fSocket.IsConnected()) {
			error = B_CANCELED;
			break;
		}
	}

	return error;
}


/**
 * @brief Background thread function that continuously reads incoming messages.
 *
 * Routes each message to the reply map (if it carries a receiver ID) or to
 * the unsolicited message queue, then signals all waiters.  On error or
 * disconnect it unblocks any threads waiting on the semaphore.
 *
 * @param arg  Pointer to the owning BSocketMessenger instance.
 * @return Error code indicating the reason the thread exited.
 */
status_t
BSocketMessenger::_MessageReader(void* arg)
{
	BSocketMessenger* messenger = (BSocketMessenger*)arg;
	BSocketMessenger::Private* data = messenger->fPrivateData;
	status_t error = B_OK;

	for (;;) {
		BMessage message;
		error = messenger->_ReadMessage(message);
		if (error != B_OK)
			break;

		int64 replyID;
		if (message.FindInt64(kReplyReceiverIDField, &replyID) == B_OK) {
			error = data->fReceivedReplies.Put(replyID, message);
			if (error != B_OK)
				break;
		} else {
			BMessage* queueMessage = new(std::nothrow) BMessage(message);
			if (queueMessage == NULL) {
				error = B_NO_MEMORY;
				break;
			}

			AutoLocker<BMessageQueue> queueLocker(
				data->fReceivedMessages);
			data->fReceivedMessages.AddMessage(queueMessage);
		}


		release_sem_etc(data->fMessageWaiters, 1, B_RELEASE_ALL);
	}

	// if we exit our message loop, ensure everybody wakes up and knows
	// we're no longer receiving messages.
	messenger->fSocket.Disconnect();
	release_sem_etc(data->fMessageWaiters, 1, B_RELEASE_ALL);
	return error;
}
