/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file MediaEventLooper.h
 *  @brief Defines BMediaEventLooper, a BMediaNode subclass with a timed event dispatch loop.
 */

#ifndef _MEDIA_EVENT_LOOPER_H
#define _MEDIA_EVENT_LOOPER_H


#include <MediaNode.h>
#include <TimedEventQueue.h>


/** @brief BMediaNode subclass that manages a dedicated control thread and timed event queues.
 *
 *  BMediaEventLooper spawns a thread that calls WaitForMessage(), pushes BMediaNode
 *  messages onto BTimedEventQueues, and invokes HandleEvent() at the appropriate
 *  performance time.  Override HandleEvent() to perform your node's work.
 */
class BMediaEventLooper : public virtual BMediaNode {
protected:
	/** @brief Run-state values for this looper's control thread. */
	enum run_state {
		B_IN_DISTRESS		= -1,  /**< Node is in an error state. */
		B_UNREGISTERED,            /**< Node has not been registered yet. */
		B_STOPPED,                 /**< Node is stopped. */
		B_STARTED,                 /**< Node is running. */
		B_QUITTING,                /**< Node is in the process of quitting. */
		B_TERMINATED,              /**< Node has terminated. */
		B_USER_RUN_STATES	= 0x4000 /**< First value available for subclass use. */
	};

protected:
	/** @brief Constructs the looper.
	 *  @param apiVersion The API version to use; defaults to B_BEOS_VERSION.
	 */
	explicit					BMediaEventLooper(
									uint32 apiVersion = B_BEOS_VERSION);
	virtual						~BMediaEventLooper();

protected:
	// BMediaNode interface

	/** @brief Called after the node is registered; must call Run() here. */
	virtual	void				NodeRegistered();

	/** @brief Pushes a B_START event onto the event queue.
	 *  @param performanceTime The performance time at which to start.
	 */
	virtual	void				Start(bigtime_t performanceTime);

	/** @brief Pushes a B_STOP event onto the event queue.
	 *  @param performanceTime The performance time at which to stop.
	 *  @param immediate If true, stop without waiting for the queue.
	 */
	virtual	void				Stop(bigtime_t performanceTime,
									bool immediate);

	/** @brief Pushes a B_SEEK event onto the event queue.
	 *  @param mediaTime The media time to seek to.
	 *  @param performanceTime The performance time at which to apply the seek.
	 */
	virtual	void				Seek(bigtime_t mediaTime,
									bigtime_t performanceTime);

	/** @brief Pushes a B_WARP event onto the real-time event queue.
	 *  @param atRealTime The real time at which the warp takes effect.
	 *  @param toPerformanceTime The new performance time to warp to.
	 */
	virtual	void				TimeWarp(bigtime_t atRealTime,
									bigtime_t toPerformanceTime);

	/** @brief Schedules a timer event.
	 *  @param atPerformanceTime The performance time at which to fire.
	 *  @param cookie An arbitrary value passed back in HandleEvent().
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			AddTimer(bigtime_t atPerformanceTime,
									int32 cookie);

	/** @brief Notifies the looper of a change in run mode.
	 *  @param mode The new run_mode value.
	 */
	virtual	void 				SetRunMode(run_mode mode);

protected:
	// BMediaEventLooper Hook functions

	/** @brief Called when it is time to handle a scheduled event.
	 *  @param event The event to handle.
	 *  @param lateness How late the event is being handled, in microseconds.
	 *  @param realTimeEvent True if the event came from the real-time queue.
	 */
	virtual void				HandleEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false) = 0;

	/** @brief Override to clean up resources associated with a custom event.
	 *  @param event The event being removed from the queue.
	 */
	virtual void				CleanUpEvent(const media_timed_event* event);

	/** @brief Returns the current time for offline-mode nodes.
	 *  @return Current performance time in microseconds.
	 */
	virtual	bigtime_t			OfflineTime();

	/** @brief Override only if you need a custom control loop implementation. */
	virtual	void				ControlLoop();

	/** @brief Returns the thread ID of the control thread.
	 *  @return The thread_id, or B_BAD_THREAD_ID if not yet started.
	 */
			thread_id			ControlThread();

