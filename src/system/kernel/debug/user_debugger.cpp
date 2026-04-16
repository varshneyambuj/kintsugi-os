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
 *   Copyright 2005-2016, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2015, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file user_debugger.cpp
 * @brief Kernel-side implementation of the user-space debugger protocol.
 *
 * Implements the team/thread attach-detach machinery (install_team_debugger,
 * remove_team_debugger) plus the debug nub thread that services debugger
 * requests over a port. Intercepts signals, exceptions, breakpoints, watchpoints,
 * single-stepping, profiling ticks, and syscall/image/thread/team lifecycle
 * events in a debugged team and forwards B_DEBUGGER_MESSAGE_* payloads to the
 * debugger's port. Also provides the _user_* syscall entry points that user
 * space calls to manipulate its own (or another team's) debug state.
 */


#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <arch/debug.h>
#include <arch/user_debugger.h>
#include <core_dump.h>
#include <cpu.h>
#include <debugger.h>
#include <kernel.h>
#include <KernelExport.h>
#include <kscheduler.h>
#include <ksignal.h>
#include <ksyscalls.h>
#include <port.h>
#include <sem.h>
#include <team.h>
#include <thread.h>
#include <thread_types.h>
#include <user_debugger.h>
#include <vm/vm.h>
#include <vm/vm_types.h>

#include <AutoDeleter.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>

#include "BreakpointManager.h"


//#define TRACE_USER_DEBUGGER
#ifdef TRACE_USER_DEBUGGER
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


// TODO: Since the introduction of team_debug_info::debugger_changed_condition
// there's some potential for simplifications. E.g. clear_team_debug_info() and
// destroy_team_debug_info() are now only used in nub_thread_cleanup() (plus
// arch_clear_team_debug_info() in install_team_debugger_init_debug_infos()).


static port_id sDefaultDebuggerPort = -1;
	// accessed atomically

static timer sProfilingTimers[SMP_MAX_CPUS];
	// a profiling timer for each CPU -- used when a profiled thread is running
	// on that CPU


static void schedule_profiling_timer(Thread* thread, bigtime_t interval);
static int32 profiling_event(timer* unused);
static void profiling_flush(void*);

static status_t ensure_debugger_installed();
static void get_team_debug_info(team_debug_info &teamDebugInfo);


/**
 * @brief Write to a port in kill-interruptable mode.
 *
 * Thin wrapper over write_port_etc() used when posting to a debug port so that
 * a pending SIGKILL can unblock the write.
 *
 * @param port Destination port.
 * @param code Message code.
 * @param buffer Message payload.
 * @param bufferSize Size of @a buffer in bytes.
 * @return B_OK on success, otherwise a port_etc error.
 */
static inline status_t
kill_interruptable_write_port(port_id port, int32 code, const void *buffer,
	size_t bufferSize)
{
	return write_port_etc(port, code, buffer, bufferSize, B_KILL_CAN_INTERRUPT,
		0);
}


/**
 * @brief Serialized write to the debugger port of the current team.
 *
 * Acquires the team's debugger_write_lock, re-checks that the debugger port
 * has not changed (and that we are not in the middle of a handover), and only
 * then delivers the message.
 *
 * @param port Debugger port the caller intended to write to.
 * @param code Debugger message code.
 * @param buffer Message payload.
 * @param bufferSize Size of @a buffer in bytes.
 * @param dontWait If true, uses B_RELATIVE_TIMEOUT (0); otherwise blocks kill-interruptably.
 * @return B_OK on success, error from acquire_sem_etc/write_port_etc otherwise.
 */
static status_t
debugger_write(port_id port, int32 code, const void *buffer, size_t bufferSize,
	bool dontWait)
{
	TRACE(("debugger_write(): thread: %" B_PRId32 ", team %" B_PRId32 ", "
		"port: %" B_PRId32 ", code: %" B_PRIx32 ", message: %p, size: %lu, "
		"dontWait: %d\n", thread_get_current_thread()->id,
		thread_get_current_thread()->team->id, port, code, buffer, bufferSize,
		dontWait));

	status_t error = B_OK;

	// get the team debug info
	team_debug_info teamDebugInfo;
	get_team_debug_info(teamDebugInfo);
	sem_id writeLock = teamDebugInfo.debugger_write_lock;

	// get the write lock
	TRACE(("debugger_write(): acquiring write lock...\n"));
	error = acquire_sem_etc(writeLock, 1,
		dontWait ? (uint32)B_RELATIVE_TIMEOUT : (uint32)B_KILL_CAN_INTERRUPT, 0);
	if (error != B_OK) {
		TRACE(("debugger_write() done1: %" B_PRIx32 "\n", error));
		return error;
	}

	// re-get the team debug info
	get_team_debug_info(teamDebugInfo);

	if (teamDebugInfo.debugger_port != port
		|| (teamDebugInfo.flags & B_TEAM_DEBUG_DEBUGGER_HANDOVER)) {
		// The debugger has changed in the meantime or we are about to be
		// handed over to a new debugger. In either case we don't send the
		// message.
		TRACE(("debugger_write(): %s\n",
			(teamDebugInfo.debugger_port != port ? "debugger port changed"
				: "handover flag set")));
	} else {
		TRACE(("debugger_write(): writing to port...\n"));

		error = write_port_etc(port, code, buffer, bufferSize,
			dontWait ? (uint32)B_RELATIVE_TIMEOUT : (uint32)B_KILL_CAN_INTERRUPT, 0);
	}

	// release the write lock
	release_sem(writeLock);

	TRACE(("debugger_write() done: %" B_PRIx32 "\n", error));

	return error;
}


/**
 * @brief Mirror B_THREAD_DEBUG_STOP into Thread::flags.
 *
 * Interrupts must be disabled and the thread's debug_info lock must be held.
 *
 * @param thread Thread whose flags to update.
 */
static void
update_thread_user_debug_flag(Thread* thread)
{
	if ((atomic_get(&thread->debug_info.flags) & B_THREAD_DEBUG_STOP) != 0)
		atomic_or(&thread->flags, THREAD_FLAGS_DEBUG_THREAD);
	else
		atomic_and(&thread->flags, ~THREAD_FLAGS_DEBUG_THREAD);
}


/**
 * @brief Mirror arch-level "has breakpoints" state into Thread::flags.
 *
 * Interrupts must be disabled and the thread's debug_info lock must be held.
 *
 * @param thread Thread whose flags to update.
 */
static void
update_thread_breakpoints_flag(Thread* thread)
{
	Team* team = thread->team;

	if (arch_has_breakpoints(&team->debug_info.arch_info))
		atomic_or(&thread->flags, THREAD_FLAGS_BREAKPOINTS_DEFINED);
	else
		atomic_and(&thread->flags, ~THREAD_FLAGS_BREAKPOINTS_DEFINED);
}


/**
 * @brief Propagate the team's breakpoint state to every thread in the team.
 *
 * Walks the current team's thread list under the team lock and updates the
 * THREAD_FLAGS_BREAKPOINTS_DEFINED bit of each.
 */
static void
update_threads_breakpoints_flag()
{
	Team* team = thread_get_current_thread()->team;

	TeamLocker teamLocker(team);

	if (arch_has_breakpoints(&team->debug_info.arch_info)) {
		for (Thread* thread = team->thread_list.First(); thread != NULL;
				thread = team->thread_list.GetNext(thread)) {
			atomic_or(&thread->flags, THREAD_FLAGS_BREAKPOINTS_DEFINED);
		}
	} else {
		for (Thread* thread = team->thread_list.First(); thread != NULL;
				thread = team->thread_list.GetNext(thread)) {
			atomic_and(&thread->flags, ~THREAD_FLAGS_BREAKPOINTS_DEFINED);
		}
	}
}


/**
 * @brief Mirror the team's B_TEAM_DEBUG_DEBUGGER_INSTALLED bit into Thread::flags.
 *
 * @a thread must be the current thread.
 *
 * @param thread Thread whose flags to update.
 */
static void
update_thread_debugger_installed_flag(Thread* thread)
{
	Team* team = thread->team;

	if (atomic_get(&team->debug_info.flags) & B_TEAM_DEBUG_DEBUGGER_INSTALLED)
		atomic_or(&thread->flags, THREAD_FLAGS_DEBUGGER_INSTALLED);
	else
		atomic_and(&thread->flags, ~THREAD_FLAGS_DEBUGGER_INSTALLED);
}


/**
 * @brief Propagate the team's "debugger installed" state to every thread.
 *
 * The team's lock must be held by the caller.
 *
 * @param team Team whose threads should be updated.
 */
static void
update_threads_debugger_installed_flag(Team* team)
{
	if (atomic_get(&team->debug_info.flags) & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
		for (Thread* thread = team->thread_list.First(); thread != NULL;
				thread = team->thread_list.GetNext(thread)) {
			atomic_or(&thread->flags, THREAD_FLAGS_DEBUGGER_INSTALLED);
		}
	} else {
		for (Thread* thread = team->thread_list.First(); thread != NULL;
				thread = team->thread_list.GetNext(thread)) {
			atomic_and(&thread->flags, ~THREAD_FLAGS_DEBUGGER_INSTALLED);
		}
	}
}


/**
 * @brief Reset a team_debug_info to the "no debugger" state.
 *
 * For the first initialization the function must be called with @a initLock
 * set to true. If another thread might access the structure concurrently,
 * @c info->lock must be held when calling.
 *
 * @param info team_debug_info to reset.
 * @param initLock If true, initialize the spinlock and condition pointer.
 */
void
clear_team_debug_info(struct team_debug_info *info, bool initLock)
{
	if (info) {
		arch_clear_team_debug_info(&info->arch_info);
		atomic_set(&info->flags, B_TEAM_DEBUG_DEFAULT_FLAGS);
		info->debugger_team = -1;
		info->debugger_port = -1;
		info->nub_thread = -1;
		info->nub_port = -1;
		info->debugger_write_lock = -1;
		info->causing_thread = -1;
		info->image_event = 0;
		info->breakpoint_manager = NULL;

		if (initLock) {
			B_INITIALIZE_SPINLOCK(&info->lock);
			info->debugger_changed_condition = NULL;
		}
	}
}

/**
 * @brief Release resources owned by a team_debug_info.
 *
 * Must be called with interrupts enabled and without holding @c info->lock;
 * deletes the breakpoint manager, the debugger write lock, the nub port, and
 * waits for the nub thread. The usual pattern is: lock, copy the debug info,
 * clear_team_debug_info() on the original, unlock, then destroy the copy.
 *
 * @param info team_debug_info to destroy.
 */
static void
destroy_team_debug_info(struct team_debug_info *info)
{
	if (info) {
		arch_destroy_team_debug_info(&info->arch_info);

		// delete the breakpoint manager
		delete info->breakpoint_manager ;
		info->breakpoint_manager = NULL;

		// delete the debugger port write lock
		if (info->debugger_write_lock >= 0) {
			delete_sem(info->debugger_write_lock);
			info->debugger_write_lock = -1;
		}

		// delete the nub port
		if (info->nub_port >= 0) {
			set_port_owner(info->nub_port, B_CURRENT_TEAM);
			delete_port(info->nub_port);
			info->nub_port = -1;
		}

		// wait for the nub thread
		if (info->nub_thread >= 0) {
			if (info->nub_thread != thread_get_current_thread()->id) {
				int32 result;
				wait_for_thread(info->nub_thread, &result);
			}

			info->nub_thread = -1;
		}

		atomic_set(&info->flags, 0);
		info->debugger_team = -1;
		info->debugger_port = -1;
		info->causing_thread = -1;
		info->image_event = -1;
	}
}


/**
 * @brief Initialize a freshly allocated thread_debug_info to defaults.
 *
 * @param info thread_debug_info to initialize.
 */
void
init_thread_debug_info(struct thread_debug_info *info)
{
	if (info) {
		B_INITIALIZE_SPINLOCK(&info->lock);
		arch_clear_thread_debug_info(&info->arch_info);
		info->flags = B_THREAD_DEBUG_DEFAULT_FLAGS;
		info->debug_port = -1;
		info->ignore_signals = 0;
		info->ignore_signals_once = 0;
		info->profile.sample_area = -1;
		info->profile.interval = 0;
		info->profile.samples = NULL;
		info->profile.flush_needed = false;
		info->profile.installed_timer = NULL;
	}
}


/**
 * @brief Reset a thread_debug_info to the "not being debugged" state.
 *
 * Cancels the profiling timer if any and clears arch state. The caller must
 * hold the thread debug info lock.
 *
 * @param info thread_debug_info to clear.
 * @param dying If true, sets B_THREAD_DEBUG_DYING in the cleared flags.
 */
void
clear_thread_debug_info(struct thread_debug_info *info, bool dying)
{
	if (info) {
		// cancel profiling timer
		if (info->profile.installed_timer != NULL) {
			cancel_timer(info->profile.installed_timer);
			info->profile.installed_timer->hook = NULL;
			info->profile.installed_timer = NULL;
		}

		arch_clear_thread_debug_info(&info->arch_info);
		atomic_set(&info->flags,
			B_THREAD_DEBUG_DEFAULT_FLAGS | (dying ? B_THREAD_DEBUG_DYING : 0));
		info->debug_port = -1;
		info->ignore_signals = 0;
		info->ignore_signals_once = 0;
		info->profile.sample_area = -1;
		info->profile.interval = 0;
		info->profile.samples = NULL;
		info->profile.flush_needed = false;
	}
}


/**
 * @brief Tear down a thread_debug_info, releasing its sample area and debug port.
 *
 * Called when a thread dies or detaches from its debugger.
 *
 * @param info thread_debug_info to destroy.
 */
void
destroy_thread_debug_info(struct thread_debug_info *info)
{
	if (info) {
		area_id sampleArea = info->profile.sample_area;
		if (sampleArea >= 0) {
			area_info areaInfo;
			if (get_area_info(sampleArea, &areaInfo) == B_OK) {
				unlock_memory(areaInfo.address, areaInfo.size, B_READ_DEVICE);
				delete_area(sampleArea);
			}
		}

		arch_destroy_thread_debug_info(&info->arch_info);

		if (info->debug_port >= 0) {
			delete_port(info->debug_port);
			info->debug_port = -1;
		}

		info->ignore_signals = 0;
		info->ignore_signals_once = 0;

		atomic_set(&info->flags, 0);
	}
}


/**
 * @brief Acquire exclusive rights to change a team's debugger.
 *
 * Looks up the team by id, installs @a condition as the team's
 * debugger_changed_condition, and returns with the caller holding that
 * exclusive right. If another caller already owns it, waits on the existing
 * condition and retries. Enforces the invariant that debugger changes are
 * serialized per-team.
 *
 * @param teamID Target team id (or B_CURRENT_TEAM).
 * @param condition Condition variable the caller will notify in finish_debugger_change().
 * @param team [out] Pointer to the locked team on success (caller does not hold the team lock upon return).
 * @return B_OK, B_BAD_TEAM_ID, or B_NOT_ALLOWED for the kernel team.
 */
static status_t
prepare_debugger_change(team_id teamID, ConditionVariable& condition,
	Team*& team)
{
	// We look up the team by ID, even in case of the current team, so we can be
	// sure, that the team is not already dying.
	if (teamID == B_CURRENT_TEAM)
		teamID = thread_get_current_thread()->team->id;

	while (true) {
		// get the team
		team = Team::GetAndLock(teamID);
		if (team == NULL)
			return B_BAD_TEAM_ID;
		BReference<Team> teamReference(team, true);
		TeamLocker teamLocker(team, true);

		// don't allow messing with the kernel team
		if (team == team_get_kernel_team())
			return B_NOT_ALLOWED;

		// check whether the condition is already set
		InterruptsSpinLocker debugInfoLocker(team->debug_info.lock);

		if (team->debug_info.debugger_changed_condition == NULL) {
			// nobody there yet -- set our condition variable and be done
			team->debug_info.debugger_changed_condition = &condition;
			return B_OK;
		}

		// we'll have to wait
		ConditionVariableEntry entry;
		team->debug_info.debugger_changed_condition->Add(&entry);

		debugInfoLocker.Unlock();
		teamLocker.Unlock();

		entry.Wait();
	}
}


/**
 * @brief Acquire exclusive debugger-change rights for an already-known team.
 *
 * Same semantics as the team_id overload but skips the team lookup. Blocks
 * until the team has no other pending debugger change.
 *
 * @param team Target team.
 * @param condition Condition variable to install.
 */
static void
prepare_debugger_change(Team* team, ConditionVariable& condition)
{
	while (true) {
		// check whether the condition is already set
		InterruptsSpinLocker debugInfoLocker(team->debug_info.lock);

		if (team->debug_info.debugger_changed_condition == NULL) {
			// nobody there yet -- set our condition variable and be done
			team->debug_info.debugger_changed_condition = &condition;
			return;
		}

		// we'll have to wait
		ConditionVariableEntry entry;
		team->debug_info.debugger_changed_condition->Add(&entry);

		debugInfoLocker.Unlock();

		entry.Wait();
	}
}


/**
 * @brief Release debugger-change rights and wake any waiters.
 *
 * Clears the team's debugger_changed_condition and calls NotifyAll() on the
 * previously-held condition variable. Must pair with prepare_debugger_change().
 *
 * @param team Target team.
 */
static void
finish_debugger_change(Team* team)
{
	// unset our condition variable and notify all threads waiting on it
	InterruptsSpinLocker debugInfoLocker(team->debug_info.lock);

	ConditionVariable* condition = team->debug_info.debugger_changed_condition;
	team->debug_info.debugger_changed_condition = NULL;

	condition->NotifyAll();
}


/**
 * @brief Temporarily reparent the debug port before an exec() wipes team ports.
 *
 * exec_team() destroys all ports owned by the current team. If a debugger is
 * installed, transfers ownership of the thread's debug port to the kernel team
 * so it survives the exec; user_debug_finish_after_exec() restores ownership.
 */
void
user_debug_prepare_for_exec()
{
	Thread *thread = thread_get_current_thread();
	Team *team = thread->team;

	// If a debugger is installed for the team and the thread debug stuff
	// initialized, change the ownership of the debug port for the thread
	// to the kernel team, since exec_team() deletes all ports owned by this
	// team. We change the ownership back later.
	if (atomic_get(&team->debug_info.flags) & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
		// get the port
		port_id debugPort = -1;

		InterruptsSpinLocker threadDebugInfoLocker(thread->debug_info.lock);

		if ((thread->debug_info.flags & B_THREAD_DEBUG_INITIALIZED) != 0)
			debugPort = thread->debug_info.debug_port;

		threadDebugInfoLocker.Unlock();

		// set the new port ownership
		if (debugPort >= 0)
			set_port_owner(debugPort, team_get_kernel_team_id());
	}
}


/**
 * @brief Restore debug port ownership to the team after exec() completes.
 *
 * Counterpart to user_debug_prepare_for_exec(): hands the port back from the
 * kernel team to the now-exec'd team.
 */
void
user_debug_finish_after_exec()
{
	Thread *thread = thread_get_current_thread();
	Team *team = thread->team;

	// If a debugger is installed for the team and the thread debug stuff
	// initialized for this thread, change the ownership of its debug port
	// back to this team.
	if (atomic_get(&team->debug_info.flags) & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
		// get the port
		port_id debugPort = -1;

		InterruptsSpinLocker threadDebugInfoLocker(thread->debug_info.lock);

		if (thread->debug_info.flags & B_THREAD_DEBUG_INITIALIZED)
			debugPort = thread->debug_info.debug_port;

		threadDebugInfoLocker.Unlock();

		// set the new port ownership
		if (debugPort >= 0)
			set_port_owner(debugPort, team->id);
	}
}


/**
 * @brief Perform architecture-specific user-debugger initialization at boot.
 */
void
init_user_debug()
{
	#ifdef ARCH_INIT_USER_DEBUG
		ARCH_INIT_USER_DEBUG();
	#endif
}


/**
 * @brief Snapshot the current team's team_debug_info under its lock.
 *
 * Disables interrupts, takes the team debug info lock, memcpy()s the struct
 * onto the caller's stack, and releases. Used wherever code needs a stable
 * read of several fields at once.
 *
 * @param teamDebugInfo [out] Destination snapshot.
 */
static void
get_team_debug_info(team_debug_info &teamDebugInfo)
{
	Thread *thread = thread_get_current_thread();

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);

	memcpy(&teamDebugInfo, &thread->team->debug_info, sizeof(team_debug_info));

	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
	restore_interrupts(state);
}


