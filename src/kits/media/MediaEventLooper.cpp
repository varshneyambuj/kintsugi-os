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
 *   Copyright (c) 2015 Dario Casalinuovo <b.vitruvio@gmail.com>
 *   Copyright (c) 2002, 2003 Marcus Overhagen <Marcus@Overhagen.de>
 *
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software and associated documentation files or portions
 *   thereof (the "Software"), to deal in the Software without restriction,
 *   including without limitation the rights to use, copy, modify, merge,
 *   publish, distribute, sublicense, and/or sell copies of the Software,
 *   and to permit persons to whom the Software is furnished to do so, subject
 *   to the following conditions:
 *
 *    * Redistributions of source code must retain the above copyright notice,
 *      this list of conditions and the following disclaimer.
 *
 *    * Redistributions in binary form must reproduce the above copyright notice
 *      in the  binary, as well as this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided with
 *      the distribution.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *   THE SOFTWARE.
 */

/** @file MediaEventLooper.cpp
 *  @brief Implements BMediaEventLooper, the event-driven control-thread mixin for media nodes. */


#include <MediaEventLooper.h>
#include <TimeSource.h>
#include <scheduler.h>
#include <Buffer.h>
#include <ServerInterface.h>
#include "MediaDebug.h"

/*************************************************************
 * protected BMediaEventLooper
 *************************************************************/

/** @brief Destructor; calls Quit() if the control thread was not already stopped.
 *
 *  Subclasses MUST call BMediaEventLooper::Quit() in their own destructor before
 *  this base destructor runs. */
/* virtual */
BMediaEventLooper::~BMediaEventLooper()
{
	CALLED();

	// don't call Quit(); here, except if the user was stupid
	if (fControlThread != -1) {
		printf("You MUST call BMediaEventLooper::Quit() in your destructor!\n");
		Quit();
	}
}

/** @brief Constructs a BMediaEventLooper with the given API version.
 *
 *  Initialises both event queues and registers cleanup hooks.
 *
 *  @param apiVersion The API version to advertise; defaults to B_BEOS_VERSION. */
/* explicit */
BMediaEventLooper::BMediaEventLooper(uint32 apiVersion) :
	BMediaNode("called by BMediaEventLooper"),
	fControlThread(-1),
	fCurrentPriority(B_URGENT_PRIORITY),
	fSetPriority(B_URGENT_PRIORITY),
	fRunState(B_UNREGISTERED),
	fEventLatency(0),
	fSchedulingLatency(0),
	fBufferDuration(0),
	fOfflineTime(0),
	fApiVersion(apiVersion)
{
	CALLED();
	fEventQueue.SetCleanupHook(BMediaEventLooper::_CleanUpEntry, this);
	fRealTimeQueue.SetCleanupHook(BMediaEventLooper::_CleanUpEntry, this);
}

/** @brief Called by the media server after the node is registered; starts the control loop.
 *
 *  Calls Run() to spawn the control thread.  BeOS R5 called Run() here;
 *  some nodes require it even though the BeBook says the subclass should do it. */
/* virtual */ void
BMediaEventLooper::NodeRegistered()
{
	CALLED();
	// Calling Run() should be done by the derived class,
	// at least that's how it is documented by the BeBook.
	// It appears that BeOS R5 called it here. Calling Run()
	// twice doesn't hurt, and some nodes need it to be called here.
	Run();
}


/** @brief Hook called when the node is started; enqueues a B_START event.
 *
 *  @param performance_time The performance time at which the node should start. */
/* virtual */ void
BMediaEventLooper::Start(bigtime_t performance_time)
{
	CALLED();
	// This hook function is called when a node is started
	// by a call to the BMediaRoster. The specified
	// performanceTime, the time at which the node
	// should start running, may be in the future.
	fEventQueue.AddEvent(media_timed_event(performance_time, BTimedEventQueue::B_START));
}


/** @brief Hook called when the node is stopped; enqueues a B_STOP event.
 *
 *  If \a immediate is true the stop time is forced to 0 so the event is
 *  processed before any pending buffer events.
 *
 *  @param performance_time The scheduled performance time for the stop.
 *  @param immediate        If true, stop immediately regardless of \a performance_time. */
