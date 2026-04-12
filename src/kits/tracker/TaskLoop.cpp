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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file TaskLoop.cpp
 * @brief Deferred and periodic task scheduling for the Tracker.
 *
 * Implements DelayedTask, OneShotDelayedTask, PeriodicDelayedTask,
 * PeriodicDelayedTaskWithTimeout, and RunWhenIdleTask along with three
 * TaskLoop concrete classes: StandAloneTaskLoop (owns its own thread),
 * PiggybackTaskLoop (driven by an existing pulse), and
 * AccumulatedOneShotDelayedTask (coalesces repeated calls).
 *
 * @see TaskLoop, StandAloneTaskLoop, PiggybackTaskLoop
 */


#include <Debug.h>
#include <InterfaceDefs.h>

#include "AutoLock.h"
#include "TaskLoop.h"


const float kIdleTreshold = 0.15f;

const bigtime_t kInfinity = B_INFINITE_TIMEOUT;


/**
 * @brief Return the average cumulative CPU active time across all processors.
 *
 * Used by RunWhenIdleTask to decide whether the system is idle enough to
 * run deferred work.
 *
 * @return Average per-CPU active time in microseconds.
 */
static bigtime_t
ActivityLevel()
{
	// stolen from roster server
	bigtime_t time = 0;
	system_info	sinfo;
	get_system_info(&sinfo);

	cpu_info* cpuInfos = new cpu_info[sinfo.cpu_count];
	get_cpu_info(0, sinfo.cpu_count, cpuInfos);

	for (uint32 index = 0; index < sinfo.cpu_count; index++)
		time += cpuInfos[index].active_time;

	delete[] cpuInfos;
	return time / ((bigtime_t) sinfo.cpu_count);
}


class AccumulatedOneShotDelayedTask : public OneShotDelayedTask {
	// supports accumulating functors
public:
	AccumulatedOneShotDelayedTask(AccumulatingFunctionObject* functor,
		bigtime_t delay, bigtime_t maxAccumulatingTime = 0,
		int32 maxAccumulateCount = 0)
		:
		OneShotDelayedTask(functor, delay),
		maxAccumulateCount(maxAccumulateCount),
		accumulateCount(1),
		maxAccumulatingTime(maxAccumulatingTime),
		initialTime(system_time())
	{
	}

	bool CanAccumulate(const AccumulatingFunctionObject* accumulateThis) const
	{
		if (maxAccumulateCount && accumulateCount > maxAccumulateCount)
			// don't accumulate if too may accumulated already
			return false;

		if (maxAccumulatingTime && system_time() > initialTime
				+ maxAccumulatingTime) {
			// don't accumulate if too late past initial task
			return false;
		}

		return static_cast<AccumulatingFunctionObject*>(fFunctor)->
			CanAccumulate(accumulateThis);
	}

	virtual void Accumulate(AccumulatingFunctionObject* accumulateThis,
		bigtime_t delay)
	{
		fRunAfter = system_time() + delay;
			// reset fRunAfter
		accumulateCount++;
		static_cast<AccumulatingFunctionObject*>(fFunctor)->
			Accumulate(accumulateThis);
	}

private:
	int32 maxAccumulateCount;
	int32 accumulateCount;
	bigtime_t maxAccumulatingTime;
	bigtime_t initialTime;
};


//	#pragma mark - DelayedTask


/**
 * @brief Construct a DelayedTask scheduled to run \a delay microseconds from now.
 *
 * @param delay  How long to wait (in microseconds) before running.
 */
DelayedTask::DelayedTask(bigtime_t delay)
	:
	fRunAfter(system_time() + delay)
{
}


/**
 * @brief Destructor.
 */
DelayedTask::~DelayedTask()
{
}


//	#pragma mark - OneShotDelayedTask


/**
 * @brief Construct a one-shot task that invokes \a functor after \a delay.
 *
 * @param functor  Function object to invoke; ownership is transferred.
 * @param delay    Delay in microseconds before the first invocation.
 */
OneShotDelayedTask::OneShotDelayedTask(FunctionObject* functor,
	bigtime_t delay)
	:
	DelayedTask(delay),
	fFunctor(functor)
{
}


