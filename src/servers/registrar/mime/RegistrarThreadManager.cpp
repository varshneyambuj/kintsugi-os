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
   Copyright 2001-2007, Haiku.
   Distributed under the terms of the MIT License.

   Authors:
   Tyler Dauwalder
 */

/** @file RegistrarThreadManager.cpp
 *  @brief Implements lifecycle management for registrar-spawned threads. */
#include "RegistrarThreadManager.h"

#include <RegistrarDefs.h>

#include <stdio.h>

#include "RegistrarThread.h"


//#define DBG(x) x
#define DBG(x)
#define OUT printf

using namespace BPrivate;

/** @brief Constructs a RegistrarThreadManager with no managed threads. */
RegistrarThreadManager::RegistrarThreadManager()
	: fThreadCount(0)
{
}

/**
 * @brief Destroys the manager, killing and deleting any remaining threads.
 */
RegistrarThreadManager::~RegistrarThreadManager()
{
	KillThreads();
}

/**
 * @brief Handles B_REG_MIME_UPDATE_THREAD_FINISHED messages to clean up threads.
 *
 * All other messages are forwarded to the base BHandler.
 *
 * @param message The incoming message.
 */
void
RegistrarThreadManager::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_REG_MIME_UPDATE_THREAD_FINISHED:
		{
			CleanupThreads();
			break;
		}

		default:
		{
			BHandler::MessageReceived(message);
			break;
		}
	}
}

/**
 * @brief Launches a thread and takes ownership on success.
 *
 * If the thread limit has been reached, @c B_NO_MORE_THREADS is returned
 * and the caller retains ownership.
 *
 * @param thread The RegistrarThread to launch. Ownership transfers on success.
 * @return @c B_OK on success, @c B_BAD_VALUE if @a thread is NULL,
 *         @c B_NO_MORE_THREADS if the limit is reached, or another error code.
 */
status_t
RegistrarThreadManager::LaunchThread(RegistrarThread *thread)
{
	status_t err = thread ? B_OK : B_BAD_VALUE;
	if (!err) {
		if (atomic_add(&fThreadCount, 1) >= kThreadLimit) {
			err = B_NO_MORE_THREADS;
			atomic_add(&fThreadCount, -1);
		}
	}

	if (!err) {
		fThreads.push_back(thread);
		err = thread->Run();
		if (err) {
			std::list<RegistrarThread*>::iterator i;
			for (i = fThreads.begin(); i != fThreads.end();) {
				if ((*i) == thread) {
					i = fThreads.erase(i);
					atomic_add(&fThreadCount, -1);
					break;
				} else
					++i;
			}
		}
	}
	if (!err)
		DBG(OUT("RegistrarThreadManager::LaunchThread(): launched new '%s'"
			" thread, id %ld\n", thread->Name(), thread->Id()));
	return err;
}

/**
 * @brief Frees the resources of any threads that have finished executing.
 *
 * @return @c B_OK always.
 */
status_t
RegistrarThreadManager::CleanupThreads()
{
	std::list<RegistrarThread*>::iterator i;
	for (i = fThreads.begin(); i != fThreads.end(); ) {
		if (*i) {
			if ((*i)->IsFinished()) {
				DBG(OUT("RegistrarThreadManager::CleanupThreads(): Cleaning up"
					" thread %ld\n", (*i)->Id()));
				RemoveThread(i);
					// adjusts i
			} else
				++i;
		} else {
			OUT("WARNING: RegistrarThreadManager::CleanupThreads(): NULL"
				" mime_update_thread_shared_data pointer found in and removed"
				" from RegistrarThreadManager::fThreads list\n");
			i = fThreads.erase(i);
		}
	}
	return B_OK;
}

/**
 * @brief Asks all running threads to exit and cleans up finished ones.
 *
 * Running threads are politely asked to exit via AskToExit(); already
 * finished threads are removed and deleted.
 *
 * @return @c B_OK always.
 */
status_t
RegistrarThreadManager::ShutdownThreads()
{
	std::list<RegistrarThread*>::iterator i;
	for (i = fThreads.begin(); i != fThreads.end(); ) {
		if (*i) {
			if ((*i)->IsFinished()) {
				DBG(OUT("RegistrarThreadManager::ShutdownThreads(): Cleaning up"
					" thread %ld\n", (*i)->Id()));
				RemoveThread(i);
					// adjusts i
			} else {
				DBG(OUT("RegistrarThreadManager::ShutdownThreads(): Shutting"
					" down thread %ld\n", (*i)->Id()));
				(*i)->AskToExit();
				++i;
			}
		} else {
			OUT("WARNING: RegistrarThreadManager::ShutdownThreads(): NULL"
				" mime_update_thread_shared_data pointer found in and removed"
				" from RegistrarThreadManager::fThreads list\n");
			i = fThreads.erase(i);
		}
	}

	/*! \todo We may want to iterate back through the list at this point,
		snooze for a moment if find an unfinished thread, and kill it if
		it still isn't finished by the time we're done snoozing.
	*/

	return B_OK;
}

/**
 * @brief Forcibly kills all running threads and frees their resources.
 *
 * @return @c B_OK always.
 */
status_t
RegistrarThreadManager::KillThreads()
{
	std::list<RegistrarThread*>::iterator i;
	for (i = fThreads.begin(); i != fThreads.end(); ) {
		if (*i) {
			if (!(*i)->IsFinished()) {
				DBG(OUT("RegistrarThreadManager::KillThreads(): Killing thread"
					" %ld\n", (*i)->Id()));
				status_t err = kill_thread((*i)->Id());
				if (err)
					OUT("WARNING: RegistrarThreadManager::KillThreads():"
						" kill_thread(%" B_PRId32 ") failed with error code"
						" 0x%" B_PRIx32 "\n", (*i)->Id(), err);
			}
			DBG(OUT("RegistrarThreadManager::KillThreads(): Cleaning up thread"
				" %ld\n", (*i)->Id()));
			RemoveThread(i);
				// adjusts i
		} else {
			OUT("WARNING: RegistrarThreadManager::KillThreads(): NULL"
				" mime_update_thread_shared_data pointer found in and removed"
				" from RegistrarThreadManager::fThreads list\n");
			i = fThreads.erase(i);
		}
	}
	return B_OK;
}

/**
 * @brief Returns the number of threads currently under management.
 *
 * This includes threads that have finished but not yet been cleaned up.
 *
 * @return The managed thread count.
 */
uint
RegistrarThreadManager::ThreadCount() const
{
	return fThreadCount;
}

/**
 * @brief Waits for the thread to exit, deletes it, and removes it from the list.
 *
 * @param i Iterator pointing to the thread to remove; updated to the next
 *          element after removal.
 * @return A reference to the updated iterator.
 */
std::list<RegistrarThread*>::iterator&
RegistrarThreadManager::RemoveThread(std::list<RegistrarThread*>::iterator &i)
{
	status_t dummy;
	wait_for_thread((*i)->Id(), &dummy);

	delete *i;
	atomic_add(&fThreadCount, -1);
	return (i = fThreads.erase(i));
}

