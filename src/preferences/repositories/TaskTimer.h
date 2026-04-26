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

/** @file TaskTimer.h
    @brief Per-task watchdog timer with cancel-on-timeout user prompt. */

#ifndef TASKTIMER_H
#define TASKTIMER_H


#include <Alert.h>
#include <Invoker.h>
#include <Looper.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <String.h>

#include "RepoRow.h"

class TaskTimer;
class TaskLooper;


/**
 * @brief Plain-old-data record describing one in-flight repository task.
 *
 * Holds the originating row, requested task type, parameter (URL or name),
 * worker thread id, owning TaskLooper, paired TaskTimer, and the result
 * fields filled in by the worker thread before reporting completion.
 */
typedef struct {
		RepoRow*		rowItem;
		int32			taskType;
		BString			name, taskParam;
		thread_id		threadId;
		TaskLooper*		owner;
		BString			resultName, resultErrorDetails;
		TaskTimer*		fTimer;
} Task;


/**
 * @brief Long-running-task watchdog paired with a single Task.
 *
 * Posts a TASK_TIMEOUT message to itself after the configured timeout and,
 * when the timer is still running, prompts the user to either keep waiting
 * or cancel the underlying job.
 */
class TaskTimer : public BLooper {
public:
							TaskTimer(const BMessenger& target, Task* owner);
							~TaskTimer();
	virtual bool			QuitRequested();
	virtual void			MessageReceived(BMessage*);
	void					Start(const char* name);
	void					Stop(const char* name);

private:
	int32					_NextAlertStackCount();

	int32					fTimeoutMicroSeconds;
	bool					fTimerIsRunning;
	BString					fRepositoryName;
	BMessenger				fReplyTarget;
	BMessenger				fMessenger;
	BMessageRunner*			fMessageRunner;
	BMessage				fTimeoutMessage;
	BAlert*					fTimeoutAlert;
	BInvoker				fTimeoutAlertInvoker;
	Task*					fOwner;
};


#endif
