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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold (bonefish@users.sf.net)
 */


/** @file Messenger.cpp
 *  @brief Implementation of BMessenger for inter-application and
 *         intra-application messaging.
 *
 *  BMessenger provides the ability to send BMessage objects to local or
 *  remote BHandler/BLooper targets identified by team ID, port, and
 *  handler token.  It is the primary mechanism for inter-object and
 *  inter-application communication in the Application Kit.
 */


#include <Messenger.h>

#include <new>
#include <stdio.h>
#include <strings.h>

#include <Application.h>
#include <AutoLocker.h>
#include <Handler.h>
#include <Looper.h>
#include <Message.h>
#include <OS.h>
#include <Roster.h>

#include <AppMisc.h>
#include <LaunchRoster.h>
#include <LooperList.h>
#include <MessagePrivate.h>
#include <MessageUtils.h>
#include <TokenSpace.h>


// debugging
//#define DBG(x) x
#define DBG(x)
#define OUT	printf

using BPrivate::gDefaultTokens;
using BPrivate::gLooperList;
using BPrivate::BLooperList;


/** @brief Default constructor, creates an uninitialized BMessenger.
 *
 *  The messenger is not valid until initialized via SetTo(), assignment,
 *  or one of the parameterized constructors.
 */
BMessenger::BMessenger()
	:
	fPort(-1),
	fHandlerToken(B_NULL_TOKEN),
	fTeam(-1)
{
}


/** @brief Constructs a BMessenger targeting an application by signature.
 *
 *  Looks up a running application whose signature matches @a signature
 *  and targets its preferred handler.  If @a team is >= 0, the
 *  application is identified by team ID and the signature is verified.
 *
 *  @param signature The MIME application signature (e.g.
 *         "application/x-vnd.MyApp"). May be @c NULL if @a team is valid.
 *  @param team      The target application's team ID, or a negative value
 *                   to look up by signature alone.
 *  @param result    Optional pointer to receive the initialization status.
 *  @see _InitData(const char*, team_id, status_t*)
 */
BMessenger::BMessenger(const char* signature, team_id team, status_t* result)
	:
	fPort(-1),
	fHandlerToken(B_NULL_TOKEN),
	fTeam(-1)
{
	_InitData(signature, team, result);
}


/** @brief Constructs a BMessenger targeting a local BHandler and/or BLooper.
 *
 *  If @a handler is non-NULL and @a looper is NULL, the handler's owning
 *  looper is used.  If both are supplied the handler must belong to that
 *  looper.  A NULL handler with a non-NULL looper targets the looper's
 *  preferred handler.
 *
 *  @param handler  The target handler. May be @c NULL.
 *  @param looper   The target looper. May be @c NULL.
 *  @param _result  Optional pointer to receive the initialization status.
 *  @see _InitData(const BHandler*, const BLooper*, status_t*)
 */
BMessenger::BMessenger(const BHandler* handler, const BLooper* looper,
	status_t* _result)
	:
	fPort(-1),
	fHandlerToken(B_NULL_TOKEN),
	fTeam(-1)
{
	_InitData(handler, looper, _result);
}


/** @brief Copy constructor.
 *
 *  Creates a BMessenger that targets the same handler, port, and team as
 *  @a other.
 *
 *  @param other The BMessenger to copy.
 */
BMessenger::BMessenger(const BMessenger& other)
	:
	fPort(other.fPort),
	fHandlerToken(other.fHandlerToken),
	fTeam(other.fTeam)
{
}


/** @brief Destructor.
 *
 *  BMessenger holds no resources that require explicit cleanup.
 */
BMessenger::~BMessenger()
{
}


//	#pragma mark - Target


/** @brief Returns whether the messenger's target lives in the calling team.
 *  @return @c true if the target is in the same team, @c false otherwise.
 */
bool
BMessenger::IsTargetLocal() const
{
	return BPrivate::current_team() == fTeam;
}


