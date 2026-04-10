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
 *   Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 *   Copyright 2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file UserTimer.cpp
 * @brief POSIX timer implementation for user-space processes (timer_create/settime/gettime).
 *
 * Implements POSIX per-process and per-thread timers. Timers fire via signal
 * delivery or thread event. Supports CLOCK_REALTIME, CLOCK_MONOTONIC,
 * CLOCK_PROCESS_CPUTIME_ID, and CLOCK_THREAD_CPUTIME_ID. Periodic timers
 * rearm themselves automatically.
 *
 * @see UserEvent.cpp, signal.cpp, thread.cpp
 */


#include <UserTimer.h>

#include <algorithm>

#include <AutoDeleter.h>

#include <debug.h>
#include <kernel.h>
#include <real_time_clock.h>
#include <syscall_clock_info.h>
#include <team.h>
#include <thread_types.h>
#include <UserEvent.h>
#include <util/AutoLock.h>


#define CPUCLOCK_TEAM		0x00000000
#define CPUCLOCK_THREAD		0x80000000
#define CPUCLOCK_SPECIAL	0xc0000000
#define CPUCLOCK_ID_MASK	(~(CPUCLOCK_SPECIAL))


// Minimum interval length in microseconds for a periodic timer. This is not a
// restriction on the user timer interval length itself, but the minimum time
// span by which we advance the start time for kernel timers. A shorted user
// timer interval will result in the overrun count to be increased every time
// the kernel timer is rescheduled.
static const bigtime_t kMinPeriodicTimerInterval = 100;

static RealTimeUserTimerList sAbsoluteRealTimeTimers;
static spinlock sAbsoluteRealTimeTimersLock = B_SPINLOCK_INITIALIZER;

static seqlock sUserTimerLock = B_SEQLOCK_INITIALIZER;


// #pragma mark - TimerLocker


namespace {

/**
 * @brief RAII helper that locks the team and optionally a thread for timer operations.
 *
 * Acquires the team lock unconditionally, and optionally acquires the thread lock
 * when a non-NULL thread is provided. The destructor releases both locks in
 * reverse order.
 */
struct TimerLocker {
	Team*	team;
	Thread*	thread;

	/**
	 * @brief Constructs a TimerLocker with both pointers initialised to NULL.
	 */
	TimerLocker()
		:
		team(NULL),
		thread(NULL)
	{
	}

	/**
	 * @brief Destructor; releases all held locks via Unlock().
	 */
	~TimerLocker()
	{
		Unlock();
	}

	/**
	 * @brief Locks the given team and, if non-NULL, the given thread.
	 *
	 * @param team   The team to lock; must not be NULL.
	 * @param thread The thread to lock and reference, or NULL if not needed.
	 */
	void Lock(Team* team, Thread* thread)
	{
		this->team = team;
		team->Lock();

		this->thread = thread;

		if (thread != NULL) {
			thread->AcquireReference();
			thread->Lock();
		}

		// We don't check thread->team != team here, since this method can be
		// called for new threads not added to the team yet.
	}

	/**
	 * @brief Locks the current team (and optionally a thread) and retrieves a timer by ID.
	 *
	 * Locks the current thread's team. If @p threadID is non-negative also locks that
	 * thread and verifies it belongs to the current team. Then looks up the timer with
	 * @p timerID in the appropriate container.
	 *
	 * @param threadID  Thread ID to look up, or negative to look in the team.
	 * @param timerID   ID of the timer to retrieve.
	 * @param _timer    Output: set to the found UserTimer on success.
	 * @retval B_OK            Timer found; @p _timer is valid.
	 * @retval B_BAD_THREAD_ID @p threadID was non-negative but the thread does not exist.
	 * @retval B_NOT_ALLOWED   The thread does not belong to the current team.
	 * @retval B_BAD_VALUE     No timer with @p timerID was found.
	 */
	status_t LockAndGetTimer(thread_id threadID, int32 timerID,
		UserTimer*& _timer)
	{
		team = thread_get_current_thread()->team;
		team->Lock();

		if (threadID >= 0) {
			thread = Thread::GetAndLock(threadID);
			if (thread == NULL)
				return B_BAD_THREAD_ID;
			if (thread->team != team)
				return B_NOT_ALLOWED;
		}

		UserTimer* timer = thread != NULL
			? thread->UserTimerFor(timerID) : team->UserTimerFor(timerID);
		if (timer == NULL)
			return B_BAD_VALUE;

		_timer = timer;
		return B_OK;
	}

	/**
	 * @brief Releases all locks held by this locker and resets the stored pointers.
	 */
	void Unlock()
	{
		if (thread != NULL) {
			thread->UnlockAndReleaseReference();
			thread = NULL;
		}
		if (team != NULL) {
			team->Unlock();
			team = NULL;
		}
	}
};

}	// unnamed namespace


// #pragma mark - UserTimer


/**
 * @brief Constructs a UserTimer in an inactive, unscheduled state.
 *
 * Initialises all fields to zero/null/false and stores @c this in the
 * embedded kernel timer's @c user_data field so the static callback can
 * recover the object pointer.
 */
UserTimer::UserTimer()
	:
	fID(-1),
	fEvent(NULL),
	fNextTime(0),
	fInterval(0),
	fOverrunCount(0),
	fScheduled(false),
	fSkip(0)
{
	// mark the timer unused
	fTimer.user_data = this;
}


/**
 * @brief Destroys the UserTimer and releases the associated UserEvent reference.
 *
 * @note The timer must have been cancelled before destruction; cancellation is
 *       the caller's responsibility.
 */
UserTimer::~UserTimer()
{
	if (fEvent != NULL)
		fEvent->ReleaseReference();
}


/*!	\fn UserTimer::Schedule(bigtime_t nextTime, bigtime_t interval,
		bigtime_t& _oldRemainingTime, bigtime_t& _oldInterval)
	Cancels the timer, if it is already scheduled, and optionally schedules it
	with new parameters.

	\param nextTime The time at which the timer should go off the next time. If
		\c B_INFINITE_TIMEOUT, the timer will not be scheduled. Whether the
		value is interpreted as absolute or relative time, depends on \c flags.
	\param interval If <tt> >0 </tt>, the timer will be scheduled to fire
		periodically every \a interval microseconds. Otherwise it will fire
		only once at \a nextTime. If \a nextTime is \c B_INFINITE_TIMEOUT, it
		will fire never in either case.
	\param flags Bitwise OR of flags. Currently \c B_ABSOLUTE_TIMEOUT and
		\c B_RELATIVE_TIMEOUT are supported, indicating whether \a nextTime is
		an absolute or relative time.
	\param _oldRemainingTime Return variable that will be set to the
		microseconds remaining to the time for which the timer was scheduled
		next before the call. If it wasn't scheduled, the variable is set to
		\c B_INFINITE_TIMEOUT.
	\param _oldInterval Return variable that will be set to the interval in
		microseconds the timer was to be scheduled periodically. If the timer
		wasn't periodic, the variable is set to \c 0.
*/


/**
 * @brief Cancels the timer if it is currently scheduled.
 *
 * Convenience wrapper around Schedule() that passes B_INFINITE_TIMEOUT and
 * a zero interval, effectively disarming the timer.
 */
void
UserTimer::Cancel()
{
	bigtime_t oldNextTime;
	bigtime_t oldInterval;
	return Schedule(B_INFINITE_TIMEOUT, 0, 0, oldNextTime, oldInterval);
}


/*!	\fn UserTimer::GetInfo(bigtime_t& _remainingTime, bigtime_t& _interval,
		uint32& _overrunCount)
	Return information on the current timer.

	\param _remainingTime Return variable that will be set to the microseconds
		remaining to the time for which the timer was scheduled next before the
		call. If it wasn't scheduled, the variable is set to
		\c B_INFINITE_TIMEOUT.
	\param _interval Return variable that will be set to the interval in
		microseconds the timer is to be scheduled periodically. If the timer
		isn't periodic, the variable is set to \c 0.
	\param _overrunCount Return variable that will be set to the number of times
		the timer went off, but its event couldn't be delivered, since it's
		previous delivery hasn't been handled yet.
*/


/**
 * @brief Static kernel-timer callback; invoked by the timer subsystem at interrupt level.
 *
 * Recovers the UserTimer from the kernel @c timer struct's @c user_data field,
 * acquires the sequential write lock @c sUserTimerLock (spinning while @c fSkip
 * is non-zero), and calls HandleTimer() under that lock.
 *
 * @param timer Pointer to the kernel timer structure that fired.
 * @retval B_HANDLED_INTERRUPT Always; this is an interrupt-level callback.
 *
 * @note Called with interrupts disabled.
 */
/*static*/ int32
UserTimer::HandleTimerHook(struct timer* timer)
{
	UserTimer* userTimer = reinterpret_cast<UserTimer*>(timer->user_data);

	InterruptsLocker _;

	bool locked = false;
	while (!locked && atomic_get(&userTimer->fSkip) == 0) {
		locked = try_acquire_write_seqlock(&sUserTimerLock);
		if (!locked)
			cpu_pause();
	}

	if (locked) {
		userTimer->HandleTimer();
		release_write_seqlock(&sUserTimerLock);
	}

	return B_HANDLED_INTERRUPT;
}


/**
 * @brief Fires the timer's associated event and marks the timer as no longer scheduled.
 *
 * Calls UserEvent::Fire(); if the event is still busy from a previous delivery,
 * increments @c fOverrunCount (capped at MAX_USER_TIMER_OVERRUN_COUNT).
 * Clears @c fScheduled because the kernel timer is one-shot; periodic subclasses
 * override this method to reschedule the kernel timer after calling the base version.
 *
 * @note Must be called with @c sUserTimerLock held for writing.
 */
void
UserTimer::HandleTimer()
{
	if (fEvent != NULL) {
		// fire the event and update the overrun count, if necessary
		status_t error = fEvent->Fire();
		if (error == B_BUSY) {
			if (fOverrunCount < MAX_USER_TIMER_OVERRUN_COUNT)
				fOverrunCount++;
		}
	}

	// Since we don't use periodic kernel timers, it isn't scheduled anymore.
	// If the timer is periodic, the derived class' version will schedule it
	// again.
	fScheduled = false;
}


/*!	Updates the start time for a periodic timer after it expired, enforcing
	sanity limits and updating \c fOverrunCount, if necessary.

	The caller must not hold \c sUserTimerLock.
*/
/**
 * @brief Advances @c fNextTime by one interval (or more) after a periodic timer fires.
 *
 * Enforces a minimum advancement of @c kMinPeriodicTimerInterval microseconds to
 * avoid starving the system with extremely short intervals. Any skipped intervals
 * are reflected in @c fOverrunCount.
 *
 * @note The caller must not hold @c sUserTimerLock.
 */
