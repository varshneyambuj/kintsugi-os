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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebugEvent.cpp
 * @brief Implements the DebugEvent class hierarchy used to deliver kernel
 *        debug-port messages to the rest of the debugger as typed objects.
 *
 * Each subclass corresponds to a B_DEBUGGER_MESSAGE_* code (or one of the
 * synthetic DEBUGGER_MESSAGE_* codes for system-watching events) and carries
 * the per-event payload such as CpuState snapshots, image info, and signal
 * info.
 */


#include "DebugEvent.h"

#include "CpuState.h"


// #pragma mark - DebugEvent


/**
 * @brief Constructs a debug event with the supplied event type, team, and thread.
 *
 * @param eventType  Type code identifying the event (typically a
 *                   B_DEBUGGER_MESSAGE_* constant).
 * @param team       Team that produced the event.
 * @param thread     Thread that produced the event, or -1 if not applicable.
 */
DebugEvent::DebugEvent(int32 eventType, team_id team,
	thread_id thread)
	:
	fEventType(eventType),
	fTeam(team),
	fThread(thread),
	fThreadStopped(false)
{
}


/**
 * @brief Virtual destructor; nothing to release in the base class.
 */
DebugEvent::~DebugEvent()
{
}


/**
 * @brief Records whether the event left the originating thread stopped.
 *
 * @param stopped  true if the thread is currently stopped, false otherwise.
 */
void
DebugEvent::SetThreadStopped(bool stopped)
{
	fThreadStopped = stopped;
}


// #pragma mark - CpuStateEvent


/**
 * @brief Constructs a CPU-state-bearing event and acquires a reference on the state.
 *
 * @param eventType  Debugger-message code carried by this event.
 * @param team       Originating team.
 * @param thread     Originating thread.
 * @param state      CPU state snapshot to attach; may be NULL.
 */
CpuStateEvent::CpuStateEvent(debug_debugger_message eventType, team_id team,
	thread_id thread, CpuState* state)
	:
	DebugEvent(eventType, team, thread),
	fCpuState(state)
{
	if (fCpuState != NULL)
		fCpuState->AcquireReference();
}


/**
 * @brief Releases the CPU state reference if one was attached.
 */
CpuStateEvent::~CpuStateEvent()
{
	if (fCpuState != NULL)
		fCpuState->ReleaseReference();
}


// #pragma mark - ThreadDebuggedEvent


/**
 * @brief Constructs a thread-debugged event for @a team and @a thread.
 *
 * @param team    Originating team.
 * @param thread  Thread that has entered debugged state.
 */
ThreadDebuggedEvent::ThreadDebuggedEvent(team_id team, thread_id thread)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_THREAD_DEBUGGED, team, thread)
{
}


// #pragma mark - DebuggerCallEvent


/**
 * @brief Constructs an event describing an explicit debugger() call.
 *
 * @param team     Originating team.
 * @param thread   Thread that issued the call.
 * @param message  Target-side address of the message string passed to debugger().
 */
DebuggerCallEvent::DebuggerCallEvent(team_id team, thread_id thread,
	target_addr_t message)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_DEBUGGER_CALL, team, thread),
	fMessage(message)
{
}


// #pragma mark - BreakpointHitEvent


/**
 * @brief Constructs a breakpoint-hit event with an attached CPU state.
 *
 * @param team    Originating team.
 * @param thread  Thread that hit the breakpoint.
 * @param state   CPU state at the moment of the hit; may be NULL.
 */
BreakpointHitEvent::BreakpointHitEvent(team_id team, thread_id thread,
	CpuState* state)
	:
	CpuStateEvent(B_DEBUGGER_MESSAGE_BREAKPOINT_HIT, team, thread, state)
{
}


// #pragma mark - WatchpointHitEvent


/**
 * @brief Constructs a watchpoint-hit event with an attached CPU state.
 *
 * @param team    Originating team.
 * @param thread  Thread that triggered the watchpoint.
 * @param state   CPU state at the moment of the hit; may be NULL.
 */
WatchpointHitEvent::WatchpointHitEvent(team_id team, thread_id thread,
	CpuState* state)
	:
	CpuStateEvent(B_DEBUGGER_MESSAGE_WATCHPOINT_HIT, team, thread, state)
{
}



// #pragma mark - SingleStepEvent


/**
 * @brief Constructs a single-step completion event with an attached CPU state.
 *
 * @param team    Originating team.
 * @param thread  Thread that completed the step.
 * @param state   CPU state after the step; may be NULL.
 */
SingleStepEvent::SingleStepEvent(team_id team, thread_id thread,
	CpuState* state)
	:
	CpuStateEvent(B_DEBUGGER_MESSAGE_SINGLE_STEP, team, thread, state)
{
}


// #pragma mark - ExceptionOccurredEvent


/**
 * @brief Constructs an exception-occurred event for the given exception type.
 *
 * @param team       Originating team.
 * @param thread     Thread that took the exception.
 * @param exception  Architecture-defined exception code.
 */
ExceptionOccurredEvent::ExceptionOccurredEvent(team_id team, thread_id thread,
	debug_exception_type exception)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED, team, thread),
	fException(exception)
{
}


