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
 *   Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file kernel_errno.cpp
 * @brief Kernel-side implementation of per-thread errno storage.
 *
 * Provides _errnop(), the hook used by the kernel's libc-style routines to
 * locate a writable errno slot. The returned pointer addresses storage inside
 * the current Thread structure so kernel code paths never disturb the user
 * space errno value seen through POSIX calls.
 */


#include "thread.h"

#include <errno.h>


/**
 * @brief Return a pointer to the current thread's kernel-side errno slot.
 *
 * Kernel-internal POSIX-style routines dereference the returned pointer to
 * read or write errno without touching the user space errno stored elsewhere
 * in the thread. The slot lives in the current Thread structure.
 *
 * @return Pointer to the int errno slot owned by the currently running thread.
 */
int *
_errnop(void)
{
	Thread *thread = thread_get_current_thread();

	return &thread->kernel_errno;
}

