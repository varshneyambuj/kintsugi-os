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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SyscallInfo.cpp
 * @brief Implementation of SyscallInfo, capturing one observed system call
 *        on a debugged thread.
 *
 * SyscallInfo records start and end timestamps, syscall number, return
 * value, and the raw argument-byte buffer captured by the kernel debugger
 * hook. The debugger UI consults this to render syscall traces.
 */

#include "SyscallInfo.h"

#include <string.h>


/**
 * @brief Constructs an empty SyscallInfo with all fields zeroed.
 */
SyscallInfo::SyscallInfo()
	:
	fStartTime(0),
	fEndTime(0),
	fReturnValue(0),
	fSyscall(0)
{
	memset(fArguments, 0, sizeof(fArguments));
}


/**
 * @brief Copy-constructs from another SyscallInfo, duplicating the argument bytes.
 *
 * @param other Source instance to copy.
 */
SyscallInfo::SyscallInfo(const SyscallInfo& other)
	:
	fStartTime(other.fStartTime),
	fEndTime(other.fEndTime),
	fReturnValue(other.fReturnValue),
	fSyscall(other.fSyscall)
{
	memcpy(fArguments, other.fArguments, sizeof(fArguments));
}


/**
 * @brief Constructs a SyscallInfo from explicit values.
 *
 * @param startTime   Timestamp when the syscall was entered (microseconds).
 * @param endTime     Timestamp when the syscall returned (microseconds).
 * @param returnValue Kernel return value of the syscall.
 * @param syscall     Syscall number identifying the operation.
 * @param args        Pointer to the raw argument-byte buffer to copy.
 */
SyscallInfo::SyscallInfo(bigtime_t startTime, bigtime_t endTime,
	uint64 returnValue, uint32 syscall, const uint8* args)
	:
	fStartTime(startTime),
	fEndTime(endTime),
	fReturnValue(returnValue),
	fSyscall(syscall)
{
	memcpy(fArguments, args, sizeof(fArguments));
}


/**
 * @brief Replaces all fields and re-copies the argument-byte buffer.
 *
 * @param startTime   Timestamp when the syscall was entered (microseconds).
 * @param endTime     Timestamp when the syscall returned (microseconds).
 * @param returnValue Kernel return value of the syscall.
 * @param syscall     Syscall number identifying the operation.
 * @param args        Pointer to the raw argument-byte buffer to copy.
 */
void
SyscallInfo::SetTo(bigtime_t startTime, bigtime_t endTime, uint64 returnValue,
	uint32 syscall, const uint8* args)
{
	fStartTime = startTime;
	fEndTime = endTime;
	fReturnValue = returnValue;
	fSyscall = syscall;
	memcpy(fArguments, args, sizeof(fArguments));
}
