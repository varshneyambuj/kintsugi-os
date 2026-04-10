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
 *   Copyright 2018, Jérôme Duval, jerome.duval@gmail.com.
 *   Copyright 2014, Paweł Dziepak, pdziepak@quarnos.org.
 *   Copyright 2011-2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2002-2009, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2002, Angelo Mottola, a.mottola@libero.it.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file signal.cpp
 * @brief POSIX signal delivery, masking, and handler dispatch.
 *
 * Implements signal sending (send_signal_to_thread, send_signal_to_team),
 * signal masking (sigprocmask), the signal handler dispatch path called on
 * return from kernel mode, and all signal-related syscalls. Handles both
 * real-time and standard signal queuing with proper priority ordering.
 *
 * @see thread.cpp, team.cpp, ksignal.h
 */


#include <ksignal.h>

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include <OS.h>
#include <KernelExport.h>

#include <cpu.h>
#include <core_dump.h>
#include <debug.h>
#include <kernel.h>
#include <kscheduler.h>
#include <sem.h>
#include <syscall_restart.h>
#include <syscall_utils.h>
#include <team.h>
#include <thread.h>
#include <tracing.h>
#include <user_debugger.h>
#include <user_thread.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>


//#define TRACE_SIGNAL
#ifdef TRACE_SIGNAL
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


#define BLOCKABLE_SIGNALS	\
	(~(KILL_SIGNALS | SIGNAL_TO_MASK(SIGSTOP)	\
	| SIGNAL_TO_MASK(SIGNAL_DEBUG_THREAD)	\
	| SIGNAL_TO_MASK(SIGNAL_CONTINUE_THREAD)	\
	| SIGNAL_TO_MASK(SIGNAL_CANCEL_THREAD)))
#define STOP_SIGNALS \
	(SIGNAL_TO_MASK(SIGSTOP) | SIGNAL_TO_MASK(SIGTSTP) \
	| SIGNAL_TO_MASK(SIGTTIN) | SIGNAL_TO_MASK(SIGTTOU))
#define CONTINUE_SIGNALS \
	(SIGNAL_TO_MASK(SIGCONT) | SIGNAL_TO_MASK(SIGNAL_CONTINUE_THREAD) \
	| SIGNAL_TO_MASK(SIGNAL_DEBUG_THREAD))
#define DEFAULT_IGNORE_SIGNALS \
	(SIGNAL_TO_MASK(SIGCHLD) | SIGNAL_TO_MASK(SIGWINCH) \
	| SIGNAL_TO_MASK(SIGCONT) \
	| SIGNAL_RANGE_TO_MASK(SIGNAL_REALTIME_MIN, SIGNAL_REALTIME_MAX))
#define NON_DEFERRABLE_SIGNALS	\
	(KILL_SIGNALS				\
	| SIGNAL_TO_MASK(SIGNAL_DEBUG_THREAD) \
	| SIGNAL_TO_MASK(SIGILL)	\
	| SIGNAL_TO_MASK(SIGFPE)	\
	| SIGNAL_TO_MASK(SIGSEGV))


static const struct {
	const char*	name;
	int32		priority;
} kSignalInfos[__MAX_SIGNO + 1] = {
	{"NONE",			-1},
	{"HUP",				0},
	{"INT",				0},
	{"QUIT",			0},
	{"ILL",				0},
	{"CHLD",			0},
	{"ABRT",			0},
	{"PIPE",			0},
	{"FPE",				0},
	{"KILL",			100},
	{"STOP",			0},
	{"SEGV",			0},
	{"CONT",			0},
	{"TSTP",			0},
	{"ALRM",			0},
	{"TERM",			0},
	{"TTIN",			0},
	{"TTOU",			0},
	{"USR1",			0},
	{"USR2",			0},
	{"WINCH",			0},
	{"KILLTHR",			100},
	{"TRAP",			0},
	{"POLL",			0},
	{"PROF",			0},
	{"SYS",				0},
	{"URG",				0},
	{"VTALRM",			0},
	{"XCPU",			0},
	{"XFSZ",			0},
	{"SIGBUS",			0},
	{"SIGRESERVED1",	0},
	{"SIGRESERVED2",	0},
	{"SIGRT1",			8},
	{"SIGRT2",			7},
	{"SIGRT3",			6},
	{"SIGRT4",			5},
	{"SIGRT5",			4},
	{"SIGRT6",			3},
	{"SIGRT7",			2},
	{"SIGRT8",			1},
	{"invalid 41",		0},
	{"invalid 42",		0},
	{"invalid 43",		0},
	{"invalid 44",		0},
	{"invalid 45",		0},
	{"invalid 46",		0},
	{"invalid 47",		0},
	{"invalid 48",		0},
	{"invalid 49",		0},
	{"invalid 50",		0},
	{"invalid 51",		0},
	{"invalid 52",		0},
	{"invalid 53",		0},
	{"invalid 54",		0},
	{"invalid 55",		0},
	{"invalid 56",		0},
	{"invalid 57",		0},
	{"invalid 58",		0},
	{"invalid 59",		0},
	{"invalid 60",		0},
	{"invalid 61",		0},
	{"invalid 62",		0},
	{"CANCEL_THREAD",	0},
	{"CONTINUE_THREAD",	0}	// priority must be <= that of SIGSTOP
};


/**
 * @brief Returns the human-readable name string for a signal number.
 *
 * @param number The signal number to look up.
 * @return A pointer to the signal name string, or "invalid" if the number
 *         exceeds @c __MAX_SIGNO.
 * @note The returned pointer is into a static read-only table; do not free it.
 */
static inline const char*
signal_name(uint32 number)
{
	return number <= __MAX_SIGNO ? kSignalInfos[number].name : "invalid";
}


// #pragma mark - SignalHandledCaller


struct SignalHandledCaller {
	SignalHandledCaller(Signal* signal)
		:
		fSignal(signal)
	{
	}

	~SignalHandledCaller()
	{
		Done();
	}

	void Done()
	{
		if (fSignal != NULL) {
			fSignal->Handled();
			fSignal = NULL;
		}
	}

private:
	Signal*	fSignal;
};


// #pragma mark - QueuedSignalsCounter


/*!	Creates a counter with the given limit.
	The limit defines the maximum the counter may reach. Since the
	BReferenceable's reference count is used, it is assumed that the owning
	team holds a reference and the reference count is one greater than the
	counter value.
	\param limit The maximum allowed value the counter may have. When
		\code < 0 \endcode, the value is not limited.
*/
QueuedSignalsCounter::QueuedSignalsCounter(int32 limit)
	:
	fLimit(limit)
{
}


/*!	Increments the counter, if the limit allows that.
	\return \c true, if incrementing the counter succeeded, \c false otherwise.
*/
bool
QueuedSignalsCounter::Increment()
{
	// no limit => no problem
	if (fLimit < 0) {
		AcquireReference();
		return true;
	}

	// Increment the reference count manually, so we can check atomically. We
	// compare the old value > fLimit, assuming that our (primary) owner has a
	// reference, we don't want to count.
	if (atomic_add(&fReferenceCount, 1) > fLimit) {
		ReleaseReference();
		return false;
	}

	return true;
}


// #pragma mark - Signal


/**
 * @brief Default constructor — creates an uninitialised, non-pending Signal.
 *
 * @note The signal has no associated QueuedSignalsCounter and is not pending.
 *       Callers must call SetTo() or use another constructor before sending.
 */
Signal::Signal()
	:
	fCounter(NULL),
	fPending(false)
{
}


/**
 * @brief Copy constructor — creates a non-pending copy of @a other.
 *
 * @param other The signal to copy fields from. The new signal shares no
 *              counter reference with @a other and starts in a non-pending
 *              state.
 * @note The fCounter of the copy is NULL; it is set by CreateQueuable() when
 *       the signal is placed on a queue.
 */
Signal::Signal(const Signal& other)
	:
	fCounter(NULL),
	fNumber(other.fNumber),
	fSignalCode(other.fSignalCode),
	fErrorCode(other.fErrorCode),
	fSendingProcess(other.fSendingProcess),
	fSendingUser(other.fSendingUser),
	fStatus(other.fStatus),
	fPollBand(other.fPollBand),
	fAddress(other.fAddress),
	fUserValue(other.fUserValue),
	fPending(false)
{
}


/**
 * @brief Constructs a fully specified Signal with sender information.
 *
 * @param number        The POSIX signal number (e.g. SIGKILL).
 * @param signalCode    The SI_* origin code (e.g. SI_USER, SI_QUEUE).
 * @param errorCode     An errno value associated with the signal, or 0.
 * @param sendingProcess The PID of the process originating the signal.
 * @note The effective UID is captured from the current thread's team at
 *       construction time.
 */
Signal::Signal(uint32 number, int32 signalCode, int32 errorCode,
	pid_t sendingProcess)
	:
	fCounter(NULL),
	fNumber(number),
	fSignalCode(signalCode),
	fErrorCode(errorCode),
	fSendingProcess(sendingProcess),
	fSendingUser(getuid()),
	fStatus(0),
	fPollBand(0),
	fAddress(NULL),
	fPending(false)
{
	fUserValue.sival_ptr = NULL;
}


/**
 * @brief Destructor — releases the team's QueuedSignalsCounter reference.
 *
 * @note If the signal was never associated with a counter (e.g. stack-allocated
 *       signals), this is a no-op.
 */
Signal::~Signal()
{
	if (fCounter != NULL)
		fCounter->ReleaseReference();
}


/*!	Creates a queuable clone of the given signal.
	Also enforces the current team's signal queuing limit.

	\param signal The signal to clone.
	\param queuingRequired If \c true, the function will return an error code
		when creating the clone fails for any reason. Otherwise, the function
		will set \a _signalToQueue to \c NULL, but still return \c B_OK.
	\param _signalToQueue Return parameter. Set to the clone of the signal.
	\return When \c queuingRequired is \c false, always \c B_OK. Otherwise
		\c B_OK, when creating the signal clone succeeds, another error code,
		when it fails.
*/
/*static*/ status_t
Signal::CreateQueuable(const Signal& signal, bool queuingRequired,
	Signal*& _signalToQueue)
{
	_signalToQueue = NULL;

	// If interrupts are disabled, we can't allocate a signal.
	if (!are_interrupts_enabled())
		return queuingRequired ? B_BAD_VALUE : B_OK;

	// increment the queued signals counter
	QueuedSignalsCounter* counter
		= thread_get_current_thread()->team->QueuedSignalsCounter();
	if (!counter->Increment())
		return queuingRequired ? EAGAIN : B_OK;

	// allocate the signal
	Signal* signalToQueue = new(std::nothrow) Signal(signal);
	if (signalToQueue == NULL) {
		counter->Decrement();
		return queuingRequired ? B_NO_MEMORY : B_OK;
	}

	signalToQueue->fCounter = counter;

	_signalToQueue = signalToQueue;
	return B_OK;
}

/**
 * @brief Reinitialises this signal as a plain SI_USER signal for @a number.
 *
 * @param number The signal number to assign.
 * @note All sender fields are taken from the current thread's team. This
 *       is used to cheaply (re-)initialise stack-allocated signal buffers
 *       inside PendingSignals::DequeueSignal().
 */
void
Signal::SetTo(uint32 number)
{
	Team* team = thread_get_current_thread()->team;

	fNumber = number;
	fSignalCode = SI_USER;
	fErrorCode = 0;
	fSendingProcess = team->id;
	fSendingUser = team->effective_uid;
	fStatus = 0;
	fPollBand = 0;
	fAddress = NULL;
	fUserValue.sival_ptr = NULL;
}


