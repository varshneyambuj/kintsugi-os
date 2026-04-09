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
 *   Copyright 2005-2016, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file MessageLooper.cpp
    @brief Implements the MessageLooper base class providing a dedicated message-dispatch thread. */


#include "MessageLooper.h"

#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include <Autolock.h>


/** @brief Constructs a MessageLooper with the given name.
 *
 * Initialises the underlying BLocker, stores a copy of the name, and sets
 * thread and semaphore handles to their sentinel values.
 *
 * @param name Human-readable name used for the looper thread and lock.
 */
MessageLooper::MessageLooper(const char* name)
	:
	BLocker(name),
	fName(strdup(name)),
	fThread(-1),
	fQuitting(false),
	fDeathSemaphore(-1)
{
}


/** @brief Destructor — frees the duplicated name string. */
MessageLooper::~MessageLooper()
{
	free((void*)fName);
}


/** @brief Spawns and resumes the message-dispatch thread.
 *
 * Acquires the looper lock, derives the thread name via _GetLooperName(),
 * spawns the thread at B_DISPLAY_PRIORITY, and resumes it. If either step
 * fails the looper is marked as quitting.
 *
 * @return B_OK on success, or an appropriate error code on failure.
 */
status_t
MessageLooper::Run()
{
	BAutolock locker(this);

	fQuitting = false;

	char name[B_OS_NAME_LENGTH];
	_GetLooperName(name, sizeof(name));

	// Spawn our message-monitoring thread
	fThread = spawn_thread(_message_thread, name, B_DISPLAY_PRIORITY, this);
	if (fThread < B_OK) {
		fQuitting = true;
		return fThread;
	}

	if (resume_thread(fThread) != B_OK) {
		fQuitting = true;
		kill_thread(fThread);
		fThread = -1;
		return B_BAD_THREAD_ID;
	}

	return B_OK;
}


/** @brief Requests the looper to stop processing messages and destroys itself.
 *
 * Sets the quitting flag and calls _PrepareQuit(). If called from the looper's
 * own thread the object is deleted directly; otherwise a kMsgQuitLooper message
 * is posted so the thread can terminate cleanly.
 */
void
MessageLooper::Quit()
{
	fQuitting = true;
	_PrepareQuit();

	if (fThread < B_OK) {
		// thread has not been started yet
		delete this;
		return;
	}

	if (fThread == find_thread(NULL)) {
		// called from our message looper
		delete this;
		exit_thread(0);
	} else {
		// called from a different thread
		PostMessage(kMsgQuitLooper);
	}
}


/** @brief Sends a code-only message to the looper's message port.
 *
 * @param code    ID code of the message to post.
 * @param timeout Maximum time (in microseconds) to wait for the port,
 *                or B_INFINITE_TIMEOUT to wait indefinitely.
 * @return B_OK on success, or an error code if the send failed.
 */
status_t
MessageLooper::PostMessage(int32 code, bigtime_t timeout)
{
	BPrivate::LinkSender link(MessagePort());
	link.StartMessage(code);
	return link.Flush(timeout);
}


/** @brief Blocks until the specified semaphore is released (looper death signal) or times out.
 *
 * Retries automatically on B_INTERRUPTED so spurious wake-ups are transparent
 * to the caller.
 *
 * @param semaphore The semaphore to wait on (typically the looper's death semaphore).
 * @param timeout   Maximum wait time in microseconds.
 * @return B_OK if the semaphore was acquired, B_TIMED_OUT if the timeout expired.
 */
/*static*/
status_t
MessageLooper::WaitForQuit(sem_id semaphore, bigtime_t timeout)
{
	status_t status;
	do {
		status = acquire_sem_etc(semaphore, 1, B_RELATIVE_TIMEOUT, timeout);
	} while (status == B_INTERRUPTED);

	if (status == B_TIMED_OUT)
		return status;

	return B_OK;
}


/** @brief Hook called just before the looper exits; subclasses may override to flush state.
 *
 * The default implementation does nothing.
 */
void
MessageLooper::_PrepareQuit()
{
	// to be implemented by subclasses
}


/** @brief Fills \a name with the looper's thread name.
 *
 * Uses the stored name string if available, otherwise falls back to
 * "unnamed looper".
 *
 * @param name   Buffer to receive the null-terminated name.
 * @param length Size of the buffer in bytes.
 */
void
MessageLooper::_GetLooperName(char* name, size_t length)
{
	if (fName != NULL)
		strlcpy(name, fName, length);
	else
		strlcpy(name, "unnamed looper", length);
}


/** @brief Hook for subclasses to handle a decoded message.
 *
 * The default implementation is a no-op; subclasses must override this method
 * to process application-specific messages.
 *
 * @param code Message identifier code.
 * @param link LinkReceiver positioned at the start of the message payload.
 */
void
MessageLooper::_DispatchMessage(int32 code, BPrivate::LinkReceiver &link)
{
}


/** @brief Main message-processing loop executed by the looper thread.
 *
 * Reads messages from the link receiver in a loop, acquires the looper lock,
 * and either quits (on kMsgQuitLooper) or delegates to _DispatchMessage().
 * Exits if the underlying port is unexpectedly destroyed.
 */
void
MessageLooper::_MessageLooper()
{
	BPrivate::LinkReceiver& receiver = fLink.Receiver();

	while (true) {
		int32 code;
		status_t status = receiver.GetNextMessage(code);
		if (status < B_OK) {
			// that shouldn't happen, it's our port
			char name[256];
			_GetLooperName(name, 256);
			printf("MessageLooper \"%s\": Someone deleted our message port %"
				B_PRId32 ", %s!\n", name, receiver.Port(), strerror(status));
			break;
		}

		Lock();

		if (code == kMsgQuitLooper)
			Quit();
		else
			_DispatchMessage(code, receiver);

		Unlock();
	}
}


/** @brief Static thread entry point that forwards execution to _MessageLooper().
 *
 * @param _looper Pointer to the MessageLooper instance cast from void*.
 * @return Always returns 0.
 */
/*static*/
int32
MessageLooper::_message_thread(void* _looper)
{
	MessageLooper* looper = (MessageLooper*)_looper;

	looper->_MessageLooper();
	return 0;
}