/* virtual */ void
BMediaEventLooper::Stop(bigtime_t performance_time,
						bool immediate)
{
	CALLED();
	// This hook function is called when a node is stopped
	// by a call to the BMediaRoster. The specified performanceTime,
	// the time at which the node should stop, may be in the future.
	// If immediate is true, your node should ignore the performanceTime
	// value and synchronously stop performance. When Stop() returns,
	// you're promising not to write into any BBuffers you may have
	// received from your downstream consumers, and you promise not
	// to send any more buffers until Start() is called again.

	if (immediate) {
		// always be sure to add to the front of the queue so we can make sure it is
		// handled before any buffers are sent!
		performance_time = 0;
	}
	fEventQueue.AddEvent(media_timed_event(performance_time, BTimedEventQueue::B_STOP));
}


/** @brief Hook called when the node should seek; enqueues a B_SEEK event.
 *
 *  @param media_time        The target media (stream) time to seek to.
 *  @param performance_time  The performance time at which the seek should begin. */
/* virtual */ void
BMediaEventLooper::Seek(bigtime_t media_time,
						bigtime_t performance_time)
{
	CALLED();
	// This hook function is called when a node is asked to seek to
	// the specified mediaTime by a call to the BMediaRoster.
	// The specified performanceTime, the time at which the node
	// should begin the seek operation, may be in the future.
	fEventQueue.AddEvent(media_timed_event(performance_time, BTimedEventQueue::B_SEEK, NULL,
		BTimedEventQueue::B_NO_CLEANUP, 0, media_time, NULL));
}


/** @brief Hook called on a time-warp; enqueues a B_WARP event in the real-time queue.
 *
 *  Also forwards the call to BMediaNode::TimeWarp() as required by the BeBook.
 *
 *  @param at_real_time         Real time at which the warp takes effect.
 *  @param to_performance_time  New performance time corresponding to \a at_real_time. */
/* virtual */ void
BMediaEventLooper::TimeWarp(bigtime_t at_real_time,
							bigtime_t to_performance_time)
{
	CALLED();
	// This hook function is called when the time source to which the
	// node is slaved is repositioned (via a seek operation) such that
	// there will be a sudden jump in the performance time progression
	// as seen by the node. The to_performance_time argument indicates
	// the new performance time; the change should occur at the real
	// time specified by the at_real_time argument.

	// place in the realtime queue
	fRealTimeQueue.AddEvent(media_timed_event(at_real_time,	BTimedEventQueue::B_WARP,
		NULL, BTimedEventQueue::B_NO_CLEANUP, 0, to_performance_time, NULL));

	// BeBook: Your implementation of TimeWarp() should call through to BMediaNode::TimeWarp()
	// BeBook: as well as all other inherited forms of TimeWarp()
	// XXX should we do this here?
	BMediaNode::TimeWarp(at_real_time, to_performance_time);
}


/** @brief Adds a B_TIMER event that fires at the given performance time.
 *
 *  @param at_performance_time The performance time at which TimerExpired() should be called.
 *  @param cookie              An arbitrary value forwarded to TimerExpired().
 *  @return B_OK on success, or an error code from BTimedEventQueue::AddEvent(). */
/* virtual */ status_t
BMediaEventLooper::AddTimer(bigtime_t at_performance_time,
							int32 cookie)
{
	CALLED();

	media_timed_event event(at_performance_time,
		BTimedEventQueue::B_TIMER, NULL,
		BTimedEventQueue::B_EXPIRE_TIMER);
	event.data = cookie;
	return EventQueue()->AddEvent(event);
}


/** @brief Sets the run mode and adjusts the control thread priority accordingly.
 *
 *  Switching to B_OFFLINE mode clamps the priority to B_NORMAL_PRIORITY;
 *  leaving it restores the configured priority.
 *
 *  @param mode The new run mode (one of the BMediaNode::run_mode values). */
/* virtual */ void
BMediaEventLooper::SetRunMode(run_mode mode)
{
	CALLED();
	// The SetRunMode() hook function is called when someone requests that your node's run mode be changed.

	// bump or reduce priority when switching from/to offline run mode
	int32 priority;
	priority = (mode == B_OFFLINE) ? min_c(B_NORMAL_PRIORITY, fSetPriority) : fSetPriority;
	if (priority != fCurrentPriority) {
		fCurrentPriority = priority;
		if (fControlThread > 0) {
			set_thread_priority(fControlThread, fCurrentPriority);
			fSchedulingLatency = estimate_max_scheduling_latency(fControlThread);
			printf("BMediaEventLooper: SchedulingLatency is %" B_PRId64 "\n",
				fSchedulingLatency);
		}
	}

	BMediaNode::SetRunMode(mode);
}


/** @brief Hook for cleaning up after custom user-defined events; default does nothing.
 *
 *  Subclasses may override this to perform any resource release needed for
 *  custom event types when they are flushed from the queue.
 *
 *  @param event The custom event being removed. */
