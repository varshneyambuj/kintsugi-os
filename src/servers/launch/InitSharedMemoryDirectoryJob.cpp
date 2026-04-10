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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file InitSharedMemoryDirectoryJob.cpp
 *  @brief Implements the job that creates, empties, and mounts the shared memory ramfs directory. */


#include <fs_volume.h>

#include "InitSharedMemoryDirectoryJob.h"


/** @brief Constructs the shared memory directory initialization job. */
InitSharedMemoryDirectoryJob::InitSharedMemoryDirectoryJob()
	:
	AbstractEmptyDirectoryJob("init /var/shared_memory")
{
}


/**
 * @brief Creates, empties, and mounts a ramfs volume on /var/shared_memory.
 *
 * First ensures the directory exists and is empty via CreateAndEmpty(), then
 * mounts a ramfs filesystem on it and sets world-readable/writable permissions.
 *
 * @return B_OK on success, or an error code if directory creation or mounting fails.
 */
status_t
InitSharedMemoryDirectoryJob::Execute()
{
	status_t status = CreateAndEmpty("/var/shared_memory");
	if (status != B_OK)
		return status;

	status = fs_mount_volume("/var/shared_memory", NULL, "ramfs", 0, NULL);
	if (status < B_OK)
		return status;

	chmod("/var/shared_memory", 0777);
	return B_OK;
}
