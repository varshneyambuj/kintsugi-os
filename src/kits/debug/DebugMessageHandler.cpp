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
 *   Copyright 2010, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file DebugMessageHandler.cpp
 * @brief Base class for handling debugger messages dispatched by BDebugLooper.
 *
 * BDebugMessageHandler provides a virtual dispatch table for all kernel debug
 * events. Subclasses override the specific Handle*() methods they care about;
 * unhandled events fall through to UnhandledDebugMessage(), which returns true
 * (continue the thread) by default.
 *
 * @see BDebugLooper, BTeamDebugger
 */


#include <DebugMessageHandler.h>


/**
 * @brief Virtual destructor — allows safe polymorphic deletion of subclasses.
 */
BDebugMessageHandler::~BDebugMessageHandler()
{
}


/*!	Handles the supplied debugger message.
	Can be overridded by subclasses. The base class implementation calls the
	respective Handle*() hook for the message.

	\param messageCode The (port) message code identifying the debugger message.
	\param message The message data.
	\return \c true, if the caller is supposed to continue the thread, \c false
		otherwise.
*/
/**
 * @brief Dispatch a raw debugger port message to the appropriate Handle*() hook.
 *
 * Switches on @a messageCode and calls the matching Handle*() virtual method.
 * Can be overridden by subclasses that need to intercept dispatch before the
 * individual hooks run.
 *
 * @param messageCode  The B_DEBUGGER_MESSAGE_* port code identifying the event.
 * @param message      The full debug message data union.
 * @return true if the caller should continue the suspended thread, false to
 *         keep it suspended.
 */
bool
BDebugMessageHandler::HandleDebugMessage(int32 messageCode,
	const debug_debugger_message_data& message)
{
	switch (messageCode) {
		case B_DEBUGGER_MESSAGE_THREAD_DEBUGGED:
			return HandleThreadDebugged(message.thread_debugged);
		case B_DEBUGGER_MESSAGE_DEBUGGER_CALL:
			return HandleDebuggerCall(message.debugger_call);
		case B_DEBUGGER_MESSAGE_BREAKPOINT_HIT:
			return HandleBreakpointHit(message.breakpoint_hit);
		case B_DEBUGGER_MESSAGE_WATCHPOINT_HIT:
			return HandleWatchpointHit(message.watchpoint_hit);
		case B_DEBUGGER_MESSAGE_SINGLE_STEP:
			return HandleSingleStep(message.single_step);
		case B_DEBUGGER_MESSAGE_PRE_SYSCALL:
			return HandlePreSyscall(message.pre_syscall);
		case B_DEBUGGER_MESSAGE_POST_SYSCALL:
			return HandlePostSyscall(message.post_syscall);
		case B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED:
			return HandleSignalReceived(message.signal_received);
		case B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED:
			return HandleExceptionOccurred(message.exception_occurred);
		case B_DEBUGGER_MESSAGE_TEAM_CREATED:
			return HandleTeamCreated(message.team_created);
		case B_DEBUGGER_MESSAGE_TEAM_DELETED:
			return HandleTeamDeleted(message.team_deleted);
		case B_DEBUGGER_MESSAGE_TEAM_EXEC:
			return HandleTeamExec(message.team_exec);
		case B_DEBUGGER_MESSAGE_THREAD_CREATED:
			return HandleThreadCreated(message.thread_created);
		case B_DEBUGGER_MESSAGE_THREAD_DELETED:
			return HandleThreadDeleted(message.thread_deleted);
		case B_DEBUGGER_MESSAGE_IMAGE_CREATED:
			return HandleImageCreated(message.image_created);
		case B_DEBUGGER_MESSAGE_IMAGE_DELETED:
			return HandleImageDeleted(message.image_deleted);
		case B_DEBUGGER_MESSAGE_PROFILER_UPDATE:
			return HandleProfilerUpdate(message.profiler_update);
		case B_DEBUGGER_MESSAGE_HANDED_OVER:
			return HandleHandedOver(message.handed_over);
		default:
			return true;
	}
}