/* virtual */ void
BMediaEventLooper::CleanUpEvent(const media_timed_event *event)
{
	CALLED();
	// Implement this function to clean up after custom events you've created
	// and added to your queue. It's called when a custom event is removed from
	// the queue, to let you handle any special tidying-up that the event might require.
}


/** @brief Returns the current offline time used when the node runs in B_OFFLINE mode.
 *  @return The value set by the most recent SetOfflineTime() call. */
/* virtual */ bigtime_t
BMediaEventLooper::OfflineTime()
{
	CALLED();
	return fOfflineTime;
}


/** @brief The main control-thread loop; processes events from both event queues.
 *
 *  Calls WaitForMessage() with a deadline computed from the next earliest event
 *  time (adjusted for event and scheduling latency).  When the wait times out
 *  the earliest due event is dispatched via DispatchEvent(). */
/* virtual */ void
BMediaEventLooper::ControlLoop()
{
	CALLED();

	status_t err = B_OK;
	bigtime_t waitUntil = B_INFINITE_TIMEOUT;
	bool hasRealtime = false;
	bool hasEvent = false;

	// While there are no events or it is not time for the earliest event,
	// process messages using WaitForMessages. Whenever this funtion times out,
	// we need to handle the next event

	fSchedulingLatency = estimate_max_scheduling_latency(fControlThread);
	while (RunState() != B_QUITTING) {
		if (err == B_TIMED_OUT
				|| err == B_WOULD_BLOCK) {
			// NOTE: The reference for doing the lateness calculus this way can
			// be found in the BeBook article "A BMediaEventLooper Example".
			// The value which we are going to calculate, is referred there as
			// 'lateness'.
			media_timed_event event;
			if (hasEvent)
				err = fEventQueue.RemoveFirstEvent(&event);
			else if (hasRealtime)
				err = fRealTimeQueue.RemoveFirstEvent(&event);

			if (err == B_OK) {
				// The general idea of lateness is to allow
				// the client code to detect when the buffer
				// is handled late or early.
				bigtime_t lateness = TimeSource()->RealTime() - waitUntil;

				DispatchEvent(&event, lateness, hasRealtime);
			}
		} else if (err != B_OK)
			return;

		// BMediaEventLooper compensates your performance time by adding
		// the event latency (see SetEventLatency()) and the scheduling
		// latency (or, for real-time events, only the scheduling latency).

		hasRealtime = fRealTimeQueue.HasEvents();
		hasEvent = fEventQueue.HasEvents();

		if (hasEvent) {
			waitUntil = TimeSource()->RealTimeFor(
				fEventQueue.FirstEventTime(),
				fEventLatency + fSchedulingLatency);
		} else if (!hasRealtime)
			waitUntil = B_INFINITE_TIMEOUT;

		if (hasRealtime) {
			bigtime_t realtimeWait = fRealTimeQueue.FirstEventTime()
				- fSchedulingLatency;

			if (!hasEvent || realtimeWait <= waitUntil) {
				waitUntil = realtimeWait;
				hasEvent = false;
			} else
				hasRealtime = false;
		}

		if (waitUntil != B_INFINITE_TIMEOUT
				&& TimeSource()->RealTime() >= waitUntil) {
			err = WaitForMessage(0);
		} else
			err = WaitForMessage(waitUntil);
	}
}


/** @brief Returns the thread ID of the control thread.
 *  @return The thread_id, or -1 if the thread has not been started. */
thread_id
BMediaEventLooper::ControlThread()
{
	CALLED();
	return fControlThread;
}

/*************************************************************
 * protected BMediaEventLooper
 *************************************************************/


/** @brief Returns a pointer to the performance-time event queue.
 *  @return Pointer to the internal fEventQueue. */
BTimedEventQueue *
BMediaEventLooper::EventQueue()
{
	CALLED();
	return &fEventQueue;
}


/** @brief Returns a pointer to the real-time event queue used for time warps.
 *  @return Pointer to the internal fRealTimeQueue. */
BTimedEventQueue *
BMediaEventLooper::RealTimeQueue()
{
	CALLED();
	return &fRealTimeQueue;
}


/** @brief Returns the current effective thread priority.
 *  @return The thread priority currently applied to the control thread. */
int32
BMediaEventLooper::Priority() const
{
	CALLED();
	return fCurrentPriority;
}


/** @brief Returns the current run state of the node.
 *  @return One of B_UNREGISTERED, B_STOPPED, B_STARTED, B_QUITTING, or B_TERMINATED. */
