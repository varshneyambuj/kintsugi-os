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
 * @file TaskTimer.cpp
 * @brief Implementation of TaskTimer, the per-repository long-running watchdog.
 *
 * One TaskTimer is paired with each in-flight pkgman task. When a task
 * exceeds the configured timeout the timer pops a stacked alert offering
 * the user a chance to keep waiting or cancel the task; on completion the
 * alert is replaced with a success message in the same screen position.
 */


#include "TaskTimer.h"

#include <Application.h>
#include <Catalog.h>

#include "constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TaskTimer"

/** @brief Rolling counter used to stack timeout alerts on screen. */
static int32 sAlertStackCount = 0;


/**
 * @brief Constructs a timer for a given task and starts its looper.
 *
 * Sets up a self-targeted messenger for the BMessageRunner and a target
 * BInvoker for the user-facing timeout alert.
 *
 * @param target Messenger used to forward kill requests back to the
 *               TaskLooper.
 * @param owner  Task this timer guards; passed back in kill replies.
 */
TaskTimer::TaskTimer(const BMessenger& target, Task* owner)
	:
	BLooper(),
	fTimeoutMicroSeconds(kTimerTimeoutSeconds * 1000000),
	fTimerIsRunning(false),
	fReplyTarget(target),
	fMessageRunner(NULL),
	fTimeoutMessage(TASK_TIMEOUT),
	fTimeoutAlert(NULL),
	fOwner(owner)
{
	Run();

	// Messenger for the Message Runner to use to send its message to the timer
	fMessenger.SetTo(this);
	// Invoker for the Alerts to use to send their messages to the timer
	fTimeoutAlertInvoker.SetMessage(
		new BMessage(TIMEOUT_ALERT_BUTTON_SELECTION));
	fTimeoutAlertInvoker.SetTarget(this);
}


/**
 * @brief Closes any visible timeout alert and stops the underlying runner.
 */
TaskTimer::~TaskTimer()
{
	if (fTimeoutAlert) {
		fTimeoutAlert->Lock();
		fTimeoutAlert->Quit();
	}
	if (fMessageRunner)
		fMessageRunner->SetCount(0);
}


/**
 * @brief Always allows the looper to quit on request.
 *
 * @return Always true.
 */
bool
TaskTimer::QuitRequested()
{
	return true;
}


/**
 * @brief Handles timeout firing and the user's response to the alert.
 *
 * On TASK_TIMEOUT, while the timer is still considered running, an alert
 * is opened offering "Keep trying" or "Cancel task". On
 * TIMEOUT_ALERT_BUTTON_SELECTION the user's choice either rearms the
 * timer or sends a TASK_KILL_REQUEST to the TaskLooper.
 *
 * @param message Incoming BMessage.
 */
void
TaskTimer::MessageReceived(BMessage* message)
{
	switch (message->what)
	{
		case TASK_TIMEOUT:
		{
			fMessageRunner = NULL;
			if (fTimerIsRunning) {
				BString text(B_TRANSLATE_COMMENT("The task for repository"
					" %name% is taking a long time to complete.",
					"Alert message.  Do not translate %name%"));
				BString nameString("\"");
				nameString.Append(fRepositoryName).Append("\"");
				text.ReplaceFirst("%name%", nameString);
				fTimeoutAlert = new BAlert("timeout", text,
					B_TRANSLATE_COMMENT("Keep trying", "Button label"),
					B_TRANSLATE_COMMENT("Cancel task", "Button label"),
					NULL, B_WIDTH_AS_USUAL, B_WARNING_ALERT);
				fTimeoutAlert->SetShortcut(0, B_ESCAPE);
				// Calculate the position to correctly stack this alert
				BRect windowFrame = be_app->WindowAt(0)->Frame();
				int32 stackPos = _NextAlertStackCount();
				float xPos = windowFrame.left
					+ windowFrame.Width()/2 + stackPos * kTimerAlertOffset;
				float yPos = windowFrame.top
					+ (stackPos + 1) * kTimerAlertOffset;
				fTimeoutAlert->Go(&fTimeoutAlertInvoker);
				xPos -= fTimeoutAlert->Frame().Width()/2;
					// The correct frame for the alert is not available until
					// after Go is called
				fTimeoutAlert->MoveTo(xPos, yPos);
			}
			break;
		}
		
		case TIMEOUT_ALERT_BUTTON_SELECTION:
		{
			fTimeoutAlert = NULL;
			// Timeout alert was invoked by user and timer still has not
			// been stopped
			if (fTimerIsRunning) {
				// find which button was pressed
				int32 selection = -1;
				message->FindInt32("which", &selection);
				if (selection == 1) {
					BMessage reply(TASK_KILL_REQUEST);
					reply.AddPointer(key_taskptr, fOwner);
					fReplyTarget.SendMessage(&reply);
				} else if (selection == 0) {
					// Create new timer
					fMessageRunner = new BMessageRunner(fMessenger,
						&fTimeoutMessage, kTimerRetrySeconds * 1000000, 1);
				}
			}
			break;
		}
	}
}


