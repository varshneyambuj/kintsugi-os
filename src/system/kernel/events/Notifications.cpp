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
 *   Copyright 2007-2009, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *           Axel Dörfler, axeld@pinc-software.de
 *           Ingo Weinhold, bonefish@cs.tu-berlin.de
 */

/**
 * @file Notifications.cpp
 * @brief Kernel notification services: listener registration and dispatch.
 *
 * Implements the NotificationManager singleton, named NotificationService
 * objects, and the NotificationListener interface. Subsystems register a
 * NotificationService by name; clients (kernel or user) attach
 * NotificationListener instances filtered by an event mask, and a service's
 * NotifyLocked() fan-out delivers a KMessage event to every listener whose
 * mask intersects. UserMessagingListener bridges kernel events to user-space
 * ports using a batching UserMessagingMessageSender.
 */

#include <Notifications.h>

#include <new>

#include <team.h>


#ifdef _KERNEL_MODE

static const char* kEventMaskString = "event mask";

NotificationManager NotificationManager::sManager;

#endif


// #pragma mark - NotificationListener


NotificationListener::~NotificationListener()
{
}


/**
 * @brief Default EventOccurred() hook; base implementation is a no-op.
 *
 * Subclasses override to react to a notification. Called for every matching
 * listener during NotifyLocked() fan-out.
 *
 * @param service The service emitting the event.
 * @param event   The event payload as a KMessage.
 */
void
NotificationListener::EventOccurred(NotificationService& service,
	const KMessage* event)
{
}


/**
 * @brief Called once after every listener has received a given event.
 *
 * Useful for listeners that batch deliveries (e.g. UserMessagingListener
 * flushes pending messages here). Default implementation is a no-op.
 *
 * @param service The service that emitted the event.
 */
void
NotificationListener::AllListenersNotified(NotificationService& service)
{
}


/**
 * @brief Default equality operator: listeners compare equal iff identical.
 *
 * Subclasses that represent the same logical target with distinct instances
 * (such as UserMessagingListener keyed by port+token) override this.
 *
 * @param other Other listener to compare against.
 * @return True if @p other refers to the same object in memory.
 */
bool
NotificationListener::operator==(const NotificationListener& other) const
{
	return &other == this;
}


// #pragma mark - UserMessagingMessageSender


#ifdef _KERNEL_MODE


/**
 * @brief Constructs an empty user-messaging batching sender.
 *
 * Targets accumulate via SendMessage() until the underlying message changes or
 * the target capacity is reached, at which point a single send_message() call
 * delivers the payload to all registered ports.
 */
UserMessagingMessageSender::UserMessagingMessageSender()
	:
	fMessage(NULL),
	fTargetCount(0)
{
}


/**
 * @brief Queues a delivery of @p message to (port, token), flushing if needed.
 *
 * When the queued message differs from the previous one or the target array
 * is full, the currently buffered batch is flushed first, so batched delivery
 * preserves strict per-message ordering.
 *
 * @param message Message to deliver; callers must not free it before flush.
 * @param port    Destination port.
 * @param token   Destination token within @p port.
 */
void
UserMessagingMessageSender::SendMessage(const KMessage* message, port_id port,
	int32 token)
{
	if ((message != fMessage && fMessage != NULL)
		|| fTargetCount == MAX_MESSAGING_TARGET_COUNT) {
		FlushMessage();
	}

	fMessage = message;
	fTargets[fTargetCount].port = port;
	fTargets[fTargetCount].token = token;
	fTargetCount++;
}


/**
 * @brief Emits any queued message to all accumulated targets and resets state.
 *
 * No-op if no message is pending or no targets are recorded.
 */
void
UserMessagingMessageSender::FlushMessage()
{
	if (fMessage != NULL && fTargetCount > 0) {
		send_message(fMessage->Buffer(), fMessage->ContentSize(),
			fTargets, fTargetCount);
	}

	fMessage = NULL;
	fTargetCount = 0;
}


// #pragma mark - UserMessagingListener


/**
 * @brief Creates a listener that forwards events to a user-space port/token.
 *
 * @param sender Shared batching sender used to coalesce deliveries.
 * @param port   Destination port id in the target team.
 * @param token  Destination token within the port.
 */