int32
BMediaEventLooper::RunState() const
{
	PRINT(6, "CALLED BMediaEventLooper::RunState()\n");
	return fRunState;
}


/** @brief Returns the event latency added to performance-time deadlines.
 *  @return Event latency in microseconds. */
bigtime_t
BMediaEventLooper::EventLatency() const
{
	CALLED();
	return fEventLatency;
}


/** @brief Returns the buffer duration used for scheduling calculations.
 *  @return Buffer duration in microseconds. */
bigtime_t
BMediaEventLooper::BufferDuration() const
{
	CALLED();
	return fBufferDuration;
}


/** @brief Returns the estimated scheduling latency for the control thread.
 *  @return Scheduling latency in microseconds as estimated by estimate_max_scheduling_latency(). */
bigtime_t
BMediaEventLooper::SchedulingLatency() const
{
	CALLED();
	return fSchedulingLatency;
}


/** @brief Sets the desired thread priority, clamped to [5, 120].
 *
 *  If the control thread is already running its priority is adjusted immediately.
 *  In B_OFFLINE run mode the effective priority is further clamped to B_NORMAL_PRIORITY.
 *
 *  @param priority The desired scheduling priority.
 *  @return B_OK always. */
status_t
BMediaEventLooper::SetPriority(int32 priority)
{
	CALLED();

	// clamp to a valid value
	if (priority < 5)
		priority = 5;

	if (priority > 120)
		priority = 120;

	fSetPriority = priority;
	fCurrentPriority = (RunMode() == B_OFFLINE) ? min_c(B_NORMAL_PRIORITY, fSetPriority) : fSetPriority;

	if (fControlThread > 0) {
		set_thread_priority(fControlThread, fCurrentPriority);
		fSchedulingLatency = estimate_max_scheduling_latency(fControlThread);
		printf("BMediaEventLooper: SchedulingLatency is %" B_PRId64 "\n",
			fSchedulingLatency);
	}

	return B_OK;
}


/** @brief Sets the run state, ignoring changes after B_QUITTING (unless moving to B_TERMINATED).
 *
 *  @param state The new run state. */
void
BMediaEventLooper::SetRunState(run_state state)
{
	CALLED();

	// don't allow run state changes while quitting,
	// also needed for correct terminating of the ControlLoop()
	if (fRunState == B_QUITTING && state != B_TERMINATED)
		return;

	fRunState = state;
}


/** @brief Sets the additional latency added when computing event wake-up times.
 *
 *  Values below zero are clamped to zero.  The control port is poked to wake
 *  the control thread and recalculate its wait time.
 *
 *  @param latency The new event latency in microseconds. */
void
BMediaEventLooper::SetEventLatency(bigtime_t latency)
{
	CALLED();
	// clamp to a valid value
	if (latency < 0)
		latency = 0;

	fEventLatency = latency;
	write_port_etc(ControlPort(), GENERAL_PURPOSE_WAKEUP, 0, 0, B_TIMEOUT, 0);
}


/** @brief Sets the buffer duration used by subclasses for scheduling decisions.
 *
 *  Values below zero are clamped to zero.
 *
 *  @param duration The buffer duration in microseconds. */
void
BMediaEventLooper::SetBufferDuration(bigtime_t duration)
{
	CALLED();

	if (duration < 0)
		duration = 0;

	fBufferDuration = duration;
}


/** @brief Sets the current offline time used when running in B_OFFLINE mode.
 *  @param offTime The new offline time in microseconds. */
void
BMediaEventLooper::SetOfflineTime(bigtime_t offTime)
{
	CALLED();
	fOfflineTime = offTime;
}


/** @brief Spawns the control thread and starts processing events.
 *
 *  A no-op if the control thread is already running.  Transitions the run
 *  state from B_UNREGISTERED to B_STOPPED before the thread starts. */
void
BMediaEventLooper::Run()
{
	CALLED();

	if (fControlThread != -1)
		return; // thread already running

	// until now, the run state is B_UNREGISTERED, but we need to start in B_STOPPED state.
	SetRunState(B_STOPPED);

	char threadName[32];
	sprintf(threadName, "%.20s control", Name());
	fControlThread = spawn_thread(_ControlThreadStart, threadName, fCurrentPriority, this);
	resume_thread(fControlThread);

	// get latency information
	fSchedulingLatency = estimate_max_scheduling_latency(fControlThread);
}


/** @brief Signals the control thread to stop and waits for it to terminate.
 *
 *  Closes the control port to unblock WaitForMessage(), then joins the thread. */
