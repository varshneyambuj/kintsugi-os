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
 * @file DebugContext.cpp
 * @brief C++ RAII wrapper around the C-level debug_context structure.
 *
 * BDebugContext manages the lifecycle of a debug_context and exposes typed
 * C++ methods for the most common debug nub operations: memory read/write,
 * breakpoint and watchpoint management, thread control, and CPU state queries.
 * It is the base class for BTeamDebugger.
 *
 * @see BTeamDebugger, debug_support.h
 */


#include <DebugContext.h>


/**
 * @brief Default constructor — marks the context as uninitialised.
 */
BDebugContext::BDebugContext()
{
	fContext.team = -1;
}


/**
 * @brief Destructor — calls Uninit() to release any active debug context.
 */
BDebugContext::~BDebugContext()
{
	Uninit();
}


/**
 * @brief Initialise this context for debugging the given team.
 *
 * Calls Uninit() first to release any prior context, then delegates to
 * init_debug_context(). On failure the context is left in the uninitialised
 * state (team == -1).
 *
 * @param team     ID of the team to debug.
 * @param nubPort  The nub port returned by install_team_debugger().
 * @return B_OK on success, or the error returned by init_debug_context().
 */
status_t
BDebugContext::Init(team_id team, port_id nubPort)
{
	Uninit();

	status_t error = init_debug_context(&fContext, team, nubPort);
	if (error != B_OK) {
		fContext.team = -1;
		return error;
	}

	return B_OK;
}


/**
 * @brief Release the underlying debug_context if it is currently initialised.
 *
 * Safe to call multiple times; subsequent calls are no-ops.
 */
void
BDebugContext::Uninit()
{
	if (fContext.team >= 0) {
		destroy_debug_context(&fContext);
		fContext.team = -1;
	}
}


/**
 * @brief Send a typed message to the team's debug nub port.
 *
 * Thin wrapper around send_debug_message() using this context's fContext.
 *
 * @param messageCode  B_DEBUG_MESSAGE_* code identifying the request type.
 * @param message      Pointer to the request structure.
 * @param messageSize  Byte size of @a message.
 * @param reply        Buffer to receive the reply, or NULL for fire-and-forget.
 * @param replySize    Byte capacity of @a reply.
 * @return B_OK on success, or a port communication error.
 */
status_t
BDebugContext::SendDebugMessage(int32 messageCode, const void *message,
	size_t messageSize, void* reply, size_t replySize)
{
	return send_debug_message(&fContext, messageCode, message, messageSize,
		reply, replySize);
}


/**
 * @brief Set the team-wide debugging flags for the debugged team.
 *
 * @param flags  Bit mask of B_TEAM_DEBUG_* flags to apply.
 * @return B_OK on success, or a port communication error.
 */
status_t
BDebugContext::SetTeamDebuggingFlags(int32 flags)
{
	debug_nub_set_team_flags message;
	message.flags = flags;

	return SendDebugMessage(B_DEBUG_MESSAGE_SET_TEAM_FLAGS, &message,
		sizeof(message), NULL, 0);
}


/**
 * @brief Read up to @a size bytes from the debugged team at @a address.
 *
 * @param address  Source address in the target team's address space.
 * @param buffer   Destination buffer.
 * @param size     Maximum bytes to read.
 * @return Bytes actually read, or a negative error code.
 */
ssize_t
BDebugContext::ReadMemoryPartial(const void* address, void* buffer, size_t size)
{
	return debug_read_memory_partial(&fContext, address, buffer, size);
}


/**
 * @brief Read exactly @a size bytes from the debugged team at @a address.
 *
 * Issues multiple partial reads as needed until all bytes are transferred.
 *
 * @param address  Source address in the target team's address space.
 * @param buffer   Destination buffer.
 * @param size     Number of bytes to read.
 * @return Total bytes read, or a negative error code.
 */
ssize_t
BDebugContext::ReadMemory(const void* address, void* buffer, size_t size)
{
	return debug_read_memory(&fContext, address, buffer, size);
}


/**
 * @brief Read a NUL-terminated string from the debugged team.
 *
 * @param address  Address of the string in the target team.
 * @param buffer   Destination buffer.
 * @param size     Byte capacity of @a buffer.
 * @return Number of characters stored, or a negative error code.
 */
ssize_t
BDebugContext::ReadString(const void* address, char* buffer, size_t size)
{
	return debug_read_string(&fContext, address, buffer, size);
}