UserMessagingListener::UserMessagingListener(UserMessagingMessageSender& sender,
		port_id port, int32 token)
	:
	fSender(sender),
	fPort(port),
	fToken(token)
{
}


UserMessagingListener::~UserMessagingListener()
{
}


/**
 * @brief Queues the event for delivery to this listener's user port.
 *
 * @param service The service that emitted the event (unused).
 * @param event   The KMessage payload to deliver.
 */
void
UserMessagingListener::EventOccurred(NotificationService& service,
	const KMessage* event)
{
	fSender.SendMessage(event, fPort, fToken);
}


/**
 * @brief Finalizes per-event batched delivery to user space.
 *
 * Invoked after every listener has processed the current event; flushes the
 * sender so the accumulated messages actually hit the destination ports.
 *
 * @param service The service that emitted the event (unused).
 */
void
UserMessagingListener::AllListenersNotified(NotificationService& service)
{
	fSender.FlushMessage();
}


//	#pragma mark - NotificationService


NotificationService::~NotificationService()
{
}


//	#pragma mark - default_listener


/**
 * @brief Destructor for default_listener entries.
 *
 * Frees the wrapped NotificationListener only when it is a
 * UserMessagingListener, which is owned by the service; externally supplied
 * listeners are the caller's responsibility.
 */
default_listener::~default_listener()
{
	// Only delete the listener if it's one of ours
	if (dynamic_cast<UserMessagingListener*>(listener) != NULL) {
		delete listener;
	}
}


//	#pragma mark - DefaultNotificationService


/**
 * @brief Constructs a named default notification service.
 *
 * Initializes the recursive lock that serializes listener list mutations and
 * dispatch.
 *
 * @param name Service name; used both as the registry key and lock label.
 */
DefaultNotificationService::DefaultNotificationService(const char* name)
	:
	fName(name)
{
	recursive_lock_init(&fLock, name);
}


DefaultNotificationService::~DefaultNotificationService()
{
	recursive_lock_destroy(&fLock);
}


/**
 * @brief Fans out an event to every listener whose mask intersects @p eventMask.
 *
 * Must be called with fLock held. Iterates twice over the listener list using
 * the doubly-linked-list iterator, which tolerates concurrent self-removal by
 * listeners inside their hook methods: the first pass delivers EventOccurred()
 * and the second pass signals AllListenersNotified() so batching listeners can
 * flush.
 *
 * @param event     Event payload delivered to matching listeners.
 * @param eventMask Mask of events being emitted; a listener receives the event
 *     when its own mask shares at least one bit with this mask.
 */
void
DefaultNotificationService::NotifyLocked(const KMessage& event, uint32 eventMask)
{
	// Note: The following iterations support that the listener removes itself
	// in the hook method. That's a property of the DoublyLinkedList iterator.

	// notify all listeners about the event
	DefaultListenerList::Iterator iterator = fListeners.GetIterator();
	while (default_listener* listener = iterator.Next()) {
		if ((eventMask & listener->eventMask) != 0)
			listener->listener->EventOccurred(*this, &event);
	}

	// notify all listeners that all listeners have been notified
	iterator = fListeners.GetIterator();
	while (default_listener* listener = iterator.Next()) {
		if ((eventMask & listener->eventMask) != 0)
			listener->listener->AllListenersNotified(*this);
	}
}


/**
 * @brief Registers a listener with an event mask derived from the specifier.
 *
 * Delegates to ToEventMask() to turn @p eventSpecifier into a uint32, then
 * appends a default_listener entry. Invokes FirstAdded() when transitioning
 * from an empty to non-empty listener list, so subclasses can arm hardware or
 * hook deeper kernel machinery lazily.
 *
 * @param eventSpecifier KMessage describing the event mask. Must not be NULL.
 * @param notificationListener Listener to register; ownership is retained
 *     by the caller.
 * @return B_OK on success, B_BAD_VALUE if @p eventSpecifier is NULL,
 *     B_NO_MEMORY on allocation failure, or the error from ToEventMask().
 */