/** @brief Returns the target BHandler and optionally the target BLooper.
 *
 *  If the target is not local, or the handler token is invalid, returns
 *  @c NULL and sets @a _looper (if provided) to @c NULL.
 *
 *  @param _looper Optional pointer to a BLooper pointer that will be set
 *                 to the target looper. May be @c NULL.
 *  @return The target BHandler, or @c NULL if it cannot be resolved.
 *  @see IsTargetLocal()
 */
BHandler*
BMessenger::Target(BLooper** _looper) const
{
	BHandler* handler = NULL;
	if (IsTargetLocal()
		&& (fHandlerToken > B_NULL_TOKEN
			|| fHandlerToken == B_PREFERRED_TOKEN)) {
		gDefaultTokens.GetToken(fHandlerToken, B_HANDLER_TOKEN,
			(void**)&handler);
		if (_looper)
			*_looper = BPrivate::gLooperList.LooperForPort(fPort);
	} else if (_looper)
		*_looper = NULL;

	return handler;
}


/** @brief Locks the target looper if it is local.
 *
 *  Resolves the target looper and attempts to lock it.  After locking,
 *  verifies that the looper's port still matches to guard against the
 *  looper being deleted and replaced between resolution and locking.
 *
 *  @return @c true if the looper was successfully locked, @c false if the
 *          target is not local, the looper could not be resolved, or
 *          the lock attempt failed.
 *  @see LockTargetWithTimeout()
 */
bool
BMessenger::LockTarget() const
{
	BLooper* looper = NULL;
	Target(&looper);
	if (looper != NULL && looper->Lock()) {
		if (looper->fMsgPort == fPort)
			return true;

		looper->Unlock();
		return false;
	}

	return false;
}


/** @brief Locks the target looper with a timeout.
 *
 *  Like LockTarget(), but allows specifying a maximum time to wait for
 *  the lock.  After acquiring the lock the looper port is validated.
 *
 *  @param timeout Maximum time in microseconds to wait for the lock.
 *  @return @c B_OK on success, @c B_BAD_VALUE if the looper could not be
 *          resolved, @c B_BAD_PORT_ID if the port changed after locking,
 *          or the result of BLooper::LockWithTimeout() on timeout/error.
 *  @see LockTarget()
 */
status_t
BMessenger::LockTargetWithTimeout(bigtime_t timeout) const
{
	BLooper* looper = NULL;
	Target(&looper);
	if (looper == NULL)
		return B_BAD_VALUE;

	status_t result = looper->LockWithTimeout(timeout);

	if (result == B_OK && looper->fMsgPort != fPort) {
		looper->Unlock();
		return B_BAD_PORT_ID;
	}

	return result;
}


//	#pragma mark - Message sending


/** @brief Sends a message identified by a command constant.
 *
 *  Constructs a BMessage from @a command and sends it asynchronously.
 *  Replies are directed to @a replyTo.
 *
 *  @param command  The message command constant (becomes BMessage::what).
 *  @param replyTo  The BHandler that should receive the reply. May be @c NULL.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see SendMessage(BMessage*, BHandler*, bigtime_t)
 */
status_t
BMessenger::SendMessage(uint32 command, BHandler* replyTo) const
{
	BMessage message(command);
	return SendMessage(&message, replyTo);
}


/** @brief Sends a BMessage asynchronously with a reply handler.
 *
 *  Wraps @a replyTo in a BMessenger and delegates to the BMessenger-based
 *  overload.
 *
 *  @param message  The message to send. Must not be @c NULL.
 *  @param replyTo  The BHandler that should receive the reply. May be @c NULL.
 *  @param timeout  Delivery timeout in microseconds.
 *  @return @c B_OK on success, @c B_BAD_VALUE if @a message is @c NULL,
 *          or another error code on failure.
 *  @see SendMessage(BMessage*, BMessenger, bigtime_t)
 */
