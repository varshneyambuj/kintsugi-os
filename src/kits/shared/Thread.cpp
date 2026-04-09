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
 *   Open Tracker License
 *
 *   Terms and Conditions
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the "Software"),
 *   to deal in the Software without restriction, including without limitation
 *   the rights to use, copy, modify, merge, publish, distribute, sublicense,
 *   and/or sell copies of the Software, and to permit persons to whom the
 *   Software is furnished to do so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all
 *   licensees and shall be included in all copies or substantial portions of
 *   the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE,
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 *   IN NO EVENT SHALL BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR
 *   OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 *   ARISING FROM, OUT OF, OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 *   OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Distributed under the terms of the MIT License.
 */

/** @file Thread.cpp
 *  @brief Lightweight thread wrappers: SimpleThread, Thread, and
 *         ThreadSequence.
 *
 *  SimpleThread provides a base class for spawning a single kernel thread.
 *  Thread executes a single FunctionObject functor in its own thread and
 *  self-destructs on completion. ThreadSequence runs a list of functors
 *  sequentially in a single dedicated thread.
 */

#include "Thread.h"
#include "FunctionObject.h"


/** @brief Constructs a SimpleThread with the given scheduling priority and
 *         optional name.
 *
 *  The thread is not started until Go() is called.
 *
 *  @param priority  The scheduling priority for the new thread (e.g.
 *                   B_NORMAL_PRIORITY).
 *  @param name      Optional human-readable name for the thread. May be
 *                   NULL.
 */
SimpleThread::SimpleThread(int32 priority, const char* name)
	:	fScanThread(-1),
		fPriority(priority),
		fName(name)
{
}


/** @brief Destructor. If the thread is still running and is not the current
 *         thread, kills it.
 */
SimpleThread::~SimpleThread()
{
	if (fScanThread > 0 && fScanThread != find_thread(NULL)) {
		// kill the thread if it is not the one we are running in
		kill_thread(fScanThread);
	}
}


/** @brief Spawns and resumes the thread.
 *
 *  Calls spawn_thread() with the RunBinder trampoline and then
 *  resume_thread() to start execution.
 */
void
SimpleThread::Go()
{
	fScanThread = spawn_thread(SimpleThread::RunBinder,
		fName ? fName : "TrackerTaskLoop", fPriority, this);
	resume_thread(fScanThread);
}


/** @brief Static trampoline that bridges the C-style thread entry point to
 *         the virtual Run() method.
 *
 *  @param castToThis  The SimpleThread instance passed as the thread cookie.
 *  @return B_OK after Run() returns.
 */
status_t
SimpleThread::RunBinder(void* castToThis)
{
	SimpleThread* self = static_cast<SimpleThread*>(castToThis);
	self->Run();
	return B_OK;
}


/** @brief Convenience factory that allocates and immediately starts a Thread.
 *
 *  Ownership of @p functor is transferred to the new Thread object, which
 *  deletes itself (and the functor) when its Run() method completes.
 *
 *  @param functor   The FunctionObject to invoke in the new thread.
 *  @param priority  Scheduling priority for the new thread.
 *  @param name      Optional name for the thread. May be NULL.
 */
void
Thread::Launch(FunctionObject* functor, int32 priority, const char* name)
{
	new Thread(functor, priority, name);
}


/** @brief Constructs a Thread with the given functor and immediately starts
 *         it by calling Go().
 *
 *  @param functor   The FunctionObject to invoke. Ownership is assumed.
 *  @param priority  Scheduling priority for the thread.
 *  @param name      Optional name for the thread. May be NULL.
 */
Thread::Thread(FunctionObject* functor, int32 priority, const char* name)
	:	SimpleThread(priority, name),
		fFunctor(functor)
{
	Go();
}


/** @brief Destructor. Deletes the owned FunctionObject. */
Thread::~Thread()
{
	delete fFunctor;
}


/** @brief Invokes the functor then deletes this Thread (self-destructs).
 *
 *  Called by the thread trampoline. After invoking the functor, this
 *  method performs "delete this" so that Thread objects are fully
 *  self-managing.
 */
void
Thread::Run()
{
	(*fFunctor)();
	delete this;
		// commit suicide
}


/** @brief Convenience factory that runs a list of functors, either
 *         synchronously or in a new thread.
 *
 *  If @p async is false the list is executed in the calling thread without
 *  creating a new one. If @p async is true, a ThreadSequence is allocated
 *  and started; it self-destructs when done.
 *
 *  @param list      The list of FunctionObjects to execute in order.
 *                   Ownership is transferred to the ThreadSequence when
 *                   async is true.
 *  @param async     If true, run the sequence in a new thread.
 *  @param priority  Scheduling priority for the new thread (ignored when
 *                   async is false).
 */
void
ThreadSequence::Launch(BObjectList<FunctionObject, true>* list, bool async,
	int32 priority)
{
	if (!async) {
		// if not async, don't even create a thread, just do it right away
		Run(list);
	} else
		new ThreadSequence(list, priority);
}


/** @brief Constructs a ThreadSequence with the given functor list and
 *         immediately starts the thread by calling Go().
 *
 *  @param list      The list of FunctionObjects to execute sequentially.
 *                   Ownership is assumed.
 *  @param priority  Scheduling priority for the thread.
 */
ThreadSequence::ThreadSequence(BObjectList<FunctionObject, true>* list,
	int32 priority)
	:	SimpleThread(priority),
		fFunctorList(list)
{
	Go();
}


/** @brief Destructor. Deletes the owned functor list. */
ThreadSequence::~ThreadSequence()
{
	delete fFunctorList;
}


/** @brief Executes each functor in @p list sequentially in the calling
 *         context.
 *
 *  Used for both the synchronous Launch() path and by the thread entry
 *  point Run().
 *
 *  @param list  The list of FunctionObjects to invoke in order.
 */
void
ThreadSequence::Run(BObjectList<FunctionObject, true>* list)
{
	int32 count = list->CountItems();
	for (int32 index = 0; index < count; index++)
		(*list->ItemAt(index))();
}


/** @brief Thread entry point. Runs all functors then self-destructs.
 *
 *  Called by the thread trampoline. Invokes the overload that takes an
 *  explicit list pointer, then performs "delete this".
 */
void
ThreadSequence::Run()
{
	Run(fFunctorList);
	delete this;
		// commit suicide
}