status_t
DefaultNotificationService::AddListener(const KMessage* eventSpecifier,
	NotificationListener& notificationListener)
{
	if (eventSpecifier == NULL)
		return B_BAD_VALUE;

	uint32 eventMask;
	status_t status = ToEventMask(*eventSpecifier, eventMask);
	if (status != B_OK)
		return status;

	default_listener* listener = new(std::nothrow) default_listener;
	if (listener == NULL)
		return B_NO_MEMORY;

	listener->eventMask = eventMask;
	listener->team = -1;
	listener->listener = &notificationListener;

	RecursiveLocker _(fLock);
	if (fListeners.IsEmpty())
		FirstAdded();
	fListeners.Add(listener);

	return B_OK;
}


/**
 * @brief Updates an existing listener's mask; not supported on this service.
 *
 * @param eventSpecifier Ignored.
 * @param notificationListener Ignored.
 * @return B_NOT_SUPPORTED.
 */
status_t
DefaultNotificationService::UpdateListener(const KMessage* eventSpecifier,
	NotificationListener& notificationListener)
{
	return B_NOT_SUPPORTED;
}


/**
 * @brief Removes a previously registered listener matched by pointer identity.
 *
 * Invokes LastRemoved() once the listener list becomes empty, allowing the
 * concrete service to tear down any resources owned only while at least one
 * listener existed.
 *
 * @param eventSpecifier Ignored.
 * @param notificationListener Listener to remove.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the listener was not present.
 */