/**
 * @brief Called when a thread has been put under debugger control.
 *
 * @param message  The B_DEBUGGER_MESSAGE_THREAD_DEBUGGED payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleThreadDebugged(const debug_thread_debugged& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_THREAD_DEBUGGED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a thread invoked the debugger() syscall.
 *
 * @param message  The B_DEBUGGER_MESSAGE_DEBUGGER_CALL payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleDebuggerCall(const debug_debugger_call& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_DEBUGGER_CALL,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a thread has hit a software breakpoint.
 *
 * @param message  The B_DEBUGGER_MESSAGE_BREAKPOINT_HIT payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleBreakpointHit(const debug_breakpoint_hit& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_BREAKPOINT_HIT,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a hardware watchpoint has been triggered.
 *
 * @param message  The B_DEBUGGER_MESSAGE_WATCHPOINT_HIT payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleWatchpointHit(const debug_watchpoint_hit& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_WATCHPOINT_HIT,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called after a thread executes a single instruction in step mode.
 *
 * @param message  The B_DEBUGGER_MESSAGE_SINGLE_STEP payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleSingleStep(const debug_single_step& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_SINGLE_STEP,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called immediately before a thread enters a syscall.
 *
 * @param message  The B_DEBUGGER_MESSAGE_PRE_SYSCALL payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandlePreSyscall(const debug_pre_syscall& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_PRE_SYSCALL,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called immediately after a thread returns from a syscall.
 *
 * @param message  The B_DEBUGGER_MESSAGE_POST_SYSCALL payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandlePostSyscall(const debug_post_syscall& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_POST_SYSCALL,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a signal has been delivered to a thread.
 *
 * @param message  The B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleSignalReceived(const debug_signal_received& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_SIGNAL_RECEIVED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a CPU exception has occurred in a thread.
 *
 * @param message  The B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED payload.
 * @return true to continue the thread; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleExceptionOccurred(
	const debug_exception_occurred& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_EXCEPTION_OCCURRED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a new team has been created.
 *
 * @param message  The B_DEBUGGER_MESSAGE_TEAM_CREATED payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleTeamCreated(const debug_team_created& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_TEAM_CREATED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a team has been deleted.
 *
 * @param message  The B_DEBUGGER_MESSAGE_TEAM_DELETED payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleTeamDeleted(const debug_team_deleted& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_TEAM_DELETED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a team has executed a new program via exec().
 *
 * @param message  The B_DEBUGGER_MESSAGE_TEAM_EXEC payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleTeamExec(const debug_team_exec& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_TEAM_EXEC,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a new thread has been created in the debugged team.
 *
 * @param message  The B_DEBUGGER_MESSAGE_THREAD_CREATED payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleThreadCreated(const debug_thread_created& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_THREAD_CREATED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a thread in the debugged team has exited.
 *
 * @param message  The B_DEBUGGER_MESSAGE_THREAD_DELETED payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleThreadDeleted(const debug_thread_deleted& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_THREAD_DELETED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when a new image (shared library or executable) has been loaded.
 *
 * @param message  The B_DEBUGGER_MESSAGE_IMAGE_CREATED payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleImageCreated(const debug_image_created& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_IMAGE_CREATED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when an image has been unloaded from the debugged team.
 *
 * @param message  The B_DEBUGGER_MESSAGE_IMAGE_DELETED payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleImageDeleted(const debug_image_deleted& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_IMAGE_DELETED,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when the profiler has accumulated new sampling data.
 *
 * @param message  The B_DEBUGGER_MESSAGE_PROFILER_UPDATE payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleProfilerUpdate(const debug_profiler_update& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_PROFILER_UPDATE,
		(const debug_debugger_message_data&)message);
}


/**
 * @brief Called when the debugger has been handed control of a thread.
 *
 * @param message  The B_DEBUGGER_MESSAGE_HANDED_OVER payload.
 * @return true to continue; the base implementation delegates to
 *         UnhandledDebugMessage().
 */
bool
BDebugMessageHandler::HandleHandedOver(const debug_handed_over& message)
{
	return UnhandledDebugMessage(B_DEBUGGER_MESSAGE_HANDED_OVER,
		(const debug_debugger_message_data&)message);
}


/*!	Called by the base class versions of the specific Handle*() methods.
	Can be overridded to handle any message not handled otherwise.
*/
/**
 * @brief Fallback handler for any debug message not overridden by a subclass.
 *
 * The default implementation simply returns true to continue the thread.
 * Subclasses can override this method to intercept all otherwise-unhandled
 * messages in a single place.
 *
 * @param messageCode  The B_DEBUGGER_MESSAGE_* code.
 * @param message      The full debug message data union.
 * @return true always (continue the thread).
 */
bool
BDebugMessageHandler::UnhandledDebugMessage(int32 messageCode,
	const debug_debugger_message_data& message)
{
	return true;
}