/**
 * @brief Returns the scheduling priority of this signal.
 *
 * @return The integer priority from the @c kSignalInfos table; higher values
 *         are dequeued before lower values during signal dispatch.
 */
int32
Signal::Priority() const
{
	return kSignalInfos[fNumber].priority;
}


/**
 * @brief Marks the signal as handled and releases the caller's reference.
 *
 * @note After this call the signal object may be destroyed; the caller must
 *       not access it again.
 */
void
Signal::Handled()
{
	ReleaseReference();
}


/**
 * @brief Called by BReferenceable when the last reference is dropped.
 *
 * Deletes the signal object. If interrupts are disabled the deletion is
 * deferred to a context where memory allocation is safe.
 */
void
Signal::LastReferenceReleased()
{
	if (are_interrupts_enabled())
		delete this;
	else
		deferred_delete(this);
}


// #pragma mark - PendingSignals


/**
 * @brief Default constructor — creates an empty pending-signal set.
 */
PendingSignals::PendingSignals()
	:
	fQueuedSignalsMask(0),
	fUnqueuedSignalsMask(0)
{
}


/**
 * @brief Destructor — discards all pending signals and releases their references.
 */
PendingSignals::~PendingSignals()
{
	Clear();
}


/*!	Of the signals in \a nonBlocked returns the priority of that with the
	highest priority.
	\param nonBlocked The mask with the non-blocked signals.
	\return The priority of the highest priority non-blocked signal, or, if all
		signals are blocked, \c -1.
*/
int32
PendingSignals::HighestSignalPriority(sigset_t nonBlocked) const
{
	Signal* queuedSignal;
	int32 unqueuedSignal;
	return _GetHighestPrioritySignal(nonBlocked, queuedSignal, unqueuedSignal);
}


/**
 * @brief Removes and releases all pending signals from both the queue and
 *        the unqueued mask.
 *
 * @note The caller must hold the owning team's @c signal_lock.
 */
void
PendingSignals::Clear()
{
	// release references of all queued signals
	while (Signal* signal = fQueuedSignals.RemoveHead())
		signal->Handled();

	fQueuedSignalsMask = 0;
	fUnqueuedSignalsMask = 0;
}


/*!	Adds a signal.
	Takes over the reference to the signal from the caller.
*/
void
PendingSignals::AddSignal(Signal* signal)
{
	// queue according to priority
	int32 priority = signal->Priority();
	Signal* otherSignal = NULL;
	for (SignalList::Iterator it = fQueuedSignals.GetIterator();
			(otherSignal = it.Next()) != NULL;) {
		if (priority > otherSignal->Priority())
			break;
	}

	fQueuedSignals.InsertBefore(otherSignal, signal);
	signal->SetPending(true);

	fQueuedSignalsMask |= SIGNAL_TO_MASK(signal->Number());
}


/**
 * @brief Removes a specific queued signal and updates the queued mask.
 *
 * @param signal The signal to remove. Must currently be in the queue.
 * @note The caller retains the reference that was on the signal; this function
 *       does not call Handled().
 */
void
PendingSignals::RemoveSignal(Signal* signal)
{
	signal->SetPending(false);
	fQueuedSignals.Remove(signal);
	_UpdateQueuedSignalMask();
}


/**
 * @brief Removes all pending signals whose numbers are set in @a mask.
 *
 * Releases references of removed queued signals and clears the corresponding
 * bits in both the queued and unqueued masks.
 *
 * @param mask Bitmask of signal numbers to remove (use SIGNAL_TO_MASK()).
 * @note The caller must hold the owning team's @c signal_lock.
 */
void
PendingSignals::RemoveSignals(sigset_t mask)
{
	// remove from queued signals
	if ((fQueuedSignalsMask & mask) != 0) {
		for (SignalList::Iterator it = fQueuedSignals.GetIterator();
				Signal* signal = it.Next();) {
			// remove signal, if in mask
			if ((SIGNAL_TO_MASK(signal->Number()) & mask) != 0) {
				it.Remove();
				signal->SetPending(false);
				signal->Handled();
			}
		}

		fQueuedSignalsMask &= ~mask;
	}

	// remove from unqueued signals
	fUnqueuedSignalsMask &= ~mask;
}


/*!	Removes and returns a signal in \a nonBlocked that has the highest priority.
	The caller gets a reference to the returned signal, if any.
	\param nonBlocked The mask of non-blocked signals.
	\param buffer If the signal is not queued this buffer is returned. In this
		case the method acquires a reference to \a buffer, so that the caller
		gets a reference also in this case.
	\return The removed signal or \c NULL, if all signals are blocked.
*/
Signal*
PendingSignals::DequeueSignal(sigset_t nonBlocked, Signal& buffer)
{
	// find the signal with the highest priority
	Signal* queuedSignal;
	int32 unqueuedSignal;
	if (_GetHighestPrioritySignal(nonBlocked, queuedSignal, unqueuedSignal) < 0)
		return NULL;

	// if it is a queued signal, dequeue it
	if (queuedSignal != NULL) {
		fQueuedSignals.Remove(queuedSignal);
		queuedSignal->SetPending(false);
		_UpdateQueuedSignalMask();
		return queuedSignal;
	}

	// it is unqueued -- remove from mask
	fUnqueuedSignalsMask &= ~SIGNAL_TO_MASK(unqueuedSignal);

	// init buffer
	buffer.SetTo(unqueuedSignal);
	buffer.AcquireReference();
	return &buffer;
}


/*!	Of the signals not it \a blocked returns the priority of that with the
	highest priority.
	\param blocked The mask with the non-blocked signals.
	\param _queuedSignal If the found signal is a queued signal, the variable
		will be set to that signal, otherwise to \c NULL.
	\param _unqueuedSignal If the found signal is an unqueued signal, the
		variable is set to that signal's number, otherwise to \c -1.
	\return The priority of the highest priority non-blocked signal, or, if all
		signals are blocked, \c -1.
*/
int32
PendingSignals::_GetHighestPrioritySignal(sigset_t nonBlocked,
	Signal*& _queuedSignal, int32& _unqueuedSignal) const
{
	// check queued signals
	Signal* queuedSignal = NULL;
	int32 queuedPriority = -1;

	if ((fQueuedSignalsMask & nonBlocked) != 0) {
		for (SignalList::ConstIterator it = fQueuedSignals.GetIterator();
				Signal* signal = it.Next();) {
			if ((SIGNAL_TO_MASK(signal->Number()) & nonBlocked) != 0) {
				queuedPriority = signal->Priority();
				queuedSignal = signal;
				break;
			}
		}
	}

	// check unqueued signals
	int32 unqueuedSignal = -1;
	int32 unqueuedPriority = -1;

	sigset_t unqueuedSignals = fUnqueuedSignalsMask & nonBlocked;
	if (unqueuedSignals != 0) {
		int32 signal = 1;
		while (unqueuedSignals != 0) {
			sigset_t mask = SIGNAL_TO_MASK(signal);
			if ((unqueuedSignals & mask) != 0) {
				int32 priority = kSignalInfos[signal].priority;
				if (priority > unqueuedPriority) {
					unqueuedSignal = signal;
					unqueuedPriority = priority;
				}
				unqueuedSignals &= ~mask;
			}

			signal++;
		}
	}

	// Return found queued or unqueued signal, whichever has the higher
	// priority.
	if (queuedPriority >= unqueuedPriority) {
		_queuedSignal = queuedSignal;
		_unqueuedSignal = -1;
		return queuedPriority;
	}

	_queuedSignal = NULL;
	_unqueuedSignal = unqueuedSignal;
	return unqueuedPriority;
}


/**
 * @brief Rebuilds fQueuedSignalsMask by iterating the live queue.
 *
 * Called after one or more signals have been removed from the queue to keep
 * the mask accurate.
 *
 * @note The caller must hold the owning team's @c signal_lock.
 */
void
PendingSignals::_UpdateQueuedSignalMask()
{
	sigset_t mask = 0;
	for (SignalList::Iterator it = fQueuedSignals.GetIterator();
			Signal* signal = it.Next();) {
		mask |= SIGNAL_TO_MASK(signal->Number());
	}

	fQueuedSignalsMask = mask;
}


// #pragma mark - signal tracing


#if SIGNAL_TRACING

namespace SignalTracing {


class HandleSignal : public AbstractTraceEntry {
	public:
		HandleSignal(uint32 signal)
			:
			fSignal(signal)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("signal handle:  %" B_PRIu32 " (%s)" , fSignal,
				signal_name(fSignal));
		}

	private:
		uint32		fSignal;
};


class ExecuteSignalHandler : public AbstractTraceEntry {
	public:
		ExecuteSignalHandler(uint32 signal, struct sigaction* handler)
			:
			fSignal(signal),
			fHandler((void*)handler->sa_handler)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("signal exec handler: signal: %" B_PRIu32 " (%s), "
				"handler: %p", fSignal, signal_name(fSignal), fHandler);
		}

	private:
		uint32	fSignal;
		void*	fHandler;
};


class SendSignal : public AbstractTraceEntry {
	public:
		SendSignal(pid_t target, uint32 signal, uint32 flags)
			:
			fTarget(target),
			fSignal(signal),
			fFlags(flags)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("signal send: target: %" B_PRId32 ", signal: %" B_PRIu32
				" (%s), flags: %#" B_PRIx32, fTarget, fSignal,
				signal_name(fSignal), fFlags);
		}

	private:
		pid_t	fTarget;
		uint32	fSignal;
		uint32	fFlags;
};


class SigAction : public AbstractTraceEntry {
	public:
		SigAction(uint32 signal, const struct sigaction* act)
			:
			fSignal(signal),
			fAction(*act)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("signal action: signal: %" B_PRIu32 " (%s), "
				"action: {handler: %p, flags: %#x, mask: %#" B_PRIx64 "}",
				fSignal, signal_name(fSignal), fAction.sa_handler,
				fAction.sa_flags, (uint64)fAction.sa_mask);
		}

	private:
		uint32				fSignal;
		struct sigaction	fAction;
};


class SigProcMask : public AbstractTraceEntry {
	public:
		SigProcMask(int how, sigset_t mask)
			:
			fHow(how),
			fMask(mask),
			fOldMask(thread_get_current_thread()->sig_block_mask)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			const char* how = "invalid";
			switch (fHow) {
				case SIG_BLOCK:
					how = "block";
					break;
				case SIG_UNBLOCK:
					how = "unblock";
					break;
				case SIG_SETMASK:
					how = "set";
					break;
			}

			out.Print("signal proc mask: %s 0x%llx, old mask: 0x%llx", how,
				(long long)fMask, (long long)fOldMask);
		}

	private:
		int			fHow;
		sigset_t	fMask;
		sigset_t	fOldMask;
};


class SigSuspend : public AbstractTraceEntry {
	public:
		SigSuspend(sigset_t mask)
			:
			fMask(mask),
			fOldMask(thread_get_current_thread()->sig_block_mask)
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("signal suspend: %#llx, old mask: %#llx",
				(long long)fMask, (long long)fOldMask);
		}

	private:
		sigset_t	fMask;
		sigset_t	fOldMask;
};


class SigSuspendDone : public AbstractTraceEntry {
	public:
		SigSuspendDone()
			:
			fSignals(thread_get_current_thread()->ThreadPendingSignals())
		{
			Initialized();
		}

		virtual void AddDump(TraceOutput& out)
		{
			out.Print("signal suspend done: %#" B_PRIx32, fSignals);
		}

