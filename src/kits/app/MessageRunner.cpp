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
 *   Copyright 2001-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 */


/**
 * @file MessageRunner.cpp
 * @brief Implementation of BMessageRunner for periodic and deferred message delivery.
 *
 * BMessageRunner arranges for a BMessage to be sent to a target BMessenger
 * at a specified interval, a specified number of times. The actual scheduling
 * is delegated to the registrar via the roster private API.
 */


#include <MessageRunner.h>

#include <Application.h>
#include <AppMisc.h>
#include <RegistrarDefs.h>
#include <Roster.h>
#include <RosterPrivate.h>


using namespace BPrivate;


/** @brief Construct a message runner that sends a message pointer at regular intervals.
 *
 *  Replies to the delivered messages are directed to be_app_messenger.
 *
 *  @param target The messenger to send messages to.
 *  @param message The message to send. The runner copies the message internally.
 *  @param interval Time in microseconds between successive sends.
 *  @param count Number of times to send the message; a negative value means unlimited.
 */
BMessageRunner::BMessageRunner(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count)
	:
	fToken(-1)
{
	_InitData(target, message, interval, count, be_app_messenger);
}


/** @brief Construct a message runner that sends a message reference at regular intervals.
 *
 *  Replies to the delivered messages are directed to be_app_messenger.
 *
 *  @param target The messenger to send messages to.
 *  @param message The message to send (passed by reference).
 *  @param interval Time in microseconds between successive sends.
 *  @param count Number of times to send the message; a negative value means unlimited.
 */
BMessageRunner::BMessageRunner(BMessenger target, const BMessage& message,
	bigtime_t interval, int32 count)
	:
	fToken(-1)
{
	_InitData(target, &message, interval, count, be_app_messenger);
}


/** @brief Construct a message runner with an explicit reply target (pointer variant).
 *  @param target The messenger to send messages to.
 *  @param message The message to send.
 *  @param interval Time in microseconds between successive sends.
 *  @param count Number of times to send the message; a negative value means unlimited.
 *  @param replyTo The messenger that should receive replies to the delivered messages.
 */
BMessageRunner::BMessageRunner(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, BMessenger replyTo)
	:
	fToken(-1)
{
	_InitData(target, message, interval, count, replyTo);
}


/** @brief Construct a message runner with an explicit reply target (reference variant).
 *  @param target The messenger to send messages to.
 *  @param message The message to send (passed by reference).
 *  @param interval Time in microseconds between successive sends.
 *  @param count Number of times to send the message; a negative value means unlimited.
 *  @param replyTo The messenger that should receive replies to the delivered messages.
 */
BMessageRunner::BMessageRunner(BMessenger target, const BMessage& message,
	bigtime_t interval, int32 count, BMessenger replyTo)
	:
	fToken(-1)
{
	_InitData(target, &message, interval, count, replyTo);
}


/** @brief Destroy the message runner, unregistering it from the registrar.
 *
 *  Any pending scheduled sends are cancelled. If the runner was already
 *  invalid, the destructor does nothing.
 */
BMessageRunner::~BMessageRunner()
{
	if (fToken < B_OK)
		return;

	// compose the request message
	BMessage request(B_REG_UNREGISTER_MESSAGE_RUNNER);
	status_t result = request.AddInt32("token", fToken);

	// send the request
	BMessage reply;
	if (result == B_OK)
		result = BRoster::Private().SendTo(&request, &reply, false);

	// ignore the reply, we can't do anything anyway
}


/** @brief Check whether the message runner was successfully initialized.
 *  @return B_OK if the runner is valid, or an error code describing the failure.
 */
status_t
BMessageRunner::InitCheck() const
{
	return fToken >= 0 ? B_OK : fToken;
}


