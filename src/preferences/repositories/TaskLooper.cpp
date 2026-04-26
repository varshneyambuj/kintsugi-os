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
 *   Copyright 2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Brian Hill
 */


/**
 * @file TaskLooper.cpp
 * @brief Asynchronous task runner for repository enable/disable jobs.
 *
 * The TaskLooper accepts DO_TASK requests from the preflet UI, spawns a
 * worker thread that drives the appropriate BPackageKit request
 * (AddRepository or DropRepository, plus a refresh on enable), and posts
 * lifecycle messages back to the UI when each task starts, completes,
 * fails with errors, or is killed by the timer-driven cancel path. The
 * embedded JobStateListener captures pkgman job log lines so error alerts
 * can include detailed context.
 */


#include "TaskLooper.h"

#include <Catalog.h>
#include <MessageQueue.h>
#include <package/AddRepositoryRequest.h>
#include <package/DropRepositoryRequest.h>
#include <package/RefreshRepositoryRequest.h>
#include <package/PackageRoster.h>
#include <package/RepositoryConfig.h>

#include "constants.h"

#define DEBUGTASK 0

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TaskLooper"

/** @brief Prefix written before result lines in the captured job log. */
static const BString kLogResultIndicator = "***";
/** @brief Localized status word appended on JobSucceeded(). */
static const BString kCompletedText =
	B_TRANSLATE_COMMENT("Completed", "Completed task status message");
/** @brief Localized status word appended on JobFailed(). */
static const BString kFailedText =
	B_TRANSLATE_COMMENT("Failed", "Failed task status message");
/** @brief Localized status word appended on JobAborted(). */
static const BString kAbortedText =
	B_TRANSLATE_COMMENT("Aborted", "Aborted task status message");
/** @brief Header used before the human-readable error string in the log. */
static const BString kDescriptionText =
	B_TRANSLATE_COMMENT("Description", "Failed task error description");
/** @brief Header prefix used when appending the job log to alert details. */
static const BString kDetailsText =
	B_TRANSLATE_COMMENT("Details", "Job log details header");

using BSupportKit::BJob;


/**
 * @brief Records the title of a started job in the listener's log buffer.
 *
 * @param job Job that has just started.
 */
void
JobStateListener::JobStarted(BJob* job)
{
	fJobLog.Add(job->Title());
}


/**
 * @brief Appends a success marker for the just-completed job.
 *
 * @param job Job that has succeeded (unused, captured for interface parity).
 */
void
JobStateListener::JobSucceeded(BJob* job)
{
	BString resultText(kLogResultIndicator);
	fJobLog.Add(resultText.Append(kCompletedText));
}


/**
 * @brief Appends a failure marker plus optional description for the job.
 *
 * @param job Job that failed; its result code and ErrorString() are logged.
 */
void
JobStateListener::JobFailed(BJob* job)
{
	BString resultText(kLogResultIndicator);
	resultText.Append(kFailedText).Append(": ")
		.Append(strerror(job->Result()));
	fJobLog.Add(resultText);
	if (job->ErrorString().Length() > 0) {
		resultText.SetTo(kLogResultIndicator);
		resultText.Append(kDescriptionText).Append(": ")
			.Append(job->ErrorString());
		fJobLog.Add(resultText);
	}
}


/**
 * @brief Appends an abort marker plus optional description for the job.
 *
 * @param job Job that was aborted; its result code and ErrorString() are
 *            logged.
 */
void
JobStateListener::JobAborted(BJob* job)
{
	BString resultText(kLogResultIndicator);
	resultText.Append(kAbortedText).Append(": ")
		.Append(strerror(job->Result()));
	fJobLog.Add(resultText);
	if (job->ErrorString().Length() > 0) {
		resultText.SetTo(kLogResultIndicator);
		resultText.Append(kDescriptionText).Append(": ")
			.Append(job->ErrorString());
		fJobLog.Add(resultText);
	}
}


/**
 * @brief Returns every captured log line joined with newlines.
 *
 * @return Multi-line string of captured job events suitable for an alert
 *         body.
 */
BString
JobStateListener::GetJobLog()
{
	return fJobLog.Join("\n");
}


/**
 * @brief Constructs the task looper, starts running it, and caches a
 *        messenger pointing back at itself.
 *
 * @param target Messenger that will receive task lifecycle messages.
 */
TaskLooper::TaskLooper(const BMessenger& target)
	:
	BLooper("TaskLooper"),
	fReplyTarget(target)
{
	Run();
	fMessenger.SetTo(this);
}


