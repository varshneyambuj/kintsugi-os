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
 *   Copyright 2002, Marcus Overhagen. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file OldBufferStream.cpp
 * @brief Stub implementations for the deprecated BeOS R4 buffer stream classes.
 *
 * Contains unimplemented bodies for BAbstractBufferStream and BBufferStream —
 * the subscriber-based ring-buffer API that predates BBufferGroup and BBuffer.
 * All methods call UNIMPLEMENTED() and return default values. Compiled only
 * for GCC 2 builds needed for BeIDE compatibility.
 *
 * @see OldBufferStreamManager.cpp, OldSubscriber.cpp
 */


// This is deprecated API that is not even implemented - no need to export
// it on a GCC4 build (BeIDE needs it to run, though, so it's worthwhile for
// GCC2)
#if __GNUC__ < 3


#include "OldBufferStream.h"

#include <MediaDebug.h>
#include <new>


/*************************************************************
 * public BAbstractBufferStream
 *************************************************************/

/**
 * @brief Retrieves stream parameters such as buffer size and subscriber count (unimplemented).
 *
 * @param bufferSize       Output: size of each buffer in bytes.
 * @param bufferCount      Output: number of buffers in the stream.
 * @param isRunning        Output: whether the stream is currently running.
 * @param subscriberCount  Output: number of active subscribers.
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::GetStreamParameters(size_t *bufferSize,
										   int32 *bufferCount,
										   bool *isRunning,
										   int32 *subscriberCount) const
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Reconfigures the buffer layout of the stream (unimplemented).
 *
 * @param bufferSize   New size for each buffer in bytes.
 * @param bufferCount  New number of buffers in the ring.
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::SetStreamBuffers(size_t bufferSize,
										int32 bufferCount)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Starts data flow through the buffer stream (unimplemented).
 *
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::StartStreaming()
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Stops data flow through the buffer stream (unimplemented).
 *
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::StopStreaming()
{
	UNIMPLEMENTED();

	return B_ERROR;
}

/*************************************************************
 * protected BAbstractBufferStream
 *************************************************************/

/**
 * @brief Reserved virtual method slot 1 (unimplemented).
 */
void
BAbstractBufferStream::_ReservedAbstractBufferStream1()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 2 (unimplemented).
 */
void
BAbstractBufferStream::_ReservedAbstractBufferStream2()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 3 (unimplemented).
 */
void
BAbstractBufferStream::_ReservedAbstractBufferStream3()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 4 (unimplemented).
 */
void
BAbstractBufferStream::_ReservedAbstractBufferStream4()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the server-side stream identifier (unimplemented).
 *
 * @return 0 always.
 */