/**
 * @brief Core debug-stop handler: forward an event to the debugger and block.
 *
 * Creates the per-thread debug port if needed, marks the thread stopped,
 * sends the event to the debugger port, then reads from the thread's debug
 * port in a loop handling B_DEBUGGED_THREAD_MESSAGE_* commands (continue,
 * get/set cpu state, debugger-changed). If the debugger changes mid-flight,
 * sets @a restart so the outer loop in thread_hit_debug_event() retries.
 *
 * @param event Debugger message code being delivered.
 * @param message Payload (must start with debug_origin which will be filled in).
 * @param size Size of @a message.
 * @param requireDebugger If true, returns B_ERROR when no debugger is installed.
 * @param restart [out] Set to true if the caller should re-invoke this function.
 * @return B_THREAD_DEBUG_HANDLE_EVENT / B_THREAD_DEBUG_IGNORE_EVENT, or a negative error.
 */
static status_t
thread_hit_debug_event_internal(debug_debugger_message event,
	const void *message, int32 size, bool requireDebugger, bool &restart)
{
	restart = false;
	Thread *thread = thread_get_current_thread();

	TRACE(("thread_hit_debug_event(): thread: %" B_PRId32 ", event: %" B_PRIu32
		", message: %p, size: %" B_PRId32 "\n", thread->id, (uint32)event,
		message, size));

	// check, if there's a debug port already
	bool setPort = !(atomic_get(&thread->debug_info.flags)
		& B_THREAD_DEBUG_INITIALIZED);

	// create a port, if there is none yet
	port_id port = -1;
	if (setPort) {
		char nameBuffer[128];
		snprintf(nameBuffer, sizeof(nameBuffer), "nub to thread %" B_PRId32,
			thread->id);

		port = create_port(1, nameBuffer);
		if (port < 0) {
			dprintf("thread_hit_debug_event(): Failed to create debug port: "
				"%s\n", strerror(port));
			return port;
		}
	}

	// check the debug info structures once more: get the debugger port, set
	// the thread's debug port, and update the thread's debug flags
	port_id deletePort = port;
	port_id debuggerPort = -1;
	port_id nubPort = -1;
	status_t error = B_OK;
	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
	SpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	uint32 threadFlags = thread->debug_info.flags;
	threadFlags &= ~B_THREAD_DEBUG_STOP;
	bool debuggerInstalled
		= (thread->team->debug_info.flags & B_TEAM_DEBUG_DEBUGGER_INSTALLED);
	if (thread->id == thread->team->debug_info.nub_thread) {
		// Ugh, we're the nub thread. We shouldn't be here.
		TRACE(("thread_hit_debug_event(): Misdirected nub thread: %" B_PRId32
			"\n", thread->id));

		error = B_ERROR;
	} else if (debuggerInstalled || !requireDebugger) {
		if (debuggerInstalled) {
			debuggerPort = thread->team->debug_info.debugger_port;
			nubPort = thread->team->debug_info.nub_port;
		}

		if (setPort) {
			if (threadFlags & B_THREAD_DEBUG_INITIALIZED) {
				// someone created a port for us (the port we've created will
				// be deleted below)
				port = thread->debug_info.debug_port;
			} else {
				thread->debug_info.debug_port = port;
				deletePort = -1;	// keep the port
				threadFlags |= B_THREAD_DEBUG_INITIALIZED;
			}
		} else {
			if (threadFlags & B_THREAD_DEBUG_INITIALIZED) {
				port = thread->debug_info.debug_port;
			} else {
				// someone deleted our port
				error = B_ERROR;
			}
		}
	} else
		error = B_ERROR;

	// update the flags
	if (error == B_OK)
		threadFlags |= B_THREAD_DEBUG_STOPPED;
	atomic_set(&thread->debug_info.flags, threadFlags);

	update_thread_user_debug_flag(thread);

	threadDebugInfoLocker.Unlock();
	RELEASE_TEAM_DEBUG_INFO_LOCK(thread->team->debug_info);
	restore_interrupts(state);

	// delete the superfluous port
	if (deletePort >= 0)
		delete_port(deletePort);

	if (error != B_OK) {
		TRACE(("thread_hit_debug_event() error: thread: %" B_PRId32 ", error: "
			"%" B_PRIx32 "\n", thread->id, error));
		return error;
	}

	// send a message to the debugger port
	if (debuggerInstalled) {
		// update the message's origin info first
		debug_origin *origin = (debug_origin *)message;
		origin->thread = thread->id;
		origin->team = thread->team->id;
		origin->nub_port = nubPort;

		TRACE(("thread_hit_debug_event(): thread: %" B_PRId32 ", sending "
			"message to debugger port %" B_PRId32 "\n", thread->id,
			debuggerPort));

		error = debugger_write(debuggerPort, event, message, size, false);
	}

	status_t result = B_THREAD_DEBUG_HANDLE_EVENT;
	bool singleStep = false;

	if (error == B_OK) {
		bool done = false;
		while (!done) {
			// read a command from the debug port
			int32 command;
			debugged_thread_message_data commandMessage;
			ssize_t commandMessageSize = read_port_etc(port, &command,
				&commandMessage, sizeof(commandMessage), B_KILL_CAN_INTERRUPT,
				0);

			if (commandMessageSize < 0) {
				error = commandMessageSize;
				TRACE(("thread_hit_debug_event(): thread: %" B_PRId32 ", failed "
					"to receive message from port %" B_PRId32 ": %" B_PRIx32 "\n",
					thread->id, port, error));
				break;
			}

			switch (command) {
				/** @brief Unblock this stopped thread and decide single-step/handle-event. */
				case B_DEBUGGED_THREAD_MESSAGE_CONTINUE:
					TRACE(("thread_hit_debug_event(): thread: %" B_PRId32 ": "
						"B_DEBUGGED_THREAD_MESSAGE_CONTINUE\n",
						thread->id));
					result = commandMessage.continue_thread.handle_event;

					singleStep = commandMessage.continue_thread.single_step;
					done = true;
					break;

				/** @brief Overwrite this thread's CPU state in place. */
				case B_DEBUGGED_THREAD_SET_CPU_STATE:
				{
					TRACE(("thread_hit_debug_event(): thread: %" B_PRId32 ": "
						"B_DEBUGGED_THREAD_SET_CPU_STATE\n",
						thread->id));
					arch_set_debug_cpu_state(
						&commandMessage.set_cpu_state.cpu_state);

					break;
				}

				/** @brief Send this thread's CPU state back to the reply port. */
				case B_DEBUGGED_THREAD_GET_CPU_STATE:
				{
					port_id replyPort = commandMessage.get_cpu_state.reply_port;

					// prepare the message
					debug_nub_get_cpu_state_reply replyMessage;
					replyMessage.error = B_OK;
					replyMessage.message = event;
					arch_get_debug_cpu_state(&replyMessage.cpu_state);

					// send it
					error = kill_interruptable_write_port(replyPort, event,
						&replyMessage, sizeof(replyMessage));

					break;
				}

				/** @brief Notification that the team's debugger changed; re-evaluate and maybe restart. */
				case B_DEBUGGED_THREAD_DEBUGGER_CHANGED:
				{
					// Check, if the debugger really changed, i.e. is different
					// than the one we know.
					team_debug_info teamDebugInfo;
					get_team_debug_info(teamDebugInfo);

					if (teamDebugInfo.flags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
						if (!debuggerInstalled
							|| teamDebugInfo.debugger_port != debuggerPort) {
							// debugger was installed or has changed: restart
							// this function
							restart = true;
							done = true;
						}
					} else {
						if (debuggerInstalled) {
							// debugger is gone: continue the thread normally
							done = true;
						}
					}

					break;
				}
			}
		}
	} else {
		TRACE(("thread_hit_debug_event(): thread: %" B_PRId32 ", failed to send "
			"message to debugger port %" B_PRId32 ": %" B_PRIx32 "\n",
			thread->id, debuggerPort, error));
	}

	// update the thread debug info
	bool destroyThreadInfo = false;
	thread_debug_info threadDebugInfo;

	state = disable_interrupts();
	threadDebugInfoLocker.Lock();

	// check, if the team is still being debugged
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
		// update the single-step flag
		if (singleStep) {
			atomic_or(&thread->debug_info.flags,
				B_THREAD_DEBUG_SINGLE_STEP);
			atomic_or(&thread->flags, THREAD_FLAGS_SINGLE_STEP);
		} else {
			atomic_and(&thread->debug_info.flags,
				~(int32)B_THREAD_DEBUG_SINGLE_STEP);
		}

		// unset the "stopped" state
		atomic_and(&thread->debug_info.flags, ~B_THREAD_DEBUG_STOPPED);

		update_thread_user_debug_flag(thread);
	} else {
		// the debugger is gone: cleanup our info completely
		threadDebugInfo = thread->debug_info;
		clear_thread_debug_info(&thread->debug_info, false);
		destroyThreadInfo = true;
	}

	threadDebugInfoLocker.Unlock();
	restore_interrupts(state);

	// enable/disable single stepping
	arch_update_thread_single_step();

	if (destroyThreadInfo)
		destroy_thread_debug_info(&threadDebugInfo);

	return (error == B_OK ? result : error);
}


/**
 * @brief Deliver a debug event and handle debugger-change restarts.
 *
 * Drives thread_hit_debug_event_internal() in a loop until it settles, then
 * asks the BreakpointManager to reinstate any breakpoint that was uninstalled
 * for the single-step.
 *
 * @param event Debugger message code.
 * @param message Payload.
 * @param size Payload size.
 * @param requireDebugger If true, fail when no debugger is installed.
 * @return B_THREAD_DEBUG_HANDLE_EVENT, B_THREAD_DEBUG_IGNORE_EVENT, or an error.
 */
