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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */
#ifndef _MESSAGE_RUNNER_H
#define _MESSAGE_RUNNER_H

/**
 * @file MessageRunner.h
 * @brief Defines the BMessageRunner class for sending messages at regular intervals.
 */

#include <Messenger.h>


/**
 * @brief Sends a message to a target at regular intervals.
 *
 * BMessageRunner delivers copies of a message to a specified BMessenger target
 * repeatedly at a given time interval. It can be configured to send a fixed
 * number of messages or to run indefinitely (count of -1).
 *
 * The runner is managed by the app_server registrar. Once created, the
 * interval and count can be adjusted. For fire-and-forget runners that are
 * not tied to object lifetime, use the static StartSending() methods.
 *
 * @see BMessenger
 * @see BMessage
 */
class BMessageRunner {
public:
	/**
	 * @brief Constructs a message runner (BMessage pointer variant).
	 *
	 * Begins sending copies of @a message to @a target every @a interval
	 * microseconds, up to @a count times.
	 *
	 * @param target    The messenger identifying the target for messages.
	 * @param message   The message to send. The runner copies this message.
	 * @param interval  The time between messages in microseconds.
	 * @param count     The number of messages to send, or -1 for unlimited.
	 */
								BMessageRunner(BMessenger target,
									const BMessage* message, bigtime_t interval,
									int32 count = -1);

	/**
	 * @brief Constructs a message runner (BMessage reference variant).
	 *
	 * Begins sending copies of @a message to @a target every @a interval
	 * microseconds, up to @a count times.
	 *
	 * @param target    The messenger identifying the target for messages.
	 * @param message   The message to send. The runner copies this message.
	 * @param interval  The time between messages in microseconds.
	 * @param count     The number of messages to send, or -1 for unlimited.
	 */
								BMessageRunner(BMessenger target,
									const BMessage& message, bigtime_t interval,
									int32 count = -1);

	/**
	 * @brief Constructs a message runner with a reply target (BMessage pointer variant).
	 *
	 * @param target    The messenger identifying the target for messages.
	 * @param message   The message to send. The runner copies this message.
	 * @param interval  The time between messages in microseconds.
	 * @param count     The number of messages to send, or -1 for unlimited.
	 * @param replyTo   The messenger to receive replies to the sent messages.
	 */
								BMessageRunner(BMessenger target,
									const BMessage* message, bigtime_t interval,
									int32 count, BMessenger replyTo);

	/**
	 * @brief Constructs a message runner with a reply target (BMessage reference variant).
	 *
	 * @param target    The messenger identifying the target for messages.
	 * @param message   The message to send. The runner copies this message.
	 * @param interval  The time between messages in microseconds.
	 * @param count     The number of messages to send, or -1 for unlimited.
	 * @param replyTo   The messenger to receive replies to the sent messages.
	 */
								BMessageRunner(BMessenger target,
									const BMessage& message, bigtime_t interval,
									int32 count, BMessenger replyTo);

	/**
	 * @brief Destructor.
	 *
	 * Stops the message runner and unregisters it from the registrar.
	 */
	virtual						~BMessageRunner();

	/**
	 * @brief Checks whether the runner was initialized successfully.
	 *
	 * @return B_OK if the runner is valid and sending, or an error code otherwise.
	 */
			status_t			InitCheck() const;

	/**
	 * @brief Changes the interval between messages.
	 *
	 * @param interval  The new interval in microseconds.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetInterval(bigtime_t interval);

	/**
	 * @brief Changes the number of remaining messages to send.
	 *
	 * @param count  The new count, or -1 for unlimited.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetCount(int32 count);

	/**
	 * @brief Retrieves the current interval and remaining count.
	 *
	 * @param interval  Filled with the current interval in microseconds.
	 * @param count     Filled with the remaining message count.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			GetInfo(bigtime_t* interval,
									int32* count) const;

	/**
	 * @brief Starts a detached message runner (without a reply target).
	 *
	 * Creates a message runner that is not tied to any object's lifetime.
	 * The runner cannot be modified or stopped after creation.
	 *
	 * @param target    The messenger identifying the target for messages.
	 * @param message   The message to send.
	 * @param interval  The time between messages in microseconds.
	 * @param count     The number of messages to send.
	 * @return B_OK on success, or an error code on failure.
	 */
	static	status_t			StartSending(BMessenger target,
									const BMessage* message, bigtime_t interval,
									int32 count);

	/**
	 * @brief Starts a detached message runner with a reply target.
	 *
	 * Creates a message runner that is not tied to any object's lifetime.
	 * The runner cannot be modified or stopped after creation.
	 *
	 * @param target    The messenger identifying the target for messages.
	 * @param message   The message to send.
	 * @param interval  The time between messages in microseconds.
	 * @param count     The number of messages to send.
	 * @param replyTo   The messenger to receive replies to the sent messages.
	 * @return B_OK on success, or an error code on failure.
	 */
	static	status_t			StartSending(BMessenger target,
									const BMessage* message, bigtime_t interval,
									int32 count, BMessenger replyTo);

private:
								BMessageRunner(const BMessageRunner &);
			BMessageRunner&		operator=(const BMessageRunner&);

	static	int32				_RegisterRunner(BMessenger target,
									const BMessage* message, bigtime_t interval,
									int32 count, bool detach,
									BMessenger replyTo);

			void				_InitData(BMessenger target,
									const BMessage* message, bigtime_t interval,
									int32 count, BMessenger replyTo);
			status_t			_SetParams(bool resetInterval,
									bigtime_t interval, bool resetCount,
									int32 count);

	virtual	void				_ReservedMessageRunner1();
	virtual	void				_ReservedMessageRunner2();
	virtual	void				_ReservedMessageRunner3();
	virtual	void				_ReservedMessageRunner4();
	virtual	void				_ReservedMessageRunner5();
	virtual	void				_ReservedMessageRunner6();

private:
			int32				fToken;
			uint32				_reserved[6];
};


#endif	// _MESSAGE_RUNNER_H