status_t
BMessenger::SendMessage(BMessage* message, BHandler* replyTo,
	bigtime_t timeout) const
{
	DBG(OUT("BMessenger::SendMessage2(%.4s)\n", (char*)&message->what));

	status_t result = message != NULL ? B_OK : B_BAD_VALUE;
	if (result == B_OK) {
		BMessenger replyMessenger(replyTo);
		result = SendMessage(message, replyMessenger, timeout);
	}

	DBG(OUT("BMessenger::SendMessage2() done: %lx\n", result));

	return result;
}


/** @brief Sends a BMessage asynchronously with a reply messenger.
 *
 *  This is the core asynchronous send.  The message is delivered to the
 *  target port; replies are directed to @a replyTo.
 *
 *  @param message  The message to send. Must not be @c NULL.
 *  @param replyTo  A BMessenger that should receive the reply.
 *  @param timeout  Delivery timeout in microseconds.
 *  @return @c B_OK on success, @c B_BAD_VALUE if @a message is @c NULL,
 *          or another error code on failure.
 */
status_t
BMessenger::SendMessage(BMessage* message, BMessenger replyTo,
	bigtime_t timeout) const
{
	if (message == NULL)
		return B_BAD_VALUE;

	return BMessage::Private(message).SendMessage(fPort, fTeam, fHandlerToken,
		timeout, false, replyTo);
}


/** @brief Sends a command and waits synchronously for a reply.
 *
 *  Constructs a BMessage from @a command, sends it, and blocks until a
 *  reply is received or the timeout expires.
 *
 *  @param command The message command constant.
 *  @param reply   Pointer to a BMessage that will receive the reply.
 *                 Must not be @c NULL.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see SendMessage(BMessage*, BMessage*, bigtime_t, bigtime_t)
 */
status_t
BMessenger::SendMessage(uint32 command, BMessage* reply) const
{
	BMessage message(command);

	return SendMessage(&message, reply);
}


/** @brief Sends a BMessage synchronously and waits for a reply.
 *
 *  Delivers @a message to the target and blocks until a reply is received
 *  or @a replyTimeout expires.  The reply is written into @a reply.
 *
 *  @param message         The message to send. Must not be @c NULL.
 *  @param reply           Pointer to a BMessage to receive the reply.
 *                         Must not be @c NULL.
 *  @param deliveryTimeout Maximum time in microseconds to wait for delivery.
 *  @param replyTimeout    Maximum time in microseconds to wait for the reply.
 *  @return @c B_OK on success, @c B_BAD_VALUE if @a message or @a reply
 *          is @c NULL, or another error code on failure.
 */
status_t
BMessenger::SendMessage(BMessage* message, BMessage* reply,
	bigtime_t deliveryTimeout, bigtime_t replyTimeout) const
{
	if (message == NULL || reply == NULL)
		return B_BAD_VALUE;

	status_t result = BMessage::Private(message).SendMessage(fPort, fTeam,
		fHandlerToken, reply, deliveryTimeout, replyTimeout);

	// map this result for now
	if (result == B_BAD_TEAM_ID)
		result = B_BAD_PORT_ID;

	return result;
}


//	#pragma mark - Operators and misc


/** @brief Re-targets the messenger to an application by signature.
 *
 *  @param signature The target application's MIME signature.
 *  @param team      The target application's team ID, or a negative value
 *                   to look up by signature.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _InitData(const char*, team_id, status_t*)
 */
status_t
BMessenger::SetTo(const char* signature, team_id team)
{
	status_t result = B_OK;
	_InitData(signature, team, &result);

	return result;
}


/** @brief Re-targets the messenger to a local BHandler and/or BLooper.
 *
 *  @param handler The target handler. May be @c NULL.
 *  @param looper  The target looper. May be @c NULL.
 *  @return @c B_OK on success, or an error code on failure.
 *  @see _InitData(const BHandler*, const BLooper*, status_t*)
 */