protected:
	/** @brief Returns the timed event queue for performance-time events.
	 *  @return Pointer to the BTimedEventQueue.
	 */
			BTimedEventQueue* 	EventQueue();

	/** @brief Returns the timed event queue for real-time events.
	 *  @return Pointer to the real-time BTimedEventQueue.
	 */
			BTimedEventQueue*	RealTimeQueue();

	/** @brief Returns the current thread priority of the control thread.
	 *  @return The priority value.
	 */
			int32				Priority() const;

	/** @brief Returns the current run state.
	 *  @return One of the run_state enum values.
	 */
			int32				RunState() const;

	/** @brief Returns the reported event latency.
	 *  @return Latency in microseconds.
	 */
			bigtime_t			EventLatency() const;

	/** @brief Returns the buffer duration used for scheduling.
	 *  @return Duration in microseconds.
	 */
			bigtime_t			BufferDuration() const;

	/** @brief Returns the estimated scheduling latency.
	 *  @return Scheduling latency in microseconds.
	 */
			bigtime_t			SchedulingLatency() const;

	/** @brief Sets the priority of the control thread.
	 *  @param priority Thread priority (clamped to 5-120).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetPriority(int32 priority);

	/** @brief Sets the current run state.
	 *  @param state The new run_state value.
	 */
			void				SetRunState(run_state state);

	/** @brief Sets the event latency reported to the Media Kit.
	 *  @param latency Latency in microseconds.
	 */
			void				SetEventLatency(bigtime_t latency);

	/** @brief Sets the buffer duration used for scheduling decisions.
	 *  @param duration Buffer duration in microseconds.
	 */
			void				SetBufferDuration(bigtime_t duration);

	/** @brief Sets the current offline-mode time reference.
	 *  @param offTime The current offline performance time.
	 */
			void				SetOfflineTime(bigtime_t offTime);

	/** @brief Spawns and starts the control thread; call from NodeRegistered(). */
			void				Run();

	/** @brief Stops the control thread; call from your destructor. */
			void				Quit();

	/** @brief Calls HandleEvent() and performs internal event-queue maintenance.
	 *  @param event The event to dispatch.
	 *  @param lateness How late the dispatch is, in microseconds.
	 *  @param realTimeEvent True if the event came from the real-time queue.
	 */
			void				DispatchEvent(const media_timed_event* event,
									bigtime_t lateness,
									bool realTimeEvent = false);

private:
	static	int32				_ControlThreadStart(void* cookie);
	static	void				_CleanUpEntry(const media_timed_event* event,
									void* context);
			void				_DispatchCleanUp(
									const media_timed_event* event);

private:
			BTimedEventQueue	fEventQueue;
			BTimedEventQueue	fRealTimeQueue;
			thread_id			fControlThread;
			int32				fCurrentPriority;
			int32				fSetPriority;
			vint32				fRunState;
			bigtime_t			fEventLatency;
			bigtime_t			fSchedulingLatency;
			bigtime_t			fBufferDuration;
			bigtime_t			fOfflineTime;
			uint32				fApiVersion;

protected:
	virtual	status_t 	DeleteHook(BMediaNode * node);

private:
	// FBC padding and forbidden methods
								BMediaEventLooper(const BMediaEventLooper&);
			BMediaEventLooper&	operator=(const BMediaEventLooper&);

	virtual	status_t		 	_Reserved_BMediaEventLooper_0(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_1(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_2(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_3(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_4(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_5(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_6(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_7(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_8(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_9(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_10(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_11(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_12(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_13(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_14(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_15(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_16(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_17(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_18(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_19(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_20(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_21(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_22(int32 arg, ...);
	virtual	status_t 			_Reserved_BMediaEventLooper_23(int32 arg, ...);

	bool						_reserved_bool_[4];
	uint32						_reserved_BMediaEventLooper_[12];
};

#endif // _MEDIA_EVENT_LOOPER_H
