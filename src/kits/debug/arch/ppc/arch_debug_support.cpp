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
 *   Copyright 2005, Ingo Weinhold, bonefish@users.sf.net.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file arch_debug_support.cpp
 * @brief Architecture-specific debug support stubs for PowerPC.
 *
 * Provides PowerPC implementations of the architecture-dependent debug helper
 * functions used by the debug kit. Both functions currently return B_ERROR as
 * PPC debug register access is not yet implemented.
 *
 * @see debug_support.h, arch_debug_support.h
 */


#include <debug_support.h>

#include "arch_debug_support.h"


/**
 * @brief Retrieve the current instruction pointer and stack frame for a thread.
 *
 * Intended to query the CPU state of the specified thread through the debug
 * nub port and extract the program counter and frame pointer. Currently not
 * implemented on PowerPC.
 *
 * @param context           The debug context identifying the target team.
 * @param thread            The thread whose instruction pointer is requested.
 * @param ip                Output — receives the current instruction pointer.
 * @param stackFrameAddress Output — receives the current stack frame address.
 * @return B_OK on success, B_ERROR if not implemented or on failure.
 */
status_t
arch_debug_get_instruction_pointer(debug_context *context, thread_id thread,
	void **ip, void **stackFrameAddress)
{
	// TODO: Implement!
	return B_ERROR;
}


/**
 * @brief Walk one level up the call stack from the given frame address.
 *
 * Reads the previous frame pointer and return address from the memory at
 * @a stackFrameAddress and fills in @a stackFrameInfo accordingly. Currently
 * not implemented on PowerPC.
 *
 * @param context            The debug context identifying the target team.
 * @param stackFrameAddress  Address of the current stack frame in the target.
 * @param stackFrameInfo     Output — receives parent frame and return address.
 * @return B_OK on success, B_ERROR if not implemented or on failure.
 */
status_t
arch_debug_get_stack_frame(debug_context *context, void *stackFrameAddress,
	debug_stack_frame_info *stackFrameInfo)
{
	// TODO: Implement!
	return B_ERROR;
}