/**
 * @brief Destructor; deletes the owned functor.
 */
OneShotDelayedTask::~OneShotDelayedTask()
{
	delete fFunctor;
}


/**
 * @brief Invoke the functor if the scheduled time has arrived.
 *
 * @param currentTime  The caller's current system_time() snapshot.
 * @return true (done) if the functor was invoked; false if still waiting.
 */
bool
OneShotDelayedTask::RunIfNeeded(bigtime_t currentTime)
{
	if (currentTime < fRunAfter)
		return false;

	(*fFunctor)();
	return true;
}


//	#pragma mark - PeriodicDelayedTask


/**
 * @brief Construct a periodic task that invokes \a functor every \a period microseconds.
 *
 * @param functor        Functor to invoke; returns false when the task should stop.
 * @param initialDelay   Delay before the first invocation.
 * @param period         Interval between subsequent invocations.
 */
PeriodicDelayedTask::PeriodicDelayedTask(
	FunctionObjectWithResult<bool>* functor, bigtime_t initialDelay,
	bigtime_t period)
	:
	DelayedTask(initialDelay),
	fPeriod(period),
	fFunctor(functor)
{
}


/**
 * @brief Destructor; deletes the owned functor.
 */
PeriodicDelayedTask::~PeriodicDelayedTask()
{
	delete fFunctor;
}


/**
 * @brief Invoke the functor if the next period has elapsed.
 *
 * Reschedules the next run time and invokes the functor.  Returns the
 * functor's result: true means keep running, false means remove the task.
 *
 * @param currentTime  The caller's current system_time() snapshot.
 * @return true while the functor requests continued execution; false when done.
 */
bool
PeriodicDelayedTask::RunIfNeeded(bigtime_t currentTime)
{
	if (currentTime < fRunAfter)
		return false;

	fRunAfter = currentTime + fPeriod;
	(*fFunctor)();
	return fFunctor->Result();
}


/**
 * @brief Construct a periodic task that stops automatically after \a timeout microseconds.
 *
 * @param functor        Functor returning false when it wants to stop early.
 * @param initialDelay   Delay before the first invocation.
 * @param period         Interval between subsequent invocations.
 * @param timeout        Absolute time limit from construction after which the task ends.
 */
PeriodicDelayedTaskWithTimeout::PeriodicDelayedTaskWithTimeout(
	FunctionObjectWithResult<bool>* functor, bigtime_t initialDelay,
	bigtime_t period, bigtime_t timeout)
	:
	PeriodicDelayedTask(functor, initialDelay, period),
	fTimeoutAfter(system_time() + timeout)
{
}


/**
 * @brief Run the functor if the period has elapsed, stopping at the timeout.
 *
 * @param currentTime  The caller's current system_time() snapshot.
 * @return true if the functor requested another call or the timeout has not expired.
 */
bool
PeriodicDelayedTaskWithTimeout::RunIfNeeded(bigtime_t currentTime)
{
	if (currentTime < fRunAfter)
		return false;

	fRunAfter = currentTime + fPeriod;
	(*fFunctor)();
	if (fFunctor->Result())
		return true;

	// if call didn't terminate the task yet, check if timeout is due
	return currentTime > fTimeoutAfter;
}


//	#pragma mark - RunWhenIdleTask


/**
 * @brief Construct a task that defers execution until the system is idle.
 *
 * @param functor        Functor to call once the system has been idle long enough.
 * @param initialDelay   Delay before the idle check begins.
 * @param idleFor        Minimum continuous idle time required before invocation.
 * @param heartBeat      Polling interval for the idle check.
 */
RunWhenIdleTask::RunWhenIdleTask(FunctionObjectWithResult<bool>* functor,
	bigtime_t initialDelay, bigtime_t idleFor, bigtime_t heartBeat)
	:
	PeriodicDelayedTask(functor, initialDelay, heartBeat),
	fIdleFor(idleFor),
	fState(kInitialDelay),
	fActivityLevelStart(0),
	fActivityLevel(0),
	fLastCPUTooBusyTime(0)
{
}


/**
 * @brief Destructor.
 */
