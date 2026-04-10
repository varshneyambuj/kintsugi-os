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

/** @file Worker.cpp
 *  @brief Implements worker threads that pull and execute jobs from the job queue. */


#include "Worker.h"


static const bigtime_t kWorkerTimeout = 1000000;
	// One second until a worker thread quits without a job

static const int32 kWorkerCountPerCPU = 3;

static int32 sWorkerCount;


Worker::Worker(JobQueue& queue)
	:
	fThread(-1),
	fJobQueue(queue)
{
}


Worker::~Worker()
{
}


status_t
Worker::Init()
{
	fThread = spawn_thread(&Worker::_Process, Name(), B_NORMAL_PRIORITY,
		this);
	if (fThread < 0)
		return fThread;

	status_t status = resume_thread(fThread);
	if (status == B_OK)
		atomic_add(&sWorkerCount, 1);

	return status;
}


status_t
Worker::Process()
{
	while (true) {
		BJob* job;
		status_t status = fJobQueue.Pop(Timeout(), false, &job);
		if (status != B_OK)
			return status;

		status = Run(job);
		if (status != B_OK) {
			// TODO: proper error reporting on failed job!
			debug_printf("Launching %s failed: %s\n", job->Title().String(),
				strerror(status));
		}
	}
}


bigtime_t
Worker::Timeout() const
{
	return kWorkerTimeout;
}


const char*
Worker::Name() const
{
	return "worker";
}


status_t
Worker::Run(BJob* job)
{
	return job->Run();
}


/*static*/ status_t
Worker::_Process(void* _self)
{
	Worker* self = (Worker*)_self;
	status_t status = self->Process();
	delete self;

	return status;
}


// #pragma mark -


MainWorker::MainWorker(JobQueue& queue)
	:
	Worker(queue),
	fMaxWorkerCount(kWorkerCountPerCPU)
{
	// TODO: keep track of workers, and quit them on destruction
	system_info info;
	if (get_system_info(&info) == B_OK)
		fMaxWorkerCount = info.cpu_count * kWorkerCountPerCPU;
}


bigtime_t
MainWorker::Timeout() const
{
	return B_INFINITE_TIMEOUT;
}


const char*
MainWorker::Name() const
{
	return "main worker";
}


status_t
MainWorker::Run(BJob* job)
{
	int32 count = atomic_get(&sWorkerCount);

	size_t jobCount = fJobQueue.CountJobs();
	if (jobCount > INT_MAX)
		jobCount = INT_MAX;

	if ((int32)jobCount > count && count < fMaxWorkerCount) {
		Worker* worker = new Worker(fJobQueue);
		worker->Init();
	}

	return Worker::Run(job);
}
