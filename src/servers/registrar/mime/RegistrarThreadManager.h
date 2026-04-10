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

/** @file RegistrarThreadManager.h
 *  @brief Manages the lifecycle of registrar-spawned threads with a configurable limit. */

#ifndef REGISTRAR_THREAD_MANAGER_H
#define REGISTRAR_THREAD_MANAGER_H

#include <Handler.h>
#include <SupportDefs.h>

#include <list>

class RegistrarThread;

/** @brief BHandler that launches, tracks, cleans up, and shuts down RegistrarThread instances. */
class RegistrarThreadManager : public BHandler {
public:
	RegistrarThreadManager();
	~RegistrarThreadManager();

	// BHandler virtuals
	/** @brief Handles thread completion notification messages. */
	virtual	void MessageReceived(BMessage* message);

	// Thread management functions
	/** @brief Spawns and begins executing a new RegistrarThread. */
	status_t LaunchThread(RegistrarThread *thread);
	/** @brief Removes finished threads from the managed set. */
	status_t CleanupThreads();
	/** @brief Asks all managed threads to exit gracefully. */
	status_t ShutdownThreads();
	/** @brief Forcefully terminates all managed threads. */
	status_t KillThreads();

	/** @brief Returns the number of currently managed threads. */
	uint ThreadCount() const;

	static const int kThreadLimit = 12;
private:

	std::list<RegistrarThread*>::iterator&
		RemoveThread(std::list<RegistrarThread*>::iterator &i);

	std::list<RegistrarThread*> fThreads;
	int32 fThreadCount;
};

#endif	// THREAD_MANAGER_H