/**
 * @brief Arms the timer and remembers the repository name for the alert.
 *
 * Reuses the existing BMessageRunner when present, resetting its interval;
 * otherwise creates a new one configured for a single TASK_TIMEOUT firing.
 *
 * @param name Display name of the repository the guarded task is acting on.
 */
void
TaskTimer::Start(const char* name)
{
	fTimerIsRunning = true;
	fRepositoryName.SetTo(name);

	// Create a message runner that will send a TASK_TIMEOUT message if the
	// timer is not stopped
	if (fMessageRunner == NULL) {
		fMessageRunner = new BMessageRunner(fMessenger, &fTimeoutMessage,
			fTimeoutMicroSeconds, 1);
	}
	else
		fMessageRunner->SetInterval(fTimeoutMicroSeconds);
}


/**
 * @brief Stops the timer and replaces any visible alert with a success one.
 *
 * Disarming consists of clearing the running flag and setting the runner
 * interval to LLONG_MAX so it can be reused without firing. If the user
 * was already looking at the timeout alert it is replaced in place with a
 * "task completed" alert.
 *
 * @param name Repository name to mention in the success alert text.
 */
void
TaskTimer::Stop(const char* name)
{
	fTimerIsRunning = false;

	// Reset max timeout so we can reuse the runner at the next Start call
	if (fMessageRunner != NULL)
		fMessageRunner->SetInterval(LLONG_MAX);

	// If timeout alert is showing replace it
	if (fTimeoutAlert) {
		// Remove current alert
		BRect frame = fTimeoutAlert->Frame();
		fTimeoutAlert->Quit();
		fTimeoutAlert = NULL;

		// Display new alert that won't send a message
		BString text(B_TRANSLATE_COMMENT("Good news! The task for repository "
			"%name% completed.", "Alert message.  Do not translate %name%"));
		BString nameString("\"");
		nameString.Append(name).Append("\"");
		text.ReplaceFirst("%name%", nameString);
		BAlert* newAlert = new BAlert("timeout", text, kOKLabel, NULL, NULL,
			B_WIDTH_AS_USUAL, B_WARNING_ALERT);
		newAlert->SetShortcut(0, B_ESCAPE);
		newAlert->MoveTo(frame.left, frame.top);
		newAlert->Go(NULL);
	}
}


/**
 * @brief Returns the next stacking offset index for a timeout alert.
 *
 * Wraps around after ten alerts so concurrent alerts cycle through a fixed
 * grid of positions instead of marching off-screen indefinitely.
 *
 * @return Zero-based offset slot for the alert frame calculation.
 */
int32
TaskTimer::_NextAlertStackCount()
{
	if (sAlertStackCount > 9)
		sAlertStackCount = 0;
	return sAlertStackCount++;
}