// #pragma mark - TeamDeletedEvent


/**
 * @brief Constructs an event reporting that the team has exited.
 *
 * @param team    Team that exited.
 * @param thread  Last thread observed in the team, or -1.
 */
TeamDeletedEvent::TeamDeletedEvent(team_id team, thread_id thread)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_TEAM_DELETED, team, thread)
{
}


// #pragma mark - TeamExecEvent


/**
 * @brief Constructs an event reporting that the team has performed an exec().
 *
 * @param team    Team that exec()'d.
 * @param thread  Calling thread.
 */
TeamExecEvent::TeamExecEvent(team_id team, thread_id thread)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_TEAM_EXEC, team, thread)
{
}


// #pragma mark - ThreadCreatedEvent


/**
 * @brief Constructs an event describing a newly-created thread.
 *
 * @param team       Owning team.
 * @param thread     Thread that triggered the report (parent).
 * @param newThread  Identifier of the newly-created thread.
 */
ThreadCreatedEvent::ThreadCreatedEvent(team_id team, thread_id thread,
	thread_id newThread)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_THREAD_CREATED, team, thread),
	fNewThread(newThread)
{
}


// #pragma mark - ThreadRenamedEvent


/**
 * @brief Constructs a synthetic thread-renamed event.
 *
 * @param team           Owning team.
 * @param thread         Thread that triggered the report.
 * @param renamedThread  Thread whose name has changed.
 * @param newName        New thread name; copied into a fixed-size buffer.
 */
ThreadRenamedEvent::ThreadRenamedEvent(team_id team, thread_id thread,
	thread_id renamedThread, const char* newName)
	:
	DebugEvent(DEBUGGER_MESSAGE_THREAD_RENAMED, team, thread),
	fRenamedThread(renamedThread)
{
	strlcpy(fName, newName, sizeof(fName));
}


// #pragma mark - ThreadPriorityChangedEvent


/**
 * @brief Constructs a synthetic thread-priority-changed event.
 *
 * @param team           Owning team.
 * @param thread         Thread that triggered the report.
 * @param changedThread  Thread whose priority has changed.
 * @param newPriority    New scheduling priority value.
 */
ThreadPriorityChangedEvent::ThreadPriorityChangedEvent(team_id team,
	thread_id thread, thread_id changedThread, int32 newPriority)
	:
	DebugEvent(DEBUGGER_MESSAGE_THREAD_PRIORITY_CHANGED, team, thread),
	fChangedThread(changedThread),
	fNewPriority(newPriority)
{
}


// #pragma mark - ThreadDeletedEvent


/**
 * @brief Constructs an event describing a terminated thread.
 *
 * @param team    Owning team.
 * @param thread  Thread that has exited.
 */
ThreadDeletedEvent::ThreadDeletedEvent(team_id team, thread_id thread)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_THREAD_DELETED, team, thread)
{
}


// #pragma mark - ImageCreatedEvent


/**
 * @brief Constructs an event describing a newly-loaded image.
 *
 * @param team    Owning team.
 * @param thread  Thread that triggered the load.
 * @param info    Descriptor of the loaded image; copied.
 */
ImageCreatedEvent::ImageCreatedEvent(team_id team, thread_id thread,
	const ImageInfo& info)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_IMAGE_CREATED, team, thread),
	fInfo(info)
{
}


// #pragma mark - ImageDeletedEvent


/**
 * @brief Constructs an event describing an unloaded image.
 *
 * @param team    Owning team.
 * @param thread  Thread that triggered the unload.
 * @param info    Descriptor of the unloaded image; copied.
 */
ImageDeletedEvent::ImageDeletedEvent(team_id team, thread_id thread,
	const ImageInfo& info)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_IMAGE_DELETED, team, thread),
	fInfo(info)
{
}


// #pragma mark - PostSyscallEvent


/**
 * @brief Constructs an event describing a syscall that has just completed.
 *
 * @param team    Owning team.
 * @param thread  Thread that performed the syscall.
 * @param info    Syscall arguments and return value; copied.
 */
PostSyscallEvent::PostSyscallEvent(team_id team, thread_id thread,
	const SyscallInfo& info)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_POST_SYSCALL, team, thread),
	fInfo(info)
{
}


// #pragma mark - HandedOverEvent


/**
 * @brief Constructs an event describing a debugger handover.
 *
 * @param team           Team that has been handed over.
 * @param thread         Thread that triggered the handover.
 * @param causingThread  Thread responsible for the original installation.
 */
HandedOverEvent::HandedOverEvent(team_id team, thread_id thread,
	thread_id causingThread)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_HANDED_OVER, team, thread),
	fCausingThread(causingThread)
{
}


// #pragma mark - SignalReceivedEvent


/**
 * @brief Constructs an event describing a delivered signal.
 *
 * @param team    Owning team.
 * @param thread  Thread that received the signal.
 * @param info    Signal payload; copied.
 */
SignalReceivedEvent::SignalReceivedEvent(team_id team, thread_id thread,
	const SignalInfo& info)
	:
	DebugEvent(B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED, team, thread),
	fInfo(info)
{
}
