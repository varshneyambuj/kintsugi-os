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
 *   Copyright 2012, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file RetrieveMemoryBlockJob.cpp
 * @brief Job that fetches the contents of a memory range from the debugged team.
 *
 * RetrieveMemoryBlockJob reads the bytes backing a TeamMemoryBlock through the
 * TeamMemory interface, queries the region's protection flags so the block can
 * be tagged writable, and finally marks the block valid. Any error encountered
 * during the read is forwarded to interested observers via
 * TeamMemoryBlock::NotifyDataRetrieved().
 */


#include "Jobs.h"

#include <AutoLocker.h>

#include "Team.h"
#include "TeamMemory.h"
#include "TeamMemoryBlock.h"


/**
 * @brief Construct a RetrieveMemoryBlockJob bound to a memory block to fill.
 *
 * Acquires references to the supplied TeamMemory accessor and target memory
 * block so they remain valid for the lifetime of the job.
 *
 * @param team         The owning Team that hosts the memory.
 * @param teamMemory   Memory accessor used to perform the read.
 * @param memoryBlock  Block whose Data() buffer is filled when the job runs.
 */
RetrieveMemoryBlockJob::RetrieveMemoryBlockJob(Team* team,
	TeamMemory* teamMemory, TeamMemoryBlock* memoryBlock)
	:
	fKey(memoryBlock, JOB_TYPE_GET_MEMORY_BLOCK),
	fTeam(team),
	fTeamMemory(teamMemory),
	fMemoryBlock(memoryBlock)
{
	fTeamMemory->AcquireReference();
	fMemoryBlock->AcquireReference();
}


/**
 * @brief Releases references held on the team memory accessor and block.
 */
RetrieveMemoryBlockJob::~RetrieveMemoryBlockJob()
{
	fTeamMemory->ReleaseReference();
	fMemoryBlock->ReleaseReference();
}


/**
 * @brief Returns the unique key identifying this job in the worker queue.
 *
 * @return Reference to the job key keyed on the target memory block.
 */
const JobKey&
RetrieveMemoryBlockJob::Key() const
{
	return fKey;
}


/**
 * @brief Reads the memory block contents and marks the block valid.
 *
 * Performs the underlying ReadMemory() call into the block buffer, then
 * fetches the region's protection flags and tags the block writable when the
 * page carries B_WRITE_AREA. Failures notify observers via
 * NotifyDataRetrieved() before the error is returned.
 *
 * @return B_OK on success or the underlying TeamMemory error.
 */
status_t
RetrieveMemoryBlockJob::Do()
{
	ssize_t result = fTeamMemory->ReadMemory(fMemoryBlock->BaseAddress(),
		fMemoryBlock->Data(), fMemoryBlock->Size());
	if (result < 0) {
		fMemoryBlock->NotifyDataRetrieved(result);
		return result;
	}

	uint32 protection = 0;
	uint32 locking = 0;
	status_t error = fTeamMemory->GetMemoryProperties(
		fMemoryBlock->BaseAddress(), protection, locking);
	if (error != B_OK) {
		fMemoryBlock->NotifyDataRetrieved(error);
		return error;
	}

	fMemoryBlock->SetWritable((protection & B_WRITE_AREA) != 0);
	fMemoryBlock->MarkValid();
	return B_OK;
}