status_t
DefaultNotificationService::RemoveListener(const KMessage* eventSpecifier,
	NotificationListener& notificationListener)
{
	RecursiveLocker _(fLock);

	DefaultListenerList::Iterator iterator = fListeners.GetIterator();
	while (default_listener* listener = iterator.Next()) {
		if (listener->listener == &notificationListener) {
			iterator.Remove();
			delete listener;

			if (fListeners.IsEmpty())
				LastRemoved();
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Registers this service with the global NotificationManager.
 *
 * @return Status returned by NotificationManager::RegisterService().
 */
status_t
DefaultNotificationService::Register()
{
	return NotificationManager::Manager().RegisterService(*this);
}


/**
 * @brief Removes this service from the global NotificationManager.
 */
void
DefaultNotificationService::Unregister()
{
	NotificationManager::Manager().UnregisterService(*this);
}


/**
 * @brief Default specifier-to-mask conversion: reads the "event mask" field.
 *
 * Subclasses may override to support richer specifier grammars.
 *
 * @param eventSpecifier Specifier KMessage.
 * @param eventMask      Out parameter: decoded event mask on success.
 * @return B_OK on success, or the error from KMessage::FindInt32().
 */
status_t
DefaultNotificationService::ToEventMask(const KMessage& eventSpecifier,
	uint32& eventMask)
{
	return eventSpecifier.FindInt32("event mask", (int32*)&eventMask);
}


void
DefaultNotificationService::FirstAdded()
{
}


void
DefaultNotificationService::LastRemoved()
{
}


//	#pragma mark - DefaultUserNotificationService


/**
 * @brief Constructs a user-accessible notification service.
 *
 * Registers itself with the "teams" service to receive TEAM_REMOVED events so
 * that listeners belonging to a vanishing team can be cleaned up automatically.
 *
 * @param name Service name passed to the DefaultNotificationService base.
 */
DefaultUserNotificationService::DefaultUserNotificationService(const char* name)
	: DefaultNotificationService(name)
{
	NotificationManager::Manager().AddListener("teams", TEAM_REMOVED, *this);
}


DefaultUserNotificationService::~DefaultUserNotificationService()
{
	NotificationManager::Manager().RemoveListener("teams", NULL, *this);
}


/**
 * @brief Adds a listener while tagging it with the current team id.
 *
 * Used for kernel-internal listeners; user listeners typically arrive via
 * UpdateUserListener().
 *
 * @param eventSpecifier KMessage whose "event mask" field names the events.
 * @param listener       Listener to register.
 * @return B_OK on success, B_BAD_VALUE if @p eventSpecifier is NULL,
 *     B_NO_MEMORY on allocation failure.
 */
status_t
DefaultUserNotificationService::AddListener(const KMessage* eventSpecifier,
	NotificationListener& listener)
{
	if (eventSpecifier == NULL)
		return B_BAD_VALUE;

	uint32 eventMask = eventSpecifier->GetInt32(kEventMaskString, 0);

	return _AddListener(eventMask, listener);
}


/**
 * @brief Updates an already-registered listener's event mask in place.
 *
 * When the "add events" boolean in the specifier is true the bits are OR'd
 * into the existing mask; otherwise the mask is overwritten.
 *
 * @param eventSpecifier Specifier message, must not be NULL.
 * @param notificationListener Listener to update.
 * @return B_OK on success, B_BAD_VALUE if @p eventSpecifier is NULL,
 *     B_ENTRY_NOT_FOUND if the listener is not registered.
 */
status_t
DefaultUserNotificationService::UpdateListener(const KMessage* eventSpecifier,
	NotificationListener& notificationListener)
{
	if (eventSpecifier == NULL)
		return B_BAD_VALUE;

	uint32 eventMask = eventSpecifier->GetInt32(kEventMaskString, 0);
	bool addEvents = eventSpecifier->GetBool("add events", false);

	RecursiveLocker _(fLock);

	DefaultListenerList::Iterator iterator = fListeners.GetIterator();
	while (default_listener* listener = iterator.Next()) {
		if (*listener->listener == notificationListener) {
			if (addEvents)
				listener->eventMask |= eventMask;
			else
				listener->eventMask = eventMask;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Removes a listener without invoking LastRemoved().
 *
 * Unlike the base class, this override does not call LastRemoved() because
 * user-service lifetime is tied to its creator rather than its listener count.
 *
 * @param eventSpecifier Ignored.
 * @param notificationListener Listener to remove.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if not present.
 */
status_t
DefaultUserNotificationService::RemoveListener(const KMessage* eventSpecifier,
	NotificationListener& notificationListener)
{
	RecursiveLocker _(fLock);

	DefaultListenerList::Iterator iterator = fListeners.GetIterator();
	while (default_listener* listener = iterator.Next()) {
		if (listener->listener == &notificationListener) {
			iterator.Remove();
			delete listener;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Removes a user messaging listener matched by (port, token).
 *
 * Used when a user-space subscriber releases its port. Triggers LastRemoved()
 * when the final listener departs.
 *
 * @param port  Port id of the listener to remove.
 * @param token Token of the listener to remove.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if no matching listener exists.
 */
status_t
DefaultUserNotificationService::RemoveUserListeners(port_id port, uint32 token)
{
	UserMessagingListener userListener(fSender, port, token);

	RecursiveLocker _(fLock);

	DefaultListenerList::Iterator iterator = fListeners.GetIterator();
	while (default_listener* listener = iterator.Next()) {
		if (*listener->listener == userListener) {
			iterator.Remove();
			delete listener;

			if (fListeners.IsEmpty())
				LastRemoved();
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Merges additional events into (or creates) a user messaging listener.
 *
 * If an existing listener for (port, token) is found its mask is OR'd with
 * @p eventMask; otherwise a new UserMessagingListener is allocated, owned by
 * the service, and inserted via _AddListener().
 *
 * @param eventMask Additional events to listen for.
 * @param port      Target user port.
 * @param token     Target user token.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or the error
 *     from _AddListener().
 */
status_t
DefaultUserNotificationService::UpdateUserListener(uint32 eventMask,
	port_id port, uint32 token)
{
	UserMessagingListener userListener(fSender, port, token);

	RecursiveLocker _(fLock);

	DefaultListenerList::Iterator iterator = fListeners.GetIterator();
	while (default_listener* listener = iterator.Next()) {
		if (*listener->listener == userListener) {
			listener->eventMask |= eventMask;
			return B_OK;
		}
	}

	UserMessagingListener* copiedListener
		= new(std::nothrow) UserMessagingListener(userListener);
	if (copiedListener == NULL)
		return B_NO_MEMORY;

	status_t status = _AddListener(eventMask, *copiedListener);
	if (status != B_OK)
		delete copiedListener;

	return status;
}


/**
 * @brief Handler for TEAM_REMOVED events observed via the "teams" service.
 *
 * When a team departs, walks our listener list and drops any listener tagged
 * with the matching team id so no stale user-port listeners remain.
 *
 * @param service The teams service (unused).
 * @param event   KMessage containing "event" and "team" fields.
 */
void
DefaultUserNotificationService::EventOccurred(NotificationService& service,
	const KMessage* event)
{
	int32 eventCode = event->GetInt32("event", -1);
	team_id team = event->GetInt32("team", -1);

	if (eventCode == TEAM_REMOVED && team >= B_OK) {
		// check if we have any listeners from that team, and remove them
		RecursiveLocker _(fLock);

		DefaultListenerList::Iterator iterator = fListeners.GetIterator();
		while (default_listener* listener = iterator.Next()) {
			if (listener->team == team) {
				iterator.Remove();
				delete listener;
			}
		}
	}
}


void
DefaultUserNotificationService::AllListenersNotified(
	NotificationService& service)
{
}


/**
 * @brief Shared helper that appends a default_listener tagged with the team id.
 *
 * Calls FirstAdded() on the empty-to-non-empty transition.
 *
 * @param eventMask Event mask for the listener.
 * @param notificationListener Listener object to register.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
DefaultUserNotificationService::_AddListener(uint32 eventMask,
	NotificationListener& notificationListener)
{
	default_listener* listener = new(std::nothrow) default_listener;
	if (listener == NULL)
		return B_NO_MEMORY;

	listener->eventMask = eventMask;
	listener->team = team_get_current_team_id();
	listener->listener = &notificationListener;

	RecursiveLocker _(fLock);
	if (fListeners.IsEmpty())
		FirstAdded();
	fListeners.Add(listener);

	return B_OK;
}


//	#pragma mark - NotificationManager


/**
 * @brief Returns the single NotificationManager instance.
 *
 * @return Reference to the global manager.
 */
/*static*/ NotificationManager&
NotificationManager::Manager()
{
	return sManager;
}


/**
 * @brief Constructs the singleton in place and initializes its state.
 *
 * Called once during kernel bring-up from notifications_init().
 *
 * @return Status returned by NotificationManager::_Init().
 */
/*static*/ status_t
NotificationManager::CreateManager()
{
	new(&sManager) NotificationManager;
	return sManager._Init();
}


NotificationManager::NotificationManager()
{
}


NotificationManager::~NotificationManager()
{
}


/**
 * @brief Initializes the manager lock and the name-keyed service hash.
 *
 * @return B_OK on success or an error from the underlying hash init.
 */
status_t
NotificationManager::_Init()
{
	mutex_init(&fLock, "notification manager");

	return fServiceHash.Init();
}


/**
 * @brief Looks up a registered service by name. Must be called with fLock held.
 *
 * @param name Service name.
 * @return The service pointer or NULL if no such service is registered.
 */
NotificationService*
NotificationManager::_ServiceFor(const char* name)
{
	return fServiceHash.Lookup(name);
}


/**
 * @brief Registers a NotificationService by name, acquiring a reference.
 *
 * @param service Service to register.
 * @return B_OK on success, B_NAME_IN_USE if the name is already taken, or the
 *     error from the hash insert.
 */
status_t
NotificationManager::RegisterService(NotificationService& service)
{
	MutexLocker _(fLock);

	if (_ServiceFor(service.Name()))
		return B_NAME_IN_USE;

	status_t status = fServiceHash.Insert(&service);
	if (status == B_OK)
		service.AcquireReference();

	return status;
}


/**
 * @brief Removes a service from the registry and releases its reference.
 *
 * @param service Service to unregister; must currently be registered.
 */
void
NotificationManager::UnregisterService(NotificationService& service)
{
	MutexLocker _(fLock);
	fServiceHash.Remove(&service);
	service.ReleaseReference();
}


/**
 * @brief Convenience: adds a listener given a raw event mask.
 *
 * Packages the mask into a stack-allocated KMessage and dispatches to the
 * specifier-aware overload.
 *
 * @param serviceName Name of the target service.
 * @param eventMask   Event bits of interest.
 * @param listener    Listener to attach.
 * @return Status from the delegated AddListener() overload.
 */
status_t
NotificationManager::AddListener(const char* serviceName,
	uint32 eventMask, NotificationListener& listener)
{
	char buffer[96];
	KMessage specifier;
	specifier.SetTo(buffer, sizeof(buffer), 0);
	specifier.AddInt32(kEventMaskString, eventMask);

	return AddListener(serviceName, &specifier, listener);
}


/**
 * @brief Adds a listener to the named service using a specifier message.
 *
 * Resolves the service under the manager lock but releases the lock before
 * calling into the service (which may block or take its own lock), using a
 * BReference to keep the service alive during the call.
 *
 * @param serviceName    Name of the target service.
 * @param eventSpecifier Specifier decoded by the service.
 * @param listener       Listener to attach.
 * @return B_OK on success, B_NAME_NOT_FOUND if no such service exists, or any
 *     error returned by NotificationService::AddListener().
 */
status_t
NotificationManager::AddListener(const char* serviceName,
	const KMessage* eventSpecifier, NotificationListener& listener)
{
	MutexLocker locker(fLock);
	NotificationService* service = _ServiceFor(serviceName);
	if (service == NULL)
		return B_NAME_NOT_FOUND;

	BReference<NotificationService> reference(service);
	locker.Unlock();

	return service->AddListener(eventSpecifier, listener);
}


/**
 * @brief Convenience: updates a listener's mask given a raw event mask.
 *
 * @param serviceName Name of the target service.
 * @param eventMask   New event mask.
 * @param listener    Listener to update.
 * @return Status from the delegated UpdateListener() overload.
 */
status_t
NotificationManager::UpdateListener(const char* serviceName,
	uint32 eventMask, NotificationListener& listener)
{
	char buffer[96];
	KMessage specifier;
	specifier.SetTo(buffer, sizeof(buffer), 0);
	specifier.AddInt32(kEventMaskString, eventMask);

	return UpdateListener(serviceName, &specifier, listener);
}


/**
 * @brief Updates a listener on the named service using a specifier message.
 *
 * Uses the same locking strategy as the AddListener() overload.
 *
 * @param serviceName    Name of the target service.
 * @param eventSpecifier Specifier message decoded by the service.
 * @param listener       Listener to update.
 * @return B_OK on success, B_NAME_NOT_FOUND if no such service exists, or any
 *     error returned by NotificationService::UpdateListener().
 */
status_t
NotificationManager::UpdateListener(const char* serviceName,
	const KMessage* eventSpecifier, NotificationListener& listener)
{
	MutexLocker locker(fLock);
	NotificationService* service = _ServiceFor(serviceName);
	if (service == NULL)
		return B_NAME_NOT_FOUND;

	BReference<NotificationService> reference(service);
	locker.Unlock();

	return service->UpdateListener(eventSpecifier, listener);
}


/**
 * @brief Removes a listener from the named service.
 *
 * @param serviceName    Name of the target service.
 * @param eventSpecifier Specifier forwarded to the service (may be NULL).
 * @param listener       Listener to remove.
 * @return B_OK on success, B_NAME_NOT_FOUND if no such service exists, or any
 *     error returned by NotificationService::RemoveListener().
 */
status_t
NotificationManager::RemoveListener(const char* serviceName,
	const KMessage* eventSpecifier, NotificationListener& listener)
{
	MutexLocker locker(fLock);
	NotificationService* service = _ServiceFor(serviceName);
	if (service == NULL)
		return B_NAME_NOT_FOUND;

	BReference<NotificationService> reference(service);
	locker.Unlock();

	return service->RemoveListener(eventSpecifier, listener);
}


//	#pragma mark -


/**
 * @brief Kernel init hook: brings up the NotificationManager singleton.
 *
 * Panics the kernel if the manager cannot be created, as notification
 * services are required for many later subsystems.
 */
extern "C" void
notifications_init(void)
{
	status_t status = NotificationManager::CreateManager();
	if (status < B_OK) {
		panic("Creating the notification manager failed: %s\n",
			strerror(status));
	}
}


#endif	// _KERNEL_MODE