static status_t
thread_hit_debug_event(debug_debugger_message event, const void *message,
	int32 size, bool requireDebugger)
{
	status_t result;
	bool restart;
	do {
		restart = false;
		result = thread_hit_debug_event_internal(event, message, size,
			requireDebugger, restart);
	} while (result >= 0 && restart);

	// Prepare to continue -- we install a debugger change condition, so no one
	// will change the debugger while we're playing with the breakpoint manager.
	// TODO: Maybe better use ref-counting and a flag in the breakpoint manager.
	Team* team = thread_get_current_thread()->team;
	ConditionVariable debugChangeCondition;
	debugChangeCondition.Init(team, "debug change condition");
	prepare_debugger_change(team, debugChangeCondition);

	if (team->debug_info.breakpoint_manager != NULL) {
		bool isSyscall;
		void* pc = arch_debug_get_interrupt_pc(&isSyscall);
		if (pc != NULL && !isSyscall)
			team->debug_info.breakpoint_manager->PrepareToContinue(pc);
	}

	finish_debugger_change(team);

	return result;
}


/**
 * @brief Deliver a "serious" debug event, auto-installing the default debugger.
 *
 * Used for exceptions, breakpoint/watchpoint hits, and single steps, where we
 * must stop and cannot silently ignore.
 *
 * @param event Debugger message code.
 * @param message Payload.
 * @param messageSize Payload size.
 * @return Result of thread_hit_debug_event(), or the install error.
 */
static status_t
thread_hit_serious_debug_event(debug_debugger_message event,
	const void *message, int32 messageSize)
{
	// ensure that a debugger is installed for this team
	status_t error = ensure_debugger_installed();
	if (error != B_OK) {
		Thread *thread = thread_get_current_thread();
		dprintf("thread_hit_serious_debug_event(): Failed to install debugger: "
			"thread: %" B_PRId32 " (%s): %s\n", thread->id, thread->name,
			strerror(error));
		return error;
	}

	// enter the debug loop
	return thread_hit_debug_event(event, message, messageSize, true);
}


/**
 * @brief Deliver a pre-syscall event to the debugger, if subscribed.
 *
 * If B_TEAM_DEBUG_PRE_SYSCALL (or the per-thread equivalent) is set, sends
 * B_DEBUGGER_MESSAGE_PRE_SYSCALL with the syscall number and args; otherwise
 * just captures the start time for post-syscall timing.
 *
 * @param syscall Syscall number.
 * @param args Pointer to the syscall argument block.
 */
void
user_debug_pre_syscall(uint32 syscall, void *args)
{
	// check whether a debugger is installed
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (!(teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED))
		return;

	// check whether pre-syscall tracing is enabled for team or thread
	int32 threadDebugFlags = atomic_get(&thread->debug_info.flags);
	if ((teamDebugFlags & B_TEAM_DEBUG_PRE_SYSCALL)
			|| (threadDebugFlags & B_THREAD_DEBUG_PRE_SYSCALL)) {
		// prepare the message
		debug_pre_syscall message;
		message.syscall = syscall;

		// copy the syscall args
		if (syscall < (uint32)kSyscallCount) {
			if (kSyscallInfos[syscall].parameter_size > 0)
				memcpy(message.args, args, kSyscallInfos[syscall].parameter_size);
		}

		thread_hit_debug_event(B_DEBUGGER_MESSAGE_PRE_SYSCALL, &message,
			sizeof(message), true);
	}

	if ((teamDebugFlags & B_TEAM_DEBUG_POST_SYSCALL)
			|| (threadDebugFlags & B_THREAD_DEBUG_POST_SYSCALL)) {
		// The syscall_start_time storage is shared with the profiler's interval.
		if (thread->debug_info.profile.samples == NULL)
			thread->debug_info.profile.syscall_start_time = system_time();
	}
}


/**
 * @brief Deliver a post-syscall event and flush pending profile samples.
 *
 * Flushes the profiling buffer if a sample batch is waiting, then, if
 * post-syscall tracing is enabled, sends B_DEBUGGER_MESSAGE_POST_SYSCALL with
 * start/end times and return value.
 *
 * @param syscall Syscall number.
 * @param args Pointer to the syscall argument block.
 * @param returnValue The syscall's return value.
 */
void
user_debug_post_syscall(uint32 syscall, void *args, uint64 returnValue)
{
	// check whether a debugger is installed
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (!(teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED))
		return;

	// check if we need to flush the profiling buffer
	if (thread->debug_info.profile.flush_needed)
		profiling_flush(NULL);

	// check whether post-syscall tracing is enabled for team or thread
	int32 threadDebugFlags = atomic_get(&thread->debug_info.flags);
	if (!(teamDebugFlags & B_TEAM_DEBUG_POST_SYSCALL)
			&& !(threadDebugFlags & B_THREAD_DEBUG_POST_SYSCALL)) {
		return;
	}

	bigtime_t startTime = 0;
	if (thread->debug_info.profile.samples == NULL) {
		startTime = thread->debug_info.profile.syscall_start_time;
		thread->debug_info.profile.syscall_start_time = 0;
	}

	// prepare the message
	debug_post_syscall message;
	message.start_time = startTime;
	message.end_time = system_time();
	message.return_value = returnValue;
	message.syscall = syscall;

	// copy the syscall args
	if (syscall < (uint32)kSyscallCount) {
		if (kSyscallInfos[syscall].parameter_size > 0)
			memcpy(message.args, args, kSyscallInfos[syscall].parameter_size);
	}

	thread_hit_debug_event(B_DEBUGGER_MESSAGE_POST_SYSCALL, &message,
		sizeof(message), true);
}


/**
 * @brief Handle an unhandled processor exception by consulting the debugger.
 *
 * If a user signal handler is already installed for @a signal we just let the
 * signal through. Otherwise sends B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED and
 * reports whether the debugger wants the normal deadly-signal path or has
 * patched up the cause.
 *
 * @param exception The fault kind (debug_why_stopped value).
 * @param signal The signal that would correspond to the exception.
 * @return true if the caller should send the signal; false if the debugger continues.
 */
bool
user_debug_exception_occurred(debug_exception_type exception, int signal)
{
	// First check whether there's a signal handler installed for the signal.
	// If so, we don't want to install a debugger for the team. We always send
	// the signal instead. An already installed debugger will be notified, if
	// it has requested notifications of signal.
	struct sigaction signalAction;
	if (sigaction(signal, NULL, &signalAction) == 0
		&& signalAction.sa_handler != SIG_DFL) {
		return true;
	}

	// prepare the message
	debug_exception_occurred message;
	message.exception = exception;
	message.signal = signal;

	status_t result = thread_hit_serious_debug_event(
		B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED, &message, sizeof(message));
	return (result != B_THREAD_DEBUG_IGNORE_EVENT);
}


/**
 * @brief Notify the debugger of an incoming signal; callable from signal-delivery context.
 *
 * Only delivers if the team has both B_TEAM_DEBUG_DEBUGGER_INSTALLED and
 * B_TEAM_DEBUG_SIGNALS set. The debugger may ask for the signal to be
 * suppressed by returning B_THREAD_DEBUG_IGNORE_EVENT.
 *
 * @param signal Signal number.
 * @param handler Installed sigaction.
 * @param info siginfo_t describing the signal.
 * @param deadly Whether the signal's default action is to terminate the team.
 * @return true to deliver the signal normally, false to suppress it.
 */
bool
user_debug_handle_signal(int signal, struct sigaction *handler, siginfo_t *info,
	bool deadly)
{
	// check, if a debugger is installed and is interested in signals
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (~teamDebugFlags
		& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_SIGNALS)) {
		return true;
	}

	// prepare the message
	debug_signal_received message;
	message.signal = signal;
	message.handler = *handler;
	message.info = *info;
	message.deadly = deadly;

	status_t result = thread_hit_debug_event(B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED,
		&message, sizeof(message), true);
	return (result != B_THREAD_DEBUG_IGNORE_EVENT);
}


/**
 * @brief Enter the debugger because the thread was asked to stop.
 *
 * Detects whether this was an emulated single-step notification (in which
 * case it delegates to user_debug_single_stepped()) and otherwise sends
 * B_DEBUGGER_MESSAGE_THREAD_DEBUGGED.
 */
void
user_debug_stop_thread()
{
	// check whether this is actually an emulated single-step notification
	Thread* thread = thread_get_current_thread();
	InterruptsSpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	bool singleStepped = false;
	if ((atomic_and(&thread->debug_info.flags,
				~B_THREAD_DEBUG_NOTIFY_SINGLE_STEP)
			& B_THREAD_DEBUG_NOTIFY_SINGLE_STEP) != 0) {
		singleStepped = true;
	}

	threadDebugInfoLocker.Unlock();

	if (singleStepped) {
		user_debug_single_stepped();
	} else {
		debug_thread_debugged message;
		thread_hit_serious_debug_event(B_DEBUGGER_MESSAGE_THREAD_DEBUGGED,
			&message, sizeof(message));
	}
}


/**
 * @brief Notify the debugger that a child team was created.
 *
 * No-op unless the parent team's debugger subscribed to team-creation events.
 *
 * @param teamID Id of the newly created team.
 */
void
user_debug_team_created(team_id teamID)
{
	// check, if a debugger is installed and is interested in team creation
	// events
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (~teamDebugFlags
		& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_TEAM_CREATION)) {
		return;
	}

	// prepare the message
	debug_team_created message;
	message.new_team = teamID;

	thread_hit_debug_event(B_DEBUGGER_MESSAGE_TEAM_CREATED, &message,
		sizeof(message), true);
}


/**
 * @brief Notify a debugger that the team it was attached to has been deleted.
 *
 * Called on team teardown; bypasses debugger_write() because the current
 * thread no longer belongs to the debugged team.
 *
 * @param teamID Id of the deleted team.
 * @param debuggerPort Port to send the notification to (ignored if < 0).
 * @param status Exit status.
 * @param signal Terminating signal, or 0.
 * @param usageInfo Per-team CPU/memory accounting (may be NULL).
 */
void
user_debug_team_deleted(team_id teamID, port_id debuggerPort, status_t status, int signal,
	team_usage_info* usageInfo)
{
	if (debuggerPort >= 0) {
		TRACE(("user_debug_team_deleted(team: %" B_PRId32 ", debugger port: "
			"%" B_PRId32 ")\n", teamID, debuggerPort));

		debug_team_deleted message;
		message.origin.thread = -1;
		message.origin.team = teamID;
		message.origin.nub_port = -1;
		message.status = status;
		message.signal = signal;
		message.usage = *usageInfo;
		write_port_etc(debuggerPort, B_DEBUGGER_MESSAGE_TEAM_DELETED, &message,
			sizeof(message), B_RELATIVE_TIMEOUT, 0);
	}
}


/**
 * @brief Notify the debugger that the team has successfully exec()'d.
 *
 * Bumps the image_event counter used to correlate profiler samples with
 * image state.
 */
void
user_debug_team_exec()
{
	// check, if a debugger is installed and is interested in team creation
	// events
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (~teamDebugFlags
		& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_TEAM_CREATION)) {
		return;
	}

	// prepare the message
	debug_team_exec message;
	message.image_event = atomic_add(&thread->team->debug_info.image_event, 1)
		+ 1;

	thread_hit_debug_event(B_DEBUGGER_MESSAGE_TEAM_EXEC, &message,
		sizeof(message), true);
}


/**
 * @brief Sync a newly created thread's debug-related Thread::flags bits.
 *
 * Called by a freshly created userland thread before it first returns to
 * user space.
 *
 * @param thread The calling thread.
 */
void
user_debug_update_new_thread_flags(Thread* thread)
{
	// lock it and update it's flags
	InterruptsSpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	update_thread_user_debug_flag(thread);
	update_thread_breakpoints_flag(thread);
	update_thread_debugger_installed_flag(thread);
}


/**
 * @brief Notify the debugger that a new thread was spawned in the team.
 *
 * @param threadID Id of the new thread.
 */
void
user_debug_thread_created(thread_id threadID)
{
	// check, if a debugger is installed and is interested in thread events
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (~teamDebugFlags
		& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_THREADS)) {
		return;
	}

	// prepare the message
	debug_thread_created message;
	message.new_thread = threadID;

	thread_hit_debug_event(B_DEBUGGER_MESSAGE_THREAD_CREATED, &message,
		sizeof(message), true);
}


/**
 * @brief Notify the debugger that a thread has exited.
 *
 * Runs after the thread has already been reparented to the kernel team, so
 * we can't use debugger_write(); instead acquires the debugger write lock
 * directly and sends B_DEBUGGER_MESSAGE_THREAD_DELETED.
 *
 * @param teamID Team the thread belonged to.
 * @param threadID Exiting thread id.
 * @param status Exit status.
 */
void
user_debug_thread_deleted(team_id teamID, thread_id threadID, status_t status)
{
	// Things are a bit complicated here, since this thread no longer belongs to
	// the debugged team (but to the kernel). So we can't use debugger_write().

	// get the team debug flags and debugger port
	Team* team = Team::Get(teamID);
	if (team == NULL)
		return;
	BReference<Team> teamReference(team, true);

	InterruptsSpinLocker debugInfoLocker(team->debug_info.lock);

	int32 teamDebugFlags = atomic_get(&team->debug_info.flags);
	port_id debuggerPort = team->debug_info.debugger_port;
	sem_id writeLock = team->debug_info.debugger_write_lock;

	debugInfoLocker.Unlock();

	// check, if a debugger is installed and is interested in thread events
	if (~teamDebugFlags
		& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_THREADS)) {
		return;
	}

	// acquire the debugger write lock
	status_t error = acquire_sem_etc(writeLock, 1, B_KILL_CAN_INTERRUPT, 0);
	if (error != B_OK)
		return;

	// re-get the team debug info -- we need to check whether anything changed
	debugInfoLocker.Lock();

	teamDebugFlags = atomic_get(&team->debug_info.flags);
	port_id newDebuggerPort = team->debug_info.debugger_port;

	debugInfoLocker.Unlock();

	// Send the message only if the debugger hasn't changed in the meantime or
	// the team is about to be handed over.
	if (newDebuggerPort == debuggerPort
		|| (teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_HANDOVER) == 0) {
		debug_thread_deleted message;
		message.origin.thread = threadID;
		message.origin.team = teamID;
		message.origin.nub_port = -1;
		message.status = status;

		write_port_etc(debuggerPort, B_DEBUGGER_MESSAGE_THREAD_DELETED,
			&message, sizeof(message), B_KILL_CAN_INTERRUPT, 0);
	}

	// release the debugger write lock
	release_sem(writeLock);
}


/**
 * @brief Detach profile buffers and send a final profiler update for a dying thread.
 *
 * Called on the current thread's teardown path. Holds the thread debug info
 * lock while unlinking the sample buffer, then sends
 * B_DEBUGGER_MESSAGE_PROFILER_UPDATE with the remaining samples before the
 * sample area is destroyed.
 *
 * @param thread The current thread, which is about to die.
 */
