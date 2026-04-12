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
 * @file OldMediaModule.cpp
 * @brief Stub implementations for the deprecated BeOS R4 media module classes.
 *
 * Contains unimplemented bodies for BMediaEvent, BEventStream, BMediaRenderer,
 * BTransport, BTimeBase, and BMediaChannel — the event-driven media pipeline
 * that predates the BMediaNode API introduced in BeOS R5. All methods call
 * UNIMPLEMENTED() and return default values. Compiled only for GCC 2 builds.
 *
 * @see OldAudioModule.cpp, OldBufferStream.cpp
 */


// This is deprecated API that is not even implemented - no need to export
// it on a GCC4 build (BeIDE needs it to run, though, so it's worthwhile for
// GCC2)
#if __GNUC__ < 3


#include "OldMediaModule.h"

#include <MediaDebug.h>


/*************************************************************
 * public BMediaEvent
 *************************************************************/

/**
 * @brief Returns the duration of this media event (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BMediaEvent::Duration()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the capture timestamp of this media event (unimplemented).
 *
 * @return 0 always.
 */
bigtime_t
BMediaEvent::CaptureTime()
{
	UNIMPLEMENTED();

	return 0;
}

/*************************************************************
 * public BEventStream
 *************************************************************/

/**
 * @brief Constructs a BEventStream (unimplemented).
 */
BEventStream::BEventStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BEventStream (unimplemented).
 */
BEventStream::~BEventStream()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the stream's start time in media ticks (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BEventStream::Start()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the stream's start time (unimplemented).
 *
 * @param time  New start time in media ticks.
 */