void
BMediaEventLooper::Quit()
{
	CALLED();

	if (fRunState == B_TERMINATED)
		return;

	SetRunState(B_QUITTING);
	close_port(ControlPort());
	if (fControlThread != -1) {
		status_t err;
		wait_for_thread(fControlThread, &err);
		fControlThread = -1;
	}
	SetRunState(B_TERMINATED);
}


/** @brief Dispatches a single event to HandleEvent() and updates the run state.
 *
 *  After HandleEvent() returns, B_START and B_STOP events transition the run
 *  state, B_TIMER events invoke TimerExpired(), and user-cleanup events are
 *  forwarded to _DispatchCleanUp().
 *
 *  @param event          The event to dispatch.
 *  @param lateness       How late (in microseconds) the event is being handled.
 *  @param realTimeEvent  true if the event came from the real-time queue. */
void
BMediaEventLooper::DispatchEvent(const media_timed_event *event,
								 bigtime_t lateness,
								 bool realTimeEvent)
{
	PRINT(6, "CALLED BMediaEventLooper::DispatchEvent()\n");

	HandleEvent(event, lateness, realTimeEvent);

	switch (event->type) {
		case BTimedEventQueue::B_START:
			SetRunState(B_STARTED);
			break;

		case BTimedEventQueue::B_STOP:
			SetRunState(B_STOPPED);
			break;

		case BTimedEventQueue::B_SEEK:
			/* nothing */
			break;

		case BTimedEventQueue::B_WARP:
			/* nothing */
			break;

		case BTimedEventQueue::B_TIMER:
			TimerExpired(event->event_time, event->data);
			break;

		default:
			break;
	}

	_DispatchCleanUp(event);
}

/*************************************************************
 * private BMediaEventLooper
 *************************************************************/


/** @brief Static thread entry point; transitions state and runs ControlLoop().
 *
 *  @param arg Pointer to the BMediaEventLooper instance.
 *  @return 0 when the loop exits. */
/* static */ int32
BMediaEventLooper::_ControlThreadStart(void *arg)
{
	CALLED();
	((BMediaEventLooper *)arg)->SetRunState(B_STOPPED);
	((BMediaEventLooper *)arg)->ControlLoop();
	((BMediaEventLooper *)arg)->SetRunState(B_QUITTING);
	return 0;
}


/** @brief Static cleanup hook registered with the event queues; forwards to _DispatchCleanUp().
 *
 *  @param event   The event being flushed.
 *  @param context Pointer to the BMediaEventLooper instance. */
/* static */ void
BMediaEventLooper::_CleanUpEntry(const media_timed_event *event,
								 void *context)
{
	PRINT(6, "CALLED BMediaEventLooper::_CleanUpEntry()\n");
	((BMediaEventLooper *)context)->_DispatchCleanUp(event);
}


/** @brief Calls CleanUpEvent() for events with a user-defined cleanup code.
 *
 *  @param event The event to clean up. */
void
BMediaEventLooper::_DispatchCleanUp(const media_timed_event *event)
{
	PRINT(6, "CALLED BMediaEventLooper::_DispatchCleanUp()\n");

	// this function to clean up after custom events you've created
	if (event->cleanup >= BTimedEventQueue::B_USER_CLEANUP)
		CleanUpEvent(event);
}

/*
// unimplemented
BMediaEventLooper::BMediaEventLooper(const BMediaEventLooper &)
BMediaEventLooper &BMediaEventLooper::operator=(const BMediaEventLooper &)
*/

/*************************************************************
 * protected BMediaEventLooper
 *************************************************************/


/** @brief Called by the media server before deleting the node; calls Quit().
 *
 *  @param node The node about to be deleted (forwarded to BMediaNode::DeleteHook()).
 *  @return The return value of BMediaNode::DeleteHook(). */
status_t
BMediaEventLooper::DeleteHook(BMediaNode *node)
{
	CALLED();
	// this is the DeleteHook that gets called by the media server
	// before the media node is deleted
	Quit();
	return BMediaNode::DeleteHook(node);
}

/*************************************************************
 * private BMediaEventLooper
 *************************************************************/

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_0(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_1(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_2(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_3(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_4(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_5(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_6(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_7(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_8(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_9(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_10(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_11(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_12(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_13(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_14(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_15(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_16(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_17(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_18(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_19(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_20(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_21(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_22(int32 arg,...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaEventLooper::_Reserved_BMediaEventLooper_23(int32 arg,...) { return B_ERROR; }
