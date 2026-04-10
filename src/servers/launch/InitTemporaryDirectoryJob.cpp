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

/** @file InitTemporaryDirectoryJob.cpp
 *  @brief Implements the job that creates and empties the system temporary directory at boot. */


#include "InitTemporaryDirectoryJob.h"

#include <FindDirectory.h>
#include <Path.h>


InitTemporaryDirectoryJob::InitTemporaryDirectoryJob()
	:
	AbstractEmptyDirectoryJob("init /tmp")
{
}


status_t
InitTemporaryDirectoryJob::Execute()
{
	// TODO: the /tmp entries could be scanned synchronously, and deleted
	// later
	BPath path;
	status_t status = find_directory(B_SYSTEM_TEMP_DIRECTORY, &path, true);
	if (status == B_OK)
		status = CreateAndEmpty(path.Path());

	chmod(path.Path(), 0777);
	return status;
}
