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
 *   Copyright 2015, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file WriteMemoryJob.cpp
 * @brief Job that writes a buffer to a target memory address in the team.
 *
 * WriteMemoryJob delegates to TeamMemory::WriteMemory() for the actual byte
 * transfer and, on success, posts a Team::NotifyMemoryChanged() so observers
 * (memory views, value nodes, and so on) can refresh themselves.
 */


#include "Jobs.h"

#include "Team.h"
#include "TeamMemory.h"


/**
 * @brief Construct a WriteMemoryJob describing the buffer to push.
 *
 * Acquires a reference on the TeamMemory accessor. The caller retains
 * ownership of the @a data buffer for the lifetime of the job.
 *
 * @param team        Owning team that will be notified after the write.
 * @param teamMemory  Memory accessor used to perform the write.
 * @param address     Target address in the debugged process.
 * @param data        Pointer to the byte buffer to write.
 * @param size        Number of bytes to copy from @a data.
 */
WriteMemoryJob::WriteMemoryJob(Team* team,
	TeamMemory* teamMemory, target_addr_t address, void* data,
	target_size_t size)
	:
	fKey(data, JOB_TYPE_WRITE_MEMORY),
	fTeam(team),
	fTeamMemory(teamMemory),
	fTargetAddress(address),
	fData(data),
	fSize(size)
{
	fTeamMemory->AcquireReference();
}


/**
 * @brief Releases the reference held on the team memory accessor.
 */
WriteMemoryJob::~WriteMemoryJob()
{
	fTeamMemory->ReleaseReference();
}


/**
 * @brief Returns the worker-queue key identifying this job.
 *
 * @return Reference to the job key keyed on the data buffer pointer.
 */
const JobKey&
WriteMemoryJob::Key() const
{
	return fKey;
}


/**
 * @brief Performs the write and notifies the team on success.
 *
 * @return B_OK on success or the negative error code from WriteMemory().
 */
status_t
WriteMemoryJob::Do()
{
	ssize_t result = fTeamMemory->WriteMemory(fTargetAddress, fData, fSize);
	if (result < 0)
		return result;

	fTeam->NotifyMemoryChanged(fTargetAddress, fSize);

	return B_OK;
}
