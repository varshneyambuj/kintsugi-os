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
 *   Copyright 2012, Alex Smith, alex@alex-smith.me.uk.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file arch_debug_support.cpp
 * @brief Architecture-specific debug support for RISC-V 64-bit.
 *
 * Provides RISC-V 64-bit implementations of the architecture-dependent debug
 * helper functions used by the debug kit. The instruction pointer is read from
 * cpuState.pc and the stack frame address from the x7 (s0/fp) register.
 *
 * @see debug_support.h, arch_debug_support.h
 */


#include <debug_support.h>

#include "arch_debug_support.h"


/** @brief Standard two-word stack frame layout used on RISC-V 64. */
struct stack_frame {
	struct stack_frame	*previous;
	void				*return_address;
};


/**
 * @brief Retrieve the current instruction pointer and stack frame for a thread.
 *
 * Queries the CPU state of the specified thread through the debug nub port,
 * then reads the program counter from cpuState.pc and the frame pointer from
 * the x7 (frame pointer) register.
 *
 * @param context           The debug context identifying the target team.
 * @param thread            The thread whose instruction pointer is requested.
 * @param ip                Output — receives the current instruction pointer.
 * @param stackFrameAddress Output — receives the current stack frame address.
 * @return B_OK on success, or an error code if the CPU state cannot be read.
 */
status_t
arch_debug_get_instruction_pointer(debug_context *context, thread_id thread,
	void **ip, void **stackFrameAddress)
{
	// get the CPU state
	debug_cpu_state cpuState;
	status_t error = debug_get_cpu_state(context, thread, NULL, &cpuState);
	if (error != B_OK)
		return error;

	*ip = (void*)cpuState.pc;
	*stackFrameAddress = (void*)cpuState.x[7];

	return B_OK;
}


/**
 * @brief Walk one level up the call stack from the given frame address.
 *
 * Reads the previous frame pointer and return address from the memory
 * immediately before @a stackFrameAddress (RISC-V 64 convention) and fills in
 * @a stackFrameInfo accordingly.
 *
 * @param context            The debug context identifying the target team.
 * @param stackFrameAddress  Address of the current stack frame in the target.
 * @param stackFrameInfo     Output — receives parent frame and return address.
 * @return B_OK on success, a negative error code if the memory read fails.
 */
status_t
arch_debug_get_stack_frame(debug_context *context, void *stackFrameAddress,
	debug_stack_frame_info *stackFrameInfo)
{
	stack_frame stackFrame;
	ssize_t bytesRead = debug_read_memory(context,
		(uint8*)stackFrameAddress - sizeof(stackFrame),
		&stackFrame, sizeof(stackFrame));

	if (bytesRead < B_OK)
		return bytesRead;
	if (bytesRead != sizeof(stackFrame))
		return B_ERROR;

	stackFrameInfo->frame = stackFrameAddress;
	stackFrameInfo->parent_frame = stackFrame.previous;
	stackFrameInfo->return_address = stackFrame.return_address;

	return B_OK;
}
