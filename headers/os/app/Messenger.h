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
 *   Copyright 2001-2011 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold (bonefish@users.sf.net)
 */
#ifndef _MESSENGER_H
#define _MESSENGER_H

/**
 * @file Messenger.h
 * @brief Defines the BMessenger class for inter-handler and inter-application
 *        message delivery.
 */


#include <OS.h>
#include <ByteOrder.h>
#include <Message.h>


class BHandler;
class BLooper;

/**
 * @brief Proxy object that enables message delivery to a local or remote
 *        target.
 *
 * A BMessenger identifies a target BHandler within a BLooper, either in the
 * same application or in a different team. It is the primary mechanism for
 * sending BMessage objects between threads and applications.
 *
 * A messenger can be constructed to target:
 * - A specific BHandler/BLooper pair within the local application.
 * - A remote application identified by its MIME signature.
 *
 * Once constructed, the same SendMessage() interface is used regardless of
 * whether the target is local or remote. The messenger is lightweight and
 * safe to copy and pass by value.
 *
 * @see BMessage, BHandler, BLooper
 */
class BMessenger {
public:
	/** @name Construction and destruction */
	/** @{ */

	/**
	 * @brief Creates an uninitialized messenger.
	 *
	 * The messenger is not valid until SetTo() is called or it is assigned
	 * from another valid BMessenger.
	 *
	 * @see IsValid(), SetTo()
	 */
									BMessenger();

	/**
	 * @brief Creates a messenger targeting a remote application.
	 *
	 * The messenger will target the preferred handler of the application
	 * identified by @a signature.
	 *
	 * @param signature The MIME signature of the target application
	 *                  (e.g. "application/x-vnd.MyApp").
	 * @param team      The team_id of the target application.  Pass -1 to
	 *                  let the system locate the team by signature.
	 * @param result    Optional pointer to receive the initialization status.
	 *                  B_OK on success, or an error code on failure.
	 *
	 * @see SetTo(const char*, team_id)
	 */
									BMessenger(const char* signature,
										team_id team = -1,
										status_t* result = NULL);

	/**
	 * @brief Creates a messenger targeting a local handler.
	 *
	 * If @a looper is NULL, the handler's current looper is used. If
	 * @a handler is NULL but @a looper is given, the looper's preferred
	 * handler becomes the target.
	 *
	 * @param handler  The target BHandler, or NULL for the looper's
	 *                 preferred handler.
	 * @param looper   The BLooper that owns @a handler, or NULL to use
	 *                 the handler's own looper.
	 * @param result   Optional pointer to receive the initialization status.
	 *
	 * @see SetTo(const BHandler*, const BLooper*)
	 */
									BMessenger(const BHandler* handler,
										const BLooper* looper = NULL,
										status_t* result = NULL);

	/**
	 * @brief Copy constructor.
	 *
	 * @param other The BMessenger to copy.
	 */
									BMessenger(const BMessenger& other);

	/**
	 * @brief Destructor.
	 */
									~BMessenger();

	/** @} */

	/** @name Target information */
	/** @{ */

	/**
	 * @brief Tests whether the target lives in the same application.
	 *
	 * @return @c true if the target is within the calling team, @c false
	 *         if it is in a remote application.
	 */
			bool					IsTargetLocal() const;

	/**
	 * @brief Returns the target handler and its owning looper.
	 *
	 * This method only works for local targets. For remote targets the
	 * method returns NULL.
	 *
	 * @param[out] looper Pointer to a BLooper* that receives the owning
	 *                    looper of the target handler. May be NULL.
	 * @return The target BHandler, or NULL if the target is remote or
	 *         invalid.
	 */
			BHandler*				Target(BLooper **looper) const;

	/**
	 * @brief Locks the looper of the target handler.
	 *
	 * Equivalent to calling BLooper::Lock() on the target's looper.
	 * Only works for local targets.
	 *
	 * @return @c true if the looper was successfully locked, @c false on
	 *         failure or if the target is remote.
	 *
	 * @see LockTargetWithTimeout()
	 */
			bool					LockTarget() const;

	/**
	 * @brief Locks the looper of the target handler with a timeout.
	 *
	 * @param timeout Maximum time in microseconds to wait for the lock.
	 * @return B_OK if the looper was locked, B_TIMED_OUT if the timeout
	 *         elapsed, or another error code on failure.
	 *
	 * @see LockTarget()
	 */
			status_t				LockTargetWithTimeout(
										bigtime_t timeout) const;

	/** @} */

	/** @name Message sending */
	/** @{ */

