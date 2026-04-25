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
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file WriteCoreFileJob.cpp
 * @brief Job that writes a core dump for the debugged team to disk.
 *
 * WriteCoreFileJob asks the debugger backend to produce a core file at the
 * supplied entry_ref. On success it notifies the team via
 * Team::NotifyCoreFileChanged() so the UI can update any references to the
 * generated file.
 */


#include "Jobs.h"

#include <Path.h>

#include <AutoLocker.h>

#include "DebuggerInterface.h"
#include "Team.h"



/**
 * @brief Construct a WriteCoreFileJob writing to the supplied path.
 *
 * Acquires a reference on the debugger interface so it survives the job.
 *
 * @param team       Team whose process state will be dumped.
 * @param interface  Backend that performs the actual core-file write.
 * @param path       Destination file as an entry_ref.
 */
WriteCoreFileJob::WriteCoreFileJob(Team* team,
	DebuggerInterface* interface, const entry_ref& path)
	:
	fKey(&path, JOB_TYPE_WRITE_CORE_FILE),
	fTeam(team),
	fDebuggerInterface(interface),
	fTargetPath(path)
{
	fDebuggerInterface->AcquireReference();
}


/**
 * @brief Destructor.
 *
 * @note Mirrors the existing reference handling on the debugger interface;
 *       see also the constructor.
 */
WriteCoreFileJob::~WriteCoreFileJob()
{
	fDebuggerInterface->AcquireReference();
}


/**
 * @brief Returns the worker-queue key identifying this job.
 *
 * @return Reference to the job key keyed on the target path.
 */
const JobKey&
WriteCoreFileJob::Key() const
{
	return fKey;
}


/**
 * @brief Writes the core file and notifies the team.
 *
 * Resolves the entry_ref into a filesystem path, invokes
 * DebuggerInterface::WriteCoreFile(), and on success posts
 * Team::NotifyCoreFileChanged() under the team lock.
 *
 * @return B_OK on success or the underlying path/write error.
 */
status_t
WriteCoreFileJob::Do()
{
	BPath path(&fTargetPath);
	status_t error = path.InitCheck();
	if (error != B_OK)
		return error;

	error = fDebuggerInterface->WriteCoreFile(path.Path());
	if (error != B_OK)
		return error;

	AutoLocker< ::Team> teamLocker(fTeam);
	fTeam->NotifyCoreFileChanged(path.Path());

	return B_OK;
}