RunWhenIdleTask::~RunWhenIdleTask()
{
}


/**
 * @brief Check idle conditions and invoke the functor when ready.
 *
 * Implements a three-phase state machine: initial delay, waiting for idle,
 * and executing while idle.  Resets back to idle-wait whenever the system
 * becomes busy again.
 *
 * @param currentTime  The caller's current system_time() snapshot.
 * @return The functor's result when executed, false while waiting.
 */
bool
RunWhenIdleTask::RunIfNeeded(bigtime_t currentTime)
{
	if (currentTime < fRunAfter)
		return false;

	fRunAfter = currentTime + fPeriod;
//	PRINT(("runWhenIdle: runAfter %lld, current time %lld, period %lld\n",
//		fRunAfter, currentTime, fPeriod));

	if (fState == kInitialDelay) {
//		PRINT(("run when idle task - past intial delay\n"));
		ResetIdleTimer(currentTime);
	} else if (fState == kInIdleState && !StillIdle(currentTime)) {
		fState = kInitialIdleWait;
		ResetIdleTimer(currentTime);
	} else if (fState != kInitialIdleWait || IdleTimerExpired(currentTime)) {
		fState = kInIdleState;
		(*fFunctor)();
		return fFunctor->Result();
	}

	return false;
}


/**
 * @brief Snapshot the current CPU activity level to begin an idle measurement.
 *
 * @param currentTime  The current system time used as the measurement baseline.
 */
void
RunWhenIdleTask::ResetIdleTimer(bigtime_t currentTime)
{
	fActivityLevel = ActivityLevel();
	fActivityLevelStart = currentTime;
	fLastCPUTooBusyTime = currentTime;
	fState = kInitialIdleWait;
}


/**
 * @brief Return true if the system has been idle long enough to run the task.
 *
 * Computes the CPU load since the last call and compares it against
 * kIdleTreshold.  Also checks that no CPU activity spike occurred within
 * the required fIdleFor window.
 *
 * @param currentTime   The current system time.
 * @param taskOverhead  CPU load fraction to subtract (accounts for the task itself).
 * @return true if the system is currently idle by the required margin.
 */
bool
RunWhenIdleTask::IsIdle(bigtime_t currentTime, float taskOverhead)
{
	bigtime_t currentActivityLevel = ActivityLevel();
	float load = (float)(currentActivityLevel - fActivityLevel)
		/ (float)(currentTime - fActivityLevelStart);

	fActivityLevel = currentActivityLevel;
	fActivityLevelStart = currentTime;

	load -= taskOverhead;

	bool idle = true;

	if (load > kIdleTreshold) {
//		PRINT(("not idle enough %f\n", load));
		idle = false;
	} else if ((currentTime - fLastCPUTooBusyTime) < fIdleFor
		|| idle_time() < fIdleFor) {
//		PRINT(("load %f, not idle long enough %lld, %lld\n", load,
//			currentTime - fLastCPUTooBusyTime,
//			idle_time()));
		idle = false;
	}

#if xDEBUG
	else
		PRINT(("load %f, idle for %lld sec, go\n", load,
			(currentTime - fLastCPUTooBusyTime) / 1000000));
#endif

	return idle;
}


/**
 * @brief Return true if the system has been idle long enough to start the task.
 *
 * @param currentTime  The current system time.
 * @return true if the idle condition is met with zero overhead correction.
 */
bool
RunWhenIdleTask::IdleTimerExpired(bigtime_t currentTime)
{
	return IsIdle(currentTime, 0);
}


/**
 * @brief Return true if the system remains idle while the task is running.
 *
 * Uses kIdleTreshold as the overhead correction to account for the task
 * itself.
 *
 * @param currentTime  The current system time.
 * @return true if the system is still idle (with task overhead subtracted).
 */
bool
RunWhenIdleTask::StillIdle(bigtime_t currentTime)
{
	return IsIdle(currentTime, kIdleTreshold);
}


//	#pragma mark - TaskLoop


/**
 * @brief Construct a TaskLoop with the given heartbeat interval.
 *
 * @param heartBeat  Polling interval in microseconds between Pulse() calls.
 */