void
user_debug_thread_exiting(Thread* thread)
{
	// thread is the current thread, so using team is safe
	Team* team = thread->team;

	InterruptsLocker interruptsLocker;

	GRAB_TEAM_DEBUG_INFO_LOCK(team->debug_info);

	int32 teamDebugFlags = atomic_get(&team->debug_info.flags);
	port_id debuggerPort = team->debug_info.debugger_port;

	RELEASE_TEAM_DEBUG_INFO_LOCK(team->debug_info);

	// check, if a debugger is installed
	if ((teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) == 0
		|| debuggerPort < 0) {
		return;
	}

	// detach the profile info and mark the thread dying
	SpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	thread_debug_info& threadDebugInfo = thread->debug_info;
	if (threadDebugInfo.profile.samples == NULL)
		return;

	area_id sampleArea = threadDebugInfo.profile.sample_area;
	int32 sampleCount = threadDebugInfo.profile.sample_count;
	int32 droppedTicks = threadDebugInfo.profile.dropped_ticks;
	int32 stackDepth = threadDebugInfo.profile.stack_depth;
	bool variableStackDepth = threadDebugInfo.profile.variable_stack_depth;
	int32 imageEvent = threadDebugInfo.profile.image_event;
	threadDebugInfo.profile.sample_area = -1;
	threadDebugInfo.profile.samples = NULL;
	threadDebugInfo.profile.flush_needed = false;
	bigtime_t lastCPUTime; {
		SpinLocker threadTimeLocker(thread->time_lock);
		lastCPUTime = thread->CPUTime(false);
	}

	atomic_or(&threadDebugInfo.flags, B_THREAD_DEBUG_DYING);

	threadDebugInfoLocker.Unlock();
	interruptsLocker.Unlock();

	// notify the debugger
	debug_profiler_update message;
	message.origin.thread = thread->id;
	message.origin.team = thread->team->id;
	message.origin.nub_port = -1;	// asynchronous message
	message.sample_count = sampleCount;
	message.dropped_ticks = droppedTicks;
	message.stack_depth = stackDepth;
	message.variable_stack_depth = variableStackDepth;
	message.image_event = imageEvent;
	message.stopped = true;
	message.last_cpu_time = lastCPUTime;
	debugger_write(debuggerPort, B_DEBUGGER_MESSAGE_PROFILER_UPDATE,
		&message, sizeof(message), false);

	if (sampleArea >= 0) {
		area_info areaInfo;
		if (get_area_info(sampleArea, &areaInfo) == B_OK) {
			unlock_memory(areaInfo.address, areaInfo.size, B_READ_DEVICE);
			delete_area(sampleArea);
		}
	}
}


/**
 * @brief Notify the debugger that an image (shared object) was loaded.
 *
 * Also bumps the team's image_event counter so profilers can correlate.
 *
 * @param imageInfo Info for the newly loaded image.
 */
void
user_debug_image_created(const image_info *imageInfo)
{
	// check, if a debugger is installed and is interested in image events
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (~teamDebugFlags
		& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_IMAGES)) {
		return;
	}

	// prepare the message
	debug_image_created message;
	memcpy(&message.info, imageInfo, sizeof(image_info));
	message.image_event = atomic_add(&thread->team->debug_info.image_event, 1)
		+ 1;

	thread_hit_debug_event(B_DEBUGGER_MESSAGE_IMAGE_CREATED, &message,
		sizeof(message), true);
}


/**
 * @brief Notify the debugger that an image was unloaded.
 *
 * @param imageInfo Info for the image being removed.
 */
void
user_debug_image_deleted(const image_info *imageInfo)
{
	// check, if a debugger is installed and is interested in image events
	Thread *thread = thread_get_current_thread();
	int32 teamDebugFlags = atomic_get(&thread->team->debug_info.flags);
	if (~teamDebugFlags
		& (B_TEAM_DEBUG_DEBUGGER_INSTALLED | B_TEAM_DEBUG_IMAGES)) {
		return;
	}

	// prepare the message
	debug_image_deleted message;
	memcpy(&message.info, imageInfo, sizeof(image_info));
	message.image_event = atomic_add(&thread->team->debug_info.image_event, 1)
		+ 1;

	thread_hit_debug_event(B_DEBUGGER_MESSAGE_IMAGE_DELETED, &message,
		sizeof(message), true);
}


/**
 * @brief Report a breakpoint hit to the debugger.
 *
 * Snapshots CPU state and sends B_DEBUGGER_MESSAGE_BREAKPOINT_HIT; this is a
 * "serious" event so the debugger is auto-installed if missing.
 *
 * @param software Whether the breakpoint came from a software trap (informational).
 */
void
user_debug_breakpoint_hit(bool software)
{
	// prepare the message
	debug_breakpoint_hit message;
	arch_get_debug_cpu_state(&message.cpu_state);

	thread_hit_serious_debug_event(B_DEBUGGER_MESSAGE_BREAKPOINT_HIT, &message,
		sizeof(message));
}


/**
 * @brief Report a watchpoint hit to the debugger with current CPU state.
 */
void
user_debug_watchpoint_hit()
{
	// prepare the message
	debug_watchpoint_hit message;
	arch_get_debug_cpu_state(&message.cpu_state);

	thread_hit_serious_debug_event(B_DEBUGGER_MESSAGE_WATCHPOINT_HIT, &message,
		sizeof(message));
}


/**
 * @brief Report a single-step completion to the debugger.
 *
 * Clears the single-step thread flag and sends B_DEBUGGER_MESSAGE_SINGLE_STEP.
 */
void
user_debug_single_stepped()
{
	// clear the single-step thread flag
	Thread* thread = thread_get_current_thread();
	atomic_and(&thread->flags, ~(int32)THREAD_FLAGS_SINGLE_STEP);

	// prepare the message
	debug_single_step message;
	arch_get_debug_cpu_state(&message.cpu_state);

	thread_hit_serious_debug_event(B_DEBUGGER_MESSAGE_SINGLE_STEP, &message,
		sizeof(message));
}


/**
 * @brief Arm the per-CPU profiling timer for the current thread.
 *
 * The caller must hold the thread's debug info lock.
 *
 * @param thread The current thread.
 * @param interval Relative delay until the timer should fire.
 */
static void
schedule_profiling_timer(Thread* thread, bigtime_t interval)
{
	struct timer* timer = &sProfilingTimers[thread->cpu->cpu_num];
	// Use the "hook" field to sanity-check that this timer is not scheduled.
	ASSERT(timer->hook == NULL);
	thread->debug_info.profile.installed_timer = timer;
	thread->debug_info.profile.timer_end = system_time() + interval;
	add_timer(timer, &profiling_event, interval, B_ONE_SHOT_RELATIVE_TIMER);
}


/**
 * @brief Return the time remaining on the current profiling timer.
 *
 * The caller must hold the thread's debug info lock.
 *
 * @param thread The current thread.
 * @return Microseconds until the timer fires (may be negative).
 */
static bigtime_t
profiling_timer_left(Thread* thread)
{
	return thread->debug_info.profile.timer_end - system_time();
}


/**
 * @brief Record one profile sample (PC or stack trace) into the thread's buffer.
 *
 * Handles variable/fixed stack depth, image-event markers, and marks the
 * buffer as needing a flush when the threshold is crossed. The caller must
 * hold the thread's debug info lock.
 *
 * @return Whether the profiling timer should be rescheduled.
 */
static bool
profiling_do_sample()
{
	Thread* thread = thread_get_current_thread();
	thread_debug_info& debugInfo = thread->debug_info;

	if (debugInfo.profile.samples == NULL)
		return false;

	// Check, whether the buffer is full or an image event occurred since the
	// last sample was taken.
	int32 maxSamples = debugInfo.profile.max_samples;
	int32 sampleCount = debugInfo.profile.sample_count;
	int32 stackDepth = debugInfo.profile.stack_depth;
	int32 imageEvent = thread->team->debug_info.image_event;
	if (debugInfo.profile.sample_count > 0) {
		if (debugInfo.profile.last_image_event < imageEvent
			&& debugInfo.profile.variable_stack_depth
			&& sampleCount + 2 <= maxSamples) {
			// an image event occurred, but we use variable stack depth and
			// have enough room in the buffer to indicate an image event
			addr_t* event = debugInfo.profile.samples + sampleCount;
			event[0] = B_DEBUG_PROFILE_IMAGE_EVENT;
			event[1] = imageEvent;
			sampleCount += 2;
			debugInfo.profile.sample_count = sampleCount;
			debugInfo.profile.last_image_event = imageEvent;
		}

		if (debugInfo.profile.last_image_event < imageEvent
				|| debugInfo.profile.flush_threshold - sampleCount < stackDepth) {
			debugInfo.profile.flush_needed = true;

			// If the buffer is not full yet, we add the samples,
			// otherwise we have to drop them.
			if (maxSamples - sampleCount < stackDepth) {
				debugInfo.profile.dropped_ticks++;
				return true;
			}
		}
	} else {
		// first sample -- set the image event
		debugInfo.profile.image_event = imageEvent;
		debugInfo.profile.last_image_event = imageEvent;
	}

	// get the samples
	uint32 flags = STACK_TRACE_USER;
	int32 skipIFrames = 0;
	if (debugInfo.profile.profile_kernel) {
		flags |= STACK_TRACE_KERNEL;
		skipIFrames = 1;
	}

	addr_t* returnAddresses = debugInfo.profile.samples
		+ debugInfo.profile.sample_count;
	if (debugInfo.profile.variable_stack_depth) {
		// variable sample count per hit
		*returnAddresses = arch_debug_get_stack_trace(returnAddresses + 1,
			stackDepth - 1, skipIFrames, 0, flags);

		debugInfo.profile.sample_count += *returnAddresses + 1;
	} else {
		// fixed sample count per hit
		if (stackDepth > 1 || !debugInfo.profile.profile_kernel) {
			int32 count = arch_debug_get_stack_trace(returnAddresses,
				stackDepth, skipIFrames, 0, flags);

			for (int32 i = count; i < stackDepth; i++)
				returnAddresses[i] = 0;
		} else
			*returnAddresses = (addr_t)arch_debug_get_interrupt_pc(NULL);

		debugInfo.profile.sample_count += stackDepth;
	}

	return true;
}


/**
 * @brief Flush accumulated profile samples to the debugger.
 *
 * May be installed as a post_interrupt_callback, so it tolerates entering
 * with interrupts either enabled or disabled. Re-arms the profiling timer
 * if the thread is still being profiled after the flush.
 */
static void
profiling_flush(void*)
{
	// This function may be called as a post_interrupt_callback. When it is,
	// it is undefined whether the function is called with interrupts enabled
	// or disabled. (When called elsewhere, interrupts will always be enabled.)
	// We are allowed to enable interrupts, though. First make sure interrupts
	// are disabled.
	disable_interrupts();

	Thread* thread = thread_get_current_thread();
	thread_debug_info& debugInfo = thread->debug_info;

	SpinLocker threadDebugInfoLocker(debugInfo.lock);

	if (debugInfo.profile.samples != NULL && debugInfo.profile.flush_needed) {
		int32 sampleCount = debugInfo.profile.sample_count;
		int32 droppedTicks = debugInfo.profile.dropped_ticks;
		int32 stackDepth = debugInfo.profile.stack_depth;
		bool variableStackDepth = debugInfo.profile.variable_stack_depth;
		int32 imageEvent = debugInfo.profile.image_event;

		// prevent the timer from running until after we flush
		bigtime_t interval = debugInfo.profile.interval;
		if (debugInfo.profile.installed_timer != NULL) {
			interval = max_c(profiling_timer_left(thread), 0);
			cancel_timer(debugInfo.profile.installed_timer);
			debugInfo.profile.installed_timer->hook = NULL;
			debugInfo.profile.installed_timer = NULL;
		}
		debugInfo.profile.interval_left = -1;

		// notify the debugger
		debugInfo.profile.sample_count = 0;
		debugInfo.profile.dropped_ticks = 0;
		debugInfo.profile.flush_needed = false;

		threadDebugInfoLocker.Unlock();
		enable_interrupts();

		// prepare the message
		debug_profiler_update message;
		message.sample_count = sampleCount;
		message.dropped_ticks = droppedTicks;
		message.stack_depth = stackDepth;
		message.variable_stack_depth = variableStackDepth;
		message.image_event = imageEvent;
		message.stopped = false;

		thread_hit_debug_event(B_DEBUGGER_MESSAGE_PROFILER_UPDATE, &message,
			sizeof(message), false);

		disable_interrupts();
		threadDebugInfoLocker.Lock();
		if (debugInfo.profile.samples != NULL)
			schedule_profiling_timer(thread, interval);
	}

	threadDebugInfoLocker.Unlock();
	enable_interrupts();
}


/**
 * @brief Profiling timer callback: take a sample and schedule the next tick.
 *
 * Runs in timer (interrupt) context. If a flush is needed and we interrupted
 * user code, defers the flush via post_interrupt_callback so it can block
 * safely; otherwise just rearms the timer.
 *
 * @return B_HANDLED_INTERRUPT.
 */
static int32
profiling_event(timer* /*unused*/)
{
	Thread* thread = thread_get_current_thread();
	thread_debug_info& debugInfo = thread->debug_info;

	SpinLocker threadDebugInfoLocker(debugInfo.lock);
	debugInfo.profile.installed_timer->hook = NULL;
	debugInfo.profile.installed_timer = NULL;

	if (profiling_do_sample()) {
		// Check if the sample buffer needs to be flushed. We can't do it here,
		// since we're in an interrupt handler, and we can't set the callback
		// if we interrupted a kernel function, since the callback will pause
		// this thread. (The post_syscall hook will do the flush in that case.)
		if (debugInfo.profile.flush_needed
				&& !IS_KERNEL_ADDRESS(arch_debug_get_interrupt_pc(NULL))) {
			thread->post_interrupt_callback = profiling_flush;

			// We don't reschedule the timer here because profiling_flush() will
			// lead to the thread being descheduled until we are told to continue.
			// The timer will be rescheduled after the flush concludes.
			debugInfo.profile.interval_left = -1;
		} else
			schedule_profiling_timer(thread, debugInfo.profile.interval);
	}

	return B_HANDLED_INTERRUPT;
}


/**
 * @brief Scheduler hook: pause the profiling timer when the thread is swapped out.
 *
 * Runs with the scheduler lock held. Records how much time was left on the
 * timer so user_debug_thread_scheduled() can resume where we left off.
 *
 * @param thread Thread being unscheduled.
 */
void
user_debug_thread_unscheduled(Thread* thread)
{
	SpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	// if running, cancel the profiling timer
	struct timer* timer = thread->debug_info.profile.installed_timer;
	if (timer != NULL) {
		// track remaining time
		bigtime_t left = profiling_timer_left(thread);
		thread->debug_info.profile.interval_left = max_c(left, 0);
		thread->debug_info.profile.installed_timer->hook = NULL;
		thread->debug_info.profile.installed_timer = NULL;

		// cancel timer
		threadDebugInfoLocker.Unlock();
			// not necessary, but doesn't harm and reduces contention
		cancel_timer(timer);
			// since invoked on the same CPU, this will not possibly wait for
			// an already called timer hook
	}
}


/**
 * @brief Scheduler hook: restart the profiling timer when the thread resumes.
 *
 * Runs with the scheduler lock held.
 *
 * @param thread Thread being scheduled onto a CPU.
 */
void
user_debug_thread_scheduled(Thread* thread)
{
	SpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	if (thread->debug_info.profile.samples != NULL
			&& thread->debug_info.profile.interval_left >= 0) {
		// install profiling timer
		schedule_profiling_timer(thread,
			thread->debug_info.profile.interval_left);
	}
}


