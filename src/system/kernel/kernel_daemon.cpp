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
 *   Copyright 2003-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file kernel_daemon.cpp
 *  @brief Periodic background workers running in shared kernel daemon threads.
 *
 * Subsystems register a hook function and a frequency (in 100 ms ticks);
 * the kernel daemon thread invokes the hook every @c frequency ticks. Two
 * shared daemon instances exist: a general low-priority worker and a
 * dedicated resource-resizer used by the slab and VM layers. */


#include <kernel_daemon.h>

#include <new>
#include <stdlib.h>

#include <KernelExport.h>

#include <elf.h>
#include <lock.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>


struct daemon : DoublyLinkedListLinkImpl<struct daemon> {
	daemon_hook	function;
	void*		arg;
	int32		frequency;
	bigtime_t	last;
	bool		executing;
};


typedef DoublyLinkedList<struct daemon> DaemonList;


class KernelDaemon {
public:
			status_t			Init(const char* name);

			status_t			Register(daemon_hook function, void* arg,
									int frequency);
			status_t			Unregister(daemon_hook function, void* arg);

			void				Dump();

private:
	static	status_t			_DaemonThreadEntry(void* data);
			struct daemon*		_NextDaemon(struct daemon& marker);
			status_t			_DaemonThread();
			bool				_IsDaemon() const;

private:
			recursive_lock		fLock;
			DaemonList			fDaemons;
			sem_id				fDaemonAddedSem;
			thread_id			fThread;
			ConditionVariable	fUnregisterCondition;
			int32				fUnregisterWaiters;
};


static KernelDaemon sKernelDaemon;
static KernelDaemon sResourceResizer;


/**
 * @brief Start the daemon's worker thread and its wake-up semaphore.
 * @param name Name used for the lock, semaphore, and worker thread.
 * @return B_OK on success, or the semaphore/thread error code on failure.
 */
status_t
KernelDaemon::Init(const char* name)
{
	recursive_lock_init(&fLock, name);

	fDaemonAddedSem = create_sem(0, "kernel daemon added");
	if (fDaemonAddedSem < 0)
		return fDaemonAddedSem;

	fThread = spawn_kernel_thread(&_DaemonThreadEntry, name, B_LOW_PRIORITY,
		this);
	if (fThread < 0)
		return fThread;

	resume_thread(fThread);
	fUnregisterCondition.Init(this, name);

	return B_OK;
}


/**
 * @brief Register a hook to run every @a frequency ticks.
 *
 * One tick equals 100 ms. Wakes the daemon so the new entry is picked up
 * immediately.
 *
 * @param function  Hook to invoke.
 * @param arg       Opaque argument passed to @a function.
 * @param frequency Number of 100 ms ticks between invocations; must be >= 1.
 * @return B_OK on success, B_BAD_VALUE for bad inputs, B_NO_MEMORY on
 *         allocation failure.
 */
status_t
KernelDaemon::Register(daemon_hook function, void* arg, int frequency)
{
	if (function == NULL || frequency < 1)
		return B_BAD_VALUE;

	struct ::daemon* daemon = new(std::nothrow) (struct ::daemon);
	if (daemon == NULL)
		return B_NO_MEMORY;

	daemon->function = function;
	daemon->arg = arg;
	daemon->frequency = frequency;
	daemon->last = 0;
	daemon->executing = false;

	RecursiveLocker locker(fLock);
	fDaemons.Add(daemon);
	locker.Unlock();

	release_sem(fDaemonAddedSem);
	return B_OK;
}


/**
 * @brief Remove a previously registered (function, arg) hook pair.
 *
 * When called from a non-daemon thread, waits for any in-progress execution
 * of the hook to finish before returning.
 *
 * @param function Hook previously passed to Register().
 * @param arg      Argument previously passed to Register().
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match exists.
 */