	private:
		uint32		fSignals;
};

}	// namespace SignalTracing

#	define T(x)	new(std::nothrow) SignalTracing::x

#else
#	define T(x)
#endif	// SIGNAL_TRACING


// #pragma mark -


/**
 * @brief Updates @a thread's THREAD_FLAGS_SIGNALS_PENDING flag based on its
 *        current signal block mask.
 *
 * @param thread The thread whose flags are to be refreshed.
 * @note The caller must hold @c team->signal_lock.
 */
static void
update_thread_signals_flag(Thread* thread)
{
	sigset_t mask = ~thread->sig_block_mask;
	if ((thread->AllPendingSignals() & mask) != 0)
		atomic_or(&thread->flags, THREAD_FLAGS_SIGNALS_PENDING);
	else
		atomic_and(&thread->flags, ~THREAD_FLAGS_SIGNALS_PENDING);
}


/**
 * @brief Updates the current thread's THREAD_FLAGS_SIGNALS_PENDING flag.
 *
 * Convenience wrapper around update_thread_signals_flag() for the common
 * case where the thread to update is the running thread.
 *
 * @note The caller must hold @c team->signal_lock.
 */
static void
update_current_thread_signals_flag()
{
	update_thread_signals_flag(thread_get_current_thread());
}


/**
 * @brief Updates the THREAD_FLAGS_SIGNALS_PENDING flag for every thread in
 *        @a team.
 *
 * @param team The team whose threads are to be updated.
 * @note The caller must hold @c signal_lock.
 */
static void
update_team_threads_signal_flag(Team* team)
{
	for (Thread* thread = team->thread_list.First(); thread != NULL;
			thread = team->thread_list.GetNext(thread)) {
		update_thread_signals_flag(thread);
	}
}


/**
 * @brief Notifies the user-space debugger about a signal that is about to be
 *        handled and returns whether the signal should proceed.
 *
 * Checks per-thread debugger ignore masks first, then delivers a
 * @c B_THREAD_DEBUG_HANDLE_SIGNAL event to the debugger if one is attached.
 *
 * @param thread  The current (receiving) thread.
 * @param signal  The signal being dispatched.
 * @param handler The installed @c struct sigaction for this signal.
 * @param deadly  @c true if the signal would terminate the process.
 * @return @c true if the signal should be handled normally; @c false if the
 *         debugger has consumed the signal and it should be ignored.
 * @note The caller must not hold any locks; this function may block.
 */
static bool
notify_debugger(Thread* thread, Signal* signal, struct sigaction& handler,
	bool deadly)
{
	uint64 signalMask = SIGNAL_TO_MASK(signal->Number());

	// first check the ignore signal masks the debugger specified for the thread
	InterruptsSpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	if ((thread->debug_info.ignore_signals_once & signalMask) != 0) {
		thread->debug_info.ignore_signals_once &= ~signalMask;
		return true;
	}

	if ((thread->debug_info.ignore_signals & signalMask) != 0)
		return true;

	threadDebugInfoLocker.Unlock();

	siginfo_t info;
	info.si_signo = signal->Number();
	info.si_code = signal->SignalCode();
	info.si_errno = signal->ErrorCode();
	info.si_pid = signal->SendingProcess();
	info.si_uid = signal->SendingUser();
	info.si_addr = signal->Address();
	info.si_status = signal->Status();
	info.si_band = signal->PollBand();
	info.si_value = signal->UserValue();

	// deliver the event
	return user_debug_handle_signal(signal->Number(), &handler, &info, deadly);
}


/**
 * @brief Removes and returns the highest-priority non-blocked pending signal
 *        from @a thread or its team, updating the relevant flags afterwards.
 *
 * The highest-priority signal is chosen between those pending on the thread
 * and those pending on the team. The caller receives a reference to the
 * returned signal.
 *
 * @param thread     The thread for which to dequeue a signal.
 * @param nonBlocked Mask of signals that are not blocked by the thread.
 * @param buffer     Stack-allocated fallback Signal used for unqueued signals;
 *                   a reference is acquired on it in that case.
 * @return The dequeued Signal (with a reference), or @c NULL if all pending
 *         signals are blocked.
 * @note The caller must hold @c team->signal_lock.
 */
static Signal*
dequeue_thread_or_team_signal(Thread* thread, sigset_t nonBlocked,
	Signal& buffer)
{
	Team* team = thread->team;
	Signal* signal;
	if (team->HighestPendingSignalPriority(nonBlocked)
			> thread->HighestPendingSignalPriority(nonBlocked)) {
		signal = team->DequeuePendingSignal(nonBlocked, buffer);
		update_team_threads_signal_flag(team);
	} else {
		signal = thread->DequeuePendingSignal(nonBlocked, buffer);
		update_thread_signals_flag(thread);
	}

	return signal;
}


/**
 * @brief Constructs the architecture-specific signal handler stack frame for
 *        @a thread and prepares it to execute the user-space handler.
 *
 * Fills a @c signal_frame_data structure with signal info, ucontext, and
 * handler details, then delegates to arch_setup_signal_frame() to push the
 * frame onto the user stack and redirect execution.
 *
 * @param thread      The thread that will execute the signal handler.
 * @param action      The @c struct sigaction describing the handler.
 * @param signal      The signal being delivered.
 * @param signalMask  The signal mask to restore when the handler returns.
 * @retval B_OK       Frame set up successfully.
 * @note This function modifies the thread's saved register state so that it
 *       returns to the signal handler rather than to the interrupted code.
 */
static status_t
setup_signal_frame(Thread* thread, struct sigaction* action, Signal* signal,
	sigset_t signalMask)
{
	// prepare the data, we need to copy onto the user stack
	signal_frame_data frameData;

	// signal info
	frameData.info.si_signo = signal->Number();
	frameData.info.si_code = signal->SignalCode();
	frameData.info.si_errno = signal->ErrorCode();
	frameData.info.si_pid = signal->SendingProcess();
	frameData.info.si_uid = signal->SendingUser();
	frameData.info.si_addr = signal->Address();
	frameData.info.si_status = signal->Status();
	frameData.info.si_band = signal->PollBand();
	frameData.info.si_value = signal->UserValue();

	// context
	frameData.context.uc_link = thread->user_signal_context;
	frameData.context.uc_sigmask = signalMask;
	// uc_stack and uc_mcontext are filled in by the architecture specific code.

	// user data
	frameData.user_data = action->sa_userdata;

	// handler function
	frameData.siginfo_handler = (action->sa_flags & SA_SIGINFO) != 0;
	frameData.handler = frameData.siginfo_handler
		? (void*)action->sa_sigaction : (void*)action->sa_handler;

	// thread flags -- save the and clear the thread's syscall restart related
	// flags
	frameData.thread_flags = atomic_and(&thread->flags,
		~(THREAD_FLAGS_RESTART_SYSCALL | THREAD_FLAGS_64_BIT_SYSCALL_RETURN));

	// syscall restart related fields
	memcpy(frameData.syscall_restart_parameters,
		thread->syscall_restart.parameters,
		sizeof(frameData.syscall_restart_parameters));

	// commpage address
	frameData.commpage_address = thread->team->commpage_address;

	// syscall_restart_return_value is filled in by the architecture specific
	// code.

	return arch_setup_signal_frame(thread, action, &frameData);
}


/**
 * @brief Dequeues and processes all non-blocked pending signals for the
 *        current thread.
 *
 * This is the central signal dispatch function. It is called from the
 * kernel-to-user return path. For each non-blocked pending signal it:
 * - Handles kernel-internal signals (SIGKILL, SIGSTOP, SIGCONT, etc.)
 *   directly without user-space involvement.
 * - Notifies any attached debugger.
 * - Sets up a user-space signal handler frame via setup_signal_frame()
 *   and returns, causing execution to divert to the handler.
 * The function may call thread_exit() (never returning) for deadly signals.
 *
 * @param thread The current thread (must equal thread_get_current_thread()).
 * @note Interrupts must be enabled on entry.
 * @note The function acquires and releases team lock and signal_lock
 *       internally as needed.
 */