status_t
BMessenger::SetTo(const BHandler* handler, const BLooper* looper)
{
	status_t result = B_OK;
	_InitData(handler, looper, &result);

	return result;
}


/** @brief Copy-assignment operator.
 *
 *  Makes this messenger target the same handler, port, and team as
 *  @a other.  Self-assignment is safe.
 *
 *  @param other The BMessenger to copy from.
 *  @return A reference to this object.
 */
BMessenger&
BMessenger::operator=(const BMessenger& other)
{
	if (this != &other) {
		fPort = other.fPort;
		fHandlerToken = other.fHandlerToken;
		fTeam = other.fTeam;
	}

	return *this;
}


/** @brief Equality operator.
 *
 *  Two messengers are equal if they target the same port and handler
 *  token.  The team ID is intentionally not compared.
 *
 *  @param other The BMessenger to compare with.
 *  @return @c true if both messengers target the same endpoint.
 */
bool
BMessenger::operator==(const BMessenger& other) const
{
	// Note: The fTeam fields are not compared.
	return fPort == other.fPort && fHandlerToken == other.fHandlerToken;
}


/** @brief Returns whether this messenger targets a valid destination.
 *
 *  Checks that the internal port is non-negative and that the port still
 *  exists in the kernel.
 *
 *  @return @c true if the messenger is valid, @c false otherwise.
 */
bool
BMessenger::IsValid() const
{
	port_info info;
	return fPort >= 0 && get_port_info(fPort, &info) == B_OK;
}


/** @brief Returns the team ID of the target application.
 *  @return The target team ID, or a negative value if uninitialized.
 */
team_id
BMessenger::Team() const
{
	return fTeam;
}


/** @brief Returns a hash value suitable for use in hash tables.
 *
 *  The hash is computed from the port and handler token.
 *
 *  @return A 32-bit hash value.
 */
uint32
BMessenger::HashValue() const
{
	return fPort * 19 + fHandlerToken;
}


//	#pragma mark - Private or reserved


/** @brief Sets the messenger's team, target looper port, and handler token.
 *
 *  This is a low-level setter used by BMessenger::Private.  To target the
 *  preferred handler, pass @c B_PREFERRED_TOKEN as @a token.
 *
 *  @param team  The target team ID.
 *  @param port  The target looper port ID.
 *  @param token The target handler token.
 */
void
BMessenger::_SetTo(team_id team, port_id port, int32 token)
{
	fTeam = team;
	fPort = port;
	fHandlerToken = token;
}


/** @brief Initializes the messenger from an application signature and/or
 *         team ID.
 *
 *  When only a signature is given and multiple instances of the application
 *  are running, it is indeterminate which one is chosen as the target.
 *  When only a team ID is passed, the target application is identified
 *  uniquely.  If both are supplied, the application identified by the team
 *  ID must have a matching signature, otherwise the initialization fails
 *  with @c B_MISMATCHED_VALUES.
 *
 *  @param signature The target application's MIME signature. May be @c NULL.
 *  @param team      The target application's team ID. May be < 0.
 *  @param _result   Optional pointer to receive the initialization status.
 */
