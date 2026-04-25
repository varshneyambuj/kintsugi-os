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
 * MIT License. Copyright 2005, Ingo Weinhold.
 */

/** @file arch_debug_support.h
    @brief Per-architecture interface used by the debug kit to read a target
           thread's instruction pointer and walk its stack frames. */

#ifndef _ARCH_DEBUG_SUPPORT_H
#define _ARCH_DEBUG_SUPPORT_H

#include <debug_support.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Retrieve the current instruction pointer and stack frame for a
 *         thread; implemented by each arch/<arch>/arch_debug_support.cpp. */
status_t arch_debug_get_instruction_pointer(debug_context *context,
			thread_id thread, void **ip, void **stackFrameAddress);

/** @brief Walk one level up the call stack from the given frame address;
 *         implemented by each arch/<arch>/arch_debug_support.cpp. */
status_t arch_debug_get_stack_frame(debug_context *context,
			void *stackFrameAddress, debug_stack_frame_info *stackFrameInfo);

#ifdef __cplusplus
}	// extern "C"
#endif

#endif	// _ARCH_DEBUG_SUPPORT_H
