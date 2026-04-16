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
 *   Copyright 2021, Jérôme Duval, jerome.duval@gmail.com.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file stack_protector.cpp
 * @brief Kernel-side support for compiler stack smashing protection.
 *
 * Defines the global __stack_chk_guard canary consulted by code compiled with
 * -fstack-protector, the __stack_chk_fail handler that panics when the canary
 * is clobbered on return, and stack_protector_init() which seeds the canary
 * from the secure random pool during kernel startup.
 */


#include <sys/cdefs.h>

#include <SupportDefs.h>

#include <util/Random.h>


extern "C" {

/**
 * @brief Global canary compared on function exit by stack-protector prologues.
 *
 * Written once during kernel initialisation by stack_protector_init() with a
 * value drawn from the secure random pool. Functions compiled with stack
 * protection copy this value onto the stack on entry and compare it on exit;
 * any mismatch triggers __stack_chk_fail().
 */
long __stack_chk_guard;


/**
 * @brief Abort handler invoked when a stack canary mismatch is detected.
 *
 * Called from compiler-generated epilogues when the on-stack copy of
 * __stack_chk_guard has been overwritten. Panics the kernel immediately
 * because continued execution would be unsafe.
 */
void
__stack_chk_fail()
{
	panic("stack smashing detected\n");
}


}


/**
 * @brief Seed the __stack_chk_guard canary from the secure random pool.
 *
 * Invoked once during kernel startup, before any thread relying on stack
 * protection runs, so that every protected function observes a random,
 * unpredictable canary value.
 */
void
stack_protector_init()
{
	__stack_chk_guard = secure_get_random<long>();
}

