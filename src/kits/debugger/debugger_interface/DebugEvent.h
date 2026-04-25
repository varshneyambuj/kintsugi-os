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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2009, Ingo Weinhold; Copyright 2013, Rene Gollent.
 */

/** @file DebugEvent.h
    @brief Polymorphic class hierarchy describing events raised by a debugged team. */

#ifndef DEBUG_EVENT_H
#define DEBUG_EVENT_H

#include <debugger.h>

#include "ImageInfo.h"
#include "SignalInfo.h"
#include "SyscallInfo.h"
#include "Types.h"


class CpuState;


// constants for synthetic events generated via the
// start_system_watching() interface
enum {
	DEBUGGER_MESSAGE_THREAD_RENAMED				= 'dmtr',
	DEBUGGER_MESSAGE_THREAD_PRIORITY_CHANGED	= 'dmpc'
};


/** @brief Base class for all debugger events; carries event type plus the team
           and thread that produced it. */
class DebugEvent {
public:
								DebugEvent(int32 eventType,
									team_id team, thread_id thread);
	virtual						~DebugEvent();

			int32 				EventType() const		{ return fEventType; }
			team_id				Team() const			{ return fTeam; }
			thread_id			Thread() const			{ return fThread; }

			bool				ThreadStopped() const { return fThreadStopped; }
			void				SetThreadStopped(bool stopped);

private:
			int32 				fEventType;
			team_id				fTeam;
			thread_id			fThread;
			bool				fThreadStopped;
};


/** @brief DebugEvent that carries an attached CpuState snapshot for the thread. */
class CpuStateEvent : public DebugEvent {
public:
								CpuStateEvent(debug_debugger_message eventType,
									team_id team, thread_id thread,
									CpuState* state);
	virtual						~CpuStateEvent();

			CpuState*			GetCpuState() const	{ return fCpuState; }

private:
			CpuState*			fCpuState;
};


/** @brief Event signaling that a thread has entered debugged state. */
class ThreadDebuggedEvent : public DebugEvent {
public:
								ThreadDebuggedEvent(team_id team,
									thread_id thread);
};


/** @brief Event raised when the team called debugger() with a diagnostic message. */
class DebuggerCallEvent : public DebugEvent {
public:
								DebuggerCallEvent(team_id team,
									thread_id thread, target_addr_t message);

			target_addr_t		Message() const	{ return fMessage; }

private:
			target_addr_t		fMessage;
};


/** @brief Event raised when an installed breakpoint fires in a thread. */
class BreakpointHitEvent : public CpuStateEvent {
public:
								BreakpointHitEvent(team_id team,
									thread_id thread, CpuState* state);
};


/** @brief Event raised when a hardware watchpoint fires in a thread. */
class WatchpointHitEvent : public CpuStateEvent {
public:
								WatchpointHitEvent(team_id team,
									thread_id thread, CpuState* state);
};


/** @brief Event raised after the kernel completes a single-step request. */
class SingleStepEvent : public CpuStateEvent {
public:
								SingleStepEvent(team_id team,
									thread_id thread, CpuState* state);
};


/** @brief Event raised when the thread takes a CPU exception (page fault, etc.). */
class ExceptionOccurredEvent : public DebugEvent {
public:
								ExceptionOccurredEvent(team_id team,
									thread_id thread,
									debug_exception_type exception);

			debug_exception_type Exception() const	{ return fException; }

private:
			debug_exception_type fException;
};


/** @brief Event raised when the debugged team has exited. */
class TeamDeletedEvent : public DebugEvent {
public:
								TeamDeletedEvent(team_id team,
									thread_id thread);
};


/** @brief Event raised when the team performs an exec() and replaces its image. */
class TeamExecEvent : public DebugEvent {
public:
								TeamExecEvent(team_id team, thread_id thread);
};


/** @brief Event raised when a new thread is spawned in the debugged team. */
class ThreadCreatedEvent : public DebugEvent {
public:
								ThreadCreatedEvent(team_id team,
									thread_id thread, thread_id newThread);

			thread_id			NewThread() const	{ return fNewThread; }

private:
			thread_id			fNewThread;
};


/** @brief Synthetic event reporting that a thread's name has been changed. */
class ThreadRenamedEvent : public DebugEvent {
public:
								ThreadRenamedEvent(team_id team,
									thread_id thread, thread_id renamedThread,
									const char* name);

			thread_id			RenamedThread() const { return fRenamedThread; }
			const char*			NewName() const	{ return fName; }

private:
			thread_id			fRenamedThread;
			char				fName[B_OS_NAME_LENGTH];
};


/** @brief Synthetic event reporting that a thread's scheduling priority has changed. */
class ThreadPriorityChangedEvent : public DebugEvent {
public:
								ThreadPriorityChangedEvent(team_id team,
									thread_id thread, thread_id changedThread,
									int32 newPriority);

			thread_id			ChangedThread() const { return fChangedThread; }
			int32				NewPriority() const	{ return fNewPriority; }

private:
			thread_id			fChangedThread;
			int32				fNewPriority;
};


/** @brief Event raised when a thread in the debugged team has terminated. */
class ThreadDeletedEvent : public DebugEvent {
public:
								ThreadDeletedEvent(team_id team,
									thread_id thread);
};


/** @brief Event raised when a new image (executable or library) is loaded. */
class ImageCreatedEvent : public DebugEvent {
public:
								ImageCreatedEvent(team_id team,
									thread_id thread, const ImageInfo& info);

			const ImageInfo&	GetImageInfo() const	{ return fInfo; }

private:
			ImageInfo			fInfo;
};


/** @brief Event raised when a previously-loaded image is unloaded. */
class ImageDeletedEvent : public DebugEvent {
public:
								ImageDeletedEvent(team_id team,
									thread_id thread, const ImageInfo& info);

			const ImageInfo&	GetImageInfo() const	{ return fInfo; }

private:
			ImageInfo			fInfo;
};


/** @brief Event raised after a syscall completes when syscall tracing is enabled. */
class PostSyscallEvent : public DebugEvent {
public:
								PostSyscallEvent(team_id team,
									thread_id thread,
									const SyscallInfo& info);

			const SyscallInfo&	GetSyscallInfo() const	{ return fInfo; }

private:
			SyscallInfo			fInfo;
};


/** @brief Event raised when team debugging is handed over from another debugger. */
class HandedOverEvent : public DebugEvent {
public:
								HandedOverEvent(team_id team,
									thread_id thread, thread_id causingThread);

			thread_id			CausingThread() const { return fCausingThread; }

private:
			thread_id			fCausingThread;
};


/** @brief Event raised when a thread receives a signal. */
class SignalReceivedEvent : public DebugEvent {
public:
								SignalReceivedEvent(team_id team,
									thread_id thread,
									const SignalInfo& info);

			const SignalInfo&	GetSignalInfo() const	{ return fInfo; }

private:
			SignalInfo			fInfo;
};


#endif	// DEBUG_EVENT_H