void
handle_signals(Thread* thread)
{
	Team* team = thread->team;

	TeamLocker teamLocker(team);
	InterruptsSpinLocker locker(thread->team->signal_lock);

	// If userland requested to defer signals, we check now, if this is
	// possible.
	sigset_t nonBlockedMask = ~thread->sig_block_mask;
	sigset_t signalMask = thread->AllPendingSignals() & nonBlockedMask;

	arch_cpu_enable_user_access();
	if (thread->user_thread->defer_signals > 0
		&& (signalMask & NON_DEFERRABLE_SIGNALS) == 0
		&& thread->sigsuspend_original_unblocked_mask == 0) {
		thread->user_thread->pending_signals = signalMask;
		arch_cpu_disable_user_access();
		return;
	}

	thread->user_thread->pending_signals = 0;
	arch_cpu_disable_user_access();

	// determine syscall restart behavior
	uint32 restartFlags = atomic_and(&thread->flags,
		~THREAD_FLAGS_DONT_RESTART_SYSCALL);
	bool alwaysRestart
		= (restartFlags & THREAD_FLAGS_ALWAYS_RESTART_SYSCALL) != 0;
	bool restart = alwaysRestart
		|| (restartFlags & THREAD_FLAGS_DONT_RESTART_SYSCALL) == 0;

	// Loop until we've handled all signals.
	bool initialIteration = true;
	while (true) {
		if (initialIteration) {
			initialIteration = false;
		} else {
			teamLocker.Lock();
			locker.Lock();

			signalMask = thread->AllPendingSignals() & nonBlockedMask;
		}

		// Unless SIGKILL[THR] are pending, check, if the thread shall stop for
		// a core dump or for debugging.
		if ((signalMask & KILL_SIGNALS) == 0) {
			if ((atomic_get(&thread->flags) & THREAD_FLAGS_TRAP_FOR_CORE_DUMP)
					!= 0) {
				locker.Unlock();
				teamLocker.Unlock();

				core_dump_trap_thread();
				continue;
			}

			if ((atomic_get(&thread->debug_info.flags) & B_THREAD_DEBUG_STOP)
					!= 0) {
				locker.Unlock();
				teamLocker.Unlock();

				user_debug_stop_thread();
				continue;
			}
		}

		// We're done, if there aren't any pending signals anymore.
		if ((signalMask & nonBlockedMask) == 0)
			break;

		// get pending non-blocked thread or team signal with the highest
		// priority
		Signal stackSignal;
		Signal* signal = dequeue_thread_or_team_signal(thread, nonBlockedMask,
			stackSignal);
		ASSERT(signal != NULL);
		SignalHandledCaller signalHandledCaller(signal);

		locker.Unlock();

		// get the action for the signal
		struct sigaction handler;
		if (signal->Number() <= MAX_SIGNAL_NUMBER) {
			handler = team->SignalActionFor(signal->Number());
		} else {
			handler.sa_handler = SIG_DFL;
			handler.sa_flags = 0;
		}

		if ((handler.sa_flags & SA_ONESHOT) != 0
			&& handler.sa_handler != SIG_IGN && handler.sa_handler != SIG_DFL) {
			team->SignalActionFor(signal->Number()).sa_handler = SIG_DFL;
		}

		T(HandleSignal(signal->Number()));

		teamLocker.Unlock();

		// debug the signal, if a debugger is installed and the signal debugging
		// flag is set
		bool debugSignal = (~atomic_get(&team->debug_info.flags)
				& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_SIGNALS))
			== 0;

		// handle the signal
		TRACE(("Thread %" B_PRId32 " received signal %s\n", thread->id,
			kSignalInfos[signal->Number()].name));

		if (handler.sa_handler == SIG_IGN) {
			// signal is to be ignored
			// TODO: apply zombie cleaning on SIGCHLD

			// notify the debugger
			if (debugSignal)
				notify_debugger(thread, signal, handler, false);
			continue;
		} else if (handler.sa_handler == SIG_DFL) {
			// default signal behaviour

			// realtime signals are ignored by default
			if (signal->Number() >= SIGNAL_REALTIME_MIN
				&& signal->Number() <= SIGNAL_REALTIME_MAX) {
				// notify the debugger
				if (debugSignal)
					notify_debugger(thread, signal, handler, false);
				continue;
			}

			bool killTeam = false;
			switch (signal->Number()) {
				case SIGCHLD:
				case SIGWINCH:
				case SIGURG:
					// notify the debugger
					if (debugSignal)
						notify_debugger(thread, signal, handler, false);
					continue;

				case SIGNAL_DEBUG_THREAD:
					// ignore -- used together with B_THREAD_DEBUG_STOP, which
					// is handled above
					continue;

				case SIGNAL_CANCEL_THREAD:
					// set up the signal handler
					handler.sa_handler = thread->cancel_function;
					handler.sa_flags = 0;
					handler.sa_mask = 0;
					handler.sa_userdata = NULL;

					restart = false;
						// we always want to interrupt
					break;

				case SIGNAL_CONTINUE_THREAD:
					// prevent syscall restart, but otherwise ignore
					restart = false;
					atomic_and(&thread->flags, ~THREAD_FLAGS_RESTART_SYSCALL);
					continue;

				case SIGCONT:
					// notify the debugger
					if (debugSignal
						&& !notify_debugger(thread, signal, handler, false))
						continue;

					// notify threads waiting for team state changes
					if (thread == team->main_thread) {
						team->LockTeamAndParent(false);

						team_set_job_control_state(team,
							JOB_CONTROL_STATE_CONTINUED, signal);

						team->UnlockTeamAndParent();

						// The standard states that the system *may* send a
						// SIGCHLD when a child is continued. I haven't found
						// a good reason why we would want to, though.
					}
					continue;

				case SIGSTOP:
				case SIGTSTP:
				case SIGTTIN:
				case SIGTTOU:
				{
					// notify the debugger
					if (debugSignal
						&& !notify_debugger(thread, signal, handler, false))
						continue;

					// The terminal-sent stop signals are allowed to stop the
					// process only, if it doesn't belong to an orphaned process
					// group. Otherwise the signal must be discarded.
					team->LockProcessGroup();
					AutoLocker<ProcessGroup> groupLocker(team->group, true);
					if (signal->Number() != SIGSTOP
						&& team->group->IsOrphaned()) {
						continue;
					}

					// notify threads waiting for team state changes
					if (thread == team->main_thread) {
						team->LockTeamAndParent(false);

						team_set_job_control_state(team,
							JOB_CONTROL_STATE_STOPPED, signal);

						// send a SIGCHLD to the parent (if it does have
						// SA_NOCLDSTOP defined)
						Team* parentTeam = team->parent;

						struct sigaction& parentHandler
							= parentTeam->SignalActionFor(SIGCHLD);
						if ((parentHandler.sa_flags & SA_NOCLDSTOP) == 0) {
							Signal childSignal(SIGCHLD, CLD_STOPPED, B_OK,
								team->id);
							childSignal.SetStatus(signal->Number());
							childSignal.SetSendingUser(signal->SendingUser());
							send_signal_to_team(parentTeam, childSignal, 0);
						}

						team->UnlockTeamAndParent();
					}

					groupLocker.Unlock();

					// Suspend the thread, unless there's already a signal to
					// continue or kill pending.
					locker.Lock();
					bool resume = (thread->AllPendingSignals()
								& (CONTINUE_SIGNALS | KILL_SIGNALS)) != 0;
					locker.Unlock();

					if (!resume)
						thread_suspend();

					continue;
				}

				case SIGSEGV:
				case SIGBUS:
				case SIGFPE:
				case SIGILL:
				case SIGTRAP:
				case SIGABRT:
				case SIGKILL:
				case SIGQUIT:
				case SIGPOLL:
				case SIGPROF:
				case SIGSYS:
				case SIGVTALRM:
				case SIGXCPU:
				case SIGXFSZ:
				default:
					TRACE(("Shutting down team %" B_PRId32 " due to signal %"
						B_PRIu32 " received in thread %" B_PRIu32 " \n",
						team->id, signal->Number(), thread->id));

					// This signal kills the team regardless which thread
					// received it.
					killTeam = true;

					// fall through
				case SIGKILLTHR:
					// notify the debugger
					if (debugSignal && signal->Number() != SIGKILL
						&& signal->Number() != SIGKILLTHR
						&& !notify_debugger(thread, signal, handler, true)) {
						continue;
					}

					if (killTeam || thread == team->main_thread) {
						// The signal is terminal for the team or the thread is
						// the main thread. In either case the team is going
						// down. Set its exit status, if that didn't happen yet.
						teamLocker.Lock();

						if (!team->exit.initialized) {
							team->exit.reason = CLD_KILLED;
							team->exit.signal = signal->Number();
							team->exit.signaling_user = signal->SendingUser();
							team->exit.status = 0;
							team->exit.initialized = true;
						}

						teamLocker.Unlock();

						// If this is not the main thread, send it a SIGKILLTHR
						// so that the team terminates.
						if (thread != team->main_thread) {
							Signal childSignal(SIGKILLTHR, SI_USER, B_OK,
								team->id);
							send_signal_to_thread_id(team->id, childSignal, 0);
						}
					}

					// explicitly get rid of the signal reference, since
					// thread_exit() won't return
					signalHandledCaller.Done();

					thread_exit();
						// won't return
			}
		}

		// User defined signal handler

		// notify the debugger
		if (debugSignal && !notify_debugger(thread, signal, handler, false))
			continue;

		if (!restart
				|| (!alwaysRestart && (handler.sa_flags & SA_RESTART) == 0)) {
			atomic_and(&thread->flags, ~THREAD_FLAGS_RESTART_SYSCALL);
		}

		T(ExecuteSignalHandler(signal->Number(), &handler));

		TRACE(("### Setting up custom signal handler frame...\n"));

		// save the old block mask -- we may need to adjust it for the handler
		locker.Lock();

		sigset_t oldBlockMask = thread->sigsuspend_original_unblocked_mask != 0
			? ~thread->sigsuspend_original_unblocked_mask
			: thread->sig_block_mask;

		// Update the block mask while the signal handler is running -- it
		// will be automatically restored when the signal frame is left.
		thread->sig_block_mask |= handler.sa_mask & BLOCKABLE_SIGNALS;

		if ((handler.sa_flags & SA_NOMASK) == 0) {
			thread->sig_block_mask
				|= SIGNAL_TO_MASK(signal->Number()) & BLOCKABLE_SIGNALS;
		}

		update_current_thread_signals_flag();

		locker.Unlock();

		setup_signal_frame(thread, &handler, signal, oldBlockMask);

		// Reset sigsuspend_original_unblocked_mask. It would have been set by
		// sigsuspend_internal(). In that case, above we set oldBlockMask
		// accordingly so that after the handler returns the thread's signal
		// mask is reset.
		thread->sigsuspend_original_unblocked_mask = 0;

		return;
	}

	// We have not handled any signal (respectively only ignored ones).

	// If sigsuspend_original_unblocked_mask is non-null, we came from a
	// sigsuspend_internal(). Not having handled any signal, we should restart
	// the syscall.
	if (thread->sigsuspend_original_unblocked_mask != 0) {
		restart = true;
		atomic_or(&thread->flags, THREAD_FLAGS_RESTART_SYSCALL);
	} else if (!restart) {
		// clear syscall restart thread flag, if we're not supposed to restart
		// the syscall
		atomic_and(&thread->flags, ~THREAD_FLAGS_RESTART_SYSCALL);
	}
}


/**
 * @brief Returns whether the given signal is blocked by every thread in
 *        @a team (i.e. the signal is team-wide blocked).
 *
 * @param team   The team to check.
 * @param signal The signal number to test.
 * @return @c true if every thread's @c sig_block_mask includes @a signal;
 *         @c false if at least one thread would receive it.
 * @note The caller must hold the team's lock and @c signal_lock.
 */
bool
is_team_signal_blocked(Team* team, int signal)
{
	sigset_t mask = SIGNAL_TO_MASK(signal);

	for (Thread* thread = team->thread_list.First(); thread != NULL;
			thread = team->thread_list.GetNext(thread)) {
		if ((thread->sig_block_mask & mask) == 0)
			return false;
	}

	return true;
}


/**
 * @brief Determines which user stack (@a thread's signal stack or normal user
 *        stack) corresponds to the given stack pointer @a address.
 *
 * @param address A stack pointer value used to identify the active stack.
 * @param stack   Output parameter filled with the base, size, and flags of
 *                the identified stack.
 * @note If the address falls outside the signal stack (or no signal stack is
 *       enabled), the normal user stack is returned unconditionally.
 */
void
signal_get_user_stack(addr_t address, stack_t* stack)
{
	// If a signal stack is enabled for the stack and the address is within it,
	// return the signal stack. In all other cases return the thread's user
	// stack, even if the address doesn't lie within it.
	Thread* thread = thread_get_current_thread();
	if (thread->signal_stack_enabled && address >= thread->signal_stack_base
		&& address < thread->signal_stack_base + thread->signal_stack_size) {
		stack->ss_sp = (void*)thread->signal_stack_base;
		stack->ss_size = thread->signal_stack_size;
	} else {
		stack->ss_sp = (void*)thread->user_stack_base;
		stack->ss_size = thread->user_stack_size;
	}

	stack->ss_flags = 0;
}


/**
 * @brief Returns @c true if any non-blocked signal is pending for @a thread.
 *
 * @param thread The thread to check.
 * @return @c true when at least one pending signal is not masked.
 * @note The caller must hold @c team->signal_lock.
 */
static bool
has_signals_pending(Thread* thread)
{
	return (thread->AllPendingSignals() & ~thread->sig_block_mask) != 0;
}


/**
 * @brief Checks whether the calling thread has permission to send a signal to
 *        @a team.
 *
 * Root (UID 0) may signal any team. Otherwise the caller's effective UID must
 * match the target team's effective UID.
 *
 * @param team The target team.
 * @return @c true if the caller is permitted, @c false otherwise.
 */
static bool
has_permission_to_signal(Team* team)
{
	// get the current user
	uid_t currentUser = thread_get_current_thread()->team->effective_uid;

	// root is omnipotent -- in the other cases the current user must match the
	// target team's
	return currentUser == 0 || currentUser == team->effective_uid;
}


/**
 * @brief Enqueues or records a signal on @a thread and wakes or interrupts the
 *        thread as required, but does not invoke the handler.
 *
 * This is the low-level, locked signal delivery primitive. It enqueues the
 * signal on the thread's pending-signal list (or records it in the unqueued
 * mask), then performs any wakeup/interrupt action needed for special signals
 * (SIGKILL, SIGCONT, SIGSTOP, etc.).
 *
 * @param thread       The target thread.
 * @param signalNumber The signal number. Pass @c 0 to perform only permission
 *                     checks without actually queuing a signal.
 * @param signal       If non-NULL, the pre-allocated Signal object to enqueue
 *                     (ownership is transferred). If NULL an unqueued signal
 *                     record is used instead.
 * @param flags        Bitwise OR of delivery flags:
 *                     - @c B_CHECK_PERMISSION: verify caller has permission.
 * @retval B_OK        Signal delivered (or no-op for signal number 0).
 * @retval EPERM       Permission check failed.
 * @note The caller must hold @c team->signal_lock. Interrupts must be enabled.
 */
