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
 *   Copyright 2025, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 */

/**
 * @file scheduler_load.cpp
 * @brief System-wide 1/5/15-minute load average (FreeBSD-style).
 *
 * A kernel daemon periodically samples the number of runnable threads across
 * every core and feeds it through the exponential decay filter used by
 * FreeBSD's kern_sync.c, producing the three load averages exposed by
 * _user_get_loadavg().
 */


#include "scheduler_cpu.h"

#include <kernel.h>
#include <scheduler_defs.h>


using namespace Scheduler;

// load average algorithm from FreeBSD, see kern_sync.c
const static int kFShift = 11;
const static long kFScale = 1 << kFShift;
static struct loadavg sAverageRunnable = {{0, 0, 0}, kFScale};
const static uint64 sCExp[3] = {(uint64)(0.9200444146293232 * kFScale),
	(uint64)(0.9834714538216174 * kFScale), (uint64)(0.9944598480048967 * kFScale)};


/**
 * @brief Kernel-daemon tick: fold the current runnable count into the EWMA.
 *
 * Sums per-core thread counts (minus the daemon itself) and applies the
 * three decay constants to produce the 1/5/15-minute averages.
 *
 * @param data      Unused (NULL is passed by register_kernel_daemon).
 * @param iteration Unused daemon iteration counter.
 */
static void
_LoadavgUpdate(void *data, int iteration)
{
	uint64 threadCount = 0;
	for (int i = 0; i < gCoreCount; i++)
		threadCount += gCoreEntries[i].ThreadCount();
	threadCount--;
	for (int i = 0; i < 3; i++) {
		sAverageRunnable.ldavg[i]
			= (sCExp[i] * sAverageRunnable.ldavg[i] + threadCount * kFScale * (kFScale - sCExp[i]))
			>> kFShift;
	}
}


/**
 * @brief Register the load-average daemon (runs once every five seconds).
 * @return B_OK (the daemon registration does not fail).
 */
status_t
scheduler_loadavg_init()
{
	register_kernel_daemon(_LoadavgUpdate, NULL, 50);
		// run the daemon once five second

	return B_OK;
}


// #pragma mark - Syscalls


/**
 * @brief Syscall entry point: copy the system load average to userspace.
 * @param userInfo Destination loadavg struct in userspace.
 * @param size     Must equal sizeof(struct loadavg).
 * @return B_OK on success, B_BAD_ADDRESS or B_BAD_VALUE on invalid input.
 */
status_t
_user_get_loadavg(struct loadavg* userInfo, size_t size)
{
	if (userInfo == NULL || !IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;
	if (size != sizeof(struct loadavg))
		return B_BAD_VALUE;
	if (user_memcpy(userInfo, &sAverageRunnable, sizeof(struct loadavg)) < B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}