void
UserTimer::UpdatePeriodicStartTime()
{
	if (fInterval < kMinPeriodicTimerInterval) {
		bigtime_t skip = (kMinPeriodicTimerInterval + fInterval - 1) / fInterval;
		fNextTime += skip * fInterval;

		// One interval is the normal advance, so don't consider it skipped.
		skip--;

		if (skip + fOverrunCount > MAX_USER_TIMER_OVERRUN_COUNT)
			fOverrunCount = MAX_USER_TIMER_OVERRUN_COUNT;
		else
			fOverrunCount += skip;
	} else
		fNextTime += fInterval;
}


/*!	Checks whether the timer start time lies too much in the past and, if so,
	adjusts it and updates \c fOverrunCount.

	The caller must not hold \c sUserTimerLock.

	\param now The current time.
*/
/**
 * @brief Skips overdue periodic intervals, bringing @c fNextTime ahead of @p now - interval.
 *
 * If @c fNextTime is more than one full interval in the past, computes how many
 * complete intervals were missed, advances @c fNextTime accordingly, and adds the
 * skip count to @c fOverrunCount (capped at MAX_USER_TIMER_OVERRUN_COUNT).
 *
 * @param now The current clock value to compare against (same unit as @c fNextTime).
 *
 * @note The caller must not hold @c sUserTimerLock.
 */
void
UserTimer::CheckPeriodicOverrun(bigtime_t now)
{
	if (fNextTime + fInterval > now)
		return;

	// The start time is a full interval or more in the past. Skip those
	// intervals.
	bigtime_t skip = (now - fNextTime) / fInterval;
	fNextTime += skip * fInterval;

	if (skip + fOverrunCount > MAX_USER_TIMER_OVERRUN_COUNT)
		fOverrunCount = MAX_USER_TIMER_OVERRUN_COUNT;
	else
		fOverrunCount += skip;
}


/**
 * @brief Cancels the underlying kernel timer, blocking until the cancel takes effect.
 *
 * Sets @c fSkip to prevent the interrupt handler from acquiring the seq-lock while
 * cancel_timer() drains any in-progress callback, then clears @c fSkip.
 *
 * @note @c fScheduled must be @c true when this is called (asserted in debug builds).
 */
void
UserTimer::CancelTimer()
{
	ASSERT(fScheduled);

	atomic_set(&fSkip, 1);
	cancel_timer(&fTimer);
	atomic_set(&fSkip, 0);
}


// #pragma mark - SystemTimeUserTimer


/**
 * @brief Arms or disarms the timer against the system monotonic clock.
 *
 * Cancels any previously scheduled kernel timer, records the old scheduling
 * parameters in the output variables, then schedules a new one-shot (or periodic)
 * kernel timer at @p nextTime on the monotonic clock.
 *
 * @param nextTime          Absolute or relative target time in microseconds.
 *                          Pass B_INFINITE_TIMEOUT to disarm without rescheduling.
 * @param interval          Repeat interval in microseconds; 0 means one-shot.
 * @param flags             B_ABSOLUTE_TIMEOUT or B_RELATIVE_TIMEOUT.
 * @param _oldRemainingTime Output: remaining time of the previous schedule, or
 *                          B_INFINITE_TIMEOUT if not previously scheduled.
 * @param _oldInterval      Output: previous interval, or 0 if not periodic.
 *
 * @note Must be called with @c sUserTimerLock held for writing (via InterruptsWriteSequentialLocker).
 */
void
SystemTimeUserTimer::Schedule(bigtime_t nextTime, bigtime_t interval,
	uint32 flags, bigtime_t& _oldRemainingTime, bigtime_t& _oldInterval)
{
	InterruptsWriteSequentialLocker locker(sUserTimerLock);

	// get the current time
	bigtime_t now = system_time();

	// Cancel the old timer, if still scheduled, and get the previous values.
	if (fScheduled) {
		CancelTimer();

		_oldRemainingTime = fNextTime - now;
		_oldInterval = fInterval;

		fScheduled = false;
	} else {
		_oldRemainingTime = B_INFINITE_TIMEOUT;
		_oldInterval = 0;
	}

	// schedule the new timer
	fNextTime = nextTime;
	fInterval = interval;
	fOverrunCount = 0;

	if (nextTime == B_INFINITE_TIMEOUT)
		return;

	if ((flags & B_RELATIVE_TIMEOUT) != 0)
		fNextTime += now;

	ScheduleKernelTimer(now, fInterval > 0);
}


/**
 * @brief Retrieves current scheduling information for the system-time timer.
 *
 * Reads @c fScheduled, @c fNextTime, @c fInterval, and @c fOverrunCount under
 * a sequential read lock to produce a consistent snapshot.
 *
 * @param _remainingTime Output: microseconds until next fire, or B_INFINITE_TIMEOUT.
 * @param _interval      Output: repeat interval in microseconds, or 0 if one-shot.
 * @param _overrunCount  Output: number of missed deliveries since last schedule.
 */
void
SystemTimeUserTimer::GetInfo(bigtime_t& _remainingTime, bigtime_t& _interval,
	uint32& _overrunCount)
{
	uint32 count;
	do {
		count = acquire_read_seqlock(&sUserTimerLock);

		if (fScheduled) {
			_remainingTime = fNextTime - system_time();
			_interval = fInterval;
		} else {
			_remainingTime = B_INFINITE_TIMEOUT;
			_interval = 0;
		}

		_overrunCount = fOverrunCount;
	} while (!release_read_seqlock(&sUserTimerLock, count));
}


/**
 * @brief Handles a fired system-time timer event and reschedules it if periodic.
 *
 * Delegates to UserTimer::HandleTimer() for event firing, then, if the timer is
 * periodic, advances the start time and re-arms the kernel timer.
 *
 * @note Must be called with @c sUserTimerLock held for writing.
 */
void
SystemTimeUserTimer::HandleTimer()
{
	UserTimer::HandleTimer();

	// if periodic, reschedule the kernel timer
	if (fInterval > 0) {
		UpdatePeriodicStartTime();
		ScheduleKernelTimer(system_time(), true);
	}
}


/*!	Schedules the kernel timer.

	The caller must hold \c sUserTimerLock.

	\param now The current system time to be used.
	\param checkPeriodicOverrun If \c true, calls CheckPeriodicOverrun() first,
		i.e. the start time will be adjusted to not lie too much in the past.
*/
/**
 * @brief Arms the underlying kernel timer at @c fNextTime on the monotonic clock.
 *
 * Optionally calls CheckPeriodicOverrun() to skip missed intervals before
 * submitting the one-shot absolute kernel timer via add_timer().
 *
 * @param now                  Current system_time() value.
 * @param checkPeriodicOverrun If @c true, call CheckPeriodicOverrun() first.
 *
 * @note The caller must hold @c sUserTimerLock.
 */
void
SystemTimeUserTimer::ScheduleKernelTimer(bigtime_t now,
	bool checkPeriodicOverrun)
{
	// If periodic, check whether the start time is too far in the past.
	if (checkPeriodicOverrun)
		CheckPeriodicOverrun(now);

	uint32 timerFlags = B_ONE_SHOT_ABSOLUTE_TIMER
			| B_TIMER_USE_TIMER_STRUCT_TIMES;

	fTimer.schedule_time = std::max(fNextTime, (bigtime_t)0);
	fTimer.period = 0;

	add_timer(&fTimer, &HandleTimerHook, fTimer.schedule_time, timerFlags);

	fScheduled = true;
}


// #pragma mark - RealTimeUserTimer


/**
 * @brief Arms or disarms the timer against CLOCK_REALTIME.
 *
 * Handles both absolute (wall-clock) and relative scheduling. For absolute
 * timers, converts the wall-clock target to a monotonic offset, registers the
 * timer in the global @c sAbsoluteRealTimeTimers list (so it can be adjusted
 * when the real-time clock is stepped), and then schedules the underlying
 * kernel timer.
 *
 * @param nextTime          Target time in microseconds (wall-clock for absolute,
 *                          delta for relative). B_INFINITE_TIMEOUT disarms.
 * @param interval          Repeat interval in microseconds; 0 means one-shot.
 * @param flags             B_ABSOLUTE_TIMEOUT or B_RELATIVE_TIMEOUT.
 * @param _oldRemainingTime Output: remaining time of the previous schedule.
 * @param _oldInterval      Output: previous interval.
 */
void
RealTimeUserTimer::Schedule(bigtime_t nextTime, bigtime_t interval,
	uint32 flags, bigtime_t& _oldRemainingTime, bigtime_t& _oldInterval)
{
	InterruptsWriteSequentialLocker locker(sUserTimerLock);

	// get the current time
	bigtime_t now = system_time();

	// Cancel the old timer, if still scheduled, and get the previous values.
	if (fScheduled) {
		CancelTimer();

		_oldRemainingTime = fNextTime - now;
		_oldInterval = fInterval;

		if (fAbsolute) {
			SpinLocker globalListLocker(sAbsoluteRealTimeTimersLock);
			sAbsoluteRealTimeTimers.Remove(this);
		}

		fScheduled = false;
	} else {
		_oldRemainingTime = B_INFINITE_TIMEOUT;
		_oldInterval = 0;
	}

	// schedule the new timer
	fNextTime = nextTime;
	fInterval = interval;
	fOverrunCount = 0;

	if (nextTime == B_INFINITE_TIMEOUT)
		return;

	fAbsolute = (flags & B_RELATIVE_TIMEOUT) == 0;

	if (fAbsolute) {
		fRealTimeOffset = rtc_boot_time();
		fNextTime -= fRealTimeOffset;

		// If periodic, check whether the start time is too far in the past.
		if (fInterval > 0)
			CheckPeriodicOverrun(now);

		// add the absolute timer to the global list
		SpinLocker globalListLocker(sAbsoluteRealTimeTimersLock);
		sAbsoluteRealTimeTimers.Insert(this);
	} else
		fNextTime += now;

	ScheduleKernelTimer(now, false);
}


/*!	Called when the real-time clock has been changed.

	The caller must hold \c sUserTimerLock. Optionally the caller may also
	hold \c sAbsoluteRealTimeTimersLock.
*/
/**
 * @brief Adjusts the timer when the real-time clock is stepped.
 *
 * Fetches the new boot-time offset, cancels the current kernel timer, shifts
 * @c fNextTime by the delta between old and new offsets, and re-arms the
 * kernel timer so that the absolute wall-clock target is preserved.
 *
 * @note The caller must hold @c sUserTimerLock (and optionally @c sAbsoluteRealTimeTimersLock).
 * @note Only valid for absolute timers (@c fAbsolute must be @c true and @c fScheduled must be @c true).
 */
