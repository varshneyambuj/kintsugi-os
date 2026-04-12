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
 * @file OldSubscriber.cpp
 * @brief Stub implementations for the deprecated BSubscriber class.
 *
 * BSubscriber was the BeOS R4 mechanism for inserting application processing
 * into a BBufferStream's subscriber chain, including optional background
 * thread execution. All methods call UNIMPLEMENTED() and return default
 * values. Compiled only for GCC 2 builds needed for BeIDE and midi library
 * binary compatibility.
 *
 * @see OldBufferStream.cpp, OldBufferStreamManager.cpp
 */


// This is deprecated API that is not even implemented - no need to export
// it on a GCC4 build (BeIDE needs it to run, though, so it's worthwhile for
// GCC2)
#if __GNUC__ < 3


#include "OldSubscriber.h"

#include "MediaDebug.h"


/*************************************************************
 * public BSubscriber
 *************************************************************/

/**
 * @brief Constructs a BSubscriber with the given name (unimplemented).
 *
 * @param name  Human-readable name for this subscriber.
 */
BSubscriber::BSubscriber(const char *name)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BSubscriber (unimplemented).
 */
BSubscriber::~BSubscriber()
{
	UNIMPLEMENTED();
}


/**
 * @brief Subscribes this entity to a BAbstractBufferStream (unimplemented).
 *
 * @param stream  The buffer stream to subscribe to.
 * @return B_ERROR always.
 */
status_t
BSubscriber::Subscribe(BAbstractBufferStream *stream)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Unsubscribes this entity from its current stream (unimplemented).
 *
 * @return B_ERROR always.
 */
status_t
BSubscriber::Unsubscribe()
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns the subscriber_id assigned after a successful Subscribe() (unimplemented).
 *
 * @return 0 always.
 */
subscriber_id
BSubscriber::ID() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the name of this subscriber (unimplemented).
 *
 * @return NULL always.
 */
const char *
BSubscriber::Name() const
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Sets the timeout used when waiting for the next buffer (unimplemented).
 *
 * @param microseconds  Timeout in microseconds.
 */
void
BSubscriber::SetTimeout(bigtime_t microseconds)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the current wait timeout for this subscriber (unimplemented).
 *
 * @return 0 always.
 */
bigtime_t
BSubscriber::Timeout() const
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Enters this subscriber into a stream's processing chain (unimplemented).
 *
 * Optionally starts a background processing thread. The entry and exit
 * hook functions are called at the corresponding chain transitions.
 *
 * @param neighbor       Reference subscriber for positional insertion.
 * @param before         true to insert before \a neighbor, false to insert after.
 * @param userData       Opaque data forwarded to the hook functions.
 * @param entryFunction  Called when the subscriber enters the chain.
 * @param exitFunction   Called when the subscriber exits the chain.
 * @param background     If true, processes buffers on a background thread.
 * @return B_ERROR always.
 */
status_t
BSubscriber::EnterStream(subscriber_id neighbor,
						 bool before,
						 void *userData,
						 enter_stream_hook entryFunction,
						 exit_stream_hook exitFunction,
						 bool background)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Removes this subscriber from its current stream chain (unimplemented).
 *
 * @param synch  If true, waits for the background thread to finish before returning.
 * @return B_ERROR always.
 */
status_t
BSubscriber::ExitStream(bool synch)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns whether this subscriber is currently in a stream chain (unimplemented).
 *
 * @return false always.
 */
bool
BSubscriber::IsInStream() const
{
	UNIMPLEMENTED();

	return false;
}

/*************************************************************
 * protected BSubscriber
 *************************************************************/

/**
 * @brief Static thread entry point that calls ProcessLoop() (unimplemented).
 *
 * @param arg  Pointer to the BSubscriber instance.
 * @return B_ERROR always.
 */
status_t
BSubscriber::_ProcessLoop(void *arg)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief The subscriber's main buffer-processing loop (unimplemented).
 *
 * Subclasses override this to implement per-buffer processing logic.
 *
 * @return B_ERROR always.
 */
status_t
BSubscriber::ProcessLoop()
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns the stream this subscriber is currently subscribed to (unimplemented).
 *
 * @return NULL always.
 */
BAbstractBufferStream *
BSubscriber::Stream() const
{
	UNIMPLEMENTED();
	return NULL;
}

/*************************************************************
 * private BSubscriber
 *************************************************************/

/**
 * @brief Reserved virtual method slot 1 (unimplemented).
 */
void
BSubscriber::_ReservedSubscriber1()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 2 (unimplemented).
 */
void
BSubscriber::_ReservedSubscriber2()
{
	UNIMPLEMENTED();
}


/**
 * @brief Reserved virtual method slot 3 (unimplemented).
 */
void
BSubscriber::_ReservedSubscriber3()
{
	UNIMPLEMENTED();
}


#endif	// __GNUC__ < 3