/**
 * @brief Refuses to quit while there are still queued tasks pending.
 *
 * @return True only when the message queue is empty so the caller will
 *         retry once outstanding work is consumed.
 */
bool
TaskLooper::QuitRequested()
{
	return MessageQueue()->IsEmpty();
}


/**
 * @brief Processes DO_TASK, completion, and kill-request messages.
 *
 * On DO_TASK a new Task is constructed, a worker thread spawned to run the
 * pkgman request, and a TaskTimer started so the user can cancel a long
 * job. The completion and kill cases relay status to fReplyTarget and
 * destroy the matching Task.
 *
 * @param message Incoming BMessage from either the UI or a worker thread.
 */
void
TaskLooper::MessageReceived(BMessage* message)
{
	switch (message->what)
	{
		case DO_TASK:
		{
			RepoRow* rowItem;
			status_t result = message->FindPointer(key_rowptr, (void**)&rowItem);
			if (result == B_OK) {
				// Check to make sure there isn't already an existing task for this
				int16 queueCount = fTaskQueue.CountItems();
				for (int16 index = 0; index<queueCount; index++) {
					Task* task = fTaskQueue.ItemAt(index);
					if (rowItem == task->rowItem)
						break;
				}

				// Initialize task
				Task* newTask = new Task();
				newTask->rowItem = rowItem;
				newTask->name = rowItem->Name();
				newTask->resultName = newTask->name;
				if (rowItem->IsEnabled()) {
					newTask->taskType = DISABLE_REPO;
					newTask->taskParam = newTask->name;
				} else {
					newTask->taskType = ENABLE_REPO;
					newTask->taskParam = rowItem->Url();
				}
				newTask->owner = this;
				newTask->fTimer = NULL;

				// Add to queue and start
				fTaskQueue.AddItem(newTask);
				BString threadName(newTask->taskType == ENABLE_REPO ?
					"enable_task" : "disable_task");
				newTask->threadId = spawn_thread(_DoTask, threadName.String(),
					B_NORMAL_PRIORITY, (void*)newTask);
				status_t threadResult;
				if (newTask->threadId < B_OK)
					threadResult = B_ERROR;
				else {
					threadResult = resume_thread(newTask->threadId);
					if (threadResult == B_OK) {
						newTask->fTimer = new TaskTimer(fMessenger, newTask);
						newTask->fTimer->Start(newTask->name);
						// Reply to view
						BMessage reply(*message);
						reply.what = TASK_STARTED;
						reply.AddInt16(key_count, fTaskQueue.CountItems());
						fReplyTarget.SendMessage(&reply);
					} else
						kill_thread(newTask->threadId);
				}
				if (threadResult != B_OK) {
					_RemoveAndDelete(newTask);
				}
			}
			break;
		}
		
		case TASK_COMPLETED:
		case TASK_COMPLETED_WITH_ERRORS:
		case TASK_CANCELED:
		{
			Task* task;
			status_t result = message->FindPointer(key_taskptr, (void**)&task);
			if (result == B_OK && fTaskQueue.HasItem(task)) {
				task->fTimer->Stop(task->resultName);
				BMessage reply(message->what);
				reply.AddInt16(key_count, fTaskQueue.CountItems()-1);
				reply.AddPointer(key_rowptr, task->rowItem);
				if (message->what == TASK_COMPLETED_WITH_ERRORS)
					reply.AddString(key_details, task->resultErrorDetails);
				if (task->taskType == ENABLE_REPO
					&& task->name.Compare(task->resultName) != 0) {
					reply.AddString(key_name, task->resultName);
				}
				fReplyTarget.SendMessage(&reply);
				_RemoveAndDelete(task);
			}
			break;
		}
		
		case TASK_KILL_REQUEST:
		{
			Task* task;
			status_t result = message->FindPointer(key_taskptr, (void**)&task);
			if (result == B_OK && fTaskQueue.HasItem(task)) {
				kill_thread(task->threadId);
				BMessage reply(TASK_CANCELED);
				reply.AddInt16(key_count, fTaskQueue.CountItems()-1);
				reply.AddPointer(key_rowptr, task->rowItem);
				fReplyTarget.SendMessage(&reply);
				_RemoveAndDelete(task);
			}
			break;
		}
	}
}


/**
 * @brief Removes a task from the queue, stops its timer, and deletes it.
 *
 * @param task Task to remove. Must currently be in fTaskQueue.
 */
void
TaskLooper::_RemoveAndDelete(Task* task)
{
	fTaskQueue.RemoveItem(task);
	if (task->fTimer) {
		task->fTimer->Lock();
		task->fTimer->Quit();
		task->fTimer = NULL;
	}
	delete task;
}


