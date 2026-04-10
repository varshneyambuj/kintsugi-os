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
 *   Copyright 2004-2010, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file AddOnMonitor.cpp
 * @brief Implementation of the AddOnMonitor looper class.
 *
 * AddOnMonitor is a BLooper subclass that drives periodic add-on discovery
 * by pairing with an AddOnMonitorHandler and sending it regular B_PULSE
 * messages. It manages the lifetime of the pulse runner and exposes a simple
 * initialization-check interface to callers.
 *
 * @see AddOnMonitorHandler
 */

#include "AddOnMonitor.h"
#include "AddOnMonitorHandler.h"
#include <Message.h>
#include <MessageRunner.h>
#include <Messenger.h>
#include <stdio.h>


/**
 * @brief Default constructor. Creates an uninitialized AddOnMonitor looper.
 *
 * The looper is created but not started and no handler is attached. Callers
 * must invoke SetHandler() and then Run() manually, or use the single-argument
 * constructor instead.
 */
AddOnMonitor::AddOnMonitor()
	:
	BLooper("AddOnMonitor"),
	fInitCheck(B_NO_INIT),
	fPulseRunner(NULL)
{
}


/**
 * @brief Constructs an AddOnMonitor, attaches a handler, and starts the looper.
 *
 * Calls SetHandler() to attach the supplied handler and configure the pulse
 * runner, then starts the looper thread via Run(). If Run() returns a negative
 * thread ID the error is stored in fInitCheck and a diagnostic message is
 * written to stderr.
 *
 * @param handler The AddOnMonitorHandler to attach to this looper. Must not
 *                be NULL.
 */
AddOnMonitor::AddOnMonitor(AddOnMonitorHandler* handler)
	:
	BLooper("AddOnMonitor"),
	fInitCheck(B_NO_INIT),
	fPulseRunner(NULL)
{
	SetHandler(handler);

	thread_id id = Run();
	if (id < 0) {
		fInitCheck = (status_t)id;
		fprintf(stderr, "AddOnMonitor() : bad id returned by Run()\n");
		return;
	}
}


/**
 * @brief Destructor. Releases the pulse runner resource.
 */
AddOnMonitor::~AddOnMonitor()
{
	delete fPulseRunner;
}


/**
 * @brief Returns the initialization status of this AddOnMonitor.
 *
 * @return B_OK if the monitor was successfully initialized, or an error code
 *         describing the failure.
 */
status_t
AddOnMonitor::InitCheck()
{
	return fInitCheck;
}


/**
 * @brief Attaches an AddOnMonitorHandler and sets up the periodic pulse runner.
 *
 * Registers the handler with the looper, sets it as the preferred message
 * target, and creates a BMessageRunner that delivers a B_PULSE message once
 * per second. An initial pulse is sent immediately so that add-on directories
 * are processed without waiting for the first timer tick.
 *
 * @param handler The handler to attach. If NULL the function returns
 *                immediately without modifying state.
 */
void
AddOnMonitor::SetHandler(AddOnMonitorHandler* handler)
{
	if (handler == NULL)
		return;

	AddHandler(handler);
	SetPreferredHandler(handler);

	delete fPulseRunner;
	fPulseRunner = NULL;

	status_t status;
	BMessenger messenger(handler, this, &status);
	if (status != B_OK) {
		fInitCheck = status;
		return;
	}

	BMessage pulseMessage(B_PULSE);
	fPulseRunner = new(std::nothrow) BMessageRunner(messenger, &pulseMessage,
		1000000);
	if (fPulseRunner == NULL) {
		fInitCheck = B_NO_MEMORY;
		return;
	}

	status = fPulseRunner->InitCheck();
	if (status != B_OK) {
		fInitCheck = status;
		fprintf(stderr, "AddOnMonitor() : bad status returned by "
			"fPulseRunner->InitCheck()\n");
		return;
	}

	// Send an initial message to process added directories immediately
	messenger.SendMessage(&pulseMessage);

	fInitCheck = B_OK;
}