void
RealTimeUserTimer::TimeWarped()
{
	ASSERT(fScheduled && fAbsolute);

	// get the new real-time offset
	bigtime_t oldRealTimeOffset = fRealTimeOffset;
	fRealTimeOffset = rtc_boot_time();
	if (fRealTimeOffset == oldRealTimeOffset)
		return;

	// cancel the kernel timer and reschedule it
	CancelTimer();

	fNextTime += oldRealTimeOffset - fRealTimeOffset;

	ScheduleKernelTimer(system_time(), fInterval > 0);
}


/**
 * @brief Handles a fired real-time timer, rescheduling or removing from the global list.
 *
 * Delegates to SystemTimeUserTimer::HandleTimer() for event firing and periodic
 * rescheduling. If the timer fired as a one-shot absolute timer, removes it from
 * the @c sAbsoluteRealTimeTimers global list.
 *
 * @note Must be called with @c sUserTimerLock held for writing.
 */
void
RealTimeUserTimer::HandleTimer()
{
	SystemTimeUserTimer::HandleTimer();

	// remove from global list, if no longer scheduled
	if (!fScheduled && fAbsolute) {
		SpinLocker globalListLocker(sAbsoluteRealTimeTimersLock);
		sAbsoluteRealTimeTimers.Remove(this);
	}
}


// #pragma mark - TeamTimeUserTimer


/**
 * @brief Constructs a TeamTimeUserTimer for the given team.
 *
 * @param teamID The ID of the team whose CPU time this timer tracks.
 */
TeamTimeUserTimer::TeamTimeUserTimer(team_id teamID)
	:
	fTeamID(teamID),
	fTeam(NULL)
{
}


/**
 * @brief Destroys the TeamTimeUserTimer.
 *
 * @note @c fTeam must be NULL at destruction time (the timer must have been
 *       deactivated beforehand); this is asserted in debug builds.
 */
TeamTimeUserTimer::~TeamTimeUserTimer()
{
	ASSERT(fTeam == NULL);
}


/**
 * @brief Arms or disarms the timer against the team's total CPU time clock.
 *
 * Cancels any active timer, releases the current team reference, then, if
 * @p nextTime is not B_INFINITE_TIMEOUT, acquires a new reference to the team
 * and registers this timer with it via UserTimerActivated(). Finally calls
 * _Update() to arm the kernel timer if any team thread is currently running.
 *
 * @param nextTime          Target CPU-time in microseconds (absolute or relative).
 * @param interval          Repeat interval; 0 for one-shot.
 * @param flags             B_ABSOLUTE_TIMEOUT or B_RELATIVE_TIMEOUT.
 * @param _oldRemainingTime Output: remaining CPU-time of previous schedule.
 * @param _oldInterval      Output: previous interval.
 */
void
TeamTimeUserTimer::Schedule(bigtime_t nextTime, bigtime_t interval,
	uint32 flags, bigtime_t& _oldRemainingTime, bigtime_t& _oldInterval)
{
	InterruptsWriteSequentialLocker locker(sUserTimerLock);
	SpinLocker timeLocker(fTeam != NULL ? &fTeam->time_lock : NULL);

	// get the current time, but only if needed
	bool nowValid = fTeam != NULL;
	bigtime_t now = nowValid ? fTeam->CPUTime(false) : 0;

	// Cancel the old timer, if still scheduled, and get the previous values.
	if (fTeam != NULL) {
		if (fScheduled) {
			CancelTimer();
			fScheduled = false;
		}

		_oldRemainingTime = fNextTime - now;
		_oldInterval = fInterval;

		fTeam->UserTimerDeactivated(this);
		fTeam->ReleaseReference();
		fTeam = NULL;
	} else {
		_oldRemainingTime = B_INFINITE_TIMEOUT;
		_oldInterval = 0;
	}

	// schedule the new timer
	fNextTime = nextTime;
	fInterval = interval;
	fOverrunCount = 0;

	if (fNextTime == B_INFINITE_TIMEOUT)
		return;

	// Get the team. If it doesn't exist anymore, just don't schedule the
	// timer anymore.
	Team* newTeam = Team::Get(fTeamID);
	if (newTeam == NULL) {
		fTeam = NULL;
		return;
	} else if (fTeam == NULL)
		timeLocker.SetTo(newTeam->time_lock, false);
	fTeam = newTeam;

	fAbsolute = (flags & B_RELATIVE_TIMEOUT) == 0;

	// convert relative to absolute timeouts
	if (!fAbsolute) {
		if (!nowValid)
			now = fTeam->CPUTime(false);
		fNextTime += now;
	}

	fTeam->UserTimerActivated(this);

	// schedule/udpate the kernel timer
	Update(NULL);
}


/**
 * @brief Returns current scheduling information for the team CPU-time timer.
 *
 * Reads the team's current CPU time under its @c time_lock to compute the
 * remaining time, all within a sequential read lock for consistency.
 *
 * @param _remainingTime Output: microseconds of CPU time remaining, or B_INFINITE_TIMEOUT.
 * @param _interval      Output: repeat interval, or 0.
 * @param _overrunCount  Output: number of missed deliveries.
 */
void
TeamTimeUserTimer::GetInfo(bigtime_t& _remainingTime, bigtime_t& _interval,
	uint32& _overrunCount)
{
	uint32 count;
	do {
		count = acquire_read_seqlock(&sUserTimerLock);

		if (fTeam != NULL) {
			InterruptsSpinLocker timeLocker(fTeam->time_lock);
			_remainingTime = fNextTime - fTeam->CPUTime(false);
			_interval = fInterval;
		} else {
			_remainingTime = B_INFINITE_TIMEOUT;
			_interval = 0;
		}

		_overrunCount = fOverrunCount;
	} while (!release_read_seqlock(&sUserTimerLock, count));
}


/*!	Deactivates the timer, if it is activated.

	The caller must hold \c time_lock and \c sUserTimerLock.
*/
/**
 * @brief Deactivates the team CPU-time timer, cancelling the kernel timer if needed.
 *
 * Cancels any scheduled kernel timer, calls UserTimerDeactivated() on the team,
 * releases the team reference, and sets @c fTeam to NULL.
 *
 * @note The caller must hold both @c time_lock and @c sUserTimerLock.
 */
void
TeamTimeUserTimer::Deactivate()
{
	if (fTeam == NULL)
		return;

	// unschedule, if scheduled
	if (fScheduled) {
		CancelTimer();
		fScheduled = false;
	}

	// deactivate
	fTeam->UserTimerDeactivated(this);
	fTeam->ReleaseReference();
	fTeam = NULL;
}


/*!	Starts/stops the timer as necessary, if it is active.

	Called whenever threads of the team whose CPU time is referred to by the
	timer are scheduled or unscheduled (or leave the team), or when the timer
	was just set. Schedules a kernel timer for the remaining time, respectively
	cancels it.

	The caller must hold \c time_lock and \c sUserTimerLock.

	\param unscheduledThread If not \c NULL, this is the thread that is
		currently running and which is in the process of being unscheduled.
*/
/**
 * @brief Recounts running team threads and re-arms or cancels the kernel timer.
 *
 * Iterates over all CPUs to determine how many threads belonging to the team are
 * currently running (excluding @p unscheduledThread). Delegates to _Update() to
 * actually (re)schedule or cancel the kernel timer.
 *
 * @param unscheduledThread Thread being preempted, excluded from the running count, or NULL.
 * @param lockedThread      Thread whose time_lock is already held, or NULL.
 *
 * @note The caller must hold @c time_lock and @c sUserTimerLock.
 */
void
TeamTimeUserTimer::Update(Thread* unscheduledThread, Thread* lockedThread)
{
	if (fTeam == NULL)
		return;

	// determine how many of the team's threads are currently running
	fRunningThreads = 0;
	int32 cpuCount = smp_get_num_cpus();
	for (int32 i = 0; i < cpuCount; i++) {
		Thread* thread = gCPU[i].running_thread;
		if (thread != unscheduledThread && thread->team == fTeam)
			fRunningThreads++;
	}

	_Update(unscheduledThread != NULL, lockedThread);
}


/*!	Called when the team's CPU time clock which this timer refers to has been
	set.

	The caller must hold \c time_lock and \c sUserTimerLock.

	\param changedBy The value by which the clock has changed.
*/
/**
 * @brief Adjusts the target time when the team's CPU clock is stepped.
 *
 * For relative timers, shifts @c fNextTime by @p changedBy so the remaining
 * interval is preserved. Then calls _Update() to reschedule the kernel timer.
 *
 * @param changedBy Signed delta applied to the team's CPU clock (new = old + changedBy).
 *
 * @note The caller must hold @c time_lock and @c sUserTimerLock.
 */
void
TeamTimeUserTimer::TimeWarped(bigtime_t changedBy)
{
	if (fTeam == NULL || changedBy == 0)
		return;

	// If this is a relative timer, adjust fNextTime by the value the clock has
	// changed.
	if (!fAbsolute)
		fNextTime += changedBy;

	// reschedule the kernel timer
	_Update(false);
}


/**
 * @brief Handles a fired team CPU-time timer event.
 *
 * Delegates to UserTimer::HandleTimer() for event firing. For one-shot timers,
 * deactivates the timer and releases the team reference. For periodic timers,
 * advances the start time and re-arms the kernel timer via _Update().
 *
 * @note Must be called with @c sUserTimerLock held for writing.
 */
void
TeamTimeUserTimer::HandleTimer()
{
	UserTimer::HandleTimer();

	// If the timer is not periodic, it is no longer active. Otherwise
	// reschedule the kernel timer.
	if (fTeam != NULL) {
		if (fInterval == 0) {
			fTeam->UserTimerDeactivated(this);
			fTeam->ReleaseReference();
			fTeam = NULL;
		} else {
			UpdatePeriodicStartTime();
			_Update(false);
		}
	}
}


/*!	Schedules/cancels the kernel timer as necessary.

	\c fRunningThreads must be up-to-date.
	The caller must hold \c time_lock and \c sUserTimerLock.

	\param unscheduling \c true, when the current thread is in the process of
		being unscheduled.
*/
/**
 * @brief Internal: arms or cancels the kernel timer based on @c fRunningThreads.
 *
 * If no team threads are currently running, cancels the kernel timer and returns.
 * Otherwise computes the wall-clock deadline by dividing the remaining CPU-time
 * budget across all running threads and submits a one-shot absolute kernel timer.
 *
 * @param unscheduling @c true when the calling thread is being preempted; passed
 *                     to CPUTime() so the running thread's partial quantum is
 *                     excluded from the current time sample.
 * @param lockedThread Thread whose @c time_lock is already held by the caller, or NULL.
 *
 * @note @c fRunningThreads must be current. The caller must hold @c time_lock
 *       and @c sUserTimerLock.
 */