status_t
send_signal_to_thread_locked(Thread* thread, uint32 signalNumber,
	Signal* signal, uint32 flags)
{
	ASSERT(signal == NULL || signalNumber == signal->Number());

	T(SendSignal(thread->id, signalNumber, flags));

	// The caller transferred a reference to the signal to us.
	BReference<Signal> signalReference(signal, true);

	if ((flags & B_CHECK_PERMISSION) != 0) {
		if (!has_permission_to_signal(thread->team))
			return EPERM;
	}

	if (signalNumber == 0)
		return B_OK;

	if (thread->team == team_get_kernel_team()) {
		// Signals to kernel threads will only wake them up
		thread_continue(thread);
		return B_OK;
	}

	if (signal != NULL)
		thread->AddPendingSignal(signal);
	else
		thread->AddPendingSignal(signalNumber);

	// the thread has the signal reference, now
	signalReference.Detach();

	switch (signalNumber) {
		case SIGKILL:
		{
			// If sent to a thread other than the team's main thread, also send
			// a SIGKILLTHR to the main thread to kill the team.
			Thread* mainThread = thread->team->main_thread;
			if (mainThread != NULL && mainThread != thread) {
				mainThread->AddPendingSignal(SIGKILLTHR);

				// wake up main thread
				thread->going_to_suspend = false;

				SpinLocker locker(mainThread->scheduler_lock);
				if (mainThread->state == B_THREAD_SUSPENDED)
					scheduler_enqueue_in_run_queue(mainThread);
				else
					thread_interrupt(mainThread, true);
				locker.Unlock();

				update_thread_signals_flag(mainThread);
			}

			// supposed to fall through
		}
		case SIGKILLTHR:
		{
			// Wake up suspended threads and interrupt waiting ones
			thread->going_to_suspend = false;

			SpinLocker locker(thread->scheduler_lock);
			if (thread->state == B_THREAD_SUSPENDED)
				scheduler_enqueue_in_run_queue(thread);
			else
				thread_interrupt(thread, true);

			break;
		}
		case SIGNAL_DEBUG_THREAD:
		{
			// Wake up thread if it was suspended, otherwise interrupt it.
			thread->going_to_suspend = false;

			SpinLocker locker(thread->scheduler_lock);
			if (thread->state == B_THREAD_SUSPENDED)
				scheduler_enqueue_in_run_queue(thread);
			else
				thread_interrupt(thread, false);

			break;
		}
		case SIGNAL_CONTINUE_THREAD:
		{
			// wake up thread, and interrupt its current syscall
			thread->going_to_suspend = false;

			SpinLocker locker(thread->scheduler_lock);
			if (thread->state == B_THREAD_SUSPENDED)
				scheduler_enqueue_in_run_queue(thread);

			atomic_or(&thread->flags, THREAD_FLAGS_DONT_RESTART_SYSCALL);
			break;
		}
		case SIGCONT:
		{
			// Wake up thread if it was suspended, otherwise interrupt it, if
			// the signal isn't blocked.
			thread->going_to_suspend = false;

			SpinLocker locker(thread->scheduler_lock);
			if (thread->state == B_THREAD_SUSPENDED)
				scheduler_enqueue_in_run_queue(thread);
			else if ((SIGNAL_TO_MASK(SIGCONT) & ~thread->sig_block_mask) != 0)
				thread_interrupt(thread, false);

			// remove any pending stop signals
			thread->RemovePendingSignals(STOP_SIGNALS);
			break;
		}
		default:
			// If the signal is not masked, interrupt the thread, if it is
			// currently waiting (interruptibly).
			if ((thread->AllPendingSignals()
						& (~thread->sig_block_mask | SIGNAL_TO_MASK(SIGCHLD)))
					!= 0) {
				// Interrupt thread if it was waiting
				SpinLocker locker(thread->scheduler_lock);
				thread_interrupt(thread, false);
			}
			break;
	}

	update_thread_signals_flag(thread);

	return B_OK;
}


/**
 * @brief Sends @a signal to @a thread, acquiring necessary locks internally.
 *
 * Clones the signal for queuing (subject to the team's queuing limit), acquires
 * the team's signal lock, calls send_signal_to_thread_locked(), and optionally
 * triggers rescheduling.
 *
 * @param thread  The target thread (caller holds a reference).
 * @param signal  Signal to deliver; the object is copied and the caller retains
 *                ownership of the original. A signal number of @c 0 performs
 *                only permission checks.
 * @param flags   Delivery flags (B_CHECK_PERMISSION, B_DO_NOT_RESCHEDULE,
 *                SIGNAL_FLAG_QUEUING_REQUIRED).
 * @retval B_OK   Signal successfully delivered.
 * @note Interrupts must be enabled on entry. Must not be called with
 *       @c signal_lock held.
 */
status_t
send_signal_to_thread(Thread* thread, const Signal& signal, uint32 flags)
{
	// Clone the signal -- the clone will be queued. If something fails and the
	// caller doesn't require queuing, we will add an unqueued signal.
	Signal* signalToQueue = NULL;
	status_t error = Signal::CreateQueuable(signal,
		(flags & SIGNAL_FLAG_QUEUING_REQUIRED) != 0, signalToQueue);
	if (error != B_OK)
		return error;

	InterruptsReadSpinLocker teamLocker(thread->team_lock);
	SpinLocker locker(thread->team->signal_lock);

	error = send_signal_to_thread_locked(thread, signal.Number(), signalToQueue,
		flags);
	if (error != B_OK)
		return error;

	locker.Unlock();
	teamLocker.Unlock();

	if ((flags & B_DO_NOT_RESCHEDULE) == 0)
		scheduler_reschedule_if_necessary();

	return B_OK;
}


/**
 * @brief Sends @a signal to the thread identified by @a threadID.
 *
 * Looks up the thread by ID (returning @c B_BAD_THREAD_ID if not found) and
 * delegates to send_signal_to_thread().
 *
 * @param threadID The ID of the target thread.
 * @param signal   Signal to deliver; copied, caller retains ownership.
 * @param flags    Delivery flags passed through to send_signal_to_thread().
 * @retval B_OK             Signal delivered.
 * @retval B_BAD_THREAD_ID  No thread with that ID exists.
 */
status_t
send_signal_to_thread_id(thread_id threadID, const Signal& signal, uint32 flags)
{
	Thread* thread = Thread::Get(threadID);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);

	return send_signal_to_thread(thread, signal, flags);
}


/**
 * @brief Enqueues or records @a signal on @a team and wakes affected threads,
 *        but does not dispatch the handler.
 *
 * This is the low-level locked team-delivery primitive. For SIGKILL/SIGKILLTHR
 * it additionally pokes the main thread; for SIGCONT it resumes all suspended
 * threads; for stop signals it fans the signal out to all threads.
 *
 * @param team         The target team.
 * @param signalNumber The signal number; 0 for permission-check only.
 * @param signal       Pre-allocated Signal to enqueue (ownership transferred),
 *                     or NULL for an unqueued record.
 * @param flags        Delivery flags (B_CHECK_PERMISSION, B_DO_NOT_RESCHEDULE).
 * @retval B_OK        Signal delivered.
 * @retval EPERM       Permission check failed or target is the kernel team.
 * @note The caller must hold @c team->signal_lock.
 */
status_t
send_signal_to_team_locked(Team* team, uint32 signalNumber, Signal* signal,
	uint32 flags)
{
	ASSERT(signal == NULL || signalNumber == signal->Number());

	T(SendSignal(team->id, signalNumber, flags));

	// The caller transferred a reference to the signal to us.
	BReference<Signal> signalReference(signal, true);

	if ((flags & B_CHECK_PERMISSION) != 0) {
		if (!has_permission_to_signal(team))
			return EPERM;
	}

	if (signalNumber == 0)
		return B_OK;

	if (team == team_get_kernel_team()) {
		// signals to the kernel team are not allowed
		return EPERM;
	}

	if (signal != NULL)
		team->AddPendingSignal(signal);
	else
		team->AddPendingSignal(signalNumber);

	// the team has the signal reference, now
	signalReference.Detach();

	switch (signalNumber) {
		case SIGKILL:
		case SIGKILLTHR:
		{
			// Also add a SIGKILLTHR to the main thread's signals and wake it
			// up/interrupt it, so we get this over with as soon as possible
			// (only the main thread shuts down the team).
			Thread* mainThread = team->main_thread;
			if (mainThread != NULL) {
				mainThread->AddPendingSignal(signalNumber);

				// wake up main thread
				mainThread->going_to_suspend = false;

				SpinLocker _(mainThread->scheduler_lock);
				if (mainThread->state == B_THREAD_SUSPENDED)
					scheduler_enqueue_in_run_queue(mainThread);
				else
					thread_interrupt(mainThread, true);
			}
			break;
		}

		case SIGCONT:
			// Wake up any suspended threads, interrupt the others, if they
			// don't block the signal.
			for (Thread* thread = team->thread_list.First(); thread != NULL;
					thread = team->thread_list.GetNext(thread)) {
				thread->going_to_suspend = false;

				SpinLocker _(thread->scheduler_lock);
				if (thread->state == B_THREAD_SUSPENDED) {
					scheduler_enqueue_in_run_queue(thread);
				} else if ((SIGNAL_TO_MASK(SIGCONT) & ~thread->sig_block_mask)
						!= 0) {
					thread_interrupt(thread, false);
				}

				// remove any pending stop signals
				thread->RemovePendingSignals(STOP_SIGNALS);
			}

			// remove any pending team stop signals
			team->RemovePendingSignals(STOP_SIGNALS);
			break;

		case SIGSTOP:
		case SIGTSTP:
		case SIGTTIN:
		case SIGTTOU:
			// send the stop signal to all threads
			// TODO: Is that correct or should we only target the main thread?
			for (Thread* thread = team->thread_list.First(); thread != NULL;
					thread = team->thread_list.GetNext(thread)) {
				thread->AddPendingSignal(signalNumber);
			}

			// remove the stop signal from the team again
			if (signal != NULL) {
				team->RemovePendingSignal(signal);
				signalReference.SetTo(signal, true);
			} else
				team->RemovePendingSignal(signalNumber);

			// fall through to interrupt threads
		default:
			// Interrupt all interruptibly waiting threads, if the signal is
			// not masked.
			for (Thread* thread = team->thread_list.First(); thread != NULL;
					thread = team->thread_list.GetNext(thread)) {
				sigset_t nonBlocked = ~thread->sig_block_mask
					| SIGNAL_TO_MASK(SIGCHLD);
				if ((thread->AllPendingSignals() & nonBlocked) != 0) {
					SpinLocker _(thread->scheduler_lock);
					thread_interrupt(thread, false);
				}
			}
			break;
	}

	update_team_threads_signal_flag(team);

	return B_OK;
}


/**
 * @brief Sends @a signal to @a team, acquiring the signal lock internally.
 *
 * Clones the signal for queuing, acquires @c team->signal_lock, calls
 * send_signal_to_team_locked(), and optionally triggers rescheduling.
 *
 * @param team   The target team.
 * @param signal Signal to deliver; copied, caller retains ownership.
 * @param flags  Delivery flags (B_CHECK_PERMISSION, B_DO_NOT_RESCHEDULE,
 *               SIGNAL_FLAG_QUEUING_REQUIRED).
 * @retval B_OK  Signal delivered.
 * @note Interrupts must be enabled. Must not be called with @c signal_lock
 *       held.
 */