void
BMessenger::_InitData(const char* signature, team_id team, status_t* _result)
{
	status_t result = B_OK;

	// get an app_info
	app_info info;
	if (team < 0) {
		// no team ID given
		if (signature != NULL) {
			// Try existing launch communication data first
			BMessage data;
			if (BLaunchRoster().GetData(signature, data) == B_OK) {
				info.port = data.GetInt32("port", -1);
				team = data.GetInt32("team", -5);
			}
			if (info.port < 0) {
				result = be_roster->GetAppInfo(signature, &info);
				team = info.team;
				// B_ERROR means that no application with the given signature
				// is running. But we are supposed to return B_BAD_VALUE.
				if (result == B_ERROR)
					result = B_BAD_VALUE;
			} else
				info.flags = 0;
		} else
			result = B_BAD_TYPE;
	} else {
		// a team ID is given
		result = be_roster->GetRunningAppInfo(team, &info);
		// Compare the returned signature with the supplied one.
		if (result == B_OK && signature != NULL
			&& strcasecmp(signature, info.signature) != 0) {
			result = B_MISMATCHED_VALUES;
		}
	}
	// check whether the app flags say B_ARGV_ONLY
	if (result == B_OK && (info.flags & B_ARGV_ONLY) != 0) {
		result = B_BAD_TYPE;
		// Set the team ID nevertheless -- that's what Be's implementation
		// does. Don't know, if that is a bug, but at least it doesn't harm.
		fTeam = team;
	}
	// init our members
	if (result == B_OK) {
		fTeam = team;
		fPort = info.port;
		fHandlerToken = B_PREFERRED_TOKEN;
	}

	// return the result
	if (_result != NULL)
		*_result = result;
}


/** @brief Initializes the messenger to target a local BHandler/BLooper.
 *
 *  When a @c NULL handler is supplied, the preferred handler in the given
 *  looper is targeted.  If no looper is supplied, the handler's owning
 *  looper is used -- the handler must therefore already belong to a looper.
 *  If both are supplied, the handler must actually belong to the specified
 *  looper.
 *
 *  @param handler The target handler. May be @c NULL.
 *  @param looper  The target looper. May be @c NULL.
 *  @param _result Optional pointer to receive the initialization status.
 */
void
BMessenger::_InitData(const BHandler* handler, const BLooper* looper,
	status_t* _result)
{
	status_t result = (handler != NULL || looper != NULL) ? B_OK : B_BAD_VALUE;
	if (result == B_OK) {
		if (handler != NULL) {
			// BHandler is given, check/retrieve the looper.
			if (looper != NULL) {
				if (handler->Looper() != looper)
					result = B_MISMATCHED_VALUES;
			} else {
				looper = handler->Looper();
				if (looper == NULL)
					result = B_MISMATCHED_VALUES;
			}
		}

		// set port, token,...
		if (result == B_OK) {
			AutoLocker<BLooperList> locker(gLooperList);
			if (locker.IsLocked() && gLooperList.IsLooperValid(looper)) {
				fPort = looper->fMsgPort;
				fHandlerToken = handler != NULL
					? _get_object_token_(handler)
					: B_PREFERRED_TOKEN;
				fTeam = looper->Team();
			} else
				result = B_BAD_VALUE;
		}
	}

	if (_result != NULL)
		*_result = result;
}


//	#pragma mark - Operator functions


/** @brief Less-than comparison for BMessenger objects.
 *
 *  Provides a strict weak ordering suitable for use in ordered containers
 *  (e.g. std::map).  Ordering is based on port, then handler token, then
 *  preferred-target status.  The team ID is not considered.
 *
 *  @param _a The left-hand operand.
 *  @param _b The right-hand operand.
 *  @return @c true if @a _a is ordered before @a _b.
 */
bool
operator<(const BMessenger& _a, const BMessenger& _b)
{
	BMessenger::Private a(const_cast<BMessenger&>(_a));
	BMessenger::Private b(const_cast<BMessenger&>(_b));

	// significance:
	// 1. fPort
	// 2. fHandlerToken
	// 3. fPreferredTarget
	// fTeam is insignificant
	return (a.Port() < b.Port()
			|| (a.Port() == b.Port()
				&& (a.Token() < b.Token()
					|| (a.Token() == b.Token()
						&& !a.IsPreferredTarget()
						&& b.IsPreferredTarget()))));
}


/** @brief Inequality comparison for BMessenger objects.
 *
 *  @param a The left-hand operand.
 *  @param b The right-hand operand.
 *  @return @c true if @a a and @a b do not target the same endpoint.
 *  @see BMessenger::operator==()
 */
bool
operator!=(const BMessenger& a, const BMessenger& b)
{
	return !(a == b);
}