/** @brief Change the time interval between message sends.
 *  @param interval The new interval in microseconds.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BMessageRunner::SetInterval(bigtime_t interval)
{
	return _SetParams(true, interval, false, 0);
}


/** @brief Change the number of remaining message sends.
 *  @param count The new send count; a negative value means unlimited.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BMessageRunner::SetCount(int32 count)
{
	return _SetParams(false, 0, true, count);
}


/** @brief Retrieve the current interval and remaining count from the registrar.
 *  @param interval Pointer to receive the current interval in microseconds;
 *                  may be NULL if not needed.
 *  @param count Pointer to receive the remaining send count; may be NULL if
 *               not needed.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BMessageRunner::GetInfo(bigtime_t* interval, int32* count) const
{
	status_t result =  fToken >= 0 ? B_OK : B_BAD_VALUE;

	// compose the request message
	BMessage request(B_REG_GET_MESSAGE_RUNNER_INFO);
	if (result == B_OK)
		result = request.AddInt32("token", fToken);

	// send the request
	BMessage reply;
	if (result == B_OK)
		result = BRoster::Private().SendTo(&request, &reply, false);

	// evaluate the reply
	if (result == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			// count
			int32 _count;
			if (reply.FindInt32("count", &_count) == B_OK) {
				if (count != 0)
					*count = _count;
			} else
				result = B_ERROR;

			// interval
			bigtime_t _interval;
			if (reply.FindInt64("interval", &_interval) == B_OK) {
				if (interval != 0)
					*interval = _interval;
			} else
				result = B_ERROR;
		} else {
			if (reply.FindInt32("error", &result) != B_OK)
				result = B_ERROR;
		}
	}

	return result;
}


/** @brief Start a detached message runner that is not tied to any object lifetime.
 *
 *  The runner sends messages independently; there is no BMessageRunner object
 *  to manage. Replies go to be_app_messenger.
 *
 *  @param target The messenger to send messages to.
 *  @param message The message to send.
 *  @param interval Time in microseconds between successive sends.
 *  @param count Number of times to send the message. Must be positive for
 *               detached runners.
 *  @return B_OK on success, or an error code on failure.
 */
/*static*/ status_t
BMessageRunner::StartSending(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count)
{
	int32 token = _RegisterRunner(target, message, interval, count, true,
		be_app_messenger);

	return token >= B_OK ? B_OK : token;
}


/** @brief Start a detached message runner with an explicit reply target.
 *  @param target The messenger to send messages to.
 *  @param message The message to send.
 *  @param interval Time in microseconds between successive sends.
 *  @param count Number of times to send the message. Must be positive for
 *               detached runners.
 *  @param replyTo The messenger that should receive replies.
 *  @return B_OK on success, or an error code on failure.
 */
/*static*/ status_t
BMessageRunner::StartSending(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, BMessenger replyTo)
{
	int32 token = _RegisterRunner(target, message, interval, count, true,
		replyTo);

	return token >= B_OK ? B_OK : token;
}


// FBC
void BMessageRunner::_ReservedMessageRunner1() {}
void BMessageRunner::_ReservedMessageRunner2() {}
void BMessageRunner::_ReservedMessageRunner3() {}
void BMessageRunner::_ReservedMessageRunner4() {}
void BMessageRunner::_ReservedMessageRunner5() {}
void BMessageRunner::_ReservedMessageRunner6() {}


#ifdef __HAIKU_BEOS_COMPATIBLE
//! Privatized copy constructor to prevent usage.
BMessageRunner::BMessageRunner(const BMessageRunner &)
	:
	fToken(-1)
{
}


//! Privatized assignment operator to prevent usage.
BMessageRunner&
BMessageRunner::operator=(const BMessageRunner&)
{
	return* this;
}
#endif


/*!	Initializes the BMessageRunner.

	The success of the initialization can (and should) be asked for via
	InitCheck().

	\note As soon as the last message has been sent, the message runner
	      becomes unusable. InitCheck() will still return \c B_OK, but
	      SetInterval(), SetCount() and GetInfo() will fail.

	\param target Target of the message(s).
	\param message The message to be sent to the target.
	\param interval Period of time before the first message is sent and
	       between messages (if more than one shall be sent) in microseconds.
	\param count Specifies how many times the message shall be sent.
	       A value less than \c 0 for an unlimited number of repetitions.
	\param replyTo Target replies to the delivered message(s) shall be sent to.
*/
void
BMessageRunner::_InitData(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, BMessenger replyTo)
{
	fToken = _RegisterRunner(target, message, interval, count, false, replyTo);
}