void
TeamTimeUserTimer::_Update(bool unscheduling, Thread* lockedThread)
{
	// unschedule the kernel timer, if scheduled
	if (fScheduled)
		CancelTimer();

	// if no more threads are running, we're done
	if (fRunningThreads == 0) {
		fScheduled = false;
		return;
	}

	// There are still threads running. Reschedule the kernel timer.
	bigtime_t now = fTeam->CPUTime(unscheduling, lockedThread);

	// If periodic, check whether the start time is too far in the past.
	if (fInterval > 0)
		CheckPeriodicOverrun(now);

	if (fNextTime > now) {
		fTimer.schedule_time = system_time()
			+ (fNextTime - now + fRunningThreads - 1) / fRunningThreads;
		// check for overflow
		if (fTimer.schedule_time < 0)
			fTimer.schedule_time = B_INFINITE_TIMEOUT;
	} else
		fTimer.schedule_time = 0;
	fTimer.period = 0;
		// We reschedule periodic timers manually in HandleTimer() to avoid
		// rounding errors.

	add_timer(&fTimer, &HandleTimerHook, fTimer.schedule_time,
		B_ONE_SHOT_ABSOLUTE_TIMER | B_TIMER_USE_TIMER_STRUCT_TIMES);
		// We use B_TIMER_USE_TIMER_STRUCT_TIMES, so period remains 0, which
		// our base class expects.

	fScheduled = true;
}


// #pragma mark - TeamUserTimeUserTimer


/**
 * @brief Constructs a TeamUserTimeUserTimer for the given team.
 *
 * @param teamID The ID of the team whose user-mode CPU time this timer tracks.
 */
TeamUserTimeUserTimer::TeamUserTimeUserTimer(team_id teamID)
	:
	fTeamID(teamID),
	fTeam(NULL)
{
}


/**
 * @brief Destroys the TeamUserTimeUserTimer.
 *
 * @note @c fTeam must be NULL at destruction; the timer must be deactivated
 *       before it is deleted (asserted in debug builds).
 */
TeamUserTimeUserTimer::~TeamUserTimeUserTimer()
{
	ASSERT(fTeam == NULL);
}


/**
 * @brief Arms or disarms the timer against the team's user-mode CPU time.
 *
 * Deactivates any currently active timer, releases the team reference, then,
 * if @p nextTime is not B_INFINITE_TIMEOUT, acquires a new team reference,
 * registers the timer, and calls Check() to fire immediately if already elapsed.
 *
 * @param nextTime          Target user-CPU-time in microseconds.
 * @param interval          Repeat interval; 0 for one-shot.
 * @param flags             B_ABSOLUTE_TIMEOUT or B_RELATIVE_TIMEOUT.
 * @param _oldRemainingTime Output: remaining user-CPU-time of the previous schedule.
 * @param _oldInterval      Output: previous interval.
 */
void
TeamUserTimeUserTimer::Schedule(bigtime_t nextTime, bigtime_t interval,
	uint32 flags, bigtime_t& _oldRemainingTime, bigtime_t& _oldInterval)
{
	InterruptsWriteSequentialLocker locker(sUserTimerLock);
	SpinLocker timeLocker(fTeam != NULL ? &fTeam->time_lock : NULL);

	// get the current time, but only if needed
	bool nowValid = fTeam != NULL;
	bigtime_t now = nowValid ? fTeam->UserCPUTime() : 0;

	// Cancel the old timer, if still active, and get the previous values.
	if (fTeam != NULL) {
		_oldRemainingTime = fNextTime - now;
		_oldInterval = fInterval;

		fTeam->UserTimerDeactivated(this);
		fTeam->ReleaseReference();
		fTeam = NULL;
	} else {
		_oldRemainingTime = B_INFINITE_TIMEOUT;
		_oldInterval = 0;
	}

	// schedule the new timer
	fNextTime = nextTime;
	fInterval = interval;
	fOverrunCount = 0;

	if (fNextTime == B_INFINITE_TIMEOUT)
		return;

	// Get the team. If it doesn't exist anymore, just don't schedule the
	// timer anymore.
	Team* newTeam = Team::Get(fTeamID);
	if (newTeam == NULL) {
		fTeam = NULL;
		return;
	} else if (fTeam == NULL)
		timeLocker.SetTo(newTeam->time_lock, false);
	fTeam = newTeam;

	// convert relative to absolute timeouts
	if ((flags & B_RELATIVE_TIMEOUT) != 0) {
		if (!nowValid)
			now = fTeam->CPUTime(false);
		fNextTime += now;
	}

	fTeam->UserTimerActivated(this);

	// fire the event, if already timed out
	Check();
}


/**
 * @brief Returns current scheduling information for the team user-CPU-time timer.
 *
 * Reads the team's user CPU time under its @c time_lock within a sequential
 * read lock to produce a consistent snapshot.
 *
 * @param _remainingTime Output: remaining user-CPU-time in microseconds, or B_INFINITE_TIMEOUT.
 * @param _interval      Output: repeat interval, or 0.
 * @param _overrunCount  Output: number of missed deliveries.
 */
void
TeamUserTimeUserTimer::GetInfo(bigtime_t& _remainingTime, bigtime_t& _interval,
	uint32& _overrunCount)
{
	uint32 count;
	do {
		count = acquire_read_seqlock(&sUserTimerLock);

		if (fTeam != NULL) {
			InterruptsSpinLocker timeLocker(fTeam->time_lock);
			_remainingTime = fNextTime - fTeam->UserCPUTime();
			_interval = fInterval;
		} else {
			_remainingTime = B_INFINITE_TIMEOUT;
			_interval = 0;
		}

		_overrunCount = fOverrunCount;
	} while (!release_read_seqlock(&sUserTimerLock, count));
}


/*!	Deactivates the timer, if it is activated.

	The caller must hold \c time_lock and \c sUserTimerLock.
*/
/**
 * @brief Deactivates the team user-CPU-time timer.
 *
 * Notifies the team that this timer is no longer active, releases the team
 * reference, and sets @c fTeam to NULL. Unlike TeamTimeUserTimer::Deactivate(),
 * there is no kernel timer to cancel because this timer class does not use one.
 *
 * @note The caller must hold @c time_lock and @c sUserTimerLock.
 */
void
TeamUserTimeUserTimer::Deactivate()
{
	if (fTeam == NULL)
		return;

	// deactivate
	fTeam->UserTimerDeactivated(this);
	fTeam->ReleaseReference();
	fTeam = NULL;
}


/*!	Checks whether the timer is up, firing an event, if so.

	The caller must hold \c time_lock and \c sUserTimerLock.
*/
/**
 * @brief Polls the team's user CPU time and fires the event if the deadline has passed.
 *
 * Called from Schedule() (initial arm) and from user_timer_check_team_user_timers()
 * each time the scheduler switches user-mode accounting. If the deadline has been
 * reached, calls HandleTimer(), then either deactivates (one-shot) or advances the
 * next deadline (periodic).
 *
 * @note The caller must hold @c time_lock and @c sUserTimerLock.
 */
void
TeamUserTimeUserTimer::Check()
{
	if (fTeam == NULL)
		return;

	// check whether we need to fire the event yet
	bigtime_t now = fTeam->UserCPUTime();
	if (now < fNextTime)
		return;

	HandleTimer();

	// If the timer is not periodic, it is no longer active. Otherwise compute
	// the event time.
	if (fInterval == 0) {
		fTeam->UserTimerDeactivated(this);
		fTeam->ReleaseReference();
		fTeam = NULL;
		return;
	}

	// First validate fNextTime, then increment it, so that fNextTime is > now
	// (CheckPeriodicOverrun() only makes it > now - fInterval).
	CheckPeriodicOverrun(now);
	fNextTime += fInterval;
	fScheduled = true;
}


// #pragma mark - ThreadTimeUserTimer


/**
 * @brief Constructs a ThreadTimeUserTimer for the given thread.
 *
 * @param threadID The ID of the thread whose CPU time this timer tracks.
 */
ThreadTimeUserTimer::ThreadTimeUserTimer(thread_id threadID)
	:
	fThreadID(threadID),
	fThread(NULL)
{
}


/**
 * @brief Destroys the ThreadTimeUserTimer.
 *
 * @note @c fThread must be NULL at destruction; the timer must be deactivated
 *       before deletion (asserted in debug builds).
 */
ThreadTimeUserTimer::~ThreadTimeUserTimer()
{
	ASSERT(fThread == NULL);
}


/**
 * @brief Arms or disarms the timer against the target thread's CPU time.
 *
 * Cancels any active kernel timer, releases the current thread reference, then,
 * if @p nextTime is not B_INFINITE_TIMEOUT, acquires a new reference to the
 * thread, registers the timer via UserTimerActivated(), and, if the thread is
 * currently running, calls Start() to arm the underlying kernel timer immediately.
 *
 * @param nextTime          Target CPU-time in microseconds (absolute or relative).
 * @param interval          Repeat interval; 0 for one-shot.
 * @param flags             B_ABSOLUTE_TIMEOUT or B_RELATIVE_TIMEOUT.
 * @param _oldRemainingTime Output: remaining CPU-time of previous schedule.
 * @param _oldInterval      Output: previous interval.
 */
void
ThreadTimeUserTimer::Schedule(bigtime_t nextTime, bigtime_t interval,
	uint32 flags, bigtime_t& _oldRemainingTime, bigtime_t& _oldInterval)
{
	InterruptsWriteSequentialLocker locker(sUserTimerLock);
	SpinLocker timeLocker(fThread != NULL ? &fThread->time_lock : NULL);

	// get the current time, but only if needed
	bool nowValid = fThread != NULL;
	bigtime_t now = nowValid ? fThread->CPUTime(false) : 0;

	// Cancel the old timer, if still scheduled, and get the previous values.
	if (fThread != NULL) {
		if (fScheduled) {
			CancelTimer();
			fScheduled = false;
		}

		_oldRemainingTime = fNextTime - now;
		_oldInterval = fInterval;

		fThread->UserTimerDeactivated(this);
		fThread->ReleaseReference();
		fThread = NULL;
	} else {
		_oldRemainingTime = B_INFINITE_TIMEOUT;
		_oldInterval = 0;
	}

	// schedule the new timer
	fNextTime = nextTime;
	fInterval = interval;
	fOverrunCount = 0;

	if (fNextTime == B_INFINITE_TIMEOUT)
		return;

	// Get the thread. If it doesn't exist anymore, just don't schedule the
	// timer anymore.
	Thread* newThread = Thread::Get(fThreadID);
	if (newThread == NULL) {
		fThread = NULL;
		return;
	} else if (fThread == NULL)
		timeLocker.SetTo(newThread->time_lock, false);
	fThread = newThread;

	fAbsolute = (flags & B_RELATIVE_TIMEOUT) == 0;

	// convert relative to absolute timeouts
	if (!fAbsolute) {
		if (!nowValid)
			now = fThread->CPUTime(false);
		fNextTime += now;
	}

	fThread->UserTimerActivated(this);

	// If the thread is currently running, also schedule a kernel timer.
	if (fThread->cpu != NULL)
		Start();
}


