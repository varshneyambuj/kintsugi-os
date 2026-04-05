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
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler, erik@cgsoftware.com
 */
#ifndef _HANDLER_H
#define _HANDLER_H

/**
 * @file Handler.h
 * @brief Defines the BHandler class, the base message-handling component of
 *        the Kintsugi OS Application Kit.
 *
 * BHandler is the foundation of the messaging architecture. Objects that need
 * to receive and respond to BMessage objects should derive from BHandler.
 * Handlers are attached to a BLooper, which dispatches incoming messages to
 * them via MessageReceived(). BHandler also provides an observer pattern
 * (StartWatching / StopWatching / SendNotices) and scripting support.
 */


#include <Archivable.h>


class BLooper;
class BMessageFilter;
class BMessage;
class BMessenger;
class BList;

/** @brief Key for the "what" code change in an observer notification. */
#define B_OBSERVE_WHAT_CHANGE "be:observe_change_what"

/** @brief Key for the original "what" code in an observer notification. */
#define B_OBSERVE_ORIGINAL_WHAT "be:observe_orig_what"

/** @brief Wildcard constant that subscribes an observer to all notification codes. */
const uint32 B_OBSERVER_OBSERVE_ALL = 0xffffffff;

namespace BPrivate {
	class ObserverList;
}

/**
 * @class BHandler
 * @brief Base class for objects that handle messages within a BLooper's
 *        message loop.
 *
 * BHandler is the fundamental building block of the Kintsugi OS messaging
 * system. Every BHandler has a name, can be attached to exactly one BLooper,
 * and receives messages through its MessageReceived() hook. Handlers can be
 * chained together via SetNextHandler() so that unhandled messages propagate
 * along the chain.
 *
 * BHandler also supports:
 * - **Message filtering**: Attach BMessageFilter objects to intercept messages
 *   before they reach MessageReceived().
 * - **Observer pattern**: Other handlers or remote teams can watch for
 *   notifications sent via SendNotices().
 * - **Scripting**: The ResolveSpecifier() and GetSupportedSuites() methods
 *   enable the standard Be scripting protocol.
 *
 * BHandler is archivable and can be serialized to/from a BMessage.
 *
 * @see BLooper, BMessage, BMessageFilter, BMessenger
 */
class BHandler : public BArchivable {
public:
	/**
	 * @brief Constructs a new BHandler with the given name.
	 * @param name The name for this handler, or NULL for an unnamed handler.
	 */
							BHandler(const char* name = NULL);

	/**
	 * @brief Destructor. Removes this handler from its looper and cleans up
	 *        resources.
	 */
	virtual					~BHandler();

	// Archiving

	/**
	 * @brief Archive constructor. Reconstructs a BHandler from an archived
	 *        BMessage.
	 * @param data The archived BMessage containing handler state.
	 */
							BHandler(BMessage* data);

	/**
	 * @brief Creates a new BHandler from an archived BMessage.
	 * @param data The archive message to instantiate from.
	 * @return A new BHandler object, or NULL if the archive is not a BHandler.
	 * @see Archive()
	 */
	static	BArchivable*	Instantiate(BMessage* data);

	/**
	 * @brief Archives this BHandler into a BMessage.
	 * @param data The BMessage to archive into.
	 * @param deep If true, child objects are also archived.
	 * @return B_OK on success, or an error code on failure.
	 * @see Instantiate()
	 */
	virtual	status_t		Archive(BMessage* data, bool deep = true) const;

	// BHandler guts.

	/**
	 * @brief Hook function called when a message is received.
	 *
	 * Override this method to handle messages dispatched to this handler.
	 * Always call the base class implementation for messages you do not
	 * handle.
	 *
	 * @param message The incoming message. Do not delete this pointer.
	 * @see BLooper::DispatchMessage()
	 */
	virtual	void			MessageReceived(BMessage* message);

	/**
	 * @brief Returns the BLooper this handler is attached to.
	 * @return The owning BLooper, or NULL if this handler is not yet added
	 *         to a looper.
	 */
			BLooper*		Looper() const;

	/**
	 * @brief Sets the name of this handler.
	 * @param name The new name, or NULL to clear the name.
	 */
			void			SetName(const char* name);

	/**
	 * @brief Returns the name of this handler.
	 * @return The handler's name, or NULL if no name has been set.
	 */
			const char*		Name() const;

