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
   Copyright 2001-2005, Haiku.
   Distributed under the terms of the MIT License.

   Authors:
   Tyler Akidau
 */

/** @file RegistrarThread.cpp
 *  @brief Implements the base registrar thread with spawn, run, and exit support. */

#include "RegistrarThread.h"
#include <string.h>


/**
 * @brief Creates a new RegistrarThread, preparing it for execution.
 *
 * The thread is not started until Run() is called. The constructor validates
 * the name and manager messenger and sets the initialization status accordingly.
 *
 * @param name              The desired name of the new thread.
 * @param priority          The desired scheduling priority of the new thread.
 * @param managerMessenger  A BMessenger to the thread manager that owns this thread.
 */
RegistrarThread::RegistrarThread(const char *name, int32 priority,
	BMessenger managerMessenger)
	:
	fManagerMessenger(managerMessenger),
	fShouldExit(false),
	fIsFinished(false),
	fStatus(B_NO_INIT),
	fId(-1),
	fPriority(priority)
{
	fName[0] = 0;
	status_t err = name && fManagerMessenger.IsValid() ? B_OK : B_BAD_VALUE;
	if (err == B_OK)
		strlcpy(fName, name, sizeof(fName));
	fStatus = err;
}

/** @brief Destroys the RegistrarThread object. */
RegistrarThread::~RegistrarThread()
{
}

/**
 * @brief Returns the initialization status of the object.
 *
 * @return @c B_OK if the thread was initialized successfully, or an error code.
 */
status_t
RegistrarThread::InitCheck()
{
	return fStatus;
}

/**
 * @brief Spawns and resumes the thread, beginning execution of ThreadFunction().
 *
 * @return @c B_OK on success, or an error code if spawning or resuming fails.
 */
status_t
RegistrarThread::Run()
{
	status_t err = InitCheck();
	if (err == B_OK) {
		fId = spawn_thread(&RegistrarThread::EntryFunction, fName,
			fPriority, (void*)this);
		err = fId >= 0 ? B_OK : fId;
		if (err == B_OK)
			err = resume_thread(fId);
	}
	return err;
}

/**
 * @brief Returns the thread ID.
 *
 * @return The kernel thread ID, or -1 if the thread has not been spawned.
 */
thread_id
RegistrarThread::Id() const
{
	return fId;
}

/**
 * @brief Returns the name of the thread.
 *
 * @return The null-terminated thread name string.
 */
const char*
RegistrarThread::Name() const
{
	return fName;
}

/**
 * @brief Signals the thread to exit gracefully as soon as possible.
 *
 * Sets the fShouldExit flag, which the thread's main loop should check
 * periodically.
 */
void
RegistrarThread::AskToExit()
{
	fShouldExit = true;
}

/**
 * @brief Returns whether the thread has finished executing.
 *
 * @return @c true if the thread has completed, @c false if it is still running.
 */
bool
RegistrarThread::IsFinished() const
{
	return fIsFinished;
}

/**
 * @brief Static entry point passed to spawn_thread().
 *
 * Casts the @a data pointer to a RegistrarThread and invokes its
 * ThreadFunction().
 *
 * @param data Pointer to the RegistrarThread instance.
 * @return The return value of ThreadFunction().
 */
int32
RegistrarThread::EntryFunction(void *data)
{
	return ((RegistrarThread*)data)->ThreadFunction();
}