status_t
send_signal_to_team(Team* team, const Signal& signal, uint32 flags)
{
	// Clone the signal -- the clone will be queued. If something fails and the
	// caller doesn't require queuing, we will add an unqueued signal.
	Signal* signalToQueue = NULL;
	status_t error = Signal::CreateQueuable(signal,
		(flags & SIGNAL_FLAG_QUEUING_REQUIRED) != 0, signalToQueue);
	if (error != B_OK)
		return error;

	InterruptsSpinLocker locker(team->signal_lock);

	error = send_signal_to_team_locked(team, signal.Number(), signalToQueue,
			flags);

	locker.Unlock();

	if ((flags & B_DO_NOT_RESCHEDULE) == 0)
		scheduler_reschedule_if_necessary();

	return error;
}


/**
 * @brief Sends @a signal to the team identified by @a teamID.
 *
 * Looks up the team by ID and delegates to send_signal_to_team().
 *
 * @param teamID The numeric team (process) ID.
 * @param signal Signal to deliver; copied, caller retains ownership.
 * @param flags  Delivery flags passed through to send_signal_to_team().
 * @retval B_OK            Signal delivered.
 * @retval B_BAD_TEAM_ID   No team with that ID exists.
 */
status_t
send_signal_to_team_id(team_id teamID, const Signal& signal, uint32 flags)
{
	// get the team
	Team* team = Team::Get(teamID);
	if (team == NULL)
		return B_BAD_TEAM_ID;
	BReference<Team> teamReference(team, true);

	return send_signal_to_team(team, signal, flags);
}


/**
 * @brief Sends @a signal to every team in the process group @a group.
 *
 * Iterates over all teams in the group and calls send_signal_to_team() on
 * each. The overall call fails only if sending to the very first team fails.
 *
 * @param group  The process group to signal (caller must hold its lock).
 * @param signal Signal to deliver; copied per team, caller retains ownership.
 * @param flags  Delivery flags; @c B_DO_NOT_RESCHEDULE is set internally and
 *               rescheduling is performed once at the end unless the caller
 *               also set that flag.
 * @retval B_OK  Signal delivered to the first team (and best-effort to others).
 * @note The caller must hold the process group lock. Interrupts must be
 *       enabled.
 */
status_t
send_signal_to_process_group_locked(ProcessGroup* group, const Signal& signal,
	uint32 flags)
{
	T(SendSignal(-group->id, signal.Number(), flags));

	bool firstTeam = true;

	for (Team* team = group->teams.First(); team != NULL; team = group->teams.GetNext(team)) {
		status_t error = send_signal_to_team(team, signal,
			flags | B_DO_NOT_RESCHEDULE);
		// If sending to the first team in the group failed, let the whole call
		// fail.
		if (firstTeam) {
			if (error != B_OK)
				return error;
			firstTeam = false;
		}
	}

	if ((flags & B_DO_NOT_RESCHEDULE) == 0)
		scheduler_reschedule_if_necessary();

	return B_OK;
}


/**
 * @brief Sends @a signal to the process group identified by @a groupID.
 *
 * Looks up the process group by ID, acquires its lock, and delegates to
 * send_signal_to_process_group_locked().
 *
 * @param groupID  The process group ID (positive).
 * @param signal   Signal to deliver; copied, caller retains ownership.
 * @param flags    Delivery flags passed through.
 * @retval B_OK            Signal delivered.
 * @retval B_BAD_TEAM_ID   No process group with that ID exists.
 * @note The caller must not hold any process group, team, or thread lock.
 *       Interrupts must be enabled.
 */
status_t
send_signal_to_process_group(pid_t groupID, const Signal& signal, uint32 flags)
{
	ProcessGroup* group = ProcessGroup::Get(groupID);
	if (group == NULL)
		return B_BAD_TEAM_ID;
	BReference<ProcessGroup> groupReference(group, true);

	T(SendSignal(-group->id, signal.Number(), flags));

	AutoLocker<ProcessGroup> groupLocker(group);

	status_t error = send_signal_to_process_group_locked(group, signal,
		flags | B_DO_NOT_RESCHEDULE);
	if (error != B_OK)
		return error;

	groupLocker.Unlock();

	if ((flags & B_DO_NOT_RESCHEDULE) == 0)
		scheduler_reschedule_if_necessary();

	return B_OK;
}


/**
 * @brief Internal helper that constructs a Signal and routes it to the correct
 *        target based on the sign and value of @a id.
 *
 * Interprets @a id using kill(2) semantics when @c SIGNAL_FLAG_SEND_TO_THREAD
 * is not set, and thread-directed semantics otherwise:
 * - @c id > 0  — specific thread or team
 * - @c id == 0 — current thread or team
 * - @c id == -1 — all permitted teams (partially implemented)
 * - @c id < -1  — process group with ID @c -id
 *
 * @param id           Target identifier (see above).
 * @param signalNumber Signal number (0–MAX_SIGNAL_NUMBER).
 * @param userValue    Application-defined value attached to the signal.
 * @param flags        Delivery flags.
 * @retval B_OK        Signal delivered.
 * @retval B_BAD_VALUE Signal number out of range.
 */
static status_t
send_signal_internal(pid_t id, uint signalNumber, union sigval userValue,
	uint32 flags)
{
	if (signalNumber > MAX_SIGNAL_NUMBER)
		return B_BAD_VALUE;

	Thread* thread = thread_get_current_thread();

	Signal signal(signalNumber,
		(flags & SIGNAL_FLAG_QUEUING_REQUIRED) != 0 ? SI_QUEUE : SI_USER,
		B_OK, thread->team->id);
		// Note: SI_USER/SI_QUEUE is not correct, if called from within the
		// kernel (or a driver), but we don't have any info here.
	signal.SetUserValue(userValue);

	// If id is > 0, send the signal to the respective thread.
	if (id > 0)
		return send_signal_to_thread_id(id, signal, flags);

	// If id == 0, send the signal to the current thread.
	if (id == 0)
		return send_signal_to_thread(thread, signal, flags);

	// If id == -1, send the signal to all teams the calling team has permission
	// to send signals to.
	if (id == -1) {
		// TODO: Implement correctly!
		// currently only send to the current team
		return send_signal_to_team_id(thread->team->id, signal, flags);
	}

	// Send a signal to the specified process group (the absolute value of the
	// id).
	return send_signal_to_process_group(-id, signal, flags);
}


/**
 * @brief Public kernel API — sends a signal with extended flags.
 *
 * Thin wrapper around send_signal_internal() with a null @c sigval.
 *
 * @param id           Target identifier (kill(2) semantics when positive or
 *                     negative; 0 targets the current thread).
 * @param signalNumber Signal number (0–MAX_SIGNAL_NUMBER).
 * @param flags        Delivery flags (e.g. B_CHECK_PERMISSION,
 *                     B_DO_NOT_RESCHEDULE, SIGNAL_FLAG_QUEUING_REQUIRED).
 * @return 0 on success, a negative error code otherwise.
 */
int
send_signal_etc(pid_t id, uint signalNumber, uint32 flags)
{
	// a dummy user value
	union sigval userValue;
	userValue.sival_ptr = NULL;

	return send_signal_internal(id, signalNumber, userValue, flags);
}


/**
 * @brief Public kernel API — sends a signal with default flags.
 *
 * Equivalent to send_signal_etc() with @c flags = 0. The BeBook notes this
 * function is available to drivers even though it is not formally exported.
 *
 * @param threadID Target thread ID.
 * @param signal   Signal number.
 * @return 0 on success, a negative error code otherwise.
 */
int
send_signal(pid_t threadID, uint signal)
{
	// The BeBook states that this function wouldn't be exported
	// for drivers, but, of course, it's wrong.
	return send_signal_etc(threadID, signal, 0);
}


/**
 * @brief Internal implementation of sigprocmask() — examines or changes the
 *        calling thread's signal block mask.
 *
 * @param how    @c SIG_BLOCK, @c SIG_UNBLOCK, or @c SIG_SETMASK.
 * @param set    New mask (or NULL to leave it unchanged).
 * @param oldSet If non-NULL, receives the previous mask.
 * @retval B_OK        Mask updated (or read) successfully.
 * @retval B_BAD_VALUE @a how is not a valid operation.
 * @note Acquires @c team->signal_lock internally. Non-blockable signals are
 *       silently filtered out of @a set.
 */
static int
sigprocmask_internal(int how, const sigset_t* set, sigset_t* oldSet)
{
	Thread* thread = thread_get_current_thread();

	InterruptsSpinLocker _(thread->team->signal_lock);

	sigset_t oldMask = thread->sig_block_mask;

	if (set != NULL) {
		T(SigProcMask(how, *set));

		switch (how) {
			case SIG_BLOCK:
				thread->sig_block_mask |= *set & BLOCKABLE_SIGNALS;
				break;
			case SIG_UNBLOCK:
				thread->sig_block_mask &= ~*set;
				break;
			case SIG_SETMASK:
				thread->sig_block_mask = *set & BLOCKABLE_SIGNALS;
				break;
			default:
				return B_BAD_VALUE;
		}

		update_current_thread_signals_flag();
	}

	if (oldSet != NULL)
		*oldSet = oldMask;

	return B_OK;
}


/**
 * @brief POSIX sigprocmask() — examines or changes the calling thread's signal
 *        block mask, setting @c errno on failure.
 *
 * @param how    @c SIG_BLOCK, @c SIG_UNBLOCK, or @c SIG_SETMASK.
 * @param set    Pointer to the new mask, or NULL.
 * @param oldSet Pointer to receive the old mask, or NULL.
 * @return 0 on success, -1 on failure with @c errno set.
 */
int
sigprocmask(int how, const sigset_t* set, sigset_t* oldSet)
{
	RETURN_AND_SET_ERRNO(sigprocmask_internal(how, set, oldSet));
}


/**
 * @brief Internal implementation of sigaction() — installs or queries the
 *        signal handler for @a signal in the current team.
 *
 * If a new action is provided and it is SIG_IGN (or SIG_DFL for a default-
 * ignore signal), any pending instances of the signal are removed from the
 * team and all its threads.
 *
 * @param signal    Signal number (1–MAX_SIGNAL_NUMBER, must be blockable).
 * @param act       New action to install, or NULL to query only.
 * @param oldAction If non-NULL, receives the previous action.
 * @retval B_OK        Action installed (or queried) successfully.
 * @retval B_BAD_VALUE Invalid signal number or attempt to change a
 *                     non-blockable signal.
 * @note Acquires the team lock and signal lock internally as needed.
 */
static status_t
sigaction_internal(int signal, const struct sigaction* act,
	struct sigaction* oldAction)
{
	if (signal < 1 || signal > MAX_SIGNAL_NUMBER
		|| (SIGNAL_TO_MASK(signal) & ~BLOCKABLE_SIGNALS) != 0)
		return B_BAD_VALUE;

	// get and lock the team
	Team* team = thread_get_current_thread()->team;
	TeamLocker teamLocker(team);

	struct sigaction& teamHandler = team->SignalActionFor(signal);
	if (oldAction) {
		// save previous sigaction structure
		*oldAction = teamHandler;
	}

	if (act) {
		T(SigAction(signal, act));

		// set new sigaction structure
		teamHandler = *act;
		teamHandler.sa_mask &= BLOCKABLE_SIGNALS;
	}