/*!	Registers the BMessageRunner in the registrar.

	\param target Target of the message(s).
	\param message The message to be sent to the target.
	\param interval Period of time before the first message is sent and
	       between messages (if more than one shall be sent) in microseconds.
	\param count Specifies how many times the message shall be sent.
	       A value less than \c 0 for an unlimited number of repetitions.
	\param replyTo Target replies to the delivered message(s) shall be sent to.

	\return The token the message runner is registered with, or the error code
	        while trying to register it.
*/
/*static*/ int32
BMessageRunner::_RegisterRunner(BMessenger target, const BMessage* message,
	bigtime_t interval, int32 count, bool detach, BMessenger replyTo)
{
	status_t result = B_OK;
	if (message == NULL || count == 0 || (count < 0 && detach))
		result = B_BAD_VALUE;

	// compose the request message
	BMessage request(B_REG_REGISTER_MESSAGE_RUNNER);
	if (result == B_OK)
		result = request.AddInt32("team", BPrivate::current_team());

	if (result == B_OK)
		result = request.AddMessenger("target", target);

	if (result == B_OK)
		result = request.AddMessage("message", message);

	if (result == B_OK)
		result = request.AddInt64("interval", interval);

	if (result == B_OK)
		result = request.AddInt32("count", count);

	if (result == B_OK)
		result = request.AddMessenger("reply_target", replyTo);

	// send the request
	BMessage reply;
	if (result == B_OK)
		result = BRoster::Private().SendTo(&request, &reply, false);

	int32 token;

	// evaluate the reply
	if (result == B_OK) {
		if (reply.what == B_REG_SUCCESS) {
			if (reply.FindInt32("token", &token) != B_OK)
				result = B_ERROR;
		} else {
			if (reply.FindInt32("error", &result) != B_OK)
				result = B_ERROR;
		}
	}

	if (result == B_OK)
		return token;

	return result;
}


/*!	Sets the message runner's interval and count parameters.

	The parameters \a resetInterval and \a resetCount specify whether
	the interval or the count parameter respectively shall be reset.

	At least one parameter must be set, otherwise the methods returns
	\c B_BAD_VALUE.

	\param resetInterval \c true, if the interval shall be reset, \c false
	       otherwise -- then \a interval is ignored.
	\param interval The new interval in microseconds.
	\param resetCount \c true, if the count shall be reset, \c false
	       otherwise -- then \a count is ignored.
	\param count Specifies how many times the message shall be sent.
	       A value less than \c 0 for an unlimited number of repetitions.

	\return A status code.
	\retval B_OK Everything went fine.
	\retval B_BAD_VALUE The message runner is not longer valid. All the
	        messages that had to be sent have already been sent. Or both
	        \a resetInterval and \a resetCount are \c false.
*/
status_t
BMessageRunner::_SetParams(bool resetInterval, bigtime_t interval,
	bool resetCount, int32 count)
{
	if ((!resetInterval && !resetCount) || fToken < 0)
		return B_BAD_VALUE;

	// compose the request message
	BMessage request(B_REG_SET_MESSAGE_RUNNER_PARAMS);
	status_t result = request.AddInt32("token", fToken);
	if (result == B_OK && resetInterval)
		result = request.AddInt64("interval", interval);

	if (result == B_OK && resetCount)
		result = request.AddInt32("count", count);

	// send the request
	BMessage reply;
	if (result == B_OK)
		result = BRoster::Private().SendTo(&request, &reply, false);

	// evaluate the reply
	if (result == B_OK) {
		if (reply.what != B_REG_SUCCESS) {
			if (reply.FindInt32("error", &result) != B_OK)
				result = B_ERROR;
		}
	}

	return result;
}