/**
 * @brief Send a message to every debug-initialized thread in the nub's team.
 *
 * Iterates the team's threads, skips the nub, and writes to each thread's
 * debug port (kill-interruptably). Used to push B_DEBUGGED_THREAD_DEBUGGER_CHANGED.
 *
 * @param nubThread The nub thread doing the broadcast.
 * @param code Message code.
 * @param message Payload (may be NULL when size is 0).
 * @param size Payload size.
 */
static void
broadcast_debugged_thread_message(Thread *nubThread, int32 code,
	const void *message, int32 size)
{
	// iterate through the threads
	thread_info threadInfo;
	int32 cookie = 0;
	while (get_next_thread_info(nubThread->team->id, &cookie, &threadInfo)
			== B_OK) {
		// get the thread and lock it
		Thread* thread = Thread::GetAndLock(threadInfo.thread);
		if (thread == NULL)
			continue;

		BReference<Thread> threadReference(thread, true);
		ThreadLocker threadLocker(thread, true);

		// get the thread's debug port
		InterruptsSpinLocker threadDebugInfoLocker(thread->debug_info.lock);

		port_id threadDebugPort = -1;
		if (thread && thread != nubThread && thread->team == nubThread->team
			&& (thread->debug_info.flags & B_THREAD_DEBUG_INITIALIZED) != 0
			&& (thread->debug_info.flags & B_THREAD_DEBUG_STOPPED) != 0) {
			threadDebugPort = thread->debug_info.debug_port;
		}

		threadDebugInfoLocker.Unlock();
		threadLocker.Unlock();

		// send the message to the thread
		if (threadDebugPort >= 0) {
			status_t error = kill_interruptable_write_port(threadDebugPort,
				code, message, size);
			if (error != B_OK) {
				TRACE(("broadcast_debugged_thread_message(): Failed to send "
					"message to thread %" B_PRId32 ": %" B_PRIx32 "\n",
					thread->id, error));
			}
		}
	}
}


/**
 * @brief Detach the debugger when the nub thread is exiting.
 *
 * Acquires exclusive debugger-change rights, snapshots and clears the team's
 * debug info, drops installed breakpoints, releases the rights, then
 * broadcasts B_DEBUGGED_THREAD_DEBUGGER_CHANGED to all stopped threads so
 * they wake up and proceed.
 *
 * @param nubThread The nub thread being torn down.
 */
static void
nub_thread_cleanup(Thread *nubThread)
{
	TRACE(("nub_thread_cleanup(%" B_PRId32 "): debugger port: %" B_PRId32 "\n",
		nubThread->id, nubThread->team->debug_info.debugger_port));

	ConditionVariable debugChangeCondition;
	debugChangeCondition.Init(nubThread->team, "debug change condition");
	prepare_debugger_change(nubThread->team, debugChangeCondition);

	team_debug_info teamDebugInfo;
	bool destroyDebugInfo = false;

	TeamLocker teamLocker(nubThread->team);
		// required by update_threads_debugger_installed_flag()

	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(nubThread->team->debug_info);

	team_debug_info &info = nubThread->team->debug_info;
	if (info.flags & B_TEAM_DEBUG_DEBUGGER_INSTALLED
			&& info.nub_thread == nubThread->id) {
		teamDebugInfo = info;
		clear_team_debug_info(&info, false);
		destroyDebugInfo = true;
	}

	// update the thread::flags fields
	update_threads_debugger_installed_flag(nubThread->team);

	RELEASE_TEAM_DEBUG_INFO_LOCK(nubThread->team->debug_info);
	restore_interrupts(state);

	teamLocker.Unlock();

	if (destroyDebugInfo)
		teamDebugInfo.breakpoint_manager->RemoveAllBreakpoints();

	finish_debugger_change(nubThread->team);

	if (destroyDebugInfo)
		destroy_team_debug_info(&teamDebugInfo);

	// notify all threads that the debugger is gone
	broadcast_debugged_thread_message(nubThread,
		B_DEBUGGED_THREAD_DEBUGGER_CHANGED, NULL, 0);
}


/**
 * @brief Resolve a stopped sibling thread's debug port for the nub to talk to.
 *
 * Validates that @a threadID belongs to the nub's team and is currently
 * B_THREAD_DEBUG_STOPPED; otherwise returns a descriptive error.
 *
 * @param nubThread The nub thread making the request.
 * @param threadID The target thread.
 * @param threadDebugPort [out] The resolved debug port on success.
 * @return B_OK, B_BAD_THREAD_ID, B_BAD_VALUE, or B_BAD_THREAD_STATE.
 */
