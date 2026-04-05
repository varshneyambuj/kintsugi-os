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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2005, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers (mflerackers@androme.be)
 */


/** @file Invoker.cpp
 *  @brief Implementation of BInvoker, a class for sending messages to targets.
 *
 *  BInvoker provides a mechanism for objects to send messages to a
 *  designated target messenger. It is commonly used as a base class for
 *  controls and other UI elements that need to notify a target when
 *  an action occurs.
 */


#include <Invoker.h>


/** @brief Constructs a BInvoker with a message and a target messenger.
 *  @param message   The message to send when invoked. Takes ownership.
 *  @param messenger The target messenger to receive invocation messages.
 */
BInvoker::BInvoker(BMessage* message, BMessenger messenger)
	:
	fMessage(message),
	fMessenger(messenger),
	fReplyTo(NULL),
	fTimeout(B_INFINITE_TIMEOUT),
	fNotifyKind(0)
{
}


/** @brief Constructs a BInvoker with a message and a handler/looper target.
 *  @param message The message to send when invoked. Takes ownership.
 *  @param handler The target handler, or NULL to use the looper's preferred handler.
 *  @param looper  The target looper.
 */
BInvoker::BInvoker(BMessage* message, const BHandler* handler,
	const BLooper* looper)
	:
	fMessage(message),
	fMessenger(BMessenger(handler, looper)),
	fReplyTo(NULL),
	fTimeout(B_INFINITE_TIMEOUT),
	fNotifyKind(0)
{
}


/** @brief Default constructor. Creates a BInvoker with no message or target.
 */
BInvoker::BInvoker()
	:
	fMessage(NULL),
	fReplyTo(NULL),
	fTimeout(B_INFINITE_TIMEOUT),
	fNotifyKind(0)
{
}


/** @brief Destroys the BInvoker and frees the owned message.
 */
BInvoker::~BInvoker()
{
	delete fMessage;
}


/** @brief Sets the message to be sent when invoked.
 *
 *  Takes ownership of the provided message and deletes the previous one.
 *  If the new message is the same pointer as the current message, no action
 *  is taken.
 *
 *  @param message The new message to use, or NULL to clear.
 *  @return B_OK.
 */
status_t
BInvoker::SetMessage(BMessage* message)
{
	if (fMessage == message)
		return B_OK;

	delete fMessage;
	fMessage = message;

	return B_OK;
}


/** @brief Returns the current invocation message.
 *  @return A pointer to the message, or NULL if no message is set.
 */
BMessage*
BInvoker::Message() const
{
	return fMessage;
}


/** @brief Returns the command constant (what field) of the current message.
 *  @return The message's what value, or 0 if no message is set.
 */
uint32
BInvoker::Command() const
{
	if (fMessage)
		return fMessage->what;

	return 0;
}


/** @brief Sets the invocation target to the specified messenger.
 *  @param messenger The target messenger.
 *  @return B_OK.
 */
status_t
BInvoker::SetTarget(BMessenger messenger)
{
	fMessenger = messenger;
	return B_OK;
}


/** @brief Sets the invocation target to the specified handler and looper.
 *  @param handler The target handler, or NULL to use the looper's preferred handler.
 *  @param looper  The target looper.
 *  @return B_OK.
 */
status_t
BInvoker::SetTarget(const BHandler* handler, const BLooper* looper)
{
	fMessenger = BMessenger(handler, looper);
	return B_OK;
}


/** @brief Checks whether the invocation target is in the same application.
 *  @return true if the target is local, false if it is in another application.
 */
bool
BInvoker::IsTargetLocal() const
{
	return fMessenger.IsTargetLocal();
}


/** @brief Returns the target handler and optionally the associated looper.
 *  @param _looper If not NULL, receives a pointer to the target's looper.
 *  @return The target BHandler, or NULL if the target is remote.
 */
BHandler*
BInvoker::Target(BLooper** _looper) const
{
	return fMessenger.Target(_looper);
}