	// Remove pending signal if it should now be ignored and remove pending
	// signal for those signals whose default action is to ignore them.
	if ((act && act->sa_handler == SIG_IGN)
		|| (act && act->sa_handler == SIG_DFL
			&& (SIGNAL_TO_MASK(signal) & DEFAULT_IGNORE_SIGNALS) != 0)) {
		InterruptsSpinLocker locker(team->signal_lock);

		team->RemovePendingSignal(signal);

		for (Thread* thread = team->thread_list.First(); thread != NULL;
				thread = team->thread_list.GetNext(thread)) {
			thread->RemovePendingSignal(signal);
		}
	}

	return B_OK;
}


/**
 * @brief POSIX sigaction() — installs or queries a signal handler, setting
 *        @c errno on failure.
 *
 * @param signal    Signal number.
 * @param act       New action, or NULL.
 * @param oldAction Receives old action if non-NULL.
 * @return 0 on success, -1 with @c errno set on failure.
 */
int
sigaction(int signal, const struct sigaction* act, struct sigaction* oldAction)
{
	RETURN_AND_SET_ERRNO(sigaction_internal(signal, act, oldAction));
}


/**
 * @brief Waits for one of the signals in @a set to become pending, then
 *        dequeues it and fills @a info.
 *
 * Temporarily unblocks the requested signals so they can interrupt the wait.
 * If a non-blocked signal arrives before one of the requested signals, the
 * function returns @c B_INTERRUPTED so the caller can handle it.
 *
 * @param set     Set of signals to wait for (non-blockable signals ignored).
 * @param info    Filled with the dequeued signal's information on success.
 * @param flags   Timeout flags (@c B_ABSOLUTE_TIMEOUT, @c B_RELATIVE_TIMEOUT,
 *                @c B_CAN_INTERRUPT are added internally).
 * @param timeout Absolute or relative timeout value in microseconds.
 * @retval B_OK         A requested signal was received.
 * @retval B_WOULD_BLOCK Timed out before any signal arrived (POSIX: EAGAIN).
 * @retval B_INTERRUPTED A non-requested, non-blocked signal is pending.
 * @note Interrupts must be enabled. Must be called from a thread context.
 */
static status_t
sigwait_internal(const sigset_t* set, siginfo_t* info, uint32 flags,
	bigtime_t timeout)
{
	// restrict mask to blockable signals
	sigset_t requestedSignals = *set & BLOCKABLE_SIGNALS;

	// make always interruptable
	flags |= B_CAN_INTERRUPT;

	// check whether we are allowed to wait at all
	bool canWait = (flags & B_RELATIVE_TIMEOUT) == 0 || timeout > 0;

	Thread* thread = thread_get_current_thread();

	InterruptsSpinLocker locker(thread->team->signal_lock);

	bool timedOut = false;
	status_t error = B_OK;

	while (!timedOut) {
		sigset_t pendingSignals = thread->AllPendingSignals();

		// If a kill signal is pending, just bail out.
		if ((pendingSignals & KILL_SIGNALS) != 0)
			return B_INTERRUPTED;

		if ((pendingSignals & requestedSignals) != 0) {
			// get signal with the highest priority
			Signal stackSignal;
			Signal* signal = dequeue_thread_or_team_signal(thread,
				requestedSignals, stackSignal);
			ASSERT(signal != NULL);

			SignalHandledCaller signalHandledCaller(signal);
			locker.Unlock();

			info->si_signo = signal->Number();
			info->si_code = signal->SignalCode();
			info->si_errno = signal->ErrorCode();
			info->si_pid = signal->SendingProcess();
			info->si_uid = signal->SendingUser();
			info->si_addr = signal->Address();
			info->si_status = signal->Status();
			info->si_band = signal->PollBand();
			info->si_value = signal->UserValue();

			return B_OK;
		}

		if (!canWait)
			return B_WOULD_BLOCK;

		sigset_t blockedSignals = thread->sig_block_mask;
		if ((pendingSignals & ~blockedSignals) != 0) {
			// Non-blocked signals are pending -- return to let them be handled.
			return B_INTERRUPTED;
		}

		// No signals yet. Set the signal block mask to not include the
		// requested mask and wait until we're interrupted.
		thread->sig_block_mask = blockedSignals & ~requestedSignals;

		while (!has_signals_pending(thread)) {
			thread_prepare_to_block(thread, flags, THREAD_BLOCK_TYPE_SIGNAL,
				NULL);

			locker.Unlock();

			if ((flags & B_ABSOLUTE_TIMEOUT) != 0) {
				error = thread_block_with_timeout(flags, timeout);
				if (error == B_WOULD_BLOCK || error == B_TIMED_OUT) {
					error = B_WOULD_BLOCK;
						// POSIX requires EAGAIN (B_WOULD_BLOCK) on timeout
					timedOut = true;

					locker.Lock();
					break;
				}
			} else
				thread_block();

			locker.Lock();
		}

		// restore the original block mask
		thread->sig_block_mask = blockedSignals;

		update_current_thread_signals_flag();
	}

	// we get here only when timed out
	return error;
}


/**
 * @brief Replaces the current thread's signal block mask and suspends it until
 *        a signal arrives, then restores the original mask.
 *
 * Implements POSIX sigsuspend(). Sets the block mask to @a _mask, sleeps
 * until interrupted by a signal, records the original mask so that
 * handle_signals() can restore it after the handler returns, then always
 * returns @c B_INTERRUPTED (as required by POSIX).
 *
 * @param _mask  The temporary signal mask to apply during the suspension.
 * @retval B_INTERRUPTED Always; the syscall layer maps this to @c EINTR.
 * @note Acquires @c team->signal_lock internally. Interrupts must be enabled.
 */
static status_t
sigsuspend_internal(const sigset_t* _mask)
{
	sigset_t mask = *_mask & BLOCKABLE_SIGNALS;

	T(SigSuspend(mask));

	Thread* thread = thread_get_current_thread();

	InterruptsSpinLocker locker(thread->team->signal_lock);

	// Set the new block mask and block until interrupted. We might be here
	// after a syscall restart, in which case sigsuspend_original_unblocked_mask
	// will still be set.
	sigset_t oldMask = thread->sigsuspend_original_unblocked_mask != 0
		? ~thread->sigsuspend_original_unblocked_mask : thread->sig_block_mask;
	thread->sig_block_mask = mask & BLOCKABLE_SIGNALS;

	update_current_thread_signals_flag();

	while (!has_signals_pending(thread)) {
		thread_prepare_to_block(thread, B_CAN_INTERRUPT,
			THREAD_BLOCK_TYPE_SIGNAL, NULL);

		locker.Unlock();
		thread_block();
		locker.Lock();
	}

	// Set sigsuspend_original_unblocked_mask (guaranteed to be non-0 due to
	// BLOCKABLE_SIGNALS). This will indicate to handle_signals() that it is
	// called after a _user_sigsuspend(). It will reset the field after invoking
	// a signal handler, or restart the syscall, if there wasn't anything to
	// handle anymore (e.g. because another thread was faster).
	thread->sigsuspend_original_unblocked_mask = ~oldMask;

	T(SigSuspendDone());

	// we're not supposed to actually succeed
	return B_INTERRUPTED;
}


/**
 * @brief Internal implementation of sigpending() — returns the set of signals
 *        that are both pending and blocked for the current thread.
 *
 * @param set Output parameter filled with the pending-and-blocked signal mask.
 * @retval B_OK        Set retrieved successfully.
 * @retval B_BAD_VALUE @a set is NULL.
 * @note Acquires @c team->signal_lock internally.
 */
static status_t
sigpending_internal(sigset_t* set)
{
	Thread* thread = thread_get_current_thread();

	if (set == NULL)
		return B_BAD_VALUE;

	InterruptsSpinLocker locker(thread->team->signal_lock);

	*set = thread->AllPendingSignals() & thread->sig_block_mask;

	return B_OK;
}


// #pragma mark - syscalls


/**
 * @brief Syscall entry point — sends a signal from user space.
 *
 * Validates and copies the optional @a userUserValue from user space, then
 * dispatches to send_signal_internal() (thread/group targets) or
 * send_signal_to_team_id() (team/kill targets).
 *
 * @param id             Target identifier (kill(2) semantics for teams;
 *                       negative = process group; 0 = current team).
 * @param signalNumber   Signal number (0–MAX_SIGNAL_NUMBER).
 * @param userUserValue  Optional user-space pointer to a @c sigval; may be
 *                       NULL.
 * @param flags          User-supplied flags filtered to
 *                       SIGNAL_FLAG_QUEUING_REQUIRED |
 *                       SIGNAL_FLAG_SEND_TO_THREAD; @c B_CHECK_PERMISSION is
 *                       always added.
 * @retval B_OK          Signal delivered.
 * @retval B_BAD_ADDRESS @a userUserValue is not a valid user address.
 * @retval B_BAD_VALUE   Signal number out of range.
 */
status_t
_user_send_signal(int32 id, uint32 signalNumber,
	const union sigval* userUserValue, uint32 flags)
{
	// restrict flags to the allowed ones and add B_CHECK_PERMISSION
	flags &= SIGNAL_FLAG_QUEUING_REQUIRED | SIGNAL_FLAG_SEND_TO_THREAD;
	flags |= B_CHECK_PERMISSION;

	// Copy the user value from userland. If not given, use a dummy value.
	union sigval userValue;
	if (userUserValue != NULL) {
		if (!IS_USER_ADDRESS(userUserValue)
			|| user_memcpy(&userValue, userUserValue, sizeof(userValue))
				!= B_OK) {
			return B_BAD_ADDRESS;
		}
	} else
		userValue.sival_ptr = NULL;

	// If to be sent to a thread, delegate to send_signal_internal(). Also do
	// that when id < 0, since in this case the semantics is the same as well.
	if ((flags & SIGNAL_FLAG_SEND_TO_THREAD) != 0 || id < 0)
		return send_signal_internal(id, signalNumber, userValue, flags);

	// kill() semantics for id >= 0
	if (signalNumber > MAX_SIGNAL_NUMBER)
		return B_BAD_VALUE;

	Thread* thread = thread_get_current_thread();

	Signal signal(signalNumber,
		(flags & SIGNAL_FLAG_QUEUING_REQUIRED) != 0 ? SI_QUEUE : SI_USER,
		B_OK, thread->team->id);
	signal.SetUserValue(userValue);

	// send to current team for id == 0, otherwise to the respective team
	return send_signal_to_team_id(id == 0 ? team_get_current_team_id() : id,
		signal, flags);
}


/**
 * @brief Syscall entry point — changes the calling thread's signal block mask
 *        from user space (sigprocmask).
 *
 * Safely copies @a userSet and @a userOldSet to/from user space and delegates
 * to sigprocmask_internal().
 *
 * @param how        @c SIG_BLOCK, @c SIG_UNBLOCK, or @c SIG_SETMASK.
 * @param userSet    User-space pointer to the new mask, or NULL.
 * @param userOldSet User-space pointer to receive the old mask, or NULL.
 * @retval B_OK          Mask updated.
 * @retval B_BAD_ADDRESS A pointer is not a valid user address.
 * @retval B_BAD_VALUE   @a how is invalid.
 */
