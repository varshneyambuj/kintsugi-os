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
 *   Copyright 2001-2005, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Frans van Nispen (xlr8@tref.nl)
 */
#ifndef _INVOKER_H
#define	_INVOKER_H

/**
 * @file Invoker.h
 * @brief Defines the BInvoker class for sending messages on behalf of other objects.
 */

#include <BeBuild.h>
#include <Messenger.h>


class BHandler;
class BLooper;
class BMessage;


/**
 * @brief A mix-in class that sends messages to a target on behalf of other objects.
 *
 * BInvoker provides the ability to send a message to a BHandler or BMessenger
 * target. It is commonly used as a base class for controls (such as BButton,
 * BCheckBox, etc.) that need to notify other objects when they are activated.
 *
 * The invoker holds a message template, a target, and an optional reply handler.
 * When Invoke() is called, the message is sent to the current target.
 *
 * @see BHandler
 * @see BMessenger
 * @see BMessage
 */
class BInvoker {
public:
	/**
	 * @brief Default constructor.
	 *
	 * Creates an uninitialized BInvoker with no message or target set.
	 */
								BInvoker();

	/**
	 * @brief Constructs a BInvoker with a message, handler target, and optional looper.
	 *
	 * @param message  The message template to send when invoked. The BInvoker
	 *                 takes ownership of this message.
	 * @param handler  The target BHandler to receive messages.
	 * @param looper   The BLooper that the handler belongs to. If NULL, the
	 *                 handler's current looper is used.
	 */
								BInvoker(BMessage* message,
									const BHandler* handler,
									const BLooper* looper = NULL);

	/**
	 * @brief Constructs a BInvoker with a message and a BMessenger target.
	 *
	 * @param message  The message template to send when invoked. The BInvoker
	 *                 takes ownership of this message.
	 * @param target   The BMessenger identifying the target for messages.
	 */
								BInvoker(BMessage* message, BMessenger target);

	/**
	 * @brief Destructor.
	 *
	 * Frees the message owned by this invoker.
	 */
	virtual						~BInvoker();

	/**
	 * @brief Sets the message to be sent when the invoker is invoked.
	 *
	 * The BInvoker takes ownership of the message and deletes the previous one.
	 *
	 * @param message  The new message template. May be NULL to clear.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			SetMessage(BMessage* message);

	/**
	 * @brief Returns the current message template.
	 *
	 * @return A pointer to the current BMessage, or NULL if none is set.
	 */
			BMessage*			Message() const;

	/**
	 * @brief Returns the 'what' field of the current message.
	 *
	 * @return The command constant (what field) of the message, or 0 if no
	 *         message is set.
	 */
			uint32				Command() const;

	/**
	 * @brief Sets the target for messages using a handler and optional looper.
	 *
	 * @param handler  The BHandler to receive messages.
	 * @param looper   The BLooper that the handler belongs to. If NULL, the
	 *                 handler's current looper is used.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			SetTarget(const BHandler* handler,
									const BLooper* looper = NULL);

	/**
	 * @brief Sets the target for messages using a BMessenger.
	 *
	 * @param messenger  The BMessenger identifying the target.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			SetTarget(BMessenger messenger);

	/**
	 * @brief Checks whether the target is in the same team as the invoker.
	 *
	 * @return true if the target is local, false if remote.
	 */
			bool				IsTargetLocal() const;

	/**
	 * @brief Returns the target BHandler.
	 *
	 * @param _looper  If non-NULL, filled with a pointer to the target's BLooper.
	 * @return A pointer to the target BHandler, or NULL if none is set.
	 */
			BHandler*			Target(BLooper** _looper = NULL) const;

	/**
	 * @brief Returns the BMessenger used to target messages.
	 *
	 * @return The BMessenger for the current target.
	 */
			BMessenger			Messenger() const;

	/**
	 * @brief Sets the BHandler that will receive replies to invoked messages.
	 *
	 * @param handler  The BHandler to receive reply messages.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			SetHandlerForReply(BHandler* handler);

	/**
	 * @brief Returns the BHandler designated to receive replies.
	 *
	 * @return A pointer to the reply handler, or NULL if none is set.
	 */
			BHandler*			HandlerForReply() const;

	/**
	 * @brief Sends the invoker's message to the target.
	 *
	 * If @a message is non-NULL, it is sent instead of the stored message
	 * template. The message's 'what' field is set to match the stored message.
	 *
	 * @param message  An optional message to send instead of the stored one.
	 * @return B_OK on success, or an error code on failure.
	 */
	virtual	status_t			Invoke(BMessage* message = NULL);

	/**
	 * @brief Sends the invoker's message with a notification kind.
	 *
	 * This method allows derived classes to distinguish between different
	 * kinds of invocations (e.g., B_CONTROL_INVOKED).
	 *
	 * @param message  An optional message to send instead of the stored one.
	 * @param kind     The notification kind (e.g., B_CONTROL_INVOKED).
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			InvokeNotify(BMessage* message,
									uint32 kind = B_CONTROL_INVOKED);

	/**
	 * @brief Sets the timeout for sending messages.
	 *
	 * @param timeout  The timeout in microseconds.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t			SetTimeout(bigtime_t timeout);

	/**
	 * @brief Returns the current message sending timeout.
	 *
	 * @return The timeout in microseconds.
	 */
			bigtime_t			Timeout() const;

protected:
	/**
	 * @brief Returns the notification kind set by InvokeNotify().
	 *
	 * This is used by derived classes inside their Invoke() override to
	 * determine what kind of invocation triggered the call.
	 *
	 * @param _notify  If non-NULL, set to true if this invocation was
	 *                 triggered by InvokeNotify(), false otherwise.
	 * @return The notification kind constant.
	 */
			uint32				InvokeKind(bool* _notify = NULL);

	/**
	 * @brief Begins an InvokeNotify() sequence.
	 *
	 * Called internally before Invoke() during an InvokeNotify() call.
	 *
	 * @param kind  The notification kind for this invocation.
	 */
			void				BeginInvokeNotify(
									uint32 kind = B_CONTROL_INVOKED);

	/**
	 * @brief Ends an InvokeNotify() sequence.
	 *
	 * Called internally after Invoke() during an InvokeNotify() call.
	 */
			void				EndInvokeNotify();

private:
	virtual	void				_ReservedInvoker1();
	virtual	void				_ReservedInvoker2();
	virtual	void				_ReservedInvoker3();

								BInvoker(const BInvoker&);
			BInvoker&			operator=(const BInvoker&);

			BMessage*			fMessage;
			BMessenger			fMessenger;
			BHandler*			fReplyTo;
			bigtime_t			fTimeout;
			uint32				fNotifyKind;
			uint32				_reserved[1];
};


#endif	// _INVOKER_H
