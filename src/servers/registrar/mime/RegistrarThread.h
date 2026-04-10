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
   This software is part of the Haiku distribution and is covered
   by the MIT License.
 */

/** @file RegistrarThread.h
 *  @brief Base class for threads spawned and managed by the registrar's thread manager. */

#ifndef REGISTRAR_THREAD_H
#define REGISTRAR_THREAD_H

#include <Messenger.h>
#include <OS.h>

/** @brief Abstract base for named, prioritized threads that report completion to a manager. */
class RegistrarThread {
public:
	RegistrarThread(const char *name, int32 priority,
		BMessenger managerMessenger);
	virtual ~RegistrarThread();

	/** @brief Returns the initialization status of the thread. */
	virtual status_t InitCheck();
	/** @brief Spawns and resumes the thread. */
	status_t Run();

	/** @brief Returns the thread_id of this thread. */
	thread_id Id() const;
	const char* Name() const;

	/** @brief Requests the thread to terminate at the next safe point. */
	void AskToExit();
	/** @brief Returns whether the thread has completed execution. */
	bool IsFinished() const;

protected:
	//! The function executed in the object's thread when Run() is called
	virtual status_t ThreadFunction() = 0;

	BMessenger fManagerMessenger;	/**< Messenger used to notify the thread manager of completion. */
	bool fShouldExit;	// Initially false, may be set to true by AskToExit()	/**< Flag indicating the thread has been asked to terminate. */
	bool fIsFinished;	// Initially false, set to true by the thread itself	/**< Flag set by the thread upon completion. */
						// upon completion
private:
	static int32 EntryFunction(void *data);

	status_t fStatus;
	thread_id fId;
	char fName[B_OS_NAME_LENGTH];
	int32 fPriority;
};

#endif	// REGISTRAR_THREAD_H