/** @brief Returns a copy of the target messenger.
 *  @return The BMessenger used for invocation.
 */
BMessenger
BInvoker::Messenger() const
{
	return fMessenger;
}


/** @brief Sets the handler that will receive replies to invocation messages.
 *  @param replyHandler The handler for replies, or NULL for no reply handling.
 *  @return B_OK.
 */
status_t
BInvoker::SetHandlerForReply(BHandler* replyHandler)
{
	fReplyTo = replyHandler;
	return B_OK;
}


/** @brief Returns the handler designated to receive replies.
 *  @return The reply handler, or NULL if none is set.
 */
BHandler*
BInvoker::HandlerForReply() const
{
	return fReplyTo;
}


/** @brief Sends a message to the target.
 *
 *  If \a message is NULL, the invoker's own message is used. The message
 *  is sent via the target messenger with the configured reply handler
 *  and timeout.
 *
 *  @param message The message to send, or NULL to use the default message.
 *  @return B_OK on success, B_BAD_VALUE if no message is available, or an error code.
 */
status_t
BInvoker::Invoke(BMessage* message)
{
	if (!message)
		message = Message();

	if (!message)
		return B_BAD_VALUE;

	return fMessenger.SendMessage(message, fReplyTo, fTimeout);
}


/** @brief Sends a message to the target with a notification kind.
 *
 *  Wraps the invocation in BeginInvokeNotify/EndInvokeNotify calls to
 *  set the notification kind for the duration of the invocation. Returns
 *  B_WOULD_BLOCK if a notification is already in progress.
 *
 *  @param message The message to send, or NULL to use the default message.
 *  @param kind    The notification kind identifier.
 *  @return B_OK on success, B_WOULD_BLOCK if already notifying, or an error code.
 */
status_t
BInvoker::InvokeNotify(BMessage* message, uint32 kind)
{
	if (fNotifyKind != 0)
		return B_WOULD_BLOCK;

	BeginInvokeNotify(kind);
	status_t err = Invoke(message);
	EndInvokeNotify();

	return err;
}


/** @brief Sets the timeout for sending invocation messages.
 *  @param timeout The timeout in microseconds, or B_INFINITE_TIMEOUT.
 *  @return B_OK.
 */
status_t
BInvoker::SetTimeout(bigtime_t timeout)
{
	fTimeout = timeout;
	return B_OK;
}


/** @brief Returns the current invocation message send timeout.
 *  @return The timeout in microseconds.
 */
bigtime_t
BInvoker::Timeout() const
{
	return fTimeout;
}


/** @brief Returns the current invocation notification kind.
 *
 *  If a notification is in progress, returns the kind set by
 *  BeginInvokeNotify(). Otherwise returns B_CONTROL_INVOKED.
 *
 *  @param _notify If not NULL, set to true if a notification is active.
 *  @return The current notification kind, or B_CONTROL_INVOKED if none is active.
 */
uint32
BInvoker::InvokeKind(bool* _notify)
{
	if (_notify)
		*_notify = fNotifyKind != 0;

	if (fNotifyKind != 0)
		return fNotifyKind;

	return B_CONTROL_INVOKED;
}


/** @brief Begins a notification invocation with the specified kind.
 *
 *  Sets the notification kind that will be returned by InvokeKind()
 *  until EndInvokeNotify() is called.
 *
 *  @param kind The notification kind identifier to set.
 */
void
BInvoker::BeginInvokeNotify(uint32 kind)
{
	fNotifyKind = kind;
}


/** @brief Ends the current notification invocation.
 *
 *  Resets the notification kind to 0, indicating no notification
 *  is in progress.
 */
void
BInvoker::EndInvokeNotify()
{
	fNotifyKind = 0;
}


void BInvoker::_ReservedInvoker1() {}
void BInvoker::_ReservedInvoker2() {}
void BInvoker::_ReservedInvoker3() {}


BInvoker::BInvoker(const BInvoker &)
{
}


BInvoker &
BInvoker::operator=(const BInvoker &)
{
	return *this;
}