static status_t
debug_nub_thread_get_thread_debug_port(Thread *nubThread,
	thread_id threadID, port_id &threadDebugPort)
{
	threadDebugPort = -1;

	// get the thread
	Thread* thread = Thread::GetAndLock(threadID);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// get the debug port
	InterruptsSpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	if (thread->team != nubThread->team)
		return B_BAD_VALUE;
	if ((thread->debug_info.flags & B_THREAD_DEBUG_STOPPED) == 0)
		return B_BAD_THREAD_STATE;

	threadDebugPort = thread->debug_info.debug_port;

	threadDebugInfoLocker.Unlock();

	if (threadDebugPort < 0)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Main loop of the per-team debug nub kernel thread.
 *
 * Reads B_DEBUG_MESSAGE_* requests from the team's nub port and dispatches
 * them: memory read/write, area clone, breakpoint/watchpoint set/clear,
 * signal mask get/set, handler get/set, profiling start/stop, debugger
 * handover, core file writing, and per-thread continue/set/get-CPU-state.
 * Runs until the nub port is deleted or a kill signal arrives, at which
 * point it calls nub_thread_cleanup() and exits.
 *
 * @return The error that terminated the loop (typically a port error or kill).
 */
static status_t
debug_nub_thread(void *)
{
	Thread *nubThread = thread_get_current_thread();

	// check, if we're still the current nub thread and get our port
	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(nubThread->team->debug_info);

	if (nubThread->team->debug_info.nub_thread != nubThread->id) {
		RELEASE_TEAM_DEBUG_INFO_LOCK(nubThread->team->debug_info);
		restore_interrupts(state);
		return 0;
	}

	port_id port = nubThread->team->debug_info.nub_port;
	sem_id writeLock = nubThread->team->debug_info.debugger_write_lock;
	BreakpointManager* breakpointManager
		= nubThread->team->debug_info.breakpoint_manager;

	RELEASE_TEAM_DEBUG_INFO_LOCK(nubThread->team->debug_info);
	restore_interrupts(state);

	TRACE(("debug_nub_thread() thread: %" B_PRId32 ", team %" B_PRId32 ", nub "
		"port: %" B_PRId32 "\n", nubThread->id, nubThread->team->id, port));

	// notify all threads that a debugger has been installed
	broadcast_debugged_thread_message(nubThread,
		B_DEBUGGED_THREAD_DEBUGGER_CHANGED, NULL, 0);

	// command processing loop
	while (true) {
		int32 command;
		debug_nub_message_data message;
		ssize_t messageSize = read_port_etc(port, &command, &message,
			sizeof(message), B_KILL_CAN_INTERRUPT, 0);

		if (messageSize < 0) {
			// The port is no longer valid or we were interrupted by a kill
			// signal: If we are still listed in the team's debug info as nub
			// thread, we need to update that.
			nub_thread_cleanup(nubThread);

			TRACE(("nub thread %" B_PRId32 ": terminating: %lx\n",
				nubThread->id, messageSize));

			return messageSize;
		}

		bool sendReply = false;
		union {
			debug_nub_read_memory_reply			read_memory;
			debug_nub_write_memory_reply		write_memory;
			debug_nub_clone_area_reply			clone_area;
			debug_nub_get_cpu_state_reply		get_cpu_state;
			debug_nub_set_breakpoint_reply		set_breakpoint;
			debug_nub_set_watchpoint_reply		set_watchpoint;
			debug_nub_get_signal_masks_reply	get_signal_masks;
			debug_nub_get_signal_handler_reply	get_signal_handler;
			debug_nub_start_profiler_reply		start_profiler;
			debug_profiler_update				profiler_update;
			debug_nub_write_core_file_reply		write_core_file;
		} reply;
		int32 replySize = 0;
		port_id replyPort = -1;

		// process the command
		switch (command) {
			/** @brief Read memory from the debugged team at @c address via BreakpointManager. */
			case B_DEBUG_MESSAGE_READ_MEMORY:
			{
				// get the parameters
				replyPort = message.read_memory.reply_port;
				void *address = message.read_memory.address;
				int32 size = message.read_memory.size;
				status_t result = B_OK;

				// check the parameters
				if (!BreakpointManager::CanAccessAddress(address, false))
					result = B_BAD_ADDRESS;
				else if (size <= 0 || size > B_MAX_READ_WRITE_MEMORY_SIZE)
					result = B_BAD_VALUE;

				// read the memory
				size_t bytesRead = 0;
				if (result == B_OK) {
					result = breakpointManager->ReadMemory(address,
						reply.read_memory.data, size, bytesRead);
				}
				reply.read_memory.error = result;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_READ_MEMORY: "
					"reply port: %" B_PRId32 ", address: %p, size: %" B_PRId32
					", result: %" B_PRIx32 ", read: %ld\n", nubThread->id,
					replyPort, address, size, result, bytesRead));

				// send only as much data as necessary
				reply.read_memory.size = bytesRead;
				replySize = reply.read_memory.data + bytesRead - (char*)&reply;
				sendReply = true;
				break;
			}

			/** @brief Write memory into the debugged team, patching over any breakpoints. */
			case B_DEBUG_MESSAGE_WRITE_MEMORY:
			{
				// get the parameters
				replyPort = message.write_memory.reply_port;
				void *address = message.write_memory.address;
				int32 size = message.write_memory.size;
				const char *data = message.write_memory.data;
				int32 realSize = (char*)&message + messageSize - data;
				status_t result = B_OK;

				// check the parameters
				if (!BreakpointManager::CanAccessAddress(address, true))
					result = B_BAD_ADDRESS;
				else if (size <= 0 || size > realSize)
					result = B_BAD_VALUE;

				// write the memory
				size_t bytesWritten = 0;
				if (result == B_OK) {
					result = breakpointManager->WriteMemory(address, data, size,
						bytesWritten);
				}
				reply.write_memory.error = result;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_WRITE_MEMORY: "
					"reply port: %" B_PRId32 ", address: %p, size: %" B_PRId32
					", result: %" B_PRIx32 ", written: %ld\n", nubThread->id,
					replyPort, address, size, result, bytesWritten));

				reply.write_memory.size = bytesWritten;
				sendReply = true;
				replySize = sizeof(debug_nub_write_memory_reply);
				break;
			}

			/** @brief Clone an area of the debugged team into the debugger team read-only. */
			case B_DEBUG_MESSAGE_CLONE_AREA:
			{
				// get the parameters
				replyPort = message.clone_area.reply_port;
				const void *address = message.clone_area.address;
				area_id result = 0;

				// check the parameters
				if (!IS_USER_ADDRESS(address))
					result = B_NOT_ALLOWED;

				// find the area
				area_id sourceArea;
				addr_t addressOffset = 0;
				if (result == B_OK) {
					sourceArea = _user_area_for((void*)address);
					if (sourceArea < 0) {
						result = sourceArea;
					} else {
						area_info info;
						result = get_area_info(sourceArea, &info);
						addressOffset = (addr_t)address - (addr_t)info.address;
					}
				}

				// clone it
				if (result == B_OK) {
					void* newAddress = NULL;
					result = vm_clone_area(nubThread->team->debug_info.debugger_team,
						"debugger-cloned area", &newAddress, B_ANY_ADDRESS, B_READ_AREA,
						REGION_NO_PRIVATE_MAP, sourceArea, true);
					reply.clone_area.address = (void*)((addr_t)newAddress + addressOffset);
				}

				reply.clone_area.area = result;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_CLONE_AREA: "
					"reply port: %" B_PRId32 ", address: %p, result: %" B_PRIx32 "\n",
					nubThread->id, replyPort, address, result));

				sendReply = true;
				replySize = sizeof(debug_nub_clone_area_reply);
				break;
			}

			/** @brief Replace the user-visible team-wide debug flags. */
			case B_DEBUG_MESSAGE_SET_TEAM_FLAGS:
			{
				// get the parameters
				int32 flags = message.set_team_flags.flags
					& B_TEAM_DEBUG_USER_FLAG_MASK;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_SET_TEAM_FLAGS"
					": flags: %" B_PRIx32 "\n", nubThread->id, flags));

				Team *team = thread_get_current_thread()->team;

				// set the flags
				cpu_status state = disable_interrupts();
				GRAB_TEAM_DEBUG_INFO_LOCK(team->debug_info);

				flags |= team->debug_info.flags & B_TEAM_DEBUG_KERNEL_FLAG_MASK;
				atomic_set(&team->debug_info.flags, flags);

				RELEASE_TEAM_DEBUG_INFO_LOCK(team->debug_info);
				restore_interrupts(state);

				break;
			}

			/** @brief Replace the user-visible per-thread debug flags. */
			case B_DEBUG_MESSAGE_SET_THREAD_FLAGS:
			{
				// get the parameters
				thread_id threadID = message.set_thread_flags.thread;
				int32 flags = message.set_thread_flags.flags
					& B_THREAD_DEBUG_USER_FLAG_MASK;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_SET_THREAD_FLAGS"
					": thread: %" B_PRId32 ", flags: %" B_PRIx32 "\n",
					nubThread->id, threadID, flags));

				// set the flags
				Thread* thread = Thread::GetAndLock(threadID);
				if (thread == NULL)
					break;
				BReference<Thread> threadReference(thread, true);
				ThreadLocker threadLocker(thread, true);

				InterruptsSpinLocker threadDebugInfoLocker(
					thread->debug_info.lock);

				if (thread->team == thread_get_current_thread()->team) {
					flags |= thread->debug_info.flags
						& B_THREAD_DEBUG_KERNEL_FLAG_MASK;
					atomic_set(&thread->debug_info.flags, flags);
				}

				break;
			}

			/** @brief Resume a stopped thread, optionally single-stepping it. */
			case B_DEBUG_MESSAGE_CONTINUE_THREAD:
			{
				// get the parameters
				thread_id threadID;
				uint32 handleEvent;
				bool singleStep;

				threadID = message.continue_thread.thread;
				handleEvent = message.continue_thread.handle_event;
				singleStep = message.continue_thread.single_step;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_CONTINUE_THREAD"
					": thread: %" B_PRId32 ", handle event: %" B_PRIu32 ", "
					"single step: %d\n", nubThread->id, threadID, handleEvent,
					singleStep));

				// find the thread and get its debug port
				port_id threadDebugPort = -1;
				status_t result = debug_nub_thread_get_thread_debug_port(
					nubThread, threadID, threadDebugPort);

				// send a message to the debugged thread
				if (result == B_OK) {
					debugged_thread_continue commandMessage;
					commandMessage.handle_event = handleEvent;
					commandMessage.single_step = singleStep;

					result = write_port(threadDebugPort,
						B_DEBUGGED_THREAD_MESSAGE_CONTINUE,
						&commandMessage, sizeof(commandMessage));
				} else if (result == B_BAD_THREAD_STATE) {
					Thread* thread = Thread::GetAndLock(threadID);
					if (thread == NULL)
						break;

					BReference<Thread> threadReference(thread, true);
					ThreadLocker threadLocker(thread, true);
					if (thread->state == B_THREAD_SUSPENDED) {
						threadLocker.Unlock();
						resume_thread(threadID);
						break;
					}
				}

				break;
			}

			/** @brief Replace a stopped thread's CPU register state. */
			case B_DEBUG_MESSAGE_SET_CPU_STATE:
			{
				// get the parameters
				thread_id threadID = message.set_cpu_state.thread;
				const debug_cpu_state &cpuState
					= message.set_cpu_state.cpu_state;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_SET_CPU_STATE"
					": thread: %" B_PRId32 "\n", nubThread->id, threadID));

				// find the thread and get its debug port
				port_id threadDebugPort = -1;
				status_t result = debug_nub_thread_get_thread_debug_port(
					nubThread, threadID, threadDebugPort);

				// send a message to the debugged thread
				if (result == B_OK) {
					debugged_thread_set_cpu_state commandMessage;
					memcpy(&commandMessage.cpu_state, &cpuState,
						sizeof(debug_cpu_state));
					write_port(threadDebugPort,
						B_DEBUGGED_THREAD_SET_CPU_STATE,
						&commandMessage, sizeof(commandMessage));
				}

				break;
			}

			/** @brief Read a stopped thread's CPU register state via its debug port. */
			case B_DEBUG_MESSAGE_GET_CPU_STATE:
			{
				// get the parameters
				thread_id threadID = message.get_cpu_state.thread;
				replyPort = message.get_cpu_state.reply_port;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_GET_CPU_STATE"
					": thread: %" B_PRId32 "\n", nubThread->id, threadID));

				// find the thread and get its debug port
				port_id threadDebugPort = -1;
				status_t result = debug_nub_thread_get_thread_debug_port(
					nubThread, threadID, threadDebugPort);

				// send a message to the debugged thread
				if (threadDebugPort >= 0) {
					debugged_thread_get_cpu_state commandMessage;
					commandMessage.reply_port = replyPort;
					result = write_port(threadDebugPort,
						B_DEBUGGED_THREAD_GET_CPU_STATE, &commandMessage,
						sizeof(commandMessage));
				}

				// send a reply to the debugger in case of error
				if (result != B_OK) {
					reply.get_cpu_state.error = result;
					sendReply = true;
					replySize = sizeof(reply.get_cpu_state);
				}

				break;
			}

			/** @brief Install a software/code breakpoint through the BreakpointManager. */
			case B_DEBUG_MESSAGE_SET_BREAKPOINT:
			{
				// get the parameters
				replyPort = message.set_breakpoint.reply_port;
				void *address = message.set_breakpoint.address;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_SET_BREAKPOINT"
					": address: %p\n", nubThread->id, address));

				// check the address
				status_t result = B_OK;
				if (address == NULL
					|| !BreakpointManager::CanAccessAddress(address, false)) {
					result = B_BAD_ADDRESS;
				}

				// set the breakpoint
				if (result == B_OK)
					result = breakpointManager->InstallBreakpoint(address);

				if (result == B_OK)
					update_threads_breakpoints_flag();

				// prepare the reply
				reply.set_breakpoint.error = result;
				replySize = sizeof(reply.set_breakpoint);
				sendReply = true;

				break;
			}

			/** @brief Remove a previously-installed breakpoint. */
			case B_DEBUG_MESSAGE_CLEAR_BREAKPOINT:
			{
				// get the parameters
				void *address = message.clear_breakpoint.address;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_CLEAR_BREAKPOINT"
					": address: %p\n", nubThread->id, address));

				// check the address
				status_t result = B_OK;
				if (address == NULL
					|| !BreakpointManager::CanAccessAddress(address, false)) {
					result = B_BAD_ADDRESS;
				}

				// clear the breakpoint
				if (result == B_OK)
					result = breakpointManager->UninstallBreakpoint(address);

				if (result == B_OK)
					update_threads_breakpoints_flag();

				break;
			}

			/** @brief Install a data watchpoint of the given type and length. */
			case B_DEBUG_MESSAGE_SET_WATCHPOINT:
			{
				// get the parameters
				replyPort = message.set_watchpoint.reply_port;
				void *address = message.set_watchpoint.address;
				uint32 type = message.set_watchpoint.type;
				int32 length = message.set_watchpoint.length;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_SET_WATCHPOINT"
					": address: %p, type: %" B_PRIu32 ", length: %" B_PRId32 "\n",
					nubThread->id, address, type, length));

				// check the address and size
				status_t result = B_OK;
				if (address == NULL
					|| !BreakpointManager::CanAccessAddress(address, false)) {
					result = B_BAD_ADDRESS;
				}
				if (length < 0)
					result = B_BAD_VALUE;

				// set the watchpoint
				if (result == B_OK) {
					result = breakpointManager->InstallWatchpoint(address, type,
						length);
				}

				if (result == B_OK)
					update_threads_breakpoints_flag();

				// prepare the reply
				reply.set_watchpoint.error = result;
				replySize = sizeof(reply.set_watchpoint);
				sendReply = true;

				break;
			}

			/** @brief Remove a previously-installed watchpoint. */
			case B_DEBUG_MESSAGE_CLEAR_WATCHPOINT:
			{
				// get the parameters
				void *address = message.clear_watchpoint.address;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_CLEAR_WATCHPOINT"
					": address: %p\n", nubThread->id, address));

				// check the address
				status_t result = B_OK;
				if (address == NULL
					|| !BreakpointManager::CanAccessAddress(address, false)) {
					result = B_BAD_ADDRESS;
				}

				// clear the watchpoint
				if (result == B_OK)
					result = breakpointManager->UninstallWatchpoint(address);

				if (result == B_OK)
					update_threads_breakpoints_flag();

				break;
			}

			/** @brief Update the per-thread "ignore these signals" and "ignore once" masks. */
			case B_DEBUG_MESSAGE_SET_SIGNAL_MASKS:
			{
				// get the parameters
				thread_id threadID = message.set_signal_masks.thread;
				uint64 ignore = message.set_signal_masks.ignore_mask;
				uint64 ignoreOnce = message.set_signal_masks.ignore_once_mask;
				uint32 ignoreOp = message.set_signal_masks.ignore_op;
				uint32 ignoreOnceOp = message.set_signal_masks.ignore_once_op;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_SET_SIGNAL_MASKS"
					": thread: %" B_PRId32 ", ignore: %" B_PRIx64 " (op: %"
					B_PRIu32 "), ignore once: %" B_PRIx64 " (op: %" B_PRIu32
					")\n", nubThread->id, threadID, ignore, ignoreOp,
					ignoreOnce, ignoreOnceOp));

				// set the masks
				Thread* thread = Thread::GetAndLock(threadID);
				if (thread == NULL)
					break;
				BReference<Thread> threadReference(thread, true);
				ThreadLocker threadLocker(thread, true);

				InterruptsSpinLocker threadDebugInfoLocker(
					thread->debug_info.lock);

				if (thread->team == thread_get_current_thread()->team) {
					thread_debug_info &threadDebugInfo = thread->debug_info;
					// set ignore mask
					switch (ignoreOp) {
						case B_DEBUG_SIGNAL_MASK_AND:
							threadDebugInfo.ignore_signals &= ignore;
							break;
						case B_DEBUG_SIGNAL_MASK_OR:
							threadDebugInfo.ignore_signals |= ignore;
							break;
						case B_DEBUG_SIGNAL_MASK_SET:
							threadDebugInfo.ignore_signals = ignore;
							break;
					}

					// set ignore once mask
					switch (ignoreOnceOp) {
						case B_DEBUG_SIGNAL_MASK_AND:
							threadDebugInfo.ignore_signals_once &= ignoreOnce;
							break;
						case B_DEBUG_SIGNAL_MASK_OR:
							threadDebugInfo.ignore_signals_once |= ignoreOnce;
							break;
						case B_DEBUG_SIGNAL_MASK_SET:
							threadDebugInfo.ignore_signals_once = ignoreOnce;
							break;
					}
				}

				break;
			}

			/** @brief Read back a thread's signal ignore masks. */
			case B_DEBUG_MESSAGE_GET_SIGNAL_MASKS:
			{
				// get the parameters
				replyPort = message.get_signal_masks.reply_port;
				thread_id threadID = message.get_signal_masks.thread;
				status_t result = B_OK;

				// get the masks
				uint64 ignore = 0;
				uint64 ignoreOnce = 0;

				Thread* thread = Thread::GetAndLock(threadID);
				if (thread != NULL) {
					BReference<Thread> threadReference(thread, true);
					ThreadLocker threadLocker(thread, true);

					InterruptsSpinLocker threadDebugInfoLocker(
						thread->debug_info.lock);

					ignore = thread->debug_info.ignore_signals;
					ignoreOnce = thread->debug_info.ignore_signals_once;
				} else
					result = B_BAD_THREAD_ID;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_GET_SIGNAL_MASKS"
					": reply port: %" B_PRId32 ", thread: %" B_PRId32 ", "
					"ignore: %" B_PRIx64 ", ignore once: %" B_PRIx64 ", result: "
					"%" B_PRIx32 "\n", nubThread->id, replyPort, threadID,
					ignore, ignoreOnce, result));

				// prepare the message
				reply.get_signal_masks.error = result;
				reply.get_signal_masks.ignore_mask = ignore;
				reply.get_signal_masks.ignore_once_mask = ignoreOnce;
				replySize = sizeof(reply.get_signal_masks);
				sendReply = true;
				break;
			}

			/** @brief Install a sigaction in the debugged team on the debugger's behalf. */
			case B_DEBUG_MESSAGE_SET_SIGNAL_HANDLER:
			{
				// get the parameters
				int signal = message.set_signal_handler.signal;
				struct sigaction &handler = message.set_signal_handler.handler;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_SET_SIGNAL_HANDLER"
					": signal: %d, handler: %p\n", nubThread->id, signal,
					handler.sa_handler));

				// set the handler
				sigaction(signal, &handler, NULL);

				break;
			}

			/** @brief Query the currently installed sigaction for a signal. */
			case B_DEBUG_MESSAGE_GET_SIGNAL_HANDLER:
			{
				// get the parameters
				replyPort = message.get_signal_handler.reply_port;
				int signal = message.get_signal_handler.signal;
				status_t result = B_OK;

				// get the handler
				if (sigaction(signal, NULL, &reply.get_signal_handler.handler)
						!= 0) {
					result = errno;
				}

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_GET_SIGNAL_HANDLER"
					": reply port: %" B_PRId32 ", signal: %d, handler: %p\n",
					nubThread->id, replyPort, signal,
					reply.get_signal_handler.handler.sa_handler));

				// prepare the message
				reply.get_signal_handler.error = result;
				replySize = sizeof(reply.get_signal_handler);
				sendReply = true;
				break;
			}

			/** @brief Set the HANDOVER flag, drain writes, and uninstall breakpoints prior to debugger handover. */
			case B_DEBUG_MESSAGE_PREPARE_HANDOVER:
			{
				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_MESSAGE_PREPARE_HANDOVER"
					"\n", nubThread->id));

				Team *team = nubThread->team;

				// Acquire the debugger write lock. As soon as we have it and
				// have set the B_TEAM_DEBUG_DEBUGGER_HANDOVER flag, no thread
				// will write anything to the debugger port anymore.
				status_t result = acquire_sem_etc(writeLock, 1,
					B_KILL_CAN_INTERRUPT, 0);
				if (result == B_OK) {
					// set the respective team debug flag
					cpu_status state = disable_interrupts();
					GRAB_TEAM_DEBUG_INFO_LOCK(team->debug_info);

					atomic_or(&team->debug_info.flags,
						B_TEAM_DEBUG_DEBUGGER_HANDOVER);
					BreakpointManager* breakpointManager
						= team->debug_info.breakpoint_manager;

					RELEASE_TEAM_DEBUG_INFO_LOCK(team->debug_info);
					restore_interrupts(state);

					// remove all installed breakpoints
					breakpointManager->RemoveAllBreakpoints();

					release_sem(writeLock);
				} else {
					// We probably got a SIGKILL. If so, we will terminate when
					// reading the next message fails.
				}

				break;
			}

			/** @brief Broadcast that handover finished so stopped threads re-read the debugger port. */
			case B_DEBUG_MESSAGE_HANDED_OVER:
			{
				// notify all threads that the debugger has changed
				broadcast_debugged_thread_message(nubThread,
					B_DEBUGGED_THREAD_DEBUGGER_CHANGED, NULL, 0);

				break;
			}

			/** @brief Start sampling a thread's PC/stack into a cloned sample area. */
			case B_DEBUG_MESSAGE_START_PROFILER:
			{
				// get the parameters
				thread_id threadID = message.start_profiler.thread;
				replyPort = message.start_profiler.reply_port;
				area_id sampleArea = message.start_profiler.sample_area;
				int32 stackDepth = message.start_profiler.stack_depth;
				bool variableStackDepth
					= message.start_profiler.variable_stack_depth;
				bool profileKernel = message.start_profiler.profile_kernel;
				bigtime_t interval = max_c(message.start_profiler.interval,
					B_DEBUG_MIN_PROFILE_INTERVAL);
				status_t result = B_OK;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_START_PROFILER: "
					"thread: %" B_PRId32 ", sample area: %" B_PRId32 "\n",
					nubThread->id, threadID, sampleArea));

				if (stackDepth < 1)
					stackDepth = 1;
				else if (stackDepth > B_DEBUG_STACK_TRACE_DEPTH)
					stackDepth = B_DEBUG_STACK_TRACE_DEPTH;

				// provision for an extra entry per hit (for the number of
				// samples), if variable stack depth
				if (variableStackDepth)
					stackDepth++;

				// clone the sample area
				area_info areaInfo;
				if (result == B_OK)
					result = get_area_info(sampleArea, &areaInfo);

				area_id clonedSampleArea = -1;
				void* samples = NULL;
				if (result == B_OK) {
					clonedSampleArea = clone_area("profiling samples", &samples,
						B_ANY_KERNEL_ADDRESS,
						B_KERNEL_READ_AREA | B_KERNEL_WRITE_AREA,
						sampleArea);
					if (clonedSampleArea >= 0) {
						// we need the memory locked
						result = lock_memory(samples, areaInfo.size,
							B_READ_DEVICE);
						if (result != B_OK) {
							delete_area(clonedSampleArea);
							clonedSampleArea = -1;
						}
					} else
						result = clonedSampleArea;
				}

				// get the thread and set the profile info
				int32 imageEvent = nubThread->team->debug_info.image_event;
				if (result == B_OK) {
					Thread* thread = Thread::GetAndLock(threadID);
					BReference<Thread> threadReference(thread, true);
					ThreadLocker threadLocker(thread, true);

					if (thread != NULL && thread->team == nubThread->team) {
						thread_debug_info &threadDebugInfo = thread->debug_info;

						InterruptsSpinLocker threadDebugInfoLocker(
							threadDebugInfo.lock);

						if (threadDebugInfo.profile.samples == NULL) {
							threadDebugInfo.profile.interval = interval;
							threadDebugInfo.profile.sample_area
								= clonedSampleArea;
							threadDebugInfo.profile.samples = (addr_t*)samples;
							threadDebugInfo.profile.max_samples
								= areaInfo.size / sizeof(addr_t);
							threadDebugInfo.profile.flush_threshold
								= threadDebugInfo.profile.max_samples
									* B_DEBUG_PROFILE_BUFFER_FLUSH_THRESHOLD
									/ 100;
							threadDebugInfo.profile.sample_count = 0;
							threadDebugInfo.profile.dropped_ticks = 0;
							threadDebugInfo.profile.stack_depth = stackDepth;
							threadDebugInfo.profile.variable_stack_depth
								= variableStackDepth;
							threadDebugInfo.profile.profile_kernel = profileKernel;
							threadDebugInfo.profile.flush_needed = false;
							threadDebugInfo.profile.interval_left = interval;
							threadDebugInfo.profile.installed_timer = NULL;
							threadDebugInfo.profile.image_event = imageEvent;
							threadDebugInfo.profile.last_image_event
								= imageEvent;
						} else
							result = B_BAD_VALUE;
					} else
						result = B_BAD_THREAD_ID;
				}

				// on error unlock and delete the sample area
				if (result != B_OK) {
					if (clonedSampleArea >= 0) {
						unlock_memory(samples, areaInfo.size, B_READ_DEVICE);
						delete_area(clonedSampleArea);
					}
				}

				// send a reply to the debugger
				reply.start_profiler.error = result;
				reply.start_profiler.interval = interval;
				reply.start_profiler.image_event = imageEvent;
				sendReply = true;
				replySize = sizeof(reply.start_profiler);

				break;
			}

			/** @brief Stop profiling, return the last batch of samples, and release the sample area. */
			case B_DEBUG_MESSAGE_STOP_PROFILER:
			{
				// get the parameters
				thread_id threadID = message.stop_profiler.thread;
				replyPort = message.stop_profiler.reply_port;
				status_t result = B_OK;

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_STOP_PROFILER: "
					"thread: %" B_PRId32 "\n", nubThread->id, threadID));

				area_id sampleArea = -1;
				addr_t* samples = NULL;
				int32 sampleCount = 0;
				int32 stackDepth = 0;
				bool variableStackDepth = false;
				int32 imageEvent = 0;
				int32 droppedTicks = 0;
				bigtime_t lastCPUTime = 0;

				// get the thread and detach the profile info
				Thread* thread = Thread::GetAndLock(threadID);
				BReference<Thread> threadReference(thread, true);
				ThreadLocker threadLocker(thread, true);

				if (thread && thread->team == nubThread->team) {
					thread_debug_info &threadDebugInfo = thread->debug_info;

					InterruptsSpinLocker threadDebugInfoLocker(
						threadDebugInfo.lock);

					if (threadDebugInfo.profile.samples != NULL) {
						sampleArea = threadDebugInfo.profile.sample_area;
						samples = threadDebugInfo.profile.samples;
						sampleCount = threadDebugInfo.profile.sample_count;
						droppedTicks = threadDebugInfo.profile.dropped_ticks;
						stackDepth = threadDebugInfo.profile.stack_depth;
						variableStackDepth
							= threadDebugInfo.profile.variable_stack_depth;
						imageEvent = threadDebugInfo.profile.image_event;
						threadDebugInfo.profile.sample_area = -1;
						threadDebugInfo.profile.samples = NULL;
						threadDebugInfo.profile.flush_needed = false;
						threadDebugInfo.profile.dropped_ticks = 0;
						{
							SpinLocker threadTimeLocker(thread->time_lock);
							lastCPUTime = thread->CPUTime(false);
						}
					} else
						result = B_BAD_VALUE;
				} else
					result = B_BAD_THREAD_ID;

				threadLocker.Unlock();

				// prepare the reply
				if (result == B_OK) {
					reply.profiler_update.origin.thread = threadID;
					reply.profiler_update.image_event = imageEvent;
					reply.profiler_update.stack_depth = stackDepth;
					reply.profiler_update.variable_stack_depth
						= variableStackDepth;
					reply.profiler_update.sample_count = sampleCount;
					reply.profiler_update.dropped_ticks = droppedTicks;
					reply.profiler_update.stopped = true;
					reply.profiler_update.last_cpu_time = lastCPUTime;
				} else
					reply.profiler_update.origin.thread = result;

				replySize = sizeof(debug_profiler_update);
				sendReply = true;

				if (sampleArea >= 0) {
					area_info areaInfo;
					if (get_area_info(sampleArea, &areaInfo) == B_OK) {
						unlock_memory(samples, areaInfo.size, B_READ_DEVICE);
						delete_area(sampleArea);
					}
				}

				break;
			}

			/** @brief Dump a core file of the debugged team to @c path. */
			case B_DEBUG_MESSAGE_WRITE_CORE_FILE:
			{
				// get the parameters
				replyPort = message.write_core_file.reply_port;
				char* path = message.write_core_file.path;
				path[sizeof(message.write_core_file.path) - 1] = '\0';

				TRACE(("nub thread %" B_PRId32 ": B_DEBUG_WRITE_CORE_FILE"
					": path: %s\n", nubThread->id, path));

				// write the core file
				status_t result = core_dump_write_core_file(path, false);

				// prepare the reply
				reply.write_core_file.error = result;
				replySize = sizeof(reply.write_core_file);
				sendReply = true;

				break;
			}
		}

		// send the reply, if necessary
		if (sendReply) {
			status_t error = kill_interruptable_write_port(replyPort, command,
				&reply, replySize);

			if (error != B_OK) {
				// The debugger port is either not longer existing or we got
				// interrupted by a kill signal. In either case we terminate.
				TRACE(("nub thread %" B_PRId32 ": failed to send reply to port "
					"%" B_PRId32 ": %s\n", nubThread->id, replyPort,
					strerror(error)));

				nub_thread_cleanup(nubThread);
				return error;
			}
		}
	}
}