TaskLoop::TaskLoop(bigtime_t heartBeat)
	:
	fTaskList(10),
	fHeartBeat(heartBeat)
{
}


/**
 * @brief Destructor.
 */
TaskLoop::~TaskLoop()
{
}


/**
 * @brief Schedule a pre-constructed DelayedTask for later execution.
 *
 * @param task  Ownership of the task is transferred to the loop.
 */
void
TaskLoop::RunLater(DelayedTask* task)
{
	AddTask(task);
}


/**
 * @brief Schedule a one-shot function object to run after \a delay.
 *
 * @param functor  Function object to invoke; ownership is transferred.
 * @param delay    Delay in microseconds before invocation.
 */
void
TaskLoop::RunLater(FunctionObject* functor, bigtime_t delay)
{
	RunLater(new OneShotDelayedTask(functor, delay));
}


/**
 * @brief Schedule a periodic function object.
 *
 * @param functor  Functor returning false when done; ownership is transferred.
 * @param delay    Initial delay before first invocation.
 * @param period   Interval between subsequent invocations.
 */
void
TaskLoop::RunLater(FunctionObjectWithResult<bool>* functor,
	bigtime_t delay, bigtime_t period)
{
	RunLater(new PeriodicDelayedTask(functor, delay, period));
}


/**
 * @brief Schedule a periodic function object with an absolute timeout.
 *
 * @param functor   Functor returning false when done; ownership is transferred.
 * @param delay     Initial delay before first invocation.
 * @param period    Interval between subsequent invocations.
 * @param timeout   Maximum total lifetime of the task in microseconds.
 */
void
TaskLoop::RunLater(FunctionObjectWithResult<bool>* functor, bigtime_t delay,
	bigtime_t period, bigtime_t timeout)
{
	RunLater(new PeriodicDelayedTaskWithTimeout(functor, delay, period,
		timeout));
}


/**
 * @brief Schedule a function object to run once the system is idle.
 *
 * @param functor        Functor to invoke when idle; ownership is transferred.
 * @param initialDelay   Delay before idle checking begins.
 * @param idleTime       Required continuous idle duration.
 * @param heartBeat      Idle-check polling interval.
 */
void
TaskLoop::RunWhenIdle(FunctionObjectWithResult<bool>* functor,
	bigtime_t initialDelay, bigtime_t idleTime, bigtime_t heartBeat)
{
	RunLater(new RunWhenIdleTask(functor, initialDelay, idleTime, heartBeat));
}


//	#pragma mark - TaskLoop


/**
 * @brief Schedule or coalesce an accumulating one-shot task.
 *
 * If an existing AccumulatedOneShotDelayedTask in the queue can absorb
 * \a functor, the delay is reset and the functor is merged.  Otherwise a
 * new task is created.
 *
 * @param functor              Accumulating functor; ownership is transferred.
 * @param delay                Delay before invocation (reset on accumulation).
 * @param maxAccumulatingTime  Stop accumulating after this many microseconds.
 * @param maxAccumulateCount   Stop accumulating after this many merges.
 */
void
TaskLoop::AccumulatedRunLater(AccumulatingFunctionObject* functor,
	bigtime_t delay, bigtime_t maxAccumulatingTime, int32 maxAccumulateCount)
{
	AutoLock<BLocker> autoLock(&fLock);
	if (!autoLock.IsLocked())
		return;

	int32 count = fTaskList.CountItems();
	for (int32 index = 0; index < count; index++) {
		AccumulatedOneShotDelayedTask* task
			= dynamic_cast<AccumulatedOneShotDelayedTask*>(
				fTaskList.ItemAt(index));
		if (task == NULL)
			continue;
		else if (task->CanAccumulate(functor)) {
			task->Accumulate(functor, delay);
			return;
		}
	}

	RunLater(new AccumulatedOneShotDelayedTask(functor, delay,
		maxAccumulatingTime, maxAccumulateCount));
}


/**
 * @brief Run all tasks that are due and remove those that are finished.
 *
 * Must be called with fLock held.
 *
 * @return true if the task list is empty and KeepPulsingWhenEmpty() is false.
 */