void
BEventStream::SetStart(mk_time)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the total duration of the stream (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BEventStream::Duration()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the total duration of the stream (unimplemented).
 *
 * @param duration  New duration in media ticks.
 */
void
BEventStream::SetDuration(mk_time)
{
	UNIMPLEMENTED();
}


/**
 * @brief Seeks the stream to the given media time (unimplemented).
 *
 * @param channel  The channel requesting the seek.
 * @param time     Target media time.
 * @return B_ERROR always.
 */
status_t
BEventStream::SeekToTime(BMediaChannel *channel,
						 mk_time time)
{
	UNIMPLEMENTED();

	return B_ERROR;
}

/*************************************************************
 * public BMediaRenderer
 *************************************************************/


/**
 * @brief Constructs a BMediaRenderer with the given name and thread priority (unimplemented).
 *
 * @param name      Human-readable renderer name.
 * @param priority  Thread scheduling priority.
 */
BMediaRenderer::BMediaRenderer(const char *name,
							   int32 priority)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BMediaRenderer (unimplemented).
 */
BMediaRenderer::~BMediaRenderer()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the name of this renderer (unimplemented).
 *
 * @return NULL always.
 */
char *
BMediaRenderer::Name()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Returns the rendering latency of this renderer (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BMediaRenderer::Latency()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the BTransport associated with this renderer (unimplemented).
 *
 * @return NULL always.
 */
BTransport *
BMediaRenderer::Transport()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Assigns a BTransport to this renderer (unimplemented).
 *
 * @param transport  The BTransport to use.
 */
void
BMediaRenderer::SetTransport(BTransport *)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the start time of the renderer's active range (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BMediaRenderer::Start()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the duration of the renderer's active range (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BMediaRenderer::Duration()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the BTimeBase used by this renderer (unimplemented).
 *
 * @return NULL always.
 */
BTimeBase *
BMediaRenderer::TimeBase()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Opens the renderer for playback (unimplemented).
 */
void
BMediaRenderer::Open()
{
	UNIMPLEMENTED();
}


/**
 * @brief Closes the renderer (unimplemented).
 */
void
BMediaRenderer::Close()
{
	UNIMPLEMENTED();
}


/**
 * @brief Wakes the renderer thread to process pending events (unimplemented).
 */
void
BMediaRenderer::WakeUp()
{
	UNIMPLEMENTED();
}


/**
 * @brief Notifies the renderer of a transport state change (unimplemented).
 *
 * @param time    Media time of the transport change.
 * @param rate    New playback rate.
 * @param status  New transport status.
 */
void
BMediaRenderer::TransportChanged(mk_time time,
								 mk_rate rate,
								 transport_status status)
{
	UNIMPLEMENTED();
}


/**
 * @brief Notifies the renderer that the stream configuration changed (unimplemented).
 */
void
BMediaRenderer::StreamChanged()
{
	UNIMPLEMENTED();
}


/**
 * @brief Called when an Open() message is received by the renderer thread (unimplemented).
 */
void
BMediaRenderer::OpenReceived()
{
	UNIMPLEMENTED();
}


/**
 * @brief Called when a Close() message is received by the renderer thread (unimplemented).
 */
void
BMediaRenderer::CloseReceived()
{
	UNIMPLEMENTED();
}


/**
 * @brief Called when a WakeUp() message is received by the renderer thread (unimplemented).
 */
void
BMediaRenderer::WakeUpReceived()
{
	UNIMPLEMENTED();
}


/**
 * @brief Called when a TransportChanged() message is received by the renderer thread (unimplemented).
 *
 * @param time    Media time of the transport change.
 * @param rate    New playback rate.
 * @param status  New transport status.
 */
void
BMediaRenderer::TransportChangedReceived(mk_time time,
										 mk_rate rate,
										 transport_status status)
{
	UNIMPLEMENTED();
}


/**
 * @brief Called when a StreamChanged() message is received by the renderer thread (unimplemented).
 */
void
BMediaRenderer::StreamChangedReceived()
{
	UNIMPLEMENTED();
}

/*************************************************************
 * private BMediaRenderer
 *************************************************************/


/**
 * @brief Static thread entry point that calls LoopThread() (unimplemented).
 *
 * @param arg  Pointer to the BMediaRenderer instance.
 * @return 0 always.
 */
int32
BMediaRenderer::_LoopThread(void *arg)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief The renderer's main event-processing loop (unimplemented).
 */
void
BMediaRenderer::LoopThread()
{
	UNIMPLEMENTED();
}

/*************************************************************
 * public BTransport
 *************************************************************/


/**
 * @brief Constructs a BTransport (unimplemented).
 */
BTransport::BTransport()
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BTransport (unimplemented).
 */
BTransport::~BTransport()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the BTimeBase providing the clock for this transport (unimplemented).
 *
 * @return NULL always.
 */
BTimeBase *
BTransport::TimeBase()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Sets the BTimeBase clock for this transport (unimplemented).
 *
 * @param timeBase  The BTimeBase to use.
 */
void
BTransport::SetTimeBase(BTimeBase *)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the list of renderers attached to this transport (unimplemented).
 *
 * @return NULL always.
 */
BList *
BTransport::Renderers()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Adds a renderer to this transport's renderer list (unimplemented).
 *
 * @param renderer  The BMediaRenderer to add.
 */
void
BTransport::AddRenderer(BMediaRenderer *)
{
	UNIMPLEMENTED();
}


/**
 * @brief Removes a renderer from this transport's renderer list (unimplemented).
 *
 * @param renderer  The BMediaRenderer to remove.
 * @return false always.
 */
bool
BTransport::RemoveRenderer(BMediaRenderer *)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Returns the current transport status (unimplemented).
 *
 * @return B_TRANSPORT_STOPPED always.
 */
transport_status
BTransport::Status()
{
	UNIMPLEMENTED();

	return B_TRANSPORT_STOPPED;
}


/**
 * @brief Sets the current transport status (unimplemented).
 *
 * @param status  New transport_status value.
 */
void
BTransport::SetStatus(transport_status)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the current performance time of the transport (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BTransport::PerformanceTime()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the current playback rate of the transport (unimplemented).
 *
 * @return 0 always.
 */
mk_rate
BTransport::PerformanceRate()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the time offset applied to media timestamps (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BTransport::TimeOffset()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the time offset applied to media timestamps (unimplemented).
 *
 * @param offset  New time offset in media ticks.
 */
void
BTransport::SetTimeOffset(mk_time)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the maximum latency among all attached renderers (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BTransport::MaximumLatency()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the start boundary of the active performance range (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BTransport::PerformanceStart()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the end boundary of the active performance range (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BTransport::PerformanceEnd()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Opens all attached renderers for playback (unimplemented).
 */
void
BTransport::Open()
{
	UNIMPLEMENTED();
}


/**
 * @brief Closes all attached renderers (unimplemented).
 */
void
BTransport::Close()
{
	UNIMPLEMENTED();
}


/**
 * @brief Notifies renderers of a transport state change (unimplemented).
 */
void
BTransport::TransportChanged()
{
	UNIMPLEMENTED();
}


/**
 * @brief Notifies renderers that the time base has skipped (unimplemented).
 */
void
BTransport::TimeSkipped()
{
	UNIMPLEMENTED();
}


/**
 * @brief Schedules a wake-up call for a renderer at a given media time (unimplemented).
 *
 * @param time      Media time at which to wake the renderer.
 * @param renderer  The BMediaRenderer to wake.
 */
void
BTransport::RequestWakeUp(mk_time,
						  BMediaRenderer *)
{
	UNIMPLEMENTED();
}


/**
 * @brief Seeks all attached renderers to the given media time (unimplemented).
 *
 * @param time  Target media time.
 */
void
BTransport::SeekToTime(mk_time)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the BMediaChannel for a given selector index (unimplemented).
 *
 * @param selector  Index of the desired channel.
 * @return NULL always.
 */
BMediaChannel *
BTransport::GetChannel(int32 selector)
{
	UNIMPLEMENTED();
	return NULL;
}

/*************************************************************
 * public BTimeBase
 *************************************************************/


/**
 * @brief Constructs a BTimeBase with the given reference rate (unimplemented).
 *
 * @param rate  Reference clock rate in ticks per second.
 */
BTimeBase::BTimeBase(mk_rate rate)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BTimeBase (unimplemented).
 */
BTimeBase::~BTimeBase()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the list of transports using this time base (unimplemented).
 *
 * @return NULL always.
 */
BList *
BTimeBase::Transports()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Adds a BTransport to this time base's transport list (unimplemented).
 *
 * @param transport  The BTransport to add.
 */
void
BTimeBase::AddTransport(BTransport *)
{
	UNIMPLEMENTED();
}


/**
 * @brief Removes a BTransport from this time base's transport list (unimplemented).
 *
 * @param transport  The BTransport to remove.
 * @return false always.
 */
bool
BTimeBase::RemoveTransport(BTransport *)
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Notifies transports that the time base has skipped discontinuously (unimplemented).
 */
void
BTimeBase::TimeSkipped()
{
	UNIMPLEMENTED();
}


/**
 * @brief Schedules a deferred function call at a given media time (unimplemented).
 *
 * @param time      Media time at which to invoke \a function.
 * @param function  The mk_deferred_call to invoke.
 * @param arg       Argument forwarded to \a function.
 * @return B_ERROR always.
 */
status_t
BTimeBase::CallAt(mk_time time,
				  mk_deferred_call function,
				  void *arg)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Returns the current media time according to this time base (unimplemented).
 *
 * @return 0 always.
 */
mk_time
BTimeBase::Time()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Returns the current clock rate of this time base (unimplemented).
 *
 * @return 0 always.
 */
mk_rate
BTimeBase::Rate()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Converts a system time to media time using this time base (unimplemented).
 *
 * @param system_time  System time in microseconds.
 * @return 0 always.
 */
mk_time
BTimeBase::TimeAt(bigtime_t system_time)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Converts a media time to system time using this time base (unimplemented).
 *
 * @param time  Media time in ticks.
 * @return 0 always.
 */
bigtime_t
BTimeBase::SystemTimeAt(mk_time time)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Synchronises the time base to a given media/system time pair (unimplemented).
 *
 * @param time         Reference media time.
 * @param system_time  Corresponding system time in microseconds.
 */
void
BTimeBase::Sync(mk_time time,
				bigtime_t system_time)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns whether this time base is driven by an absolute system clock (unimplemented).
 *
 * @return false always.
 */
bool
BTimeBase::IsAbsolute()
{
	UNIMPLEMENTED();

	return false;
}

/*************************************************************
 * private BTimeBase
 *************************************************************/

/**
 * @brief Static thread entry point for the snooze thread (unimplemented).
 *
 * @param arg  Pointer to the BTimeBase instance.
 * @return 0 always.
 */
int32
BTimeBase::_SnoozeThread(void *arg)
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief The time base's deferred-call snooze loop (unimplemented).
 */
void
BTimeBase::SnoozeThread()
{
	UNIMPLEMENTED();
}

/*************************************************************
 * public BMediaChannel
 *************************************************************/

/**
 * @brief Constructs a BMediaChannel linking a renderer and event source (unimplemented).
 *
 * @param rate      Sample/event rate for this channel.
 * @param renderer  The BMediaRenderer that consumes events from this channel.
 * @param source    The BEventStream providing events to this channel.
 */
BMediaChannel::BMediaChannel(mk_rate rate,
							 BMediaRenderer *renderer,
							 BEventStream *source)
{
	UNIMPLEMENTED();
}


/**
 * @brief Destroys the BMediaChannel (unimplemented).
 */
BMediaChannel::~BMediaChannel()
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the renderer attached to this channel (unimplemented).
 *
 * @return NULL always.
 */
BMediaRenderer *
BMediaChannel::Renderer()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Sets the renderer for this channel (unimplemented).
 *
 * @param renderer  The BMediaRenderer to use.
 */
void
BMediaChannel::SetRenderer(BMediaRenderer *)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the event stream source for this channel (unimplemented).
 *
 * @return NULL always.
 */
BEventStream *
BMediaChannel::Source()
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Sets the event stream source for this channel (unimplemented).
 *
 * @param source  The BEventStream to use.
 */
void
BMediaChannel::SetSource(BEventStream *)
{
	UNIMPLEMENTED();
}


/**
 * @brief Returns the sample/event rate of this channel (unimplemented).
 *
 * @return 0 always.
 */
mk_rate
BMediaChannel::Rate()
{
	UNIMPLEMENTED();

	return 0;
}


/**
 * @brief Sets the sample/event rate of this channel (unimplemented).
 *
 * @param rate  New rate value.
 */
void
BMediaChannel::SetRate(mk_rate)
{
	UNIMPLEMENTED();
}


/**
 * @brief Acquires the channel lock (unimplemented).
 *
 * @return false always.
 */
bool
BMediaChannel::LockChannel()
{
	UNIMPLEMENTED();

	return false;
}


/**
 * @brief Acquires the channel lock with a timeout (unimplemented).
 *
 * @param timeout  Maximum wait time in microseconds.
 * @return B_ERROR always.
 */
status_t
BMediaChannel::LockWithTimeout(bigtime_t)
{
	UNIMPLEMENTED();

	return B_ERROR;
}


/**
 * @brief Releases the channel lock (unimplemented).
 */
void
BMediaChannel::UnlockChannel()
{
	UNIMPLEMENTED();
}


/**
 * @brief Notifies the channel that the stream configuration has changed (unimplemented).
 */
void
BMediaChannel::StreamChanged()
{
	UNIMPLEMENTED();
}


#endif	// __GNUC__ < 3
