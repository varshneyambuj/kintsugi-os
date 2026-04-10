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


/** @brief Idle timeout for auxiliary worker threads (one second). */
static const bigtime_t kWorkerTimeout = 1000000;
	// One second until a worker thread quits without a job

/** @brief Maximum number of worker threads to spawn per CPU core. */
static const int32 kWorkerCountPerCPU = 3;

/** @brief Global count of currently active worker threads. */
static int32 sWorkerCount;


/**
 * @brief Constructs a worker bound to the given job queue.
 *
 * @param queue The shared job queue from which this worker will pull jobs.
 */
Worker::Worker(JobQueue& queue)
	:
	fThread(-1),
	fJobQueue(queue)
{
}


/** @brief Destroys the worker. */
Worker::~Worker()
{
}


/**
 * @brief Spawns and resumes the worker thread.
 *
 * Creates a new kernel thread running _Process(), increments the global
 * worker count on success, and returns the thread ID or an error.
 *
 * @return B_OK on success, or a negative error code on failure.
 */
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


/**
 * @brief Main processing loop that pops and runs jobs until the queue times out.
 *
 * Continuously pops jobs from the queue (blocking up to Timeout()) and
 * executes them via Run(). Returns when the queue pop times out.
 *
 * @return The status code from the last queue pop (typically B_TIMED_OUT).
 */
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


/**
 * @brief Returns the idle timeout for this worker.
 *
 * @return The timeout in microseconds before the worker exits if no job is available.
 */
bigtime_t
Worker::Timeout() const
{
	return kWorkerTimeout;
}


/**
 * @brief Returns the human-readable name of this worker thread.
 *
 * @return A static string identifying this as a "worker" thread.
 */
const char*
Worker::Name() const
{
	return "worker";
}


/**
 * @brief Executes a single job.
 *
 * @param job The job to run.
 * @return The status code returned by the job's Run() method.
 */
status_t
Worker::Run(BJob* job)
{
	return job->Run();
}


/**
 * @brief Static thread entry point that delegates to Process() and deletes the worker on exit.
 *
 * @param _self Opaque pointer to the Worker instance (cast from void*).
 * @return The status code from Process().
 */
/*static*/ status_t
Worker::_Process(void* _self)
{
	Worker* self = (Worker*)_self;
	status_t status = self->Process();
	delete self;

	return status;
}


// #pragma mark -


/**
 * @brief Constructs the main worker, computing the maximum auxiliary worker count from CPU count.
 *
 * @param queue The shared job queue from which this and spawned workers pull jobs.
 */
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


/**
 * @brief Returns an infinite timeout so the main worker never exits.
 *
 * @return B_INFINITE_TIMEOUT.
 */
bigtime_t
MainWorker::Timeout() const
{
	return B_INFINITE_TIMEOUT;
}


/**
 * @brief Returns the human-readable name of the main worker thread.
 *
 * @return A static string identifying this as the "main worker" thread.
 */
const char*
MainWorker::Name() const
{
	return "main worker";
}


/**
 * @brief Runs a job, spawning additional auxiliary workers if the queue has excess jobs.
 *
 * Before executing the job, checks if the number of pending jobs exceeds
 * the current worker count. If so and the maximum has not been reached,
 * spawns a new auxiliary Worker.
 *
 * @param job The job to execute.
 * @return The status code from the job's Run() method.
 */
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