bool
TaskLoop::Pulse()
{
	ASSERT(fLock.IsLocked());

	int32 count = fTaskList.CountItems();
	if (count > 0) {
		bigtime_t currentTime = system_time();
		for (int32 index = 0; index < count; ) {
			DelayedTask* task = fTaskList.ItemAt(index);
			// give every task a try
			if (task->RunIfNeeded(currentTime)) {
				// if done, remove from list
				RemoveTask(task);
				count--;
			} else
				index++;
		}
	}
	return count == 0 && !KeepPulsingWhenEmpty();
}


/**
 * @brief Return the soonest scheduled run time across all pending tasks.
 *
 * Must be called with fLock held.
 *
 * @return The earliest fRunAfter value, or kInfinity if no tasks are pending.
 */
bigtime_t
TaskLoop::LatestRunTime() const
{
	ASSERT(fLock.IsLocked());
	bigtime_t result = kInfinity;

#if xDEBUG
	DelayedTask* nextTask = 0;
#endif
	int32 count = fTaskList.CountItems();
	for (int32 index = 0; index < count; index++) {
		bigtime_t runAfter = fTaskList.ItemAt(index)->RunAfterTime();
		if (runAfter < result) {
			result = runAfter;

#if xDEBUG
			nextTask = fTaskList.ItemAt(index);
#endif
		}
	}


#if xDEBUG
	if (nextTask)
		PRINT(("latestRunTime : next task %s\n", typeid(*nextTask).name));
	else
		PRINT(("latestRunTime : no next task\n"));
#endif

	return result;
}


/**
 * @brief Remove a completed task from the list and delete it.
 *
 * Must be called with fLock held.
 *
 * @param task  The task to remove; it will be deleted.
 */
void
TaskLoop::RemoveTask(DelayedTask* task)
{
	ASSERT(fLock.IsLocked());
	// remove the task
	fTaskList.RemoveItem(task);
}

/**
 * @brief Add a task to the loop, acquiring the lock and starting a pulse if needed.
 *
 * @param task  The task to add; ownership is transferred.
 */
void
TaskLoop::AddTask(DelayedTask* task)
{
	AutoLock<BLocker> autoLock(&fLock);
	if (!autoLock.IsLocked()) {
		delete task;
		return;
	}

	fTaskList.AddItem(task);
	StartPulsingIfNeeded();
}


//	#pragma mark - StandAloneTaskLoop


/**
 * @brief Construct a StandAloneTaskLoop that manages its own background thread.
 *
 * @param keepThread  If true, the thread keeps running even when all tasks finish.
 * @param heartBeat   Polling interval in microseconds.
 */
StandAloneTaskLoop::StandAloneTaskLoop(bool keepThread, bigtime_t heartBeat)
	:
	TaskLoop(heartBeat),
	fNeedToQuit(false),
	fScanThread(-1),
	fKeepThread(keepThread)
{
}


/**
 * @brief Destructor; signals the thread to quit and waits up to 10 seconds for it.
 */
StandAloneTaskLoop::~StandAloneTaskLoop()
{
	fLock.Lock();
	fNeedToQuit = true;
	bool easyOut = (fScanThread == -1);
	fLock.Unlock();

	if (!easyOut)
		for (int32 timeout = 10000; ; timeout--) {
			// use a 10 sec timeout value in case the spawned
			// thread is stuck somewhere

			if (!timeout) {
				PRINT(("StandAloneTaskLoop timed out, quitting abruptly"));
				break;
			}

			bool done;

			fLock.Lock();
			done = (fScanThread == -1);
			fLock.Unlock();
			if (done)
				break;

			snooze(1000);
		}
}


/**
 * @brief Spawn the background scan thread if it is not already running.
 *
 * Must be called with fLock held.
 */
void
StandAloneTaskLoop::StartPulsingIfNeeded()
{
	ASSERT(fLock.IsLocked());
	if (fScanThread < 0) {
		// no loop thread yet, spawn one
		fScanThread = spawn_thread(StandAloneTaskLoop::RunBinder,
			"TrackerTaskLoop", B_LOW_PRIORITY, this);
		resume_thread(fScanThread);
	}
}


