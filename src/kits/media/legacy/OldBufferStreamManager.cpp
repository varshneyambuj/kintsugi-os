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
 * @file OldBufferStreamManager.cpp
 * @brief Stub implementations for the deprecated BBufferStreamManager class.
 *
 * BBufferStreamManager was the BeOS R4 controller that owned a BBufferStream
 * and managed producer/consumer lifecycle including start, stop, and abort
 * state transitions. All methods call UNIMPLEMENTED() and return default
 * values. Compiled only for GCC 2 builds needed for BeIDE compatibility.
 *
 * @see OldBufferStream.cpp, OldSubscriber.cpp
 */


// This is deprecated API that is not even implemented - no need to export
// it on a GCC4 build (BeIDE needs it to run, though, so it's worthwhile for
// GCC2)
#if __GNUC__ < 3


#include "OldBufferStreamManager.h"

#include <MediaDebug.h>


/*************************************************************
 * public BBufferStreamManager
 *************************************************************/

/**
 * @brief Constructs a BBufferStreamManager with the given name (unimplemented).
 *
 * @param name  Human-readable name for this stream manager.
 */
BBufferStreamManager::BBufferStreamManager(char *name)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BBufferStreamManager (unimplemented).
 */
BBufferStreamManager::~BBufferStreamManager()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the name of this stream manager (unimplemented).
 *
 * @return NULL always.
 */
char *
BBufferStreamManager::Name() const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the BBufferStream controlled by this manager (unimplemented).
 *
 * @return NULL always.
 */
BBufferStream *
BBufferStreamManager::Stream() const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the number of buffers in the controlled stream (unimplemented).
 *
 * @return 0 always.
 */
int32
BBufferStreamManager::BufferCount() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the number of buffers in the stream ring (unimplemented).
 *
 * @param count  Desired buffer count.
 */
void
BBufferStreamManager::SetBufferCount(int32 count)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the size of each buffer in bytes (unimplemented).
 *
 * @return 0 always.
 */
int32
BBufferStreamManager::BufferSize() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the size of each buffer in bytes (unimplemented).
 *
 * @param bytes  Desired buffer size.
 */
void
BBufferStreamManager::SetBufferSize(int32 bytes)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the target inter-buffer delivery delay (unimplemented).
 *
 * @return 0 always.
 */
bigtime_t
BBufferStreamManager::BufferDelay() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the target inter-buffer delivery delay (unimplemented).
 *
 * @param usecs  Desired delay in microseconds.
 */
void
BBufferStreamManager::SetBufferDelay(bigtime_t usecs)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the subscriber operation timeout (unimplemented).
 *
 * @return 0 always.
 */
bigtime_t
BBufferStreamManager::Timeout() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the subscriber operation timeout (unimplemented).
 *
 * @param usecs  Timeout in microseconds.
 */
void
BBufferStreamManager::SetTimeout(bigtime_t usecs)
{
	UNIMPLEMENTED();
}


/**
 * @brief Starts the stream and transitions to the running state (unimplemented).
 *
 * @return B_IDLE always.
 */
stream_state
BBufferStreamManager::Start()
{
	UNIMPLEMENTED();

	return B_IDLE;
}


/**
 * @brief Stops the stream and transitions to the idle state (unimplemented).
 *
 * @return B_IDLE always.
 */
stream_state
BBufferStreamManager::Stop()
{
	UNIMPLEMENTED();

	return B_IDLE;
}


/**
 * @brief Aborts the stream immediately and transitions to the idle state (unimplemented).
 *
 * @return B_IDLE always.
 */
stream_state
BBufferStreamManager::Abort()
{
	UNIMPLEMENTED();

	return B_IDLE;
}


/**
 * @brief Returns the current stream state (unimplemented).
 *
 * @return B_IDLE always.
 */
stream_state
BBufferStreamManager::State() const
{
	UNIMPLEMENTED();

	return B_IDLE;
}


/**
 * @brief Returns the port used for stream state-change notifications (unimplemented).
 *
 * @return 0 always.
 */
port_id
BBufferStreamManager::NotificationPort() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the port for stream state-change notifications (unimplemented).
 *
 * @param port  The port_id to send notifications to.
 */
void
BBufferStreamManager::SetNotificationPort(port_id port)
{
	UNIMPLEMENTED();
}


/**
 * @brief Acquires the stream manager lock (unimplemented).
 *
 * @return false always.
 */
bool
BBufferStreamManager::Lock()
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Releases the stream manager lock (unimplemented).
 */
void
BBufferStreamManager::Unlock()
{
	UNIMPLEMENTED();
}


/**
 * @brief Subscribes this manager to a BBufferStream for processing (unimplemented).
 *
 * @param stream  The BBufferStream to subscribe to.
 * @return B_ERROR always.
 */
status_t
BBufferStreamManager::Subscribe(BBufferStream *stream)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Unsubscribes this manager from its current stream (unimplemented).
 *
 * @return B_ERROR always.
 */
status_t
BBufferStreamManager::Unsubscribe()
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns the subscriber_id assigned to this manager (unimplemented).
 *
 * @return 0 always.
 */
subscriber_id
BBufferStreamManager::ID() const
{
	UNIMPLEMENTED();

	return 0;
}

/*************************************************************
 * protected BBufferStreamManager
 *************************************************************/

/**
 * @brief Starts the internal processing thread (unimplemented).
 */
void
BBufferStreamManager::StartProcessing()
{
	UNIMPLEMENTED();
}


/**
 * @brief Stops the internal processing thread (unimplemented).
 */
void
BBufferStreamManager::StopProcessing()
{
	UNIMPLEMENTED();
}


/**
 * @brief Static thread entry point that calls ProcessingThread() (unimplemented).
 *
 * @param arg  Pointer to the BBufferStreamManager instance.
 * @return B_ERROR always.
 */
status_t
BBufferStreamManager::_ProcessingThread(void *arg)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief The main buffer processing loop executed by the processing thread (unimplemented).
 */
void
BBufferStreamManager::ProcessingThread()
{
	UNIMPLEMENTED();
}


/**
 * @brief Updates the current stream state and sends a notification (unimplemented).
 *
 * @param newState  The new stream_state value to set.
 */
void
BBufferStreamManager::SetState(stream_state newState)
{
	UNIMPLEMENTED();
}


/**
 * @brief Sleeps until a given system time, returning the actual wake time (unimplemented).
 *
 * @param sys_time  Target system time to sleep until.
 * @return 0 always.
 */
bigtime_t
BBufferStreamManager::SnoozeUntil(bigtime_t sys_time)
{
	UNIMPLEMENTED();

	return 0;
}

/*************************************************************
 * private BBufferStreamManager
 *************************************************************/

/**
 * @brief Reserved virtual method slot 1 (unimplemented).
 */
void
BBufferStreamManager::_ReservedBufferStreamManager1()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 2 (unimplemented).
 */
void
BBufferStreamManager::_ReservedBufferStreamManager2()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 3 (unimplemented).
 */
void
BBufferStreamManager::_ReservedBufferStreamManager3()
{
	UNIMPLEMENTED();
}


#endif	// __GNUC__ < 3