/**
 * @brief Returns current scheduling information for the thread CPU-time timer.
 *
 * Reads the thread's current CPU time under its @c time_lock within a sequential
 * read lock to produce a consistent snapshot.
 *
 * @param _remainingTime Output: remaining CPU-time in microseconds, or B_INFINITE_TIMEOUT.
 * @param _interval      Output: repeat interval, or 0.
 * @param _overrunCount  Output: number of missed deliveries.
 */
void
ThreadTimeUserTimer::GetInfo(bigtime_t& _remainingTime, bigtime_t& _interval,
	uint32& _overrunCount)
{
	uint32 count;
	do {
		count = acquire_read_seqlock(&sUserTimerLock);

		if (fThread != NULL) {
			SpinLocker timeLocker(fThread->time_lock);
			_remainingTime = fNextTime - fThread->CPUTime(false);
			_interval = fInterval;
		} else {
			_remainingTime = B_INFINITE_TIMEOUT;
			_interval = 0;
		}

		_overrunCount = fOverrunCount;
	} while (!release_read_seqlock(&sUserTimerLock, count));
}


/*!	Deactivates the timer, if it is activated.

	The caller must hold \c time_lock and \c sUserTimerLock.
*/
/**
 * @brief Deactivates the thread CPU-time timer, cancelling the kernel timer if needed.
 *
 * Cancels any scheduled kernel timer, calls UserTimerDeactivated() on the thread,
 * releases the thread reference, and sets @c fThread to NULL.
 *
 * @note The caller must hold @c time_lock and @c sUserTimerLock.
 */
void
ThreadTimeUserTimer::Deactivate()
{
	if (fThread == NULL)
		return;

	// unschedule, if scheduled
	if (fScheduled) {
		CancelTimer();
		fScheduled = false;
	}

	// deactivate
	fThread->UserTimerDeactivated(this);
	fThread->ReleaseReference();
	fThread = NULL;
}


/*!	Starts the timer, if it is active.

	Called when the thread whose CPU time is referred to by the timer is
	scheduled, or, when the timer was just set and the thread is already
	running. Schedules a kernel timer for the remaining time.

	The caller must hold \c time_lock and \c sUserTimerLock.
*/
/**
 * @brief Arms the kernel timer when the tracked thread begins running.
 *
 * Reads the thread's current CPU time, optionally skips overdue periodic
 * intervals, computes the wall-clock deadline as @c system_time() + remaining,
 * and submits a one-shot absolute kernel timer.
 *
 * @note @c fScheduled must be @c false on entry (asserted). The caller must
 *       hold @c time_lock and @c sUserTimerLock.
 */
void
ThreadTimeUserTimer::Start()
{
	if (fThread == NULL)
		return;

	ASSERT(!fScheduled);

	// add the kernel timer
	bigtime_t now = fThread->CPUTime(false);

	// If periodic, check whether the start time is too far in the past.
	if (fInterval > 0)
		CheckPeriodicOverrun(now);

	if (fNextTime > now) {
		fTimer.schedule_time = system_time() + fNextTime - now;
		// check for overflow
		if (fTimer.schedule_time < 0)
			fTimer.schedule_time = B_INFINITE_TIMEOUT;
	} else
		fTimer.schedule_time = 0;
	fTimer.period = 0;

	uint32 flags = B_ONE_SHOT_ABSOLUTE_TIMER | B_TIMER_USE_TIMER_STRUCT_TIMES;
	add_timer(&fTimer, &HandleTimerHook, fTimer.schedule_time, flags);

	fScheduled = true;
}


/*!	Stops the timer, if it is active.

	Called when the thread whose CPU time is referred to by the timer is
	unscheduled, or, when the timer is canceled.

	The caller must hold \c sUserTimerLock.
*/
/**
 * @brief Cancels the kernel timer when the tracked thread stops running.
 *
 * Called by the scheduler when the thread is preempted or blocked, or when
 * the timer itself is cancelled. Calls CancelTimer() and clears @c fScheduled.
 *
 * @note @c fScheduled must be @c true on entry (asserted). The caller must
 *       hold @c sUserTimerLock.
 */
void
ThreadTimeUserTimer::Stop()
{
	if (fThread == NULL)
		return;

	ASSERT(fScheduled);

	// cancel the kernel timer
	CancelTimer();
	fScheduled = false;

	// TODO: To avoid odd race conditions, we should check the current time of
	// the thread (ignoring the time since last_time) and manually fire the
	// user event, if necessary.
}


/*!	Called when the team's CPU time clock which this timer refers to has been
	set.

	The caller must hold \c time_lock and \c sUserTimerLock.

	\param changedBy The value by which the clock has changed.
*/
/**
 * @brief Adjusts the target time when the thread's CPU clock is stepped.
 *
 * For relative timers, shifts @c fNextTime by @p changedBy. If the kernel
 * timer is currently scheduled, performs a Stop()/Start() cycle to re-arm
 * it with the updated deadline.
 *
 * @param changedBy Signed delta applied to the thread's CPU clock (new = old + changedBy).
 *
 * @note The caller must hold @c time_lock and @c sUserTimerLock.
 */
void
ThreadTimeUserTimer::TimeWarped(bigtime_t changedBy)
{
	if (fThread == NULL || changedBy == 0)
		return;

	// If this is a relative timer, adjust fNextTime by the value the clock has
	// changed.
	if (!fAbsolute)
		fNextTime += changedBy;

	// reschedule the kernel timer
	if (fScheduled) {
		Stop();
		Start();
	}
}


/**
 * @brief Handles a fired thread CPU-time timer event.
 *
 * Delegates to UserTimer::HandleTimer() for event firing. For periodic timers,
 * advances the start time and re-arms the kernel timer via Start(). For one-shot
 * timers, deactivates and releases the thread reference.
 *
 * @note Must be called with @c sUserTimerLock held for writing.
 */
void
ThreadTimeUserTimer::HandleTimer()
{
	UserTimer::HandleTimer();

	if (fThread != NULL) {
		// If the timer is periodic, reschedule the kernel timer. Otherwise it
		// is no longer active.
		if (fInterval > 0) {
			UpdatePeriodicStartTime();
			Start();
		} else {
			fThread->UserTimerDeactivated(this);
			fThread->ReleaseReference();
			fThread = NULL;
		}
	}
}


// #pragma mark - UserTimerList


/**
 * @brief Constructs an empty UserTimerList.
 */
UserTimerList::UserTimerList()
{
}


/**
 * @brief Destroys the UserTimerList.
 *
 * @note The list must be empty at destruction (asserted in debug builds).
 *       Call DeleteTimers() before destroying.
 */
UserTimerList::~UserTimerList()
{
	ASSERT(fTimers.IsEmpty());
}


/*!	Returns the user timer with the given ID.

	\param id The timer's ID
	\return The user timer with the given ID or \c NULL, if there is no such
		timer.
*/
/**
 * @brief Looks up a timer by its numeric ID.
 *
 * Performs a linear scan of the internal timer list.
 *
 * @param id The timer ID to look for.
 * @return Pointer to the matching UserTimer, or NULL if not found.
 *
 * @note The list is maintained in ascending ID order; a future optimisation
 *       could use binary search or a hash map.
 */
UserTimer*
UserTimerList::TimerFor(int32 id) const
{
	// TODO: Use a more efficient data structure. E.g. a sorted array.
	for (TimerList::ConstIterator it = fTimers.GetIterator();
			UserTimer* timer = it.Next();) {
		if (timer->ID() == id)
			return timer;
	}

	return NULL;
}


/*!	Adds the given user timer and assigns it an ID.

	\param timer The timer to be added.
*/
/**
 * @brief Inserts a timer into the list, assigning a free ID if none is set.
 *
 * For timers with a pre-assigned non-negative ID (default/pre-defined timers),
 * finds the correct sorted insertion point and panics if a duplicate is detected.
 * For user-defined timers (ID < 0), allocates the lowest available ID starting
 * from USER_TIMER_FIRST_USER_DEFINED_ID.
 *
 * @param timer The timer to add; its ID may be -1 (auto-assign) or a fixed value.
 *
 * @note The list remains sorted by ascending timer ID after insertion.
 */
void
UserTimerList::AddTimer(UserTimer* timer)
{
	int32 id = timer->ID();
	if (id < 0) {
		// user-defined timer -- find an usused ID
		id = USER_TIMER_FIRST_USER_DEFINED_ID;
		UserTimer* insertAfter = NULL;
		for (TimerList::Iterator it = fTimers.GetIterator();
				UserTimer* other = it.Next();) {
			if (other->ID() > id)
				break;
			if (other->ID() == id)
				id++;
			insertAfter = other;
		}

		// insert the timer
		timer->SetID(id);
		fTimers.InsertAfter(insertAfter, timer);
	} else {
		// default timer -- find the insertion point
		UserTimer* insertAfter = NULL;
		for (TimerList::Iterator it = fTimers.GetIterator();
				UserTimer* other = it.Next();) {
			if (other->ID() > id)
				break;
			if (other->ID() == id) {
				panic("UserTimerList::AddTimer(): timer with ID %" B_PRId32
					" already exists!", id);
			}
			insertAfter = other;
		}

		// insert the timer
		fTimers.InsertAfter(insertAfter, timer);
	}
}


/*!	Deletes all (or all user-defined) user timers.

	\param userDefinedOnly If \c true, only the user-defined timers are deleted,
		otherwise all timers are deleted.
	\return The number of user-defined timers that were removed and deleted.
*/
/**
 * @brief Cancels and deletes timers from the list.
 *
 * Iterates the list; for each qualifying timer, removes it from the list,
 * calls Cancel(), and deletes the object.
 *
 * @param userDefinedOnly If @c true, only timers with IDs >=
 *                        USER_TIMER_FIRST_USER_DEFINED_ID are deleted;
 *                        pre-defined (system) timers are skipped.
 * @return The number of user-defined timers that were deleted.
 */
