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

/** @file Worker.h
 *  @brief Worker thread that pulls BJobs from a queue and runs them. */

#ifndef WORKER_H
#define WORKER_H


#include <Job.h>
#include <JobQueue.h>


using namespace BSupportKit;
using BSupportKit::BPrivate::JobQueue;


/** @brief Background thread that pops jobs from a JobQueue and executes them.
 *
 * Subclassed by MainWorker to add demand-based scaling: when the queue is
 * deeper than the current number of workers, MainWorker spawns additional
 * Worker instances on the fly. */
class Worker {
public:
								Worker(JobQueue& queue);
	virtual						~Worker();

	/** @brief Spawns the worker thread and begins draining the queue. */
			status_t			Init();

protected:
	/** @brief Main loop body run inside the worker thread. */
	virtual	status_t			Process();
	/** @brief Returns how long the worker should idle before exiting. */
	virtual	bigtime_t			Timeout() const;
	/** @brief Returns the worker thread's debug name. */
	virtual	const char*			Name() const;
	/** @brief Runs a single dequeued job. */
	virtual	status_t			Run(BJob* job);

private:
	static	status_t			_Process(void* self);

protected:
			thread_id			fThread;     /**< Worker thread id. */
			JobQueue&			fJobQueue;   /**< Queue this worker drains from. */
};


/** @brief Singleton worker that owns the main job queue and scales additional workers. */
class MainWorker : public Worker {
public:
								MainWorker(JobQueue& queue);

protected:
	/** @brief Returns the longer idle timeout used by the main worker. */
	virtual	bigtime_t			Timeout() const;
	/** @brief Returns the main worker's debug name. */
	virtual	const char*			Name() const;
	/** @brief Runs a single job, spawning additional workers as needed. */
	virtual	status_t			Run(BJob* job);

private:
			int32				fMaxWorkerCount;  /**< Upper bound on the worker pool size. */
};


#endif // WORKER_H