/**
 * @brief Populate team/thread debug_info for a freshly attached debugger.
 *
 * Clears arch-specific break/watchpoints and resets every non-nub thread's
 * user-visible debug flags to defaults. The caller must hold both the team
 * lock and the team debug_info lock.
 *
 * @param team The team being attached.
 * @param debuggerTeam Id of the team running the debugger.
 * @param debuggerPort Port the debugger receives notifications on.
 * @param nubPort Port the nub listens on.
 * @param nubThread Spawned nub kernel thread id.
 * @param debuggerPortWriteLock Sem id serializing writes to the debugger port.
 * @param causingThread Thread whose event triggered attach (or -1).
 */
static void
install_team_debugger_init_debug_infos(Team *team, team_id debuggerTeam,
	port_id debuggerPort, port_id nubPort, thread_id nubThread,
	sem_id debuggerPortWriteLock, thread_id causingThread)
{
	atomic_set(&team->debug_info.flags,
		B_TEAM_DEBUG_DEFAULT_FLAGS | B_TEAM_DEBUG_DEBUGGER_INSTALLED);
	team->debug_info.nub_port = nubPort;
	team->debug_info.nub_thread = nubThread;
	team->debug_info.debugger_team = debuggerTeam;
	team->debug_info.debugger_port = debuggerPort;
	team->debug_info.debugger_write_lock = debuggerPortWriteLock;
	team->debug_info.causing_thread = causingThread;

	arch_clear_team_debug_info(&team->debug_info.arch_info);

	// set the user debug flags and signal masks of all threads to the default
	for (Thread *thread = team->thread_list.First(); thread != NULL;
			thread = team->thread_list.GetNext(thread)) {
		SpinLocker threadDebugInfoLocker(thread->debug_info.lock);

		if (thread->id == nubThread) {
			atomic_set(&thread->debug_info.flags, B_THREAD_DEBUG_NUB_THREAD);
		} else {
			int32 flags = thread->debug_info.flags
				& ~B_THREAD_DEBUG_USER_FLAG_MASK;
			atomic_set(&thread->debug_info.flags,
				flags | B_THREAD_DEBUG_DEFAULT_FLAGS);
			thread->debug_info.ignore_signals = 0;
			thread->debug_info.ignore_signals_once = 0;

			arch_clear_thread_debug_info(&thread->debug_info.arch_info);
		}
	}

	// update the thread::flags fields
	update_threads_debugger_installed_flag(team);
}


/**
 * @brief Attach a debugger to a team (creating the nub port + nub thread).
 *
 * Enforces the invariant that only one debugger may be installed at a time
 * per team: if one already is, either fails, succeeds trivially (dontReplace),
 * or orchestrates a handover via the B_TEAM_DEBUG_DEBUGGER_HANDOVER/HANDING_OVER
 * flags. On clean attach, creates the debugger_write_lock sem, the nub port
 * (owned by the debugger team), the BreakpointManager, and spawns the nub
 * kernel thread.
 *
 * @param teamID Team to attach (or B_CURRENT_TEAM).
 * @param debuggerPort Port to send B_DEBUGGER_MESSAGE_* to (ignored if useDefault).
 * @param causingThread Thread that triggered attach; -1 if none.
 * @param useDefault If true, use the sDefaultDebuggerPort instead of @a debuggerPort.
 * @param dontReplace If true, don't replace an already-installed debugger.
 * @return The newly created nub port, or a negative error.
 */
static port_id
install_team_debugger(team_id teamID, port_id debuggerPort,
	thread_id causingThread, bool useDefault, bool dontReplace)
{
	TRACE(("install_team_debugger(team: %" B_PRId32 ", port: %" B_PRId32 ", "
		"default: %d, dontReplace: %d)\n", teamID, debuggerPort, useDefault,
		dontReplace));

	if (useDefault)
		debuggerPort = atomic_get(&sDefaultDebuggerPort);

	// get the debugger team
	port_info debuggerPortInfo;
	status_t error = get_port_info(debuggerPort, &debuggerPortInfo);
	if (error != B_OK) {
		TRACE(("install_team_debugger(): Failed to get debugger port info: "
			"%" B_PRIx32 "\n", error));
		return error;
	}
	team_id debuggerTeam = debuggerPortInfo.team;

	// Check the debugger team: It must neither be the kernel team nor the
	// debugged team.
	if (teamID == B_CURRENT_TEAM)
		teamID = team_get_current_team_id();
	if (debuggerTeam == team_get_kernel_team_id() || debuggerTeam == teamID) {
		TRACE(("install_team_debugger(): Can't debug kernel or debugger team. "
			"debugger: %" B_PRId32 ", debugged: %" B_PRId32 "\n", debuggerTeam,
			teamID));
		return B_NOT_ALLOWED;
	}

	// get the team
	Team* team;
	ConditionVariable debugChangeCondition;
	debugChangeCondition.Init(NULL, "debug change condition");
	error = prepare_debugger_change(teamID, debugChangeCondition, team);
	if (error != B_OK)
		return error;

	// check, if a debugger is already installed

	bool done = false;
	port_id result = B_ERROR;
	bool handOver = false;
	port_id oldDebuggerPort = -1;
	port_id nubPort = -1;

	TeamLocker teamLocker(team);
	cpu_status state = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(team->debug_info);

	int32 teamDebugFlags = team->debug_info.flags;

	if (teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) {
		// There's already a debugger installed.
		if (teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_HANDOVER) {
			if (dontReplace) {
				// We're fine with already having a debugger.
				error = B_OK;
				done = true;
				result = team->debug_info.nub_port;
			} else {
				// a handover to another debugger is requested
				// Set the handing-over flag -- we'll clear both flags after
				// having sent the handed-over message to the new debugger.
				atomic_or(&team->debug_info.flags,
					B_TEAM_DEBUG_DEBUGGER_HANDING_OVER);

				oldDebuggerPort = team->debug_info.debugger_port;
				result = nubPort = team->debug_info.nub_port;
				if (causingThread < 0)
					causingThread = team->debug_info.causing_thread;

				// set the new debugger
				install_team_debugger_init_debug_infos(team, debuggerTeam,
					debuggerPort, nubPort, team->debug_info.nub_thread,
					team->debug_info.debugger_write_lock, causingThread);

				handOver = true;
				done = true;
			}
		} else {
			// there's already a debugger installed
			error = (dontReplace ? B_OK : B_BAD_VALUE);
			done = true;
			result = team->debug_info.nub_port;
		}
	} else if ((teamDebugFlags & B_TEAM_DEBUG_DEBUGGER_DISABLED) != 0
		&& useDefault) {
		// No debugger yet, disable_debugger() had been invoked, and we
		// would install the default debugger. Just fail.
		error = B_BAD_VALUE;
	}

	RELEASE_TEAM_DEBUG_INFO_LOCK(team->debug_info);
	restore_interrupts(state);
	teamLocker.Unlock();

	if (handOver && set_port_owner(nubPort, debuggerTeam) != B_OK) {
		// The old debugger must just have died. Just proceed as
		// if there was no debugger installed. We may still be too
		// early, in which case we'll fail, but this race condition
		// should be unbelievably rare and relatively harmless.
		handOver = false;
		done = false;
	}

	if (handOver) {
		// prepare the handed-over message
		debug_handed_over notification;
		notification.origin.thread = -1;
		notification.origin.team = teamID;
		notification.origin.nub_port = nubPort;
		notification.debugger = debuggerTeam;
		notification.debugger_port = debuggerPort;
		notification.causing_thread = causingThread;

		// notify the new debugger
		error = write_port_etc(debuggerPort,
			B_DEBUGGER_MESSAGE_HANDED_OVER, &notification,
			sizeof(notification), B_RELATIVE_TIMEOUT, 0);
		if (error != B_OK) {
			dprintf("install_team_debugger(): Failed to send message to new "
				"debugger: %s\n", strerror(error));
		}

		// clear the handed-over and handing-over flags
		state = disable_interrupts();
		GRAB_TEAM_DEBUG_INFO_LOCK(team->debug_info);

		atomic_and(&team->debug_info.flags,
			~(B_TEAM_DEBUG_DEBUGGER_HANDOVER
				| B_TEAM_DEBUG_DEBUGGER_HANDING_OVER));

		RELEASE_TEAM_DEBUG_INFO_LOCK(team->debug_info);
		restore_interrupts(state);

		finish_debugger_change(team);

		// notify the nub thread
		kill_interruptable_write_port(nubPort, B_DEBUG_MESSAGE_HANDED_OVER,
			NULL, 0);

		// notify the old debugger
		error = write_port_etc(oldDebuggerPort,
			B_DEBUGGER_MESSAGE_HANDED_OVER, &notification,
			sizeof(notification), B_RELATIVE_TIMEOUT, 0);
		if (error != B_OK) {
			TRACE(("install_team_debugger(): Failed to send message to old "
				"debugger: %s\n", strerror(error)));
		}

		TRACE(("install_team_debugger() done: handed over to debugger: team: "
			"%" B_PRId32 ", port: %" B_PRId32 "\n", debuggerTeam,
			debuggerPort));

		return result;
	}

	if (done || error != B_OK) {
		TRACE(("install_team_debugger() done1: %" B_PRId32 "\n",
			(error == B_OK ? result : error)));
		finish_debugger_change(team);
		return (error == B_OK ? result : error);
	}

	// create the debugger write lock semaphore
	char nameBuffer[B_OS_NAME_LENGTH];
	snprintf(nameBuffer, sizeof(nameBuffer), "team %" B_PRId32 " debugger port "
		"write", teamID);
	sem_id debuggerWriteLock = create_sem(1, nameBuffer);
	if (debuggerWriteLock < 0)
		error = debuggerWriteLock;

	// create the nub port
	snprintf(nameBuffer, sizeof(nameBuffer), "team %" B_PRId32 " debug", teamID);
	if (error == B_OK) {
		nubPort = create_port(1, nameBuffer);
		if (nubPort < 0)
			error = nubPort;
		else
			result = nubPort;
	}

	// make the debugger team the port owner; thus we know, if the debugger is
	// gone and can cleanup
	if (error == B_OK)
		error = set_port_owner(nubPort, debuggerTeam);

	// create the breakpoint manager
	BreakpointManager* breakpointManager = NULL;
	if (error == B_OK) {
		breakpointManager = new(std::nothrow) BreakpointManager;
		if (breakpointManager != NULL)
			error = breakpointManager->Init();
		else
			error = B_NO_MEMORY;
	}

	// spawn the nub thread
	thread_id nubThread = -1;
	if (error == B_OK) {
		snprintf(nameBuffer, sizeof(nameBuffer), "team %" B_PRId32 " debug task",
			teamID);
		nubThread = spawn_kernel_thread_etc(debug_nub_thread, nameBuffer,
			B_NORMAL_PRIORITY, NULL, teamID);
		if (nubThread < 0)
			error = nubThread;
	}

	// now adjust the debug info accordingly
	if (error == B_OK) {
		TeamLocker teamLocker(team);
		state = disable_interrupts();
		GRAB_TEAM_DEBUG_INFO_LOCK(team->debug_info);

		team->debug_info.breakpoint_manager = breakpointManager;
		install_team_debugger_init_debug_infos(team, debuggerTeam,
			debuggerPort, nubPort, nubThread, debuggerWriteLock,
			causingThread);

		RELEASE_TEAM_DEBUG_INFO_LOCK(team->debug_info);
		restore_interrupts(state);
	}

	finish_debugger_change(team);

	// if everything went fine, resume the nub thread, otherwise clean up
	if (error == B_OK) {
		resume_thread(nubThread);
	} else {
		// delete port and terminate thread
		if (nubPort >= 0) {
			set_port_owner(nubPort, B_CURRENT_TEAM);
			delete_port(nubPort);
		}
		if (nubThread >= 0) {
			int32 result;
			wait_for_thread(nubThread, &result);
		}

		delete breakpointManager;
	}

	TRACE(("install_team_debugger() done2: %" B_PRId32 "\n",
		(error == B_OK ? result : error)));
	return (error == B_OK ? result : error);
}