/**
 * @brief Return whether the thread should stay alive even when no tasks are queued.
 *
 * @return The value of fKeepThread.
 */
bool
StandAloneTaskLoop::KeepPulsingWhenEmpty() const
{
	return fKeepThread;
}


/**
 * @brief Static thread entry point; delegates to Run().
 *
 * @param castToThis  Pointer to the owning StandAloneTaskLoop.
 * @return B_OK always.
 */
status_t
StandAloneTaskLoop::RunBinder(void* castToThis)
{
	StandAloneTaskLoop* self = (StandAloneTaskLoop*)castToThis;
	self->Run();
	return B_OK;
}


/**
 * @brief Main loop body executed by the background thread.
 *
 * Calls Pulse() on every heartbeat interval, sleeping between iterations to
 * avoid busy-waiting.  Returns when all tasks complete (or fKeepThread is
 * false) or when fNeedToQuit is set.
 */
void
StandAloneTaskLoop::Run()
{
	for(;;) {
		AutoLock<BLocker> autoLock(&fLock);
		if (!autoLock)
			return;

		if (fNeedToQuit) {
			// task loop being deleted, let go of the thread allowing the
			// to go through deletion
			fScanThread = -1;
			return;
		}

		if (Pulse()) {
			fScanThread = -1;
			return;
		}

		// figure out when to run next by checking out when the different
		// tasks wan't to be woken up, snooze until a little bit before that
		// time
		bigtime_t now = system_time();
		bigtime_t latestRunTime = LatestRunTime() - 1000;
		bigtime_t afterHeartBeatTime = now + fHeartBeat;
		bigtime_t snoozeTill = latestRunTime < afterHeartBeatTime ?
			latestRunTime : afterHeartBeatTime;

		autoLock.Unlock();

		if (snoozeTill > now)
			snooze_until(snoozeTill, B_SYSTEM_TIMEBASE);
		else
			snooze(1000);
	}
}


/**
 * @brief Add a task and wake the background thread if it is sleeping.
 *
 * @param delayedTask  Task to add; ownership is transferred.
 */
void
StandAloneTaskLoop::AddTask(DelayedTask* delayedTask)
{
	_inherited::AddTask(delayedTask);
	if (fScanThread < 0)
		return;

	// wake up the loop thread if it is asleep
	thread_info info;
	get_thread_info(fScanThread, &info);
	if (info.state == B_THREAD_ASLEEP) {
		suspend_thread(fScanThread);
		snooze(1000);	// snooze because BeBook sez so
		resume_thread(fScanThread);
	}
}


//	#pragma mark - PiggybackTaskLoop


/**
 * @brief Construct a PiggybackTaskLoop driven by an external BView pulse.
 *
 * @param heartBeat  Minimum interval between successive Pulse() calls.
 */
PiggybackTaskLoop::PiggybackTaskLoop(bigtime_t heartBeat)
	:
	TaskLoop(heartBeat),
	fNextHeartBeatTime(0),
	fPulseMe(false)
{
}


/**
 * @brief Destructor.
 */
PiggybackTaskLoop::~PiggybackTaskLoop()
{
}


/**
 * @brief Called from a BView::Pulse() override to drive the task loop.
 *
 * Invokes Pulse() at most once per fHeartBeat interval; rate-limits by
 * comparing system_time() against fNextHeartBeatTime.
 */
void
PiggybackTaskLoop::PulseMe()
{
	if (!fPulseMe)
		return;

	bigtime_t time = system_time();
	if (fNextHeartBeatTime < time) {
		AutoLock<BLocker> autoLock(&fLock);
		if (Pulse())
			fPulseMe = false;
		fNextHeartBeatTime = time + fHeartBeat;
	}
}


/**
 * @brief PiggybackTaskLoop never keeps its pulse running when the task list is empty.
 *
 * @return false always.
 */
bool
PiggybackTaskLoop::KeepPulsingWhenEmpty() const
{
	return false;
}


/**
 * @brief Enable PulseMe() calls by setting the internal flag.
 *
 * Called by the base class AddTask() when the first task is enqueued.
 */
void
PiggybackTaskLoop::StartPulsingIfNeeded()
{
	fPulseMe = true;
}