int32
UserTimerList::DeleteTimers(bool userDefinedOnly)
{
	int32 userDefinedCount = 0;

	for (TimerList::Iterator it = fTimers.GetIterator();
			UserTimer* timer = it.Next();) {
		if (timer->ID() < USER_TIMER_FIRST_USER_DEFINED_ID) {
			if (userDefinedOnly)
				continue;
		} else
			userDefinedCount++;

		// remove, cancel, and delete the timer
		it.Remove();
		timer->Cancel();
		delete timer;
	}

	return userDefinedCount;
}


// #pragma mark - private


/**
 * @brief Internal helper: allocates and fully initialises a UserTimer of the appropriate subclass.
 *
 * Selects the concrete timer class based on @p clockID, creates the associated
 * UserEvent (signal or thread-creation) from @p event, assigns a pre-defined or
 * auto-generated ID, and inserts the timer into the team or thread's timer list.
 *
 * @param clockID            POSIX clock ID (CLOCK_MONOTONIC, CLOCK_REALTIME, etc.,
 *                           or a team-ID-encoded CPU clock).
 * @param timerID            Pre-assigned timer ID, or -1 to auto-allocate.
 * @param team               The owning team; must not be NULL for team-clock timers.
 * @param thread             The owning thread, or NULL for team timers.
 * @param flags              USER_TIMER_SIGNAL_THREAD and similar modifier flags.
 * @param event              The sigevent structure describing the notification method.
 * @param threadAttributes   Thread-creation attributes for SIGEV_THREAD; may be NULL.
 * @param isDefaultEvent     If @c true, the timer ID is used as @c sigev_value.
 * @retval >= 0              The newly assigned timer ID on success.
 * @retval B_NO_MEMORY       Allocation of the timer or event object failed.
 * @retval B_BAD_VALUE       Invalid clock ID, signal number, or NULL team for a team clock.
 * @retval B_NOT_ALLOWED     Attempt to create a timer against the kernel team's clock.
 */
static int32
create_timer(clockid_t clockID, int32 timerID, Team* team, Thread* thread,
	uint32 flags, const struct sigevent& event,
	ThreadCreationAttributes* threadAttributes, bool isDefaultEvent)
{
	// create the timer object
	UserTimer* timer;
	switch (clockID) {
		case CLOCK_MONOTONIC:
			timer = new(std::nothrow) SystemTimeUserTimer;
			break;

		case CLOCK_REALTIME:
			timer = new(std::nothrow) RealTimeUserTimer;
			break;

		case CLOCK_THREAD_CPUTIME_ID:
			timer = new(std::nothrow) ThreadTimeUserTimer(
				thread_get_current_thread()->id);
			break;

		case CLOCK_PROCESS_CPUTIME_ID:
			if (team == NULL)
				return B_BAD_VALUE;
			timer = new(std::nothrow) TeamTimeUserTimer(team->id);
			break;

		case CLOCK_PROCESS_USER_CPUTIME_ID:
			if (team == NULL)
				return B_BAD_VALUE;
			timer = new(std::nothrow) TeamUserTimeUserTimer(team->id);
			break;

		default:
		{
			// The clock ID is a ID of the team whose CPU time the clock refers
			// to. Check whether the team exists and we have permission to
			// access its clock.
			if (clockID <= 0)
				return B_BAD_VALUE;
			if (clockID == team_get_kernel_team_id())
				return B_NOT_ALLOWED;

			Team* timedTeam = Team::GetAndLock(clockID);
			if (timedTeam == NULL)
				return B_BAD_VALUE;

			uid_t uid = geteuid();
			uid_t teamUID = timedTeam->effective_uid;

			timedTeam->UnlockAndReleaseReference();

			if (uid != 0 && uid != teamUID)
				return B_NOT_ALLOWED;

			timer = new(std::nothrow) TeamTimeUserTimer(clockID);
			break;
		}
	}

	if (timer == NULL)
		return B_NO_MEMORY;
	ObjectDeleter<UserTimer> timerDeleter(timer);

	if (timerID >= 0)
		timer->SetID(timerID);

	SignalEvent* signalEvent = NULL;

	switch (event.sigev_notify) {
		case SIGEV_NONE:
			// the timer's event remains NULL
			break;

		case SIGEV_SIGNAL:
		{
			if (event.sigev_signo <= 0 || event.sigev_signo > MAX_SIGNAL_NUMBER)
				return B_BAD_VALUE;

			if (thread != NULL && (flags & USER_TIMER_SIGNAL_THREAD) != 0) {
				// The signal shall be sent to the thread.
				signalEvent = ThreadSignalEvent::Create(thread,
					event.sigev_signo, SI_TIMER, 0, team->id);
			} else {
				// The signal shall be sent to the team.
				signalEvent = TeamSignalEvent::Create(team, event.sigev_signo,
					SI_TIMER, 0);
			}

			if (signalEvent == NULL)
				return B_NO_MEMORY;

			timer->SetEvent(signalEvent);
			break;
		}

		case SIGEV_THREAD:
		{
			if (threadAttributes == NULL)
				return B_BAD_VALUE;

			CreateThreadEvent* event
				= CreateThreadEvent::Create(*threadAttributes);
			if (event == NULL)
				return B_NO_MEMORY;

			timer->SetEvent(event);
			break;
		}

		default:
			return B_BAD_VALUE;
	}

	// add it to the team/thread
	TimerLocker timerLocker;
	timerLocker.Lock(team, thread);

	status_t error = thread != NULL
		? thread->AddUserTimer(timer) : team->AddUserTimer(timer);
	if (error != B_OK)
		return error;

	// set a signal event's user value
	if (signalEvent != NULL) {
		// If no sigevent structure was given, use the timer ID.
		union sigval signalValue = event.sigev_value;
		if (isDefaultEvent)
			signalValue.sival_int = timer->ID();

		signalEvent->SetUserValue(signalValue);
	}

	return timerDeleter.Detach()->ID();
}


/*!	Called when the CPU time clock of the given thread has been set.

	The caller must hold \c time_lock.

	\param thread The thread whose CPU time clock has been set.
	\param changedBy The value by which the CPU time clock has changed
		(new = old + changedBy).
*/
/**
 * @brief Notifies all ThreadTimeUserTimers of a step in a thread's CPU clock.
 *
 * Iterates the thread's CPU-time timer list and calls TimeWarped() on each,
 * so each timer can adjust its deadline accordingly.
 *
 * @param thread    Thread whose CPU clock was adjusted.
 * @param changedBy Signed delta (new_clock = old_clock + changedBy).
 *
 * @note The caller must hold @c time_lock.
 */
static void
thread_clock_changed(Thread* thread, bigtime_t changedBy)
{
	for (ThreadTimeUserTimerList::ConstIterator it
				= thread->CPUTimeUserTimerIterator();
			ThreadTimeUserTimer* timer = it.Next();) {
		timer->TimeWarped(changedBy);
	}
}


/*!	Called when the CPU time clock of the given team has been set.

	The caller must hold \c time_lock.

	\param team The team whose CPU time clock has been set.
	\param changedBy The value by which the CPU time clock has changed
		(new = old + changedBy).
*/
/**
 * @brief Notifies all TeamTimeUserTimers of a step in a team's CPU clock.
 *
 * Iterates the team's CPU-time timer list and calls TimeWarped() on each timer.
 *
 * @param team      Team whose CPU clock was adjusted.
 * @param changedBy Signed delta (new_clock = old_clock + changedBy).
 *
 * @note The caller must hold @c time_lock.
 */
static void
team_clock_changed(Team* team, bigtime_t changedBy)
{
	for (TeamTimeUserTimerList::ConstIterator it
				= team->CPUTimeUserTimerIterator();
			TeamTimeUserTimer* timer = it.Next();) {
		timer->TimeWarped(changedBy);
	}
}


// #pragma mark - kernel private


/*!	Creates the pre-defined user timers for the given thread.
	The thread may not have been added to its team yet, hence the team must be
	passed

	\param team The thread's (future) team.
	\param thread The thread whose pre-defined timers shall be created.
	\return \c B_OK, when everything when fine, another error code otherwise.
*/
/**
 * @brief Creates the pre-defined SIGALRM timer for a new thread.
 *
 * Creates a CLOCK_MONOTONIC one-shot timer (USER_TIMER_REAL_TIME_ID) that
 * sends SIGALRM to the thread when it fires. Called during thread creation
 * before the thread is added to its team.
 *
 * @param team   The thread's owning team (thread may not yet be in the team).
 * @param thread The thread for which to create the pre-defined timer.
 * @retval B_OK  Timer created successfully.
 * @retval <0    Error code from create_timer() (e.g. B_NO_MEMORY).
 */
status_t
user_timer_create_thread_timers(Team* team, Thread* thread)
{
	// create a real time user timer
	struct sigevent event = {0};
	event.sigev_notify = SIGEV_SIGNAL;
	event.sigev_signo = SIGALRM;

	int32 timerID = create_timer(CLOCK_MONOTONIC, USER_TIMER_REAL_TIME_ID,
		team, thread, USER_TIMER_SIGNAL_THREAD, event, NULL, true);
	if (timerID < 0)
		return timerID;

	return B_OK;
}


/*!	Creates the pre-defined user timers for the given team.

	\param team The team whose pre-defined timers shall be created.
	\return \c B_OK, when everything when fine, another error code otherwise.
*/
/**
 * @brief Creates the three pre-defined timers (SIGALRM, SIGPROF, SIGVTALRM) for a team.
 *
 * - USER_TIMER_REAL_TIME_ID        — CLOCK_MONOTONIC, delivers SIGALRM.
 * - USER_TIMER_TEAM_TOTAL_TIME_ID  — CLOCK_PROCESS_CPUTIME_ID, delivers SIGPROF.
 * - USER_TIMER_TEAM_USER_TIME_ID   — CLOCK_PROCESS_USER_CPUTIME_ID, delivers SIGVTALRM.
 *
 * @param team The team for which to create the pre-defined timers.
 * @retval B_OK  All timers created successfully.
 * @retval <0    Error code from create_timer() (e.g. B_NO_MEMORY).
 */