	/**
	 * @brief Sets the next handler in the handler chain.
	 *
	 * When a message is not fully handled by this handler, it can be
	 * forwarded to the next handler in the chain. Both handlers must belong
	 * to the same BLooper.
	 *
	 * @param handler The next handler, or NULL to clear the chain.
	 * @see NextHandler()
	 */
	virtual	void			SetNextHandler(BHandler* handler);

	/**
	 * @brief Returns the next handler in the handler chain.
	 * @return The next BHandler, or NULL if none is set.
	 * @see SetNextHandler()
	 */
			BHandler*		NextHandler() const;

	// Message filtering

	/**
	 * @brief Adds a message filter to this handler.
	 *
	 * Filters are applied to incoming messages before MessageReceived() is
	 * called. The handler takes ownership of the filter.
	 *
	 * @param filter The BMessageFilter to add. Must not be NULL.
	 * @see RemoveFilter(), SetFilterList()
	 */
	virtual	void			AddFilter(BMessageFilter* filter);

	/**
	 * @brief Removes a message filter from this handler.
	 * @param filter The BMessageFilter to remove.
	 * @return true if the filter was found and removed, false otherwise.
	 *         The caller regains ownership of the filter.
	 * @see AddFilter()
	 */
	virtual	bool			RemoveFilter(BMessageFilter* filter);

	/**
	 * @brief Replaces the entire list of message filters.
	 *
	 * Any previously installed filters are deleted. The handler takes
	 * ownership of the filters in the list.
	 *
	 * @param filters A BList of BMessageFilter pointers, or NULL to clear
	 *        all filters.
	 * @see FilterList(), AddFilter()
	 */
	virtual	void			SetFilterList(BList* filters);

	/**
	 * @brief Returns the list of message filters attached to this handler.
	 * @return A BList of BMessageFilter pointers, or NULL if no filters are
	 *         installed.
	 * @see AddFilter(), SetFilterList()
	 */
			BList*			FilterList();

	/**
	 * @brief Locks the handler's looper.
	 *
	 * Equivalent to calling Looper()->Lock(). The handler must be attached
	 * to a looper.
	 *
	 * @return true if the looper was successfully locked, false if the
	 *         handler is not attached to a looper or locking failed.
	 * @see UnlockLooper(), LockLooperWithTimeout()
	 */
			bool			LockLooper();

	/**
	 * @brief Locks the handler's looper with a timeout.
	 * @param timeout Maximum time in microseconds to wait for the lock.
	 * @return B_OK if the lock was acquired, B_TIMED_OUT if the timeout
	 *         expired, or B_BAD_VALUE if no looper is attached.
	 * @see LockLooper(), UnlockLooper()
	 */
			status_t		LockLooperWithTimeout(bigtime_t timeout);

	/**
	 * @brief Unlocks the handler's looper.
	 *
	 * Equivalent to calling Looper()->Unlock(). The looper must have been
	 * previously locked by the calling thread.
	 *
	 * @see LockLooper()
	 */
			void			UnlockLooper();

	// Scripting

	/**
	 * @brief Resolves a scripting specifier to determine the target handler.
	 *
	 * Part of the standard Be scripting protocol. Override this to handle
	 * custom scripting properties.
	 *
	 * @param message The scripting message being resolved.
	 * @param index The current index into the specifier stack.
	 * @param specifier The current specifier message.
	 * @param what The specifier's "what" code (e.g., B_DIRECT_SPECIFIER).
	 * @param property The name of the property being accessed.
	 * @return The BHandler that should handle this specifier, or NULL if
	 *         the specifier could not be resolved.
	 * @see GetSupportedSuites()
	 */
	virtual BHandler*		ResolveSpecifier(BMessage* message, int32 index,
								BMessage* specifier, int32 what,
								const char* property);

	/**
	 * @brief Reports the scripting suites supported by this handler.
	 *
	 * Adds suite names and their property descriptions to @p data. Override
	 * this to advertise custom scripting suites, and always call the base
	 * class implementation.
	 *
	 * @param data The BMessage to populate with suite information.
	 * @return B_OK on success, or an error code on failure.
	 * @see ResolveSpecifier()
	 */
	virtual status_t		GetSupportedSuites(BMessage* data);

	// Observer calls, inter-looper and inter-team

	/**
	 * @brief Begins watching a target for a specific notification code.
	 *
	 * When the target sends notices matching @p what via SendNotices(),
	 * this handler will receive a B_OBSERVER_NOTICE_CHANGE message.
	 *
	 * @param target The BMessenger identifying the handler to watch.
	 * @param what The notification code to watch for.
	 * @return B_OK on success, or an error code on failure.
	 * @see StopWatching(), SendNotices()
	 */
			status_t		StartWatching(BMessenger target, uint32 what);