status_t
KernelDaemon::Unregister(daemon_hook function, void* arg)
{
	RecursiveLocker locker(fLock);

	DaemonList::Iterator iterator = fDaemons.GetIterator();

	// search for the daemon and remove it from the list
	while (iterator.HasNext()) {
		struct daemon* daemon = iterator.Next();

		if (daemon->function == function && daemon->arg == arg) {
			// found it!
			if (!_IsDaemon()) {
				// wait if it's busy
				while (daemon->executing) {
					fUnregisterWaiters++;
					fUnregisterCondition.Wait(locker.Get());
				}
			}

			iterator.Remove();
			delete daemon;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Print each registered daemon with its resolved symbol to kprintf.
 */
void
KernelDaemon::Dump()
{
	DaemonList::Iterator iterator = fDaemons.GetIterator();

	while (iterator.HasNext()) {
		struct daemon* daemon = iterator.Next();
		const char* imageName;
		const char* symbol;
		bool exactMatch;

		status_t status = elf_debug_lookup_symbol_address(
			(addr_t)daemon->function, NULL, &symbol, &imageName, &exactMatch);
		if (status == B_OK && exactMatch) {
			if (strchr(imageName, '/') != NULL)
				imageName = strrchr(imageName, '/') + 1;

			kprintf("\t%s:%s (%p)", imageName, symbol, daemon->function);
		} else
			kprintf("\t%p", daemon->function);

		kprintf(", arg %p%s\n", daemon->arg,
			daemon->executing ? " (running) " : "");
	}
}


/**
 * @brief Trampoline from spawn_kernel_thread() into the per-instance loop.
 * @param data KernelDaemon pointer disguised as a void*.
 * @return Status returned by _DaemonThread().
 */
/*static*/ status_t
KernelDaemon::_DaemonThreadEntry(void* data)
{
	return ((KernelDaemon*)data)->_DaemonThread();
}


/**
 * @brief Advance @a marker through the daemon list and return the next entry.
 *
 * The marker is re-inserted after the returned daemon so iteration can
 * continue after dropping and reacquiring the list lock.
 *
 * @param marker Marker node maintained across iterations.
 * @return Next daemon to process, or NULL at end of list.
 */
struct daemon*
KernelDaemon::_NextDaemon(struct daemon& marker)
{
	struct daemon* daemon;

	if (marker.arg == NULL) {
		// The marker is not part of the list yet, just return the first entry
		daemon = fDaemons.Head();
	} else {
		daemon = fDaemons.GetNext(&marker);
		fDaemons.Remove(&marker);
	}

	marker.arg = daemon;

	if (daemon != NULL)
		fDaemons.InsertAfter(daemon, &marker);

	return daemon;
}


/**
 * @brief Main loop: invoke due hooks and sleep until the next deadline.
 *
 * Walks the daemon list each wake-up, calls every hook whose next-due time
 * has passed, then waits on the add semaphore with a timeout equal to the
 * nearest remaining deadline. Notifies unregister waiters when a pass ends.
 *
 * @return B_OK (the function never actually returns).
 */
status_t
KernelDaemon::_DaemonThread()
{
	struct daemon marker;
	const bigtime_t start = system_time(), iterationToUsecs = 100 * 1000;

	marker.arg = NULL;

	while (true) {
		RecursiveLocker locker(fLock);

		bigtime_t timeout = INT64_MAX;

		// iterate through the list and execute each daemon if needed
		while (struct daemon* daemon = _NextDaemon(marker)) {
			daemon->executing = true;
			locker.Unlock();

			const bigtime_t time = system_time();
			bigtime_t next = (daemon->last +
				(daemon->frequency * iterationToUsecs)) - time;
			if (next <= 0) {
				daemon->last = time;
				next = daemon->frequency * iterationToUsecs;
				daemon->function(daemon->arg, (time - start) / iterationToUsecs);
			}
			timeout = min_c(timeout, next);

			locker.Lock();
			daemon->executing = false;
		}

		if (fUnregisterWaiters != 0) {
			fUnregisterCondition.NotifyAll();
			fUnregisterWaiters = 0;
		}

		locker.Unlock();

		acquire_sem_etc(fDaemonAddedSem, 1, B_RELATIVE_TIMEOUT, timeout);
	}

	return B_OK;
}


/**
 * @brief Test whether the caller is this daemon's own worker thread.
 * @return true if the calling thread is the registered worker.
 */
bool
KernelDaemon::_IsDaemon() const
{
	return find_thread(NULL) == fThread;
}


// #pragma mark -


/**
 * @brief Debugger command: dump general daemons and resource resizers.
 * @param argc Unused argument count.
 * @param argv Unused argument vector.
 * @return 0 on success.
 */
static int
dump_daemons(int argc, char** argv)
{
	kprintf("kernel daemons:\n");
	sKernelDaemon.Dump();

	kprintf("\nresource resizers:\n");
	sResourceResizer.Dump();

	return 0;
}


//	#pragma mark -


/**
 * @brief Public API: register a hook on the general kernel daemon.
 * @param function  Hook to invoke periodically.
 * @param arg       Opaque argument passed to @a function.
 * @param frequency Number of 100 ms ticks between calls.
 * @return B_OK on success, or KernelDaemon::Register() error code.
 */
extern "C" status_t
register_kernel_daemon(daemon_hook function, void* arg, int frequency)
{
	return sKernelDaemon.Register(function, arg, frequency);
}


/**
 * @brief Public API: unregister a previously registered kernel daemon hook.
 * @param function Hook previously passed to register_kernel_daemon().
 * @param arg      Argument previously passed to register_kernel_daemon().
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match exists.
 */
extern "C" status_t
unregister_kernel_daemon(daemon_hook function, void* arg)
{
	return sKernelDaemon.Unregister(function, arg);
}


/**
 * @brief Public API: register a hook on the resource-resizer daemon.
 * @param function  Hook to invoke periodically.
 * @param arg       Opaque argument passed to @a function.
 * @param frequency Number of 100 ms ticks between calls.
 * @return B_OK on success, or KernelDaemon::Register() error code.
 */
extern "C" status_t
register_resource_resizer(daemon_hook function, void* arg, int frequency)
{
	return sResourceResizer.Register(function, arg, frequency);
}


/**
 * @brief Public API: unregister a resource-resizer hook.
 * @param function Hook previously passed to register_resource_resizer().
 * @param arg      Argument previously passed to register_resource_resizer().
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no match exists.
 */
extern "C" status_t
unregister_resource_resizer(daemon_hook function, void* arg)
{
	return sResourceResizer.Unregister(function, arg);
}


//	#pragma mark -


/**
 * @brief Construct the two shared daemons and install the debugger command.
 *
 * Panics if either worker thread fails to spawn.
 *
 * @return B_OK on success.
 */
extern "C" status_t
kernel_daemon_init(void)
{
	new(&sKernelDaemon) KernelDaemon;
	if (sKernelDaemon.Init("kernel daemon") != B_OK)
		panic("kernel_daemon_init(): failed to init kernel daemon");

	new(&sResourceResizer) KernelDaemon;
	if (sResourceResizer.Init("resource resizer") != B_OK)
		panic("kernel_daemon_init(): failed to init resource resizer");

	add_debugger_command("daemons", dump_daemons,
		"Shows registered kernel daemons.");
	return B_OK;
}