stream_id
BAbstractBufferStream::StreamID() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Subscribes a named entity to this stream (unimplemented).
 *
 * @param name   Human-readable subscriber name.
 * @param subID  Output pointer for the assigned subscriber_id.
 * @param semID  Semaphore used to wake the subscriber when a buffer is ready.
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::Subscribe(char *name,
								 subscriber_id *subID,
								 sem_id semID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Removes a subscriber from this stream (unimplemented).
 *
 * @param subID  The subscriber_id to remove.
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::Unsubscribe(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Inserts a subscriber into the stream's processing chain (unimplemented).
 *
 * @param subID     The subscriber to enter.
 * @param neighbor  Reference subscriber for positional insertion.
 * @param before    true to insert before \a neighbor, false to insert after.
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::EnterStream(subscriber_id subID,
								   subscriber_id neighbor,
								   bool before)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Removes a subscriber from the stream's processing chain (unimplemented).
 *
 * @param subID  The subscriber to remove from the chain.
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::ExitStream(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns the BMessenger connected to the media server (unimplemented).
 *
 * @return NULL always.
 */
BMessenger *
BAbstractBufferStream::Server() const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Sends a message to the media server and waits for a reply (unimplemented).
 *
 * @param msg    The BMessage to send.
 * @param reply  Output BMessage for the server reply.
 * @return B_ERROR always.
 */
status_t
BAbstractBufferStream::SendRPC(BMessage *msg,
							   BMessage *reply) const
{
	UNIMPLEMENTED();

	return B_ERROR;
}

/*************************************************************
 * public BBufferStream
 *************************************************************/

/**
 * @brief Constructs a BBufferStream with the given manager and subscriber hints (unimplemented).
 *
 * @param headerSize   Size of the per-buffer header in bytes.
 * @param controller   The BBufferStreamManager controlling this stream.
 * @param headFeeder   Optional subscriber placed at the head of the chain.
 * @param tailFeeder   Optional subscriber placed at the tail of the chain.
 */
BBufferStream::BBufferStream(size_t headerSize,
							 BBufferStreamManager *controller,
							 BSubscriber *headFeeder,
							 BSubscriber *tailFeeder)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BBufferStream (unimplemented).
 */
BBufferStream::~BBufferStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Custom operator new for placement in shared memory (unimplemented).
 *
 * @param size  Allocation size.
 * @return NULL always (suppresses compiler warning via dummy variable).
 */
void *
BBufferStream::operator new(size_t size)
{
	UNIMPLEMENTED();

	void *dummy = NULL;
		// just to circumvent a warning that operator new should not return NULL
	return dummy;
}


/**
 * @brief Custom operator delete for placement-new'd instances (unimplemented).
 *
 * @param stream  Pointer to the BBufferStream to free.
 * @param size    Size of the allocation.
 */
void
BBufferStream::operator delete(void *stream, size_t size)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the per-buffer header size (unimplemented).
 *
 * @return 0 always.
 */
size_t
BBufferStream::HeaderSize() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Retrieves stream parameters for this BBufferStream (unimplemented).
 *
 * @param bufferSize       Output: size of each buffer in bytes.
 * @param bufferCount      Output: number of buffers in the ring.
 * @param isRunning        Output: whether the stream is running.
 * @param subscriberCount  Output: number of active subscribers.
 * @return B_ERROR always.
 */
status_t
BBufferStream::GetStreamParameters(size_t *bufferSize,
								   int32 *bufferCount,
								   bool *isRunning,
								   int32 *subscriberCount) const
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Reconfigures the buffer layout of this BBufferStream (unimplemented).
 *
 * @param bufferSize   New size for each buffer in bytes.
 * @param bufferCount  New number of buffers in the ring.
 * @return B_ERROR always.
 */
status_t
BBufferStream::SetStreamBuffers(size_t bufferSize,
								int32 bufferCount)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Starts data flow through this BBufferStream (unimplemented).
 *
 * @return B_ERROR always.
 */
status_t
BBufferStream::StartStreaming()
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Stops data flow through this BBufferStream (unimplemented).
 *
 * @return B_ERROR always.
 */
status_t
BBufferStream::StopStreaming()
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns the stream manager that controls this stream (unimplemented).
 *
 * @return NULL always.
 */
BBufferStreamManager *
BBufferStream::StreamManager() const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the total number of buffers allocated in this stream (unimplemented).
 *
 * @return 0 always.
 */
int32
BBufferStream::CountBuffers() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Subscribes a named entity to this BBufferStream (unimplemented).
 *
 * @param name   Human-readable subscriber name.
 * @param subID  Output pointer for the assigned subscriber_id.
 * @param semID  Semaphore for waking the subscriber.
 * @return B_ERROR always.
 */
status_t
BBufferStream::Subscribe(char *name,
						 subscriber_id *subID,
						 sem_id semID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Removes a subscriber from this BBufferStream (unimplemented).
 *
 * @param subID  The subscriber_id to remove.
 * @return B_ERROR always.
 */
status_t
BBufferStream::Unsubscribe(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Inserts a subscriber into this stream's processing chain (unimplemented).
 *
 * @param subID     The subscriber to enter.
 * @param neighbor  Reference subscriber for positional insertion.
 * @param before    true to insert before \a neighbor, false to insert after.
 * @return B_ERROR always.
 */
status_t
BBufferStream::EnterStream(subscriber_id subID,
						   subscriber_id neighbor,
						   bool before)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Removes a subscriber from this stream's processing chain (unimplemented).
 *
 * @param subID  The subscriber to remove.
 * @return B_ERROR always.
 */
status_t
BBufferStream::ExitStream(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns whether a subscriber is registered with this stream (unimplemented).
 *
 * @param subID  The subscriber_id to check.
 * @return false always.
 */
bool
BBufferStream::IsSubscribed(subscriber_id subID)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Returns whether a subscriber is currently in the processing chain (unimplemented).
 *
 * @param subID  The subscriber_id to check.
 * @return false always.
 */
bool
BBufferStream::IsEntered(subscriber_id subID)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Retrieves metadata about a subscriber in this stream (unimplemented).
 *
 * @param subID     The subscriber to query.
 * @param name      Output: subscriber name.
 * @param streamID  Output: stream identifier.
 * @param position  Output: position in the processing chain.
 * @return B_ERROR always.
 */
status_t
BBufferStream::SubscriberInfo(subscriber_id subID,
							  char **name,
							  stream_id *streamID,
							  int32 *position)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Releases a subscriber that is blocked waiting for a buffer (unimplemented).
 *
 * @param subID  The subscriber to unblock.
 * @return B_ERROR always.
 */
status_t
BBufferStream::UnblockSubscriber(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Acquires the next available buffer for a subscriber (unimplemented).
 *
 * @param subID    The subscriber requesting a buffer.
 * @param bufID    Output pointer for the acquired buffer_id.
 * @param timeout  Maximum time to wait in microseconds.
 * @return B_ERROR always.
 */
status_t
BBufferStream::AcquireBuffer(subscriber_id subID,
							 buffer_id *bufID,
							 bigtime_t timeout)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Releases a buffer back to the stream after processing (unimplemented).
 *
 * @param subID  The subscriber releasing the buffer.
 * @return B_ERROR always.
 */
status_t
BBufferStream::ReleaseBuffer(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns the size of a specific buffer in bytes (unimplemented).
 *
 * @param bufID  The buffer whose size is requested.
 * @return 0 always.
 */
size_t
BBufferStream::BufferSize(buffer_id bufID) const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns a pointer to the data region of a specific buffer (unimplemented).
 *
 * @param bufID  The buffer whose data pointer is requested.
 * @return NULL always.
 */
char *
BBufferStream::BufferData(buffer_id bufID) const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns whether a buffer is marked as the final buffer in the stream (unimplemented).
 *
 * @param bufID  The buffer to check.
 * @return false always.
 */
bool
BBufferStream::IsFinalBuffer(buffer_id bufID) const
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Returns the number of buffers currently held by a subscriber (unimplemented).
 *
 * @param subID  The subscriber to query.
 * @return 0 always.
 */
int32
BBufferStream::CountBuffersHeld(subscriber_id subID)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the total number of subscribers registered with this stream (unimplemented).
 *
 * @return 0 always.
 */
int32
BBufferStream::CountSubscribers() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the number of subscribers currently in the processing chain (unimplemented).
 *
 * @return 0 always.
 */
int32
BBufferStream::CountEnteredSubscribers() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the subscriber_id of the first subscriber in the chain (unimplemented).
 *
 * @return 0 always.
 */
subscriber_id
BBufferStream::FirstSubscriber() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the subscriber_id of the last subscriber in the chain (unimplemented).
 *
 * @return 0 always.
 */
subscriber_id
BBufferStream::LastSubscriber() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the subscriber_id following a given one in the chain (unimplemented).
 *
 * @param subID  The current subscriber.
 * @return 0 always.
 */
subscriber_id
BBufferStream::NextSubscriber(subscriber_id subID)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the subscriber_id preceding a given one in the chain (unimplemented).
 *
 * @param subID  The current subscriber.
 * @return 0 always.
 */
subscriber_id
BBufferStream::PrevSubscriber(subscriber_id subID)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Prints a debug summary of the stream state (unimplemented).
 */
void
BBufferStream::PrintStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Prints a debug summary of all buffers in the stream (unimplemented).
 */
void
BBufferStream::PrintBuffers()
{
	UNIMPLEMENTED();
}


/**
 * @brief Prints a debug summary of all subscribers in the stream (unimplemented).
 */
void
BBufferStream::PrintSubscribers()
{
	UNIMPLEMENTED();
}


/**
 * @brief Acquires the stream lock (unimplemented).
 *
 * @return false always.
 */
bool
BBufferStream::Lock()
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Releases the stream lock (unimplemented).
 */
void
BBufferStream::Unlock()
{
	UNIMPLEMENTED();
}


/**
 * @brief Adds an existing buffer to the stream's buffer ring (unimplemented).
 *
 * @param bufID  The buffer_id to add.
 * @return B_ERROR always.
 */
status_t
BBufferStream::AddBuffer(buffer_id bufID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Removes a buffer from the stream ring, optionally by force (unimplemented).
 *
 * @param force  If true, remove even if a subscriber holds the buffer.
 * @return 0 always.
 */
buffer_id
BBufferStream::RemoveBuffer(bool force)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Allocates a new buffer in the stream ring (unimplemented).
 *
 * @param size     Size of the new buffer in bytes.
 * @param isFinal  If true, marks this as the terminal buffer in the stream.
 * @return 0 always.
 */
buffer_id
BBufferStream::CreateBuffer(size_t size,
							bool isFinal)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Frees a previously created stream buffer (unimplemented).
 *
 * @param bufID  The buffer_id to destroy.
 */
void
BBufferStream::DestroyBuffer(buffer_id bufID)
{
	UNIMPLEMENTED();
}


/**
 * @brief Removes all buffers from the stream ring without freeing them (unimplemented).
 */
void
BBufferStream::RescindBuffers()
{
	UNIMPLEMENTED();
}

/*************************************************************
 * private BBufferStream
 *************************************************************/

/**
 * @brief Reserved virtual method slot 1 (unimplemented).
 */
void
BBufferStream::_ReservedBufferStream1()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 2 (unimplemented).
 */
void
BBufferStream::_ReservedBufferStream2()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 3 (unimplemented).
 */
void
BBufferStream::_ReservedBufferStream3()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 4 (unimplemented).
 */
void
BBufferStream::_ReservedBufferStream4()
{
	UNIMPLEMENTED();
}


/**
 * @brief Initialises the subscriber tracking data structures (unimplemented).
 */
void
BBufferStream::InitSubscribers()
{
	UNIMPLEMENTED();
}


/**
 * @brief Thread-safe check for subscriber registration (unimplemented).
 *
 * @param subID  The subscriber_id to check.
 * @return false always.
 */
bool
BBufferStream::IsSubscribedSafe(subscriber_id subID) const
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Thread-safe check for chain membership (unimplemented).
 *
 * @param subID  The subscriber_id to check.
 * @return false always.
 */
bool
BBufferStream::IsEnteredSafe(subscriber_id subID) const
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Initialises the buffer ring data structures (unimplemented).
 */
void
BBufferStream::InitBuffers()
{
	UNIMPLEMENTED();
}


/**
 * @brief Wakes a subscriber that is waiting for the next buffer (unimplemented).
 *
 * @param subID  The subscriber to wake.
 * @return B_ERROR always.
 */
status_t
BBufferStream::WakeSubscriber(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Transfers buffers held by a departing subscriber to its successor (unimplemented).
 *
 * @param subID  The departing subscriber whose buffers are to be inherited.
 */
void
BBufferStream::InheritBuffers(subscriber_id subID)
{
	UNIMPLEMENTED();
}


/**
 * @brief Passes buffers held by a subscriber to its predecessor before exit (unimplemented).
 *
 * @param subID  The subscriber donating its buffers.
 */
void
BBufferStream::BequeathBuffers(subscriber_id subID)
{
	UNIMPLEMENTED();
}


/**
 * @brief Thread-safe buffer release from a subscriber (unimplemented).
 *
 * @param subID  The subscriber releasing the buffer.
 * @return B_ERROR always.
 */
status_t
BBufferStream::ReleaseBufferSafe(subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Transfers a specific buffer to a target subscriber (unimplemented).
 *
 * @param bufID  The buffer to transfer.
 * @param subID  The target subscriber.
 * @return B_ERROR always.
 */
status_t
BBufferStream::ReleaseBufferTo(buffer_id bufID,
							   subscriber_id subID)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Frees all buffers in the stream ring (unimplemented).
 */
void
BBufferStream::FreeAllBuffers()
{
	UNIMPLEMENTED();
}


/**
 * @brief Removes and frees all subscriber records (unimplemented).
 */
void
BBufferStream::FreeAllSubscribers()
{
	UNIMPLEMENTED();
}


#endif	// __GNUC__ < 3