status_t
user_timer_create_team_timers(Team* team)
{
	// create a real time user timer
	struct sigevent event = {0};
	event.sigev_notify = SIGEV_SIGNAL;
	event.sigev_signo = SIGALRM;

	int32 timerID = create_timer(CLOCK_MONOTONIC, USER_TIMER_REAL_TIME_ID,
		team, NULL, 0, event, NULL, true);
	if (timerID < 0)
		return timerID;

	// create a total CPU time user timer
	event.sigev_notify = SIGEV_SIGNAL;
	event.sigev_signo = SIGPROF;

	timerID = create_timer(CLOCK_PROCESS_CPUTIME_ID,
		USER_TIMER_TEAM_TOTAL_TIME_ID, team, NULL, 0, event, NULL, true);
	if (timerID < 0)
		return timerID;

	// create a user CPU time user timer
	event.sigev_notify = SIGEV_SIGNAL;
	event.sigev_signo = SIGVTALRM;

	timerID = create_timer(CLOCK_PROCESS_USER_CPUTIME_ID,
		USER_TIMER_TEAM_USER_TIME_ID, team, NULL, 0, event, NULL, true);
	if (timerID < 0)
		return timerID;

	return B_OK;
}


/**
 * @brief Reads the current value of the specified POSIX clock.
 *
 * Supports CLOCK_MONOTONIC, CLOCK_REALTIME, CLOCK_THREAD_CPUTIME_ID,
 * CLOCK_PROCESS_CPUTIME_ID, CLOCK_PROCESS_USER_CPUTIME_ID, and encoded
 * team/thread CPU clocks (CPUCLOCK_TEAM / CPUCLOCK_THREAD bit patterns).
 *
 * @param clockID  The POSIX clock ID to read.
 * @param _time    Output: current clock value in microseconds.
 * @retval B_OK          Clock read successfully.
 * @retval B_BAD_VALUE   Unknown clock ID or referenced thread/team does not exist.
 * @retval B_NOT_ALLOWED Attempt to read the kernel team's CPU clock.
 */
status_t
user_timer_get_clock(clockid_t clockID, bigtime_t& _time)
{
	switch (clockID) {
		case CLOCK_MONOTONIC:
			_time = system_time();
			return B_OK;

		case CLOCK_REALTIME:
			_time = real_time_clock_usecs();
			return B_OK;

		case CLOCK_THREAD_CPUTIME_ID:
		{
			Thread* thread = thread_get_current_thread();
			InterruptsSpinLocker timeLocker(thread->time_lock);
			_time = thread->CPUTime(false);
			return B_OK;
		}

		case CLOCK_PROCESS_USER_CPUTIME_ID:
		{
			Team* team = thread_get_current_thread()->team;
			InterruptsSpinLocker timeLocker(team->time_lock);
			_time = team->UserCPUTime();
			return B_OK;
		}

		case CLOCK_PROCESS_CPUTIME_ID:
		default:
		{
			// get the ID of the target team (or the respective placeholder)
			team_id teamID = 0;
			if (clockID == CLOCK_PROCESS_CPUTIME_ID) {
				teamID = B_CURRENT_TEAM;
			} else if ((clockID & CPUCLOCK_SPECIAL) == CPUCLOCK_THREAD) {
				thread_id threadID = clockID & CPUCLOCK_ID_MASK;
				// get the thread
				Thread* thread = Thread::Get(threadID);
				if (thread == NULL)
					return B_BAD_VALUE;
				BReference<Thread> threadReference(thread, true);
				if (thread->team == team_get_kernel_team())
					return B_NOT_ALLOWED;
				// get the time
				InterruptsSpinLocker timeLocker(thread->time_lock);
				_time = thread->CPUTime(false);

				return B_OK;
			} else if ((clockID & CPUCLOCK_SPECIAL) == CPUCLOCK_TEAM) {
				teamID = clockID & CPUCLOCK_ID_MASK;
				if (teamID == team_get_kernel_team_id())
					return B_NOT_ALLOWED;
			} else {
				return B_BAD_VALUE;
			}

			// get the team
			Team* team = Team::Get(teamID);
			if (team == NULL)
				return B_BAD_VALUE;
			BReference<Team> teamReference(team, true);

			// get the time
			InterruptsSpinLocker timeLocker(team->time_lock);
			_time = team->CPUTime(false);

			return B_OK;
		}
	}
}


/**
 * @brief Called when the real-time clock is changed; adjusts all absolute real-time timers.
 *
 * Holds both @c sUserTimerLock and @c sAbsoluteRealTimeTimersLock, then calls
 * TimeWarped() on every timer in @c sAbsoluteRealTimeTimers so each one
 * recomputes its monotonic-clock deadline to match the new wall-clock value.
 */
void
user_timer_real_time_clock_changed()
{
	// we need to update all absolute real-time timers
	InterruptsWriteSequentialLocker locker(sUserTimerLock);
	SpinLocker globalListLocker(sAbsoluteRealTimeTimersLock);

	for (RealTimeUserTimerList::Iterator it
				= sAbsoluteRealTimeTimers.GetIterator();
			RealTimeUserTimer* timer = it.Next();) {
		timer->TimeWarped();
	}
}


/**
 * @brief Stops CPU-time timers when a thread is being preempted.
 *
 * Stops all ThreadTimeUserTimers for @p thread, and, if the next thread
 * belongs to a different team, updates all TeamTimeUserTimers for the
 * outgoing team (decrementing the running-thread count and cancelling the
 * kernel timer if it drops to zero).
 *
 * @param thread     The thread being preempted/unscheduled.
 * @param nextThread The thread that will run next, or NULL.
 */
void
user_timer_stop_cpu_timers(Thread* thread, Thread* nextThread)
{
	// stop thread timers
	for (ThreadTimeUserTimerList::ConstIterator it
				= thread->CPUTimeUserTimerIterator();
			ThreadTimeUserTimer* timer = it.Next();) {
		timer->Stop();
	}

	// update team timers
	if (nextThread == NULL || nextThread->team != thread->team) {
		for (TeamTimeUserTimerList::ConstIterator it
					= thread->team->CPUTimeUserTimerIterator();
				TeamTimeUserTimer* timer = it.Next();) {
			timer->Update(thread, thread);
		}
	}
}


/**
 * @brief Restarts CPU-time timers when a thread is scheduled onto a CPU.
 *
 * If the previously running thread belongs to a different team, updates all
 * TeamTimeUserTimers for the incoming team (incrementing the running-thread
 * count and arming the kernel timer if it was zero before). Then starts all
 * ThreadTimeUserTimers for @p thread.
 *
 * @param thread         The thread being scheduled onto a CPU.
 * @param previousThread The thread that ran before, or NULL.
 */
void
user_timer_continue_cpu_timers(Thread* thread, Thread* previousThread)
{
	// update team timers
	if (previousThread == NULL || previousThread->team != thread->team) {
		for (TeamTimeUserTimerList::ConstIterator it
					= thread->team->CPUTimeUserTimerIterator();
				TeamTimeUserTimer* timer = it.Next();) {
			timer->Update(NULL, thread);
		}
	}

	// start thread timers
	for (ThreadTimeUserTimerList::ConstIterator it
				= thread->CPUTimeUserTimerIterator();
			ThreadTimeUserTimer* timer = it.Next();) {
		timer->Start();
	}
}


/**
 * @brief Polls all user-mode CPU-time timers for @p team and fires any that have elapsed.
 *
 * Called from the scheduler after updating user-mode CPU accounting for @p team.
 * Iterates the team's TeamUserTimeUserTimerList and calls Check() on each timer.
 *
 * @param team The team whose user-CPU-time timers should be checked.
 */
void
user_timer_check_team_user_timers(Team* team)
{
	for (TeamUserTimeUserTimerList::ConstIterator it
				= team->UserTimeUserTimerIterator();
			TeamUserTimeUserTimer* timer = it.Next();) {
		timer->Check();
	}
}


// #pragma mark - syscalls


/**
 * @brief Syscall: reads the current value of a POSIX clock and copies it to userland.
 *
 * @param clockID  POSIX clock ID to query.
 * @param userTime Userland pointer to a bigtime_t that receives the result.
 * @retval B_OK          Clock value written to @p userTime.
 * @retval B_BAD_VALUE   Invalid clock ID or non-existent thread/team.
 * @retval B_BAD_ADDRESS @p userTime is NULL, not a user address, or the copy failed.
 */
