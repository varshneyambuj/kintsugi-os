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
 *   Copyright 2003-2015, Axel Doerfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file listener_output.cpp
 *  @brief Syslog output handler that forwards log messages to registered BMessenger listeners. */


#include "listener_output.h"

#include <stdio.h>


static BLocker sLocker;
static BList sListeners;


static void
listener_output(syslog_message& message)
{
	// compose the message to be sent to all listeners; just convert
	// the syslog_message into a BMessage
	BMessage output(SYSLOG_MESSAGE);

	output.AddInt32("from", message.from);
	output.AddInt32("when", message.when);
	output.AddString("ident", message.ident);
	output.AddString("message", message.message);
	output.AddInt32("options", message.options);
	output.AddInt32("priority", message.priority);

	sLocker.Lock();

	for (int32 i = sListeners.CountItems(); i-- > 0;) {
		BMessenger* target = (BMessenger*)sListeners.ItemAt(i);

		status_t status = target->SendMessage(&output);
		if (status < B_OK) {
			// remove targets once they can't be reached anymore
			sListeners.RemoveItem(target);
		}
	}

	sLocker.Unlock();
}


void
remove_listener(BMessenger* messenger)
{
	if (sLocker.Lock()) {
		sListeners.RemoveItem(messenger);

		sLocker.Unlock();
	}
}


void
add_listener(BMessenger* messenger)
{
	if (sLocker.Lock()) {
		sListeners.AddItem(messenger);

		sLocker.Unlock();
	}
}


void
init_listener_output(SyslogDaemon* daemon)
{
	daemon->AddHandler(listener_output);
}