/**
 * @brief Worker-thread entry point that performs a single repository task.
 *
 * Drives either a DropRepositoryRequest (for disable) or an
 * AddRepositoryRequest followed by a RefreshRepositoryRequest (for enable).
 * Posts TASK_COMPLETED, TASK_COMPLETED_WITH_ERRORS, or TASK_CANCELED back
 * to the owning TaskLooper depending on the result.
 *
 * @param data Pointer to the Task structure (cast from void*).
 * @return Always 0; status is communicated via the reply message.
 */
status_t
TaskLooper::_DoTask(void* data)
{
	Task* task = (Task*)data;
	BString errorDetails, repoName("");
	status_t returnResult = B_OK;
	DecisionProvider decisionProvider;
	JobStateListener listener;
	switch (task->taskType)
	{
		case DISABLE_REPO:
		{
			BString nameParam(task->taskParam);
			BPackageKit::BContext context(decisionProvider, listener);
			BPackageKit::DropRepositoryRequest dropRequest(context, nameParam);
			status_t result = dropRequest.Process();
			if (result != B_OK) {
				returnResult = result;
				if (result != B_CANCELED) {
					errorDetails.Append(B_TRANSLATE_COMMENT("There was an "
						"error disabling the repository %name%",
						"Error message, do not translate %name%"));
					BString nameString("\"");
					nameString.Append(nameParam).Append("\"");
					errorDetails.ReplaceFirst("%name%", nameString);
					_AppendErrorDetails(errorDetails, &listener);
				}
			}
			break;
		}
		
		case ENABLE_REPO:
		{
			BString urlParam(task->taskParam);
			BPackageKit::BContext context(decisionProvider, listener);
			// Add repository
			bool asUserRepository = false;
				// TODO does this ever change?
			BPackageKit::AddRepositoryRequest addRequest(context, urlParam,
				asUserRepository);
			status_t result = addRequest.Process();
			if (result != B_OK) {
				returnResult = result;
				if (result != B_CANCELED) {
					errorDetails.Append(B_TRANSLATE_COMMENT("There was an "
						"error enabling the repository %url%",
						"Error message, do not translate %url%"));
					errorDetails.ReplaceFirst("%url%", urlParam);
					_AppendErrorDetails(errorDetails, &listener);
				}
				break;
			}
			// Continue on to refresh repo cache
			repoName = addRequest.RepositoryName();
			BPackageKit::BPackageRoster roster;
			BPackageKit::BRepositoryConfig repoConfig;
			roster.GetRepositoryConfig(repoName, &repoConfig);
			BPackageKit::BRefreshRepositoryRequest refreshRequest(context,
				repoConfig);
			result = refreshRequest.Process();
			if (result != B_OK) {
				returnResult = result;
				if (result != B_CANCELED) {
					errorDetails.Append(B_TRANSLATE_COMMENT("There was an "
						"error refreshing the repository cache for %name%",
						"Error message, do not translate %name%"));
					BString nameString("\"");
					nameString.Append(repoName).Append("\"");
					errorDetails.ReplaceFirst("%name%", nameString);
					_AppendErrorDetails(errorDetails, &listener);
				}
			}
			break;
		}
	}
	// Report completion status
	BMessage reply;
	if (returnResult == B_OK) {
		reply.what = TASK_COMPLETED;
		// Add the repo name if we need to update the list row value
		if (task->taskType == ENABLE_REPO)
			task->resultName = repoName;
	} else if (returnResult == B_CANCELED)
		reply.what = TASK_CANCELED;
	else {
		reply.what = TASK_COMPLETED_WITH_ERRORS;
		task->resultErrorDetails = errorDetails;
		if (task->taskType == ENABLE_REPO)
			task->resultName = repoName;
	}
	reply.AddPointer(key_taskptr, task);
	task->owner->PostMessage(&reply);
#if DEBUGTASK
	if (returnResult == B_OK || returnResult == B_CANCELED) {
		BString degubDetails("Debug info:\n");
		degubDetails.Append(listener.GetJobLog());
		(new BAlert("debug", degubDetails, "OK"))->Go(NULL);
	}
#endif // DEBUGTASK
	return 0;
}


/**
 * @brief Appends a "Details:" section with the captured job log to @a details.
 *
 * @param details  Error message buffer to extend in place.
 * @param listener Listener whose accumulated job log is appended.
 */
void
TaskLooper::_AppendErrorDetails(BString& details, JobStateListener* listener)
{
	details.Append("\n\n").Append(kDetailsText).Append(":\n");
	details.Append(listener->GetJobLog());
}