/**
 * @brief Install the default debugger for the current team if none is attached.
 *
 * Used on serious debug events so that even unattached teams get stopped
 * rather than terminated outright.
 *
 * @return B_OK, or the error returned by install_team_debugger().
 */
static status_t
ensure_debugger_installed()
{
	port_id port = install_team_debugger(B_CURRENT_TEAM, -1,
		thread_get_current_thread_id(), true, true);
	return port >= 0 ? B_OK : port;
}


// #pragma mark -


/**
 * @brief Syscall entry point: user-space requests a debugger stop.
 *
 * Auto-installs the default debugger if needed; if installation fails the
 * calling team is exited with status 1. Sends B_DEBUGGER_MESSAGE_DEBUGGER_CALL
 * carrying @a userMessage.
 *
 * @param userMessage User pointer to a NUL-terminated string explaining the stop.
 */
void
_user_debugger(const char *userMessage)
{
	// install the default debugger, if there is none yet
	status_t error = ensure_debugger_installed();
	if (error != B_OK) {
		// time to commit suicide
		char buffer[128];
		ssize_t length = user_strlcpy(buffer, userMessage, sizeof(buffer));
		if (length >= 0) {
			dprintf("_user_debugger(): Failed to install debugger. Message is: "
				"`%s'\n", buffer);
		} else {
			dprintf("_user_debugger(): Failed to install debugger. Message is: "
				"%p (%s)\n", userMessage, strerror(length));
		}
		_user_exit_team(1);
	}

	// prepare the message
	debug_debugger_call message;
	message.message = (void*)userMessage;

	thread_hit_debug_event(B_DEBUGGER_MESSAGE_DEBUGGER_CALL, &message,
		sizeof(message), true);
}


/**
 * @brief Syscall entry point: enable/disable automatic default-debugger install.
 *
 * Flips the B_TEAM_DEBUG_DEBUGGER_DISABLED bit for the current team.
 *
 * @param state Non-zero to disable the default debugger, zero to re-enable.
 * @return The previous enable state.
 */
int
_user_disable_debugger(int state)
{
	Team *team = thread_get_current_thread()->team;

	TRACE(("_user_disable_debugger(%d): team: %" B_PRId32 "\n", state,
		team->id));

	cpu_status cpuState = disable_interrupts();
	GRAB_TEAM_DEBUG_INFO_LOCK(team->debug_info);

	int32 oldFlags;
	if (state) {
		oldFlags = atomic_or(&team->debug_info.flags,
			B_TEAM_DEBUG_DEBUGGER_DISABLED);
	} else {
		oldFlags = atomic_and(&team->debug_info.flags,
			~B_TEAM_DEBUG_DEBUGGER_DISABLED);
	}

	RELEASE_TEAM_DEBUG_INFO_LOCK(team->debug_info);
	restore_interrupts(cpuState);

	// TODO: Check, if the return value is really the old state.
	return !(oldFlags & B_TEAM_DEBUG_DEBUGGER_DISABLED);
}


/**
 * @brief Syscall entry point: set the system-wide default debugger port.
 *
 * Root-only; validates that @a debuggerPort (if >= 0) belongs to a non-kernel team.
 *
 * @param debuggerPort New default debugger port, or < 0 to clear.
 * @return B_OK, B_PERMISSION_DENIED, B_NOT_ALLOWED, or an error from get_port_info().
 */
status_t
_user_install_default_debugger(port_id debuggerPort)
{
	// Do not allow non-root processes to install a default debugger.
	if (geteuid() != 0)
		return B_PERMISSION_DENIED;

	// if supplied, check whether the port is a valid port
	if (debuggerPort >= 0) {
		port_info portInfo;
		status_t error = get_port_info(debuggerPort, &portInfo);
		if (error != B_OK)
			return error;

		// the debugger team must not be the kernel team
		if (portInfo.team == team_get_kernel_team_id())
			return B_NOT_ALLOWED;
	}

	atomic_set(&sDefaultDebuggerPort, debuggerPort);

	return B_OK;
}


/**
 * @brief Syscall entry point: attach a debugger to an arbitrary team.
 *
 * Requires root, or that the caller owns the target team. Delegates to
 * install_team_debugger() with dontReplace=false.
 *
 * @param teamID Target team.
 * @param debuggerPort Port that will receive B_DEBUGGER_MESSAGE_* events.
 * @return The nub port on success, or a negative error.
 */
port_id
_user_install_team_debugger(team_id teamID, port_id debuggerPort)
{
	if (geteuid() != 0 && team_geteuid(teamID) != geteuid())
		return B_PERMISSION_DENIED;

	return install_team_debugger(teamID, debuggerPort, -1, false, false);
}


/**
 * @brief Syscall entry point: detach the debugger from a team.
 *
 * Acquires exclusive debugger-change rights, deletes the nub port (which
 * causes the nub thread to exit and tear down debug state), then waits for
 * the nub thread to finish.
 *
 * @param teamID Target team.
 * @return B_OK on success, B_BAD_VALUE if no debugger, or B_PERMISSION_DENIED.
 */
status_t
_user_remove_team_debugger(team_id teamID)
{
	Team* team;
	ConditionVariable debugChangeCondition;
	debugChangeCondition.Init(NULL, "debug change condition");
	status_t status = prepare_debugger_change(teamID, debugChangeCondition,
		team);
	if (status != B_OK)
		return status;

	InterruptsSpinLocker debugInfoLocker(team->debug_info.lock);

	thread_id nubThread = -1;
	port_id nubPort = -1;

	if ((team->debug_info.flags & B_TEAM_DEBUG_DEBUGGER_INSTALLED) == 0) {
		// no debugger installed
		status = B_BAD_VALUE;
	}

	if (status == B_OK) {
		if (geteuid() != 0 && team->effective_uid != geteuid()
				&& team->debug_info.debugger_team != team_get_current_team_id())
			status = B_PERMISSION_DENIED;
	}

	if (status == B_OK) {
		// there's a debugger installed, and we're allowed to remove it
		nubThread = team->debug_info.nub_thread;
		nubPort = team->debug_info.nub_port;
	}

	debugInfoLocker.Unlock();

	// Delete the nub port -- this will cause the nub thread to terminate and
	// remove the debugger.
	if (nubPort >= 0)
		delete_port(nubPort);

	finish_debugger_change(team);

	// wait for the nub thread
	if (nubThread >= 0)
		wait_for_thread(nubThread, NULL);

	return status;
}


/**
 * @brief Syscall entry point: request a specific thread to stop in the debugger.
 *
 * Sets B_THREAD_DEBUG_STOP and sends SIGNAL_DEBUG_THREAD so the thread enters
 * the debug event path at the next safe point. Refuses the kernel team, the
 * nub thread, and dying threads.
 *
 * @param threadID Target thread.
 * @return B_OK, B_BAD_THREAD_ID, B_NOT_ALLOWED, or B_PERMISSION_DENIED.
 */
status_t
_user_debug_thread(thread_id threadID)
{
	TRACE(("[%" B_PRId32 "] _user_debug_thread(%" B_PRId32 ")\n",
		find_thread(NULL), threadID));

	// get the thread
	Thread* thread = Thread::GetAndLock(threadID);
	if (thread == NULL)
		return B_BAD_THREAD_ID;
	BReference<Thread> threadReference(thread, true);
	ThreadLocker threadLocker(thread, true);

	// we can't debug the kernel team
	if (thread->team == team_get_kernel_team())
		return B_NOT_ALLOWED;

	if (geteuid() != 0 && thread->team->effective_uid != geteuid()
			&& thread->team->debug_info.debugger_team != team_get_current_team_id())
		return B_PERMISSION_DENIED;

	InterruptsLocker interruptsLocker;
	SpinLocker threadDebugInfoLocker(thread->debug_info.lock);

	// If the thread is already dying, it's too late to debug it.
	if ((thread->debug_info.flags & B_THREAD_DEBUG_DYING) != 0)
		return B_BAD_THREAD_ID;

	// don't debug the nub thread
	if ((thread->debug_info.flags & B_THREAD_DEBUG_NUB_THREAD) != 0)
		return B_NOT_ALLOWED;

	// already marked stopped or being told to stop?
	if ((thread->debug_info.flags
			& (B_THREAD_DEBUG_STOPPED | B_THREAD_DEBUG_STOP)) != 0) {
		return B_OK;
	}

	// set the flag that tells the thread to stop as soon as possible
	atomic_or(&thread->debug_info.flags, B_THREAD_DEBUG_STOP);

	update_thread_user_debug_flag(thread);

	// send the thread a SIGNAL_DEBUG_THREAD, so it is interrupted (or
	// continued)
	threadDebugInfoLocker.Unlock();
	ReadSpinLocker teamLocker(thread->team_lock);
	SpinLocker locker(thread->team->signal_lock);

	send_signal_to_thread_locked(thread, SIGNAL_DEBUG_THREAD, NULL, 0);

	return B_OK;
}


/**
 * @brief Syscall entry point: block the caller until a debugger arrives.
 *
 * Sends B_DEBUGGER_MESSAGE_THREAD_DEBUGGED with requireDebugger=false, so the
 * thread sleeps in its debug port waiting for the eventual debugger attach.
 */
void
_user_wait_for_debugger(void)
{
	debug_thread_debugged message = {};
	thread_hit_debug_event(B_DEBUGGER_MESSAGE_THREAD_DEBUGGED, &message,
		sizeof(message), false);
}


/**
 * @brief Syscall entry point: set an arch-level breakpoint or watchpoint directly.
 *
 * Only valid when no debugger is installed (the debugger would otherwise own
 * the breakpoint state). A small race against concurrent debugger install is
 * considered acceptable.
 *
 * @param address Address to trap on.
 * @param type Watchpoint type (ignored for code breakpoints).
 * @param length Watchpoint length in bytes.
 * @param watchpoint If true set a data watchpoint; else an instruction breakpoint.
 * @return B_OK, B_BAD_ADDRESS, B_BAD_VALUE, or arch-layer error.
 */
status_t
_user_set_debugger_breakpoint(void *address, uint32 type, int32 length,
	bool watchpoint)
{
	// check the address and size
	if (address == NULL || !BreakpointManager::CanAccessAddress(address, false))
		return B_BAD_ADDRESS;
	if (watchpoint && length < 0)
		return B_BAD_VALUE;

	// check whether a debugger is installed already
	team_debug_info teamDebugInfo;
	get_team_debug_info(teamDebugInfo);
	if (teamDebugInfo.flags & B_TEAM_DEBUG_DEBUGGER_INSTALLED)
		return B_BAD_VALUE;

	// We can't help it, here's a small but relatively harmless race condition,
	// since a debugger could be installed in the meantime. The worst case is
	// that we install a break/watchpoint the debugger doesn't know about.

	// set the break/watchpoint
	status_t result;
	if (watchpoint)
		result = arch_set_watchpoint(address, type, length);
	else
		result = arch_set_breakpoint(address);

	if (result == B_OK)
		update_threads_breakpoints_flag();

	return result;
}


/**
 * @brief Syscall entry point: clear an arch-level breakpoint/watchpoint.
 *
 * Mirror of _user_set_debugger_breakpoint(); same "no debugger installed"
 * precondition.
 *
 * @param address Address of the existing break/watchpoint.
 * @param watchpoint True for a data watchpoint, false for a code breakpoint.
 * @return B_OK, B_BAD_ADDRESS, B_BAD_VALUE, or arch-layer error.
 */
status_t
_user_clear_debugger_breakpoint(void *address, bool watchpoint)
{
	// check the address
	if (address == NULL || !BreakpointManager::CanAccessAddress(address, false))
		return B_BAD_ADDRESS;

	// check whether a debugger is installed already
	team_debug_info teamDebugInfo;
	get_team_debug_info(teamDebugInfo);
	if (teamDebugInfo.flags & B_TEAM_DEBUG_DEBUGGER_INSTALLED)
		return B_BAD_VALUE;

	// We can't help it, here's a small but relatively harmless race condition,
	// since a debugger could be installed in the meantime. The worst case is
	// that we clear a break/watchpoint the debugger has just installed.

	// clear the break/watchpoint
	status_t result;
	if (watchpoint)
		result = arch_clear_watchpoint(address);
	else
		result = arch_clear_breakpoint(address);

	if (result == B_OK)
		update_threads_breakpoints_flag();

	return result;
}
