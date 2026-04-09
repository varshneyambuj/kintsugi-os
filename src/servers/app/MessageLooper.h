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
 * MIT License. Copyright 2005-2016, Haiku.
 * Original author: Axel Dörfler, axeld@pinc-software.de.
 */

/** @file MessageLooper.h
    @brief Base class for app-server objects that own a dedicated message-processing thread. */

#ifndef MESSAGE_LOOPER_H
#define MESSAGE_LOOPER_H


#include <PortLink.h>
#include <Locker.h>
#include <OS.h>


/** @brief Abstract base for server-side objects that run their own message thread,
           communicating over a PortLink. Subclasses override _DispatchMessage(). */
class MessageLooper : public BLocker {
public:
								MessageLooper(const char* name);
	virtual						~MessageLooper();

	/** @brief Spawns the message thread and starts the event loop.
	    @return B_OK on success, or an error code. */
	virtual	status_t			Run();

	/** @brief Posts a quit message and waits for the thread to exit. */
	virtual	void				Quit();

			/** @brief Posts a raw message code to this looper's port.
			    @param code The message code to post.
			    @param timeout Maximum time to wait for space in the port queue.
			    @return B_OK on success, or an error code. */
			status_t			PostMessage(int32 code,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

			/** @brief Returns the thread_id of the message thread.
			    @return Thread ID, or -1 if not running. */
			thread_id			Thread() const { return fThread; }

			/** @brief Returns true while a quit is in progress.
			    @return true if quitting. */
			bool				IsQuitting() const { return fQuitting; }

			/** @brief Returns the semaphore signalled when the looper thread exits.
			    @return Death semaphore ID. */
			sem_id				DeathSemaphore() const
									{ return fDeathSemaphore; }

	/** @brief Returns the port used to send messages to this looper.
	    @return Message port ID. */
	virtual	port_id				MessagePort() const = 0;

	/** @brief Blocks until the given semaphore is released (looper has exited).
	    @param semaphore The death semaphore to wait on.
	    @param timeout Maximum wait time in microseconds.
	    @return B_OK when signalled, or B_TIMED_OUT. */
	static	status_t			WaitForQuit(sem_id semaphore,
									bigtime_t timeout = B_INFINITE_TIMEOUT);

private:
	virtual	void				_PrepareQuit();
	virtual	void				_GetLooperName(char* name, size_t length);
	virtual	void				_DispatchMessage(int32 code,
									BPrivate::LinkReceiver& link);
	virtual	void				_MessageLooper();

protected:
	/** @brief Thread entry point that calls _MessageLooper().
	    @param _looper Pointer to the MessageLooper instance.
	    @return 0 on normal exit. */
	static	int32				_message_thread(void*_looper);

protected:
			const char*			fName;
			thread_id			fThread;
			BPrivate::PortLink	fLink;
			bool				fQuitting;
			sem_id				fDeathSemaphore;
};


/** @brief Message code that instructs a MessageLooper to quit its event loop. */
static const int32 kMsgQuitLooper = 'quit';


#endif	/* MESSAGE_LOOPER_H */