status_t
_user_get_clock(clockid_t clockID, bigtime_t* userTime)
{
	// get the time
	bigtime_t time;
	status_t error = user_timer_get_clock(clockID, time);
	if (error != B_OK)
		return error;

	// copy the value back to userland
	if (userTime == NULL || !IS_USER_ADDRESS(userTime)
		|| user_memcpy(userTime, &time, sizeof(time)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Syscall: sets a POSIX clock to the specified value.
 *
 * Supports CLOCK_REALTIME (root only), CLOCK_THREAD_CPUTIME_ID, and encoded
 * team/thread CPU clocks. CLOCK_MONOTONIC and CLOCK_PROCESS_USER_CPUTIME_ID
 * are read-only and return B_BAD_VALUE.
 *
 * @param clockID POSIX clock ID to set.
 * @param time    New clock value in microseconds.
 * @retval B_OK          Clock updated successfully.
 * @retval B_BAD_VALUE   Read-only or unknown clock; invalid thread/team ID.
 * @retval B_NOT_ALLOWED Caller is not root (for CLOCK_REALTIME) or tried to
 *                       adjust the kernel team's clock.
 */
status_t
_user_set_clock(clockid_t clockID, bigtime_t time)
{
	switch (clockID) {
		case CLOCK_MONOTONIC:
			return B_BAD_VALUE;

		case CLOCK_REALTIME:
			// only root may set the time
			if (geteuid() != 0)
				return B_NOT_ALLOWED;

			set_real_time_clock_usecs(time);
			return B_OK;

		case CLOCK_THREAD_CPUTIME_ID:
		{
			Thread* thread = thread_get_current_thread();
			InterruptsSpinLocker timeLocker(thread->time_lock);
			bigtime_t diff = time - thread->CPUTime(false);
			thread->cpu_clock_offset += diff;

			thread_clock_changed(thread, diff);
			return B_OK;
		}

		case CLOCK_PROCESS_USER_CPUTIME_ID:
			// not supported -- this clock is an Haiku-internal extension
			return B_BAD_VALUE;

		case CLOCK_PROCESS_CPUTIME_ID:
		default:
		{
			// get the ID of the target team (or the respective placeholder)
			team_id teamID;
			if (clockID == CLOCK_PROCESS_CPUTIME_ID) {
				teamID = B_CURRENT_TEAM;
			} else if ((clockID & CPUCLOCK_THREAD) != 0) {
				thread_id threadID = clockID & CPUCLOCK_ID_MASK;
				if (threadID < 0)
					return B_BAD_VALUE;
				// get the thread
				Thread* thread = Thread::Get(threadID);
				if (thread == NULL)
					return B_BAD_VALUE;
				BReference<Thread> threadReference(thread, true);
				if (thread->team == team_get_kernel_team())
					return B_NOT_ALLOWED;

				// set the time offset
				InterruptsSpinLocker timeLocker(thread->time_lock);
				bigtime_t diff = time - thread->CPUTime(false);
				thread->cpu_clock_offset += diff;

				thread_clock_changed(thread, diff);
				return B_OK;
			} else {
				teamID = clockID & CPUCLOCK_ID_MASK;
				if (teamID < 0)
					return B_BAD_VALUE;
				if (teamID == team_get_kernel_team_id())
					return B_NOT_ALLOWED;
			}

			// get the team
			Team* team = Team::Get(teamID);
			if (team == NULL)
				return B_BAD_VALUE;
			BReference<Team> teamReference(team, true);

			// set the time offset
			InterruptsSpinLocker timeLocker(team->time_lock);
			bigtime_t diff = time - team->CPUTime(false);
			team->cpu_clock_offset += diff;

			team_clock_changed(team, diff);
			return B_OK;
		}
	}

	return B_OK;
}


/**
 * @brief Syscall: retrieves a CPU clock ID for a given team or thread.
 *
 * Encodes the team or thread ID together with the CPUCLOCK_TEAM or
 * CPUCLOCK_THREAD bit pattern and writes the resulting clockid_t to userland.
 *
 * @param id          The team or thread ID to look up.
 * @param which       TEAM_ID or THREAD_ID, selecting which namespace to use.
 * @param userclockID Userland pointer to receive the encoded clock ID.
 * @retval B_OK          Clock ID written to @p userclockID.
 * @retval B_BAD_VALUE   @p which is not TEAM_ID or THREAD_ID, or the
 *                       referenced entity does not exist.
 * @retval B_BAD_ADDRESS @p userclockID is not a valid user address.
 */
status_t
_user_get_cpuclockid(thread_id id, int32 which, clockid_t* userclockID)
{
	clockid_t clockID;
	if (which != TEAM_ID && which != THREAD_ID)
		return B_BAD_VALUE;

	if (which == TEAM_ID) {
		Team* team = Team::Get(id);
		if (team == NULL)
			return B_BAD_VALUE;
		clockID = id | CPUCLOCK_TEAM;
	} else if (which == THREAD_ID) {
		Thread* thread = Thread::Get(id);
		if (thread == NULL)
			return B_BAD_VALUE;
		clockID = id | CPUCLOCK_THREAD;
	}

	if (userclockID != NULL
		&& (!IS_USER_ADDRESS(userclockID)
			|| user_memcpy(userclockID, &clockID, sizeof(clockID)) != B_OK)) {
		return B_BAD_ADDRESS;
	}
	return B_OK;
}


/**
 * @brief Syscall: creates a new POSIX timer and returns its ID.
 *
 * Copies the sigevent structure (and optional thread-creation attributes) from
 * userland, resolves the target thread if @p threadID is non-negative, and
 * delegates to create_timer().
 *
 * @param clockID              POSIX clock ID for the new timer.
 * @param threadID             Thread to associate the timer with, or negative for team.
 * @param flags                USER_TIMER_SIGNAL_THREAD and similar modifier flags.
 * @param userEvent            Userland pointer to struct sigevent, or NULL for defaults.
 * @param userThreadAttributes Userland thread-creation attributes for SIGEV_THREAD, or NULL.
 * @retval >= 0        The new timer's ID on success.
 * @retval B_BAD_ADDRESS @p userEvent or @p userThreadAttributes is an invalid address.
 * @retval B_BAD_THREAD_ID @p threadID is non-negative but the thread does not exist.
 * @retval B_NO_MEMORY Allocation failed.
 * @retval B_BAD_VALUE Other invalid argument.
 */
int32
_user_create_timer(clockid_t clockID, thread_id threadID, uint32 flags,
	const struct sigevent* userEvent,
	const thread_creation_attributes* userThreadAttributes)
{
	// copy the sigevent structure from userland
	struct sigevent event = {0};
	if (userEvent != NULL) {
		if (!IS_USER_ADDRESS(userEvent)
			|| user_memcpy(&event, userEvent, sizeof(event)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	} else {
		// none given -- use defaults
		event.sigev_notify = SIGEV_SIGNAL;
		event.sigev_signo = SIGALRM;
	}

	// copy thread creation attributes from userland, if specified
	char nameBuffer[B_OS_NAME_LENGTH];
	ThreadCreationAttributes threadAttributes;
	if (event.sigev_notify == SIGEV_THREAD) {
		status_t error = threadAttributes.InitFromUserAttributes(
			userThreadAttributes, nameBuffer);
		if (error != B_OK)
			return error;
	}

	// get team and thread
	Team* team = thread_get_current_thread()->team;
	Thread* thread = NULL;
	if (threadID >= 0) {
		thread = Thread::GetAndLock(threadID);
		if (thread == NULL)
			return B_BAD_THREAD_ID;

		thread->Unlock();
	}
	BReference<Thread> threadReference(thread, true);

	// create the timer
	return create_timer(clockID, -1, team, thread, flags, event,
		userThreadAttributes != NULL ? &threadAttributes : NULL,
		userEvent == NULL);
}


/**
 * @brief Syscall: deletes a user-created POSIX timer.
 *
 * Only user-defined timers (ID >= USER_TIMER_FIRST_USER_DEFINED_ID) may be
 * deleted; attempting to delete a pre-defined timer returns B_BAD_VALUE.
 *
 * @param timerID  The ID of the timer to delete.
 * @param threadID Thread that owns the timer, or negative for a team timer.
 * @retval B_OK          Timer cancelled, removed, and deleted.
 * @retval B_BAD_VALUE   @p timerID is a pre-defined timer or does not exist.
 * @retval B_BAD_THREAD_ID @p threadID is non-negative but the thread does not exist.
 * @retval B_NOT_ALLOWED The thread does not belong to the current team.
 */
status_t
_user_delete_timer(int32 timerID, thread_id threadID)
{
	// can only delete user-defined timers
	if (timerID < USER_TIMER_FIRST_USER_DEFINED_ID)
		return B_BAD_VALUE;

	// get the timer
	TimerLocker timerLocker;
	UserTimer* timer;
	status_t error = timerLocker.LockAndGetTimer(threadID, timerID, timer);
	if (error != B_OK)
		return error;

	// cancel, remove, and delete it
	timer->Cancel();

	if (threadID >= 0)
		timerLocker.thread->RemoveUserTimer(timer);
	else
		timerLocker.team->RemoveUserTimer(timer);

	delete timer;

	return B_OK;
}


/**
 * @brief Syscall: queries the current state of a POSIX timer.
 *
 * Retrieves the remaining time, interval, and overrun count of the specified
 * timer, sanitises the remaining time to at least 1 µs if the timer has already
 * elapsed, and copies the result to userland.
 *
 * @param timerID  The timer ID to query.
 * @param threadID Thread that owns the timer, or negative for a team timer.
 * @param userInfo Userland pointer to a user_timer_info struct to receive the result.
 * @retval B_OK          Info written to @p userInfo.
 * @retval B_BAD_VALUE   Timer not found.
 * @retval B_BAD_THREAD_ID @p threadID is non-negative but does not exist.
 * @retval B_NOT_ALLOWED Thread does not belong to the current team.
 * @retval B_BAD_ADDRESS @p userInfo is NULL or not a valid user address.
 */
status_t
_user_get_timer(int32 timerID, thread_id threadID,
	struct user_timer_info* userInfo)
{
	// get the timer
	TimerLocker timerLocker;
	UserTimer* timer;
	status_t error = timerLocker.LockAndGetTimer(threadID, timerID, timer);
	if (error != B_OK)
		return error;

	// get the info
	user_timer_info info;
	timer->GetInfo(info.remaining_time, info.interval, info.overrun_count);

	// Sanitize remaining_time. If it's <= 0, we set it to 1, the least valid
	// value.
	if (info.remaining_time <= 0)
		info.remaining_time = 1;

	timerLocker.Unlock();

	// copy it back to userland
	if (userInfo != NULL
		&& (!IS_USER_ADDRESS(userInfo)
			|| user_memcpy(userInfo, &info, sizeof(info)) != B_OK)) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Syscall: arms or disarms a POSIX timer and optionally returns the previous state.
 *
 * Validates the start time and interval, looks up the timer, calls Schedule()
 * to (re)arm it, sanitises the old remaining time, and copies the previous
 * timer state to userland if @p userOldInfo is non-NULL.
 *
 * @param timerID     The timer ID to modify.
 * @param threadID    Thread that owns the timer, or negative for a team timer.
 * @param startTime   Time of first fire (absolute or relative, per @p flags); must be >= 0.
 * @param interval    Repeat interval in microseconds (0 for one-shot); must be >= 0.
 * @param flags       B_ABSOLUTE_TIMEOUT or B_RELATIVE_TIMEOUT.
 * @param userOldInfo Userland pointer for the previous timer state, or NULL.
 * @retval B_OK          Timer rescheduled successfully.
 * @retval B_BAD_VALUE   @p startTime or @p interval is negative, or timer not found.
 * @retval B_BAD_THREAD_ID @p threadID is non-negative but does not exist.
 * @retval B_NOT_ALLOWED Thread does not belong to the current team.
 * @retval B_BAD_ADDRESS @p userOldInfo is not a valid user address.
 */
status_t
_user_set_timer(int32 timerID, thread_id threadID, bigtime_t startTime,
	bigtime_t interval, uint32 flags, struct user_timer_info* userOldInfo)
{
	// check the values
	if (startTime < 0 || interval < 0)
		return B_BAD_VALUE;

	// get the timer
	TimerLocker timerLocker;
	UserTimer* timer;
	status_t error = timerLocker.LockAndGetTimer(threadID, timerID, timer);
	if (error != B_OK)
		return error;

	// schedule the timer
	user_timer_info oldInfo;
	timer->Schedule(startTime, interval, flags, oldInfo.remaining_time,
		oldInfo.interval);

	// Sanitize remaining_time. If it's <= 0, we set it to 1, the least valid
	// value.
	if (oldInfo.remaining_time <= 0)
		oldInfo.remaining_time = 1;

	timerLocker.Unlock();

	// copy back the old info
	if (userOldInfo != NULL
		&& (!IS_USER_ADDRESS(userOldInfo)
			|| user_memcpy(userOldInfo, &oldInfo, sizeof(oldInfo)) != B_OK)) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}