	/**
	 * @brief Sends a message identified by a command constant.
	 *
	 * Constructs a BMessage with the given @a command and delivers it to
	 * the target. Any reply will be sent to @a replyTo.
	 *
	 * @param command  The message @c what code to send.
	 * @param replyTo  The BHandler that should receive the reply, or NULL
	 *                 for no reply.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t				SendMessage(uint32 command,
										BHandler* replyTo = NULL) const;

	/**
	 * @brief Sends a BMessage with a reply handler.
	 *
	 * @param message  Pointer to the BMessage to send. Ownership is not
	 *                 transferred.
	 * @param replyTo  The BHandler that should receive the reply, or NULL.
	 * @param timeout  Maximum time in microseconds to wait for delivery.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t				SendMessage(BMessage* message,
										BHandler* replyTo = NULL,
										bigtime_t timeout
											= B_INFINITE_TIMEOUT) const;

	/**
	 * @brief Sends a BMessage with a reply messenger.
	 *
	 * @param message  Pointer to the BMessage to send.
	 * @param replyTo  A BMessenger identifying the reply target.
	 * @param timeout  Maximum time in microseconds to wait for delivery.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t				SendMessage(BMessage* message,
										BMessenger replyTo,
										bigtime_t timeout
											= B_INFINITE_TIMEOUT) const;

	/**
	 * @brief Sends a command and waits synchronously for a reply.
	 *
	 * @param command  The message @c what code to send.
	 * @param reply    Pointer to a BMessage that receives the reply.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t				SendMessage(uint32 command,
										BMessage* reply) const;

	/**
	 * @brief Sends a BMessage and waits synchronously for a reply.
	 *
	 * This is the most flexible synchronous send variant, allowing
	 * separate timeouts for delivery and reply.
	 *
	 * @param message          Pointer to the BMessage to send.
	 * @param reply            Pointer to a BMessage that receives the reply.
	 * @param deliveryTimeout  Maximum microseconds to wait for delivery.
	 * @param replyTimeout     Maximum microseconds to wait for the reply.
	 * @return B_OK on success, B_TIMED_OUT if either timeout elapsed, or
	 *         another error code on failure.
	 */
			status_t				SendMessage(BMessage* message,
										BMessage* reply,
										bigtime_t deliveryTimeout
											= B_INFINITE_TIMEOUT,
										bigtime_t replyTimeout
											= B_INFINITE_TIMEOUT) const;

	/** @} */

	/** @name Initialization and operators */
	/** @{ */

	/**
	 * @brief Re-targets this messenger to a remote application.
	 *
	 * @param signature The MIME signature of the target application.
	 * @param team      The team_id of the target, or -1 for automatic
	 *                  lookup.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t				SetTo(const char* signature,
										team_id team = -1);

	/**
	 * @brief Re-targets this messenger to a local handler.
	 *
	 * @param handler The target BHandler, or NULL.
	 * @param looper  The owning BLooper, or NULL to use the handler's
	 *                looper.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t				SetTo(const BHandler* handler,
										const BLooper* looper = NULL);

	/**
	 * @brief Assignment operator.
	 *
	 * @param other The BMessenger to copy from.
	 * @return A reference to this messenger.
	 */
			BMessenger&				operator=(const BMessenger& other);

	/**
	 * @brief Equality operator.
	 *
	 * Two messengers are equal if they refer to the same port, handler
	 * token, and team.
	 *
	 * @param other The BMessenger to compare against.
	 * @return @c true if both messengers target the same destination.
	 */
			bool					operator==(const BMessenger& other) const;

	/**
	 * @brief Checks whether this messenger is properly initialized.
	 *
	 * @return @c true if the messenger has a valid target, @c false
	 *         otherwise.
	 */
			bool					IsValid() const;

	/**
	 * @brief Returns the team_id of the target application.
	 *
	 * @return The team ID, or -1 if the messenger is not valid.
	 */
			team_id					Team() const;

	/**
	 * @brief Computes a hash value for this messenger.
	 *
	 * Useful for storing messengers in hash-based containers.
	 *
	 * @return A 32-bit hash value.
	 */
			uint32					HashValue() const;

	/** @} */

//----- Private or reserved -----------------------------------------

	class Private;

private:
	friend class Private;

			void					_SetTo(team_id team, port_id port,
										int32 token);
			void					_InitData(const char* signature,
										team_id team, status_t* result);
			void					_InitData(const BHandler* handler,
										const BLooper *looper,
										status_t* result);

private:
			port_id					fPort;
			int32					fHandlerToken;
			team_id					fTeam;

			int32					_reserved[3];
};

/**
 * @brief Less-than comparison for BMessenger objects.
 *
 * Provides a strict weak ordering so that messengers can be stored in
 * ordered containers such as std::map.
 *
 * @param a First messenger.
 * @param b Second messenger.
 * @return @c true if @a a is ordered before @a b.
 */
bool operator<(const BMessenger& a, const BMessenger& b);

/**
 * @brief Inequality comparison for BMessenger objects.
 *
 * @param a First messenger.
 * @param b Second messenger.
 * @return @c true if @a a and @a b do not target the same destination.
 */
bool operator!=(const BMessenger& a, const BMessenger& b);


#endif	// _MESSENGER_H