	/**
	 * @brief Begins watching a target for all notification codes.
	 * @param target The BMessenger identifying the handler to watch.
	 * @return B_OK on success, or an error code on failure.
	 * @see StopWatchingAll(BMessenger), StartWatching(BMessenger, uint32)
	 */
			status_t		StartWatchingAll(BMessenger target);

	/**
	 * @brief Stops watching a target for a specific notification code.
	 * @param target The BMessenger identifying the handler to stop watching.
	 * @param what The notification code to stop watching.
	 * @return B_OK on success, or an error code on failure.
	 * @see StartWatching(BMessenger, uint32)
	 */
			status_t		StopWatching(BMessenger target, uint32 what);

	/**
	 * @brief Stops watching a target for all notification codes.
	 * @param target The BMessenger identifying the handler to stop watching.
	 * @return B_OK on success, or an error code on failure.
	 * @see StartWatchingAll(BMessenger)
	 */
			status_t		StopWatchingAll(BMessenger target);

	// Observer calls for observing targets in the local team

	/**
	 * @brief Registers a local handler as an observer for a specific
	 *        notification code.
	 *
	 * This variant is for observing handlers within the same team.
	 *
	 * @param observer The BHandler that will receive notifications.
	 * @param what The notification code to watch for.
	 * @return B_OK on success, or an error code on failure.
	 * @see StopWatching(BHandler*, uint32), SendNotices()
	 */
			status_t		StartWatching(BHandler* observer, uint32 what);

	/**
	 * @brief Registers a local handler as an observer for all notification
	 *        codes.
	 * @param observer The BHandler that will receive notifications.
	 * @return B_OK on success, or an error code on failure.
	 * @see StopWatchingAll(BHandler*)
	 */
			status_t		StartWatchingAll(BHandler* observer);

	/**
	 * @brief Stops a local handler from observing a specific notification
	 *        code.
	 * @param observer The BHandler to remove as an observer.
	 * @param what The notification code to stop watching.
	 * @return B_OK on success, or an error code on failure.
	 * @see StartWatching(BHandler*, uint32)
	 */
			status_t		StopWatching(BHandler* observer, uint32 what);

	/**
	 * @brief Stops a local handler from observing all notification codes.
	 * @param observer The BHandler to remove as an observer.
	 * @return B_OK on success, or an error code on failure.
	 * @see StartWatchingAll(BHandler*)
	 */
			status_t		StopWatchingAll(BHandler* observer);


	// Reserved

	/**
	 * @brief Reserved virtual function for binary compatibility.
	 * @param d The perform code identifying the operation.
	 * @param arg Operation-specific argument.
	 * @return B_OK or an error code depending on the operation.
	 * @note This method is for internal use and binary compatibility.
	 */
	virtual status_t		Perform(perform_code d, void* arg);

	// Notifier calls

	/**
	 * @brief Sends a notification to all observers watching for @p what.
	 *
	 * Observers registered via StartWatching() will receive a
	 * B_OBSERVER_NOTICE_CHANGE message containing the notification data.
	 *
	 * @param what The notification code that identifies this notice.
	 * @param notice Optional additional data to include in the notification
	 *        message.
	 * @see StartWatching(), IsWatched()
	 */
	virtual	void 			SendNotices(uint32 what,
								const BMessage* notice = NULL);

	/**
	 * @brief Checks whether any observers are watching this handler.
	 * @return true if at least one observer is registered, false otherwise.
	 * @see SendNotices(), StartWatching()
	 */
			bool			IsWatched() const;

private:
	typedef BArchivable		_inherited;
	friend inline int32		_get_object_token_(const BHandler* );
	friend class BLooper;
	friend class BMessageFilter;

	virtual	void			_ReservedHandler2();
	virtual	void			_ReservedHandler3();
	virtual	void			_ReservedHandler4();

			void			_InitData(const char* name);
			BPrivate::ObserverList* _ObserverList();

							BHandler(const BHandler&);
			BHandler&		operator=(const BHandler&);
			void			SetLooper(BLooper* looper);

			int32			fToken;
			char*			fName;
			BLooper*		fLooper;
			BHandler*		fNextHandler;
			BList*			fFilters;
			BPrivate::ObserverList*	fObserverList;
			uint32			_reserved[3];
};

#endif	// _HANDLER_H
