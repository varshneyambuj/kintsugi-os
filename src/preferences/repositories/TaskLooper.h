/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2017, Haiku Inc.
 * Original authors: Brian Hill.
 */

/** @file TaskLooper.h
    @brief Asynchronous task runner driving pkgman repository operations. */

#ifndef TASK_LOOPER_H
#define TASK_LOOPER_H


#include <Job.h>
#include <Looper.h>
#include <ObjectList.h>
#include <String.h>
#include <StringList.h>
#include <package/Context.h>

#include "TaskTimer.h"


/**
 * @brief Auto-confirming decision provider used during pkgman requests.
 *
 * Always returns true so non-interactive enable/disable flows proceed
 * without prompting the user mid-task; user confirmation, when needed, is
 * handled at the UI layer instead.
 */
class DecisionProvider : public BPackageKit::BDecisionProvider {
public:
								DecisionProvider() {}

	virtual	bool				YesNoDecisionNeeded(const BString& description,
									const BString& question,
									const BString& yes,
									const BString& no,
									const BString& defaultChoice)
									{ return true; }
};


/**
 * @brief Captures pkgman job lifecycle events into a string log buffer.
 *
 * The accumulated log is later folded into error alerts so the user sees
 * which job failed and why.
 */
class JobStateListener : public BSupportKit::BJobStateListener {
public:
								JobStateListener() {}

	virtual	void				JobStarted(BSupportKit::BJob* job);
	virtual	void				JobSucceeded(BSupportKit::BJob* job);
	virtual	void				JobFailed(BSupportKit::BJob* job);
	virtual	void				JobAborted(BSupportKit::BJob* job);
	BString						GetJobLog();

private:
			BStringList			fJobLog;
};


/**
 * @brief BLooper that owns the queue of pending repository tasks.
 *
 * Each DO_TASK message it receives spawns a worker thread driving the
 * pkgman request and a TaskTimer to surface "taking too long" prompts.
 * Reply messages are sent to the messenger supplied at construction.
 */
class TaskLooper : public BLooper {
public:
							TaskLooper(const BMessenger& target);
	virtual	bool			QuitRequested();
	virtual void			MessageReceived(BMessage*);

private:
	void					_RemoveAndDelete(Task* task);
	static status_t			_DoTask(void* data);
	static void				_AppendErrorDetails(BString& details,
								JobStateListener* listener);

	BObjectList<Task>		fTaskQueue;
	BMessenger				fReplyTarget;
	BMessenger				fMessenger;
};


#endif