status_t
_user_set_signal_mask(int how, const sigset_t *userSet, sigset_t *userOldSet)
{
	sigset_t set, oldSet;
	status_t status;

	if ((userSet != NULL && (!IS_USER_ADDRESS(userSet)
			|| user_memcpy(&set, userSet, sizeof(sigset_t)) < B_OK))
		|| (userOldSet != NULL && (!IS_USER_ADDRESS(userOldSet)
			|| user_memcpy(&oldSet, userOldSet, sizeof(sigset_t)) < B_OK)))
		return B_BAD_ADDRESS;

	status = sigprocmask_internal(how, userSet ? &set : NULL,
		userOldSet ? &oldSet : NULL);

	// copy old set if asked for
	if (status >= B_OK && userOldSet != NULL
		&& user_memcpy(userOldSet, &oldSet, sizeof(sigset_t)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall entry point — installs or queries a signal handler from user
 *        space (sigaction).
 *
 * Copies @a userAction and @a userOldAction to/from user space and delegates
 * to sigaction_internal().
 *
 * @param signal        Signal number.
 * @param userAction    User-space pointer to the new @c sigaction, or NULL.
 * @param userOldAction User-space pointer to receive the old @c sigaction, or
 *                      NULL.
 * @retval B_OK          Action installed/queried.
 * @retval B_BAD_ADDRESS A pointer is not a valid user address.
 * @retval B_BAD_VALUE   Invalid signal number.
 */
status_t
_user_sigaction(int signal, const struct sigaction *userAction,
	struct sigaction *userOldAction)
{
	struct sigaction act, oact;
	status_t status;

	if ((userAction != NULL && (!IS_USER_ADDRESS(userAction)
			|| user_memcpy(&act, userAction, sizeof(struct sigaction)) < B_OK))
		|| (userOldAction != NULL && (!IS_USER_ADDRESS(userOldAction)
			|| user_memcpy(&oact, userOldAction, sizeof(struct sigaction))
				< B_OK)))
		return B_BAD_ADDRESS;

	status = sigaction_internal(signal, userAction ? &act : NULL,
		userOldAction ? &oact : NULL);

	// only copy the old action if a pointer has been given
	if (status >= B_OK && userOldAction != NULL
		&& user_memcpy(userOldAction, &oact, sizeof(struct sigaction)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall entry point — waits for a signal from user space (sigwaitinfo
 *        / sigtimedwait).
 *
 * Copies @a userSet from user space, calls sigwait_internal(), and copies
 * the resulting @c siginfo_t back to @a userInfo. Handles syscall restart if
 * interrupted by an unrelated signal.
 *
 * @param userSet  User-space pointer to the set of signals to wait for.
 * @param userInfo User-space pointer to receive signal info (optional).
 * @param flags    Timeout flags (@c B_ABSOLUTE_TIMEOUT / @c B_RELATIVE_TIMEOUT).
 * @param timeout  Timeout value in microseconds.
 * @retval B_OK          A requested signal was received.
 * @retval B_WOULD_BLOCK Timed out.
 * @retval B_BAD_ADDRESS A pointer is not a valid user address.
 */
status_t
_user_sigwait(const sigset_t *userSet, siginfo_t *userInfo, uint32 flags,
	bigtime_t timeout)
{
	// copy userSet to stack
	sigset_t set;
	if (userSet == NULL || !IS_USER_ADDRESS(userSet)
		|| user_memcpy(&set, userSet, sizeof(sigset_t)) != B_OK) {
		return B_BAD_ADDRESS;
	}

	// userInfo is optional, but must be a user address when given
	if (userInfo != NULL && !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	syscall_restart_handle_timeout_pre(flags, timeout);

	flags |= B_CAN_INTERRUPT;

	siginfo_t info;
	status_t status = sigwait_internal(&set, &info, flags, timeout);
	if (status == B_OK) {
		// copy the info back to userland, if userSet is non-NULL
		if (userInfo != NULL)
			status = user_memcpy(userInfo, &info, sizeof(info));
	} else if (status == B_INTERRUPTED) {
		// make sure we'll be restarted
		Thread* thread = thread_get_current_thread();
		atomic_or(&thread->flags, THREAD_FLAGS_ALWAYS_RESTART_SYSCALL);
	}

	return syscall_restart_handle_timeout_post(status, timeout);
}


/**
 * @brief Syscall entry point — suspends the calling thread until a signal
 *        arrives (sigsuspend).
 *
 * Copies @a userMask from user space and delegates to sigsuspend_internal().
 * Always returns @c B_INTERRUPTED (EINTR) per POSIX.
 *
 * @param userMask User-space pointer to the temporary signal mask.
 * @retval B_INTERRUPTED  Always (indicates a signal was received).
 * @retval B_BAD_VALUE    @a userMask is NULL.
 * @retval B_BAD_ADDRESS  @a userMask is not a valid user address.
 */
status_t
_user_sigsuspend(const sigset_t *userMask)
{
	sigset_t mask;

	if (userMask == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userMask)
		|| user_memcpy(&mask, userMask, sizeof(sigset_t)) < B_OK) {
		return B_BAD_ADDRESS;
	}

	return sigsuspend_internal(&mask);
}


/**
 * @brief Syscall entry point — retrieves the set of pending blocked signals
 *        (sigpending).
 *
 * Calls sigpending_internal() and copies the result to user space.
 *
 * @param userSet User-space pointer to receive the pending signal mask.
 * @retval B_OK          Set retrieved and copied successfully.
 * @retval B_BAD_VALUE   @a userSet is NULL.
 * @retval B_BAD_ADDRESS @a userSet is not a valid user address.
 */
status_t
_user_sigpending(sigset_t *userSet)
{
	sigset_t set;
	int status;

	if (userSet == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userSet))
		return B_BAD_ADDRESS;

	status = sigpending_internal(&set);
	if (status == B_OK
		&& user_memcpy(userSet, &set, sizeof(sigset_t)) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


/**
 * @brief Syscall entry point — installs or removes the per-thread alternate
 *        signal stack (sigaltstack).
 *
 * Copies @a newUserStack from user space, validates it, and updates the
 * current thread's signal stack fields. Copies the old stack info to
 * @a oldUserStack if requested.
 *
 * @param newUserStack User-space pointer to a @c stack_t describing the new
 *                     alternate stack, or NULL to leave it unchanged.
 * @param oldUserStack User-space pointer to receive the previous @c stack_t,
 *                     or NULL.
 * @retval B_OK          Stack installed/queried.
 * @retval B_BAD_ADDRESS A pointer is not a valid user address.
 * @retval B_BAD_VALUE   Invalid flags or stack pointer.
 * @retval B_NO_MEMORY   Requested stack is smaller than MINSIGSTKSZ.
 * @retval B_NOT_ALLOWED The thread is currently executing on the signal stack.
 */
status_t
_user_set_signal_stack(const stack_t* newUserStack, stack_t* oldUserStack)
{
	Thread *thread = thread_get_current_thread();
	struct stack_t newStack, oldStack;
	bool onStack = false;

	if ((newUserStack != NULL && (!IS_USER_ADDRESS(newUserStack)
			|| user_memcpy(&newStack, newUserStack, sizeof(stack_t)) < B_OK))
		|| (oldUserStack != NULL && (!IS_USER_ADDRESS(oldUserStack)
			|| user_memcpy(&oldStack, oldUserStack, sizeof(stack_t)) < B_OK)))
		return B_BAD_ADDRESS;

	if (thread->signal_stack_enabled) {
		// determine whether or not the user thread is currently
		// on the active signal stack
		onStack = arch_on_signal_stack(thread);
	}

	if (oldUserStack != NULL) {
		oldStack.ss_sp = (void *)thread->signal_stack_base;
		oldStack.ss_size = thread->signal_stack_size;
		oldStack.ss_flags = (thread->signal_stack_enabled ? 0 : SS_DISABLE)
			| (onStack ? SS_ONSTACK : 0);
	}

	if (newUserStack != NULL) {
		// no flags other than SS_DISABLE are allowed
		if ((newStack.ss_flags & ~SS_DISABLE) != 0)
			return B_BAD_VALUE;

		if ((newStack.ss_flags & SS_DISABLE) == 0) {
			// check if the size is valid
			if (newStack.ss_size < MINSIGSTKSZ)
				return B_NO_MEMORY;
			if (onStack)
				return B_NOT_ALLOWED;
			if (!IS_USER_ADDRESS(newStack.ss_sp))
				return B_BAD_VALUE;

			thread->signal_stack_base = (addr_t)newStack.ss_sp;
			thread->signal_stack_size = newStack.ss_size;
			thread->signal_stack_enabled = true;
		} else
			thread->signal_stack_enabled = false;
	}

	// only copy the old stack info if a pointer has been given
	if (oldUserStack != NULL
		&& user_memcpy(oldUserStack, &oldStack, sizeof(stack_t)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


/**
 * @brief Syscall entry point — restores the thread state after a signal
 *        handler returns.
 *
 * This syscall is invoked by the signal trampoline when a user-space signal
 * handler returns. It deconstructs the signal handler frame, restores the
 * pre-signal register state, signal mask, and syscall-restart flags.
 *
 * The syscall is unusual in that it does not return to its caller but instead
 * resumes execution at the point that was interrupted by the signal. It
 * returns an @c int64 because it may need to carry the 64-bit return value of
 * an interrupted syscall back to user space.
 *
 * @param userSignalFrameData User-space pointer to the @c signal_frame_data
 *                            that was set up by setup_signal_frame(). The
 *                            signal handler may have modified some fields
 *                            (e.g. the saved register context).
 * @return The return value of the interrupted syscall (or the value needed to
 *         reconstruct the interrupted environment for hardware faults).
 * @note If the frame data cannot be read from user space, the thread is killed.
 */
int64
_user_restore_signal_frame(struct signal_frame_data* userSignalFrameData)
{
	syscall_64_bit_return_value();

	Thread *thread = thread_get_current_thread();

	// copy the signal frame data from userland
	signal_frame_data signalFrameData;
	if (userSignalFrameData == NULL || !IS_USER_ADDRESS(userSignalFrameData)
		|| user_memcpy(&signalFrameData, userSignalFrameData,
			sizeof(signalFrameData)) != B_OK) {
		// We failed to copy the signal frame data from userland. This is a
		// serious problem. Kill the thread.
		dprintf("_user_restore_signal_frame(): thread %" B_PRId32 ": Failed to "
			"copy signal frame data (%p) from userland. Killing thread...\n",
			thread->id, userSignalFrameData);
		kill_thread(thread->id);
		return B_BAD_ADDRESS;
	}

	// restore the signal block mask
	InterruptsSpinLocker locker(thread->team->signal_lock);

	thread->sig_block_mask
		= signalFrameData.context.uc_sigmask & BLOCKABLE_SIGNALS;
	update_current_thread_signals_flag();

	locker.Unlock();

	// restore the syscall restart related thread flags and the syscall restart
	// parameters
	atomic_and(&thread->flags,
		~(THREAD_FLAGS_RESTART_SYSCALL | THREAD_FLAGS_64_BIT_SYSCALL_RETURN));
	atomic_or(&thread->flags, signalFrameData.thread_flags
		& (THREAD_FLAGS_RESTART_SYSCALL | THREAD_FLAGS_64_BIT_SYSCALL_RETURN));

	memcpy(thread->syscall_restart.parameters,
		signalFrameData.syscall_restart_parameters,
		sizeof(thread->syscall_restart.parameters));

	// restore the previously stored Thread::user_signal_context
	thread->user_signal_context = signalFrameData.context.uc_link;
	if (thread->user_signal_context != NULL
		&& !IS_USER_ADDRESS(thread->user_signal_context)) {
		thread->user_signal_context = NULL;
	}

	// let the architecture specific code restore the registers
	return arch_restore_signal_frame(&signalFrameData);
}