/**
 * @brief Install a software breakpoint at the given address.
 *
 * Sends a B_DEBUG_MESSAGE_SET_BREAKPOINT request and returns the nub's reply
 * error code.
 *
 * @param address  Address in the target team at which to set the breakpoint.
 * @return B_OK on success, or a nub/kernel error code on failure.
 */
status_t
BDebugContext::SetBreakpoint(void* address)
{
	debug_nub_set_breakpoint message;
	message.reply_port = fContext.reply_port;
	message.address = address;

	debug_nub_set_breakpoint_reply reply;
	status_t error = SendDebugMessage(B_DEBUG_MESSAGE_SET_BREAKPOINT, &message,
		sizeof(message), &reply, sizeof(reply));

	return error == B_OK ? reply.error : error;
}


/**
 * @brief Remove a previously installed software breakpoint.
 *
 * @param address  Address of the breakpoint to clear.
 * @return B_OK on success, or a port communication error.
 */
status_t
BDebugContext::ClearBreakpoint(void* address)
{
	debug_nub_clear_breakpoint message;
	message.address = address;

	return SendDebugMessage(B_DEBUG_MESSAGE_CLEAR_BREAKPOINT, &message,
		sizeof(message), NULL, 0);
}


/**
 * @brief Install a hardware watchpoint at the given address.
 *
 * @param address  Address to watch in the target team.
 * @param type     Watchpoint type (e.g. B_DATA_WRITE_WATCHPOINT).
 * @param length   Number of bytes to watch (hardware-dependent granularity).
 * @return B_OK on success, or a nub/kernel error code on failure.
 */
status_t
BDebugContext::SetWatchpoint(void* address, uint32 type, int32 length)
{
	debug_nub_set_watchpoint message;
	message.reply_port = fContext.reply_port;
	message.address = address;
	message.type = type;
	message.length = length;

	debug_nub_set_watchpoint_reply reply;
	status_t error = SendDebugMessage(B_DEBUG_MESSAGE_SET_WATCHPOINT, &message,
		sizeof(message), &reply, sizeof(reply));

	return error == B_OK ? reply.error : error;
}


/**
 * @brief Remove a previously installed hardware watchpoint.
 *
 * @param address  Address of the watchpoint to clear.
 * @return B_OK on success, or a port communication error.
 */
status_t
BDebugContext::ClearWatchpoint(void* address)
{
	debug_nub_clear_watchpoint message;
	message.address = address;

	return SendDebugMessage(B_DEBUG_MESSAGE_CLEAR_WATCHPOINT, &message,
		sizeof(message), NULL, 0);
}


/**
 * @brief Resume execution of a suspended thread.
 *
 * Sends B_DEBUG_MESSAGE_CONTINUE_THREAD. If @a singleStep is true the thread
 * will stop again after executing a single instruction.
 *
 * @param thread      The thread to resume.
 * @param singleStep  When true, the thread is resumed in single-step mode.
 * @return B_OK on success, or a port communication error.
 */
status_t
BDebugContext::ContinueThread(thread_id thread, bool singleStep)
{
	debug_nub_continue_thread message;
	message.thread = thread;
	message.handle_event = B_THREAD_DEBUG_HANDLE_EVENT;
	message.single_step = singleStep;

	return SendDebugMessage(B_DEBUG_MESSAGE_CONTINUE_THREAD, &message,
		sizeof(message), NULL, 0);
}


/**
 * @brief Set per-thread debugging flags for the specified thread.
 *
 * @param thread  The target thread ID.
 * @param flags   Bit mask of B_THREAD_DEBUG_* flags to apply.
 * @return B_OK on success, or a port communication error.
 */
status_t
BDebugContext::SetThreadDebuggingFlags(thread_id thread, int32 flags)
{
	debug_nub_set_thread_flags message;
	message.thread = thread;
	message.flags = flags;

	return SendDebugMessage(B_DEBUG_MESSAGE_SET_THREAD_FLAGS, &message,
		sizeof(message), NULL, 0);
}


/**
 * @brief Retrieve the full CPU register state for a thread.
 *
 * Delegates to debug_get_cpu_state() using this context's fContext.
 *
 * @param thread        The target thread ID.
 * @param _messageCode  Output — receives the debug message code, or NULL.
 * @param cpuState      Output — receives the full CPU state structure.
 * @return B_OK on success, or an error from the debug nub.
 */
status_t
BDebugContext::GetThreadCpuState(thread_id thread,
	debug_debugger_message* _messageCode, debug_cpu_state* cpuState)
{
	return debug_get_cpu_state(&fContext, thread, _messageCode, cpuState);
}
