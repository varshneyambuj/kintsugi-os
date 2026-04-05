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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Erik Jaesler, erik@cgsoftware.com
 */
#ifndef _LOOPER_H
#define _LOOPER_H

/**
 * @file Looper.h
 * @brief Defines the BLooper class, which provides a message loop running in
 *        its own thread.
 *
 * BLooper extends BHandler by spawning a dedicated thread that continuously
 * retrieves messages from a port-backed message queue and dispatches them to
 * the appropriate BHandler. A looper maintains a list of BHandler objects and
 * one preferred handler. All access to a BLooper (and its handlers) must be
 * serialized through its locking mechanism.
 *
 * BLooper is the base class for BWindow and BApplication.
 */


#include <BeBuild.h>
#include <Handler.h>
#include <List.h>
#include <OS.h>


class BMessage;
class BMessageQueue;
namespace BPrivate {
	class BDirectMessageTarget;
	class BLooperList;
}

/** @brief Default capacity (in messages) for a BLooper's message port. */
#define B_LOOPER_PORT_DEFAULT_CAPACITY	200


/**
 * @class BLooper
 * @brief A BHandler subclass that owns a thread running a message loop.
 *
 * BLooper creates a message port and spawns a thread that reads messages
 * from the port, places them in a BMessageQueue, and dispatches them to the
 * appropriate BHandler via DispatchMessage(). Multiple BHandler objects can
 * be added to a single BLooper, and one of them can be designated as the
 * preferred handler.
 *
 * Key responsibilities:
 * - **Threading**: Run() spawns the message loop thread; Quit() terminates it.
 * - **Message posting**: PostMessage() enqueues messages for asynchronous
 *   dispatch.
 * - **Handler management**: AddHandler() / RemoveHandler() manage the set of
 *   handlers attached to this looper.
 * - **Locking**: Lock() / Unlock() serialize access from multiple threads.
 * - **Common filters**: Message filters that apply to all handlers in the
 *   looper.
 *
 * @note You must Lock() the looper before calling most methods and Unlock()
 *       when done. The looper's thread automatically holds the lock while
 *       dispatching messages.
 *
 * @see BHandler, BMessage, BMessageQueue, BApplication, BWindow
 */
class BLooper : public BHandler {
public:
	/**
	 * @brief Constructs a new BLooper.
	 * @param name The name of the looper (also used as the thread name).
	 * @param priority The scheduling priority for the looper's thread.
	 *        Defaults to B_NORMAL_PRIORITY.
	 * @param portCapacity The maximum number of messages the port can hold.
	 *        Defaults to B_LOOPER_PORT_DEFAULT_CAPACITY (200).
	 */
							BLooper(const char* name = NULL,
								int32 priority = B_NORMAL_PRIORITY,
								int32 portCapacity
									= B_LOOPER_PORT_DEFAULT_CAPACITY);

	/**
	 * @brief Destructor. Frees resources and removes the looper from the
	 *        global looper list.
	 */
	virtual					~BLooper();

	// Archiving

	/**
	 * @brief Archive constructor. Reconstructs a BLooper from an archived
	 *        BMessage.
	 * @param data The archived BMessage containing looper state.
	 */
							BLooper(BMessage* data);

	/**
	 * @brief Creates a new BLooper from an archived BMessage.
	 * @param data The archive message to instantiate from.
	 * @return A new BLooper object, or NULL if the archive is not a BLooper.
	 * @see Archive()
	 */
	static	BArchivable*	Instantiate(BMessage* data);

	/**
	 * @brief Archives this BLooper into a BMessage.
	 * @param data The BMessage to archive into.
	 * @param deep If true, all attached handlers are also archived.
	 * @return B_OK on success, or an error code on failure.
	 * @see Instantiate()
	 */
	virtual	status_t		Archive(BMessage* data, bool deep = true) const;

	// Message transmission

	/**
	 * @brief Posts a message to this looper by command code.
	 *
	 * Creates a BMessage with the given command code and enqueues it.
	 *
	 * @param command The message "what" code to post.
	 * @return B_OK on success, or an error code on failure.
	 * @see PostMessage(BMessage*)
	 */
			status_t		PostMessage(uint32 command);

	/**
	 * @brief Posts a message to this looper.
	 *
	 * The message is copied and enqueued for dispatch. The caller retains
	 * ownership of @p message.
	 *
	 * @param message The BMessage to post. Must not be NULL.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t		PostMessage(BMessage* message);

	/**
	 * @brief Posts a message to a specific handler within this looper.
	 * @param command The message "what" code to post.
	 * @param handler The target handler that should receive the message.
	 *        Must belong to this looper.
	 * @param replyTo Optional handler to receive the reply. May be NULL.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t		PostMessage(uint32 command, BHandler* handler,
								BHandler* replyTo = NULL);

	/**
	 * @brief Posts a message to a specific handler within this looper.
	 * @param message The BMessage to post. Must not be NULL.
	 * @param handler The target handler that should receive the message.
	 *        Must belong to this looper.
	 * @param replyTo Optional handler to receive the reply. May be NULL.
	 * @return B_OK on success, or an error code on failure.
	 */
			status_t		PostMessage(BMessage* message, BHandler* handler,
								BHandler* replyTo = NULL);

	/**
	 * @brief Dispatches a message to the given handler.
	 *
	 * Override this to intercept all message dispatch. The default
	 * implementation calls handler->MessageReceived(message).
	 *
	 * @param message The message to dispatch. Do not delete this pointer.
	 * @param handler The handler to receive the message.
	 * @see MessageReceived()
	 */
	virtual	void			DispatchMessage(BMessage* message,
								BHandler* handler);

	/**
	 * @brief Hook function called when the looper receives a message.
	 *
	 * Override this to handle messages directed at the looper itself.
	 * Always call the base class implementation for unhandled messages.
	 *
	 * @param message The incoming message. Do not delete this pointer.
	 */
	virtual	void			MessageReceived(BMessage* message);

	/**
	 * @brief Returns the message currently being dispatched.
	 * @return The current BMessage, or NULL if no message is being
	 *         dispatched.
	 * @see DetachCurrentMessage()
	 */
			BMessage*		CurrentMessage() const;

	/**
	 * @brief Detaches and returns the currently dispatched message.
	 *
	 * After calling this, the looper will not delete the message when
	 * dispatch completes. The caller takes ownership.
	 *
	 * @return The current BMessage, or NULL if no message is being
	 *         dispatched.
	 * @see CurrentMessage()
	 */
			BMessage*		DetachCurrentMessage();

	/**
	 * @brief Dispatches a message received from an external source.
	 * @param message The externally received message.
	 * @param handler The target handler.
	 * @param _detached Set to true if the message was detached during
	 *        dispatch, false otherwise.
	 */
			void			DispatchExternalMessage(BMessage* message,
								BHandler* handler, bool& _detached);

	/**
	 * @brief Returns the looper's message queue.
	 * @return The BMessageQueue used by this looper.
	 */
			BMessageQueue*	MessageQueue() const;

	/**
	 * @brief Checks whether there are messages waiting in the queue.
	 * @return true if at least one message is waiting, false otherwise.
	 */
			bool			IsMessageWaiting() const;

	// Message handlers

	/**
	 * @brief Adds a handler to this looper.
	 *
	 * The handler can then receive messages dispatched by this looper.
	 * The looper must be locked when calling this method.
	 *
	 * @param handler The BHandler to add. Must not be NULL and must not
	 *        already belong to another looper.
	 * @see RemoveHandler()
	 */
			void			AddHandler(BHandler* handler);

	/**
	 * @brief Removes a handler from this looper.
	 *
	 * The looper must be locked when calling this method.
	 *
	 * @param handler The BHandler to remove.
	 * @return true if the handler was found and removed, false otherwise.
	 * @see AddHandler()
	 */
			bool			RemoveHandler(BHandler* handler);

	/**
	 * @brief Returns the number of handlers attached to this looper.
	 * @return The handler count, including the looper itself.
	 */
			int32			CountHandlers() const;

	/**
	 * @brief Returns the handler at the specified index.
	 * @param index Zero-based index into the handler list.
	 * @return The BHandler at @p index, or NULL if the index is out of range.
	 * @see CountHandlers(), IndexOf()
	 */
			BHandler*		HandlerAt(int32 index) const;

	/**
	 * @brief Returns the index of a handler in this looper's handler list.
	 * @param handler The BHandler to look up.
	 * @return The zero-based index, or B_ERROR if the handler is not
	 *         attached to this looper.
	 * @see HandlerAt()
	 */
			int32			IndexOf(BHandler* handler) const;

	/**
	 * @brief Returns the preferred handler for this looper.
	 *
	 * Messages posted without a specific target handler are dispatched to
	 * the preferred handler.
	 *
	 * @return The preferred BHandler, or NULL if none is set.
	 * @see SetPreferredHandler()
	 */
			BHandler*		PreferredHandler() const;

	/**
	 * @brief Sets the preferred handler for this looper.
	 * @param handler The BHandler to designate as preferred. Must belong to
	 *        this looper, or NULL to clear the preferred handler.
	 * @see PreferredHandler()
	 */
			void			SetPreferredHandler(BHandler* handler);

	// Loop control

	/**
	 * @brief Spawns the looper's thread and begins the message loop.
	 *
	 * After calling Run(), the looper is unlocked and begins processing
	 * messages. You must have locked the looper before calling Run().
	 *
	 * @return The thread_id of the newly spawned thread, or a negative
	 *         error code on failure.
	 * @see Quit(), Loop()
	 */
	virtual	thread_id		Run();

	/**
	 * @brief Runs the message loop in the calling thread.
	 *
	 * Unlike Run(), this does not spawn a new thread. The calling thread
	 * becomes the looper's thread. This method does not return until the
	 * looper quits.
	 *
	 * @see Run()
	 */
			void			Loop();

	/**
	 * @brief Terminates the message loop and deletes the looper.
	 *
	 * If called from the looper's own thread, the looper is deleted
	 * immediately. If called from another thread, a B_QUIT_REQUESTED
	 * message is posted.
	 *
	 * @note The looper must be locked when calling Quit(). The looper
	 *       object is deleted by this call -- do not use it afterward.
	 *
	 * @see QuitRequested(), Run()
	 */
	virtual	void			Quit();

	/**
	 * @brief Hook function called to determine if the looper may quit.
	 *
	 * Override this to prevent the looper from quitting under certain
	 * conditions. The default implementation returns true.
	 *
	 * @return true if the looper should quit, false to prevent quitting.
	 * @see Quit()
	 */
	virtual	bool			QuitRequested();

	/**
	 * @brief Locks the looper for exclusive access.
	 *
	 * You must lock the looper before calling most BLooper and BHandler
	 * methods. Locking is recursive -- the same thread may lock multiple
	 * times but must unlock the same number of times.
	 *
	 * @return true if the lock was acquired, false if the looper has been
	 *         deleted.
	 * @see Unlock(), LockWithTimeout(), IsLocked()
	 */
			bool			Lock();

	/**
	 * @brief Unlocks the looper.
	 * @see Lock()
	 */
			void			Unlock();

	/**
	 * @brief Tests whether the looper is locked by the calling thread.
	 * @return true if the calling thread holds the lock, false otherwise.
	 * @see Lock()
	 */
			bool			IsLocked() const;

	/**
	 * @brief Locks the looper with a timeout.
	 * @param timeout Maximum time in microseconds to wait for the lock.
	 * @return B_OK if the lock was acquired, B_TIMED_OUT if the timeout
	 *         expired, or B_BAD_VALUE if the looper has been deleted.
	 * @see Lock(), Unlock()
	 */
			status_t		LockWithTimeout(bigtime_t timeout);

	/**
	 * @brief Returns the thread ID of the looper's message loop thread.
	 * @return The thread_id, or -1 if the looper is not yet running.
	 * @see Run(), Team()
	 */
			thread_id		Thread() const;

	/**
	 * @brief Returns the team ID of the looper's thread.
	 * @return The team_id of the team this looper belongs to.
	 * @see Thread()
	 */
			team_id			Team() const;

	/**
	 * @brief Finds the BLooper whose thread matches the given thread ID.
	 * @param thread The thread_id to search for.
	 * @return The BLooper running on @p thread, or NULL if no match is
	 *         found.
	 */
	static	BLooper*		LooperForThread(thread_id thread);

	// Loop debugging

	/**
	 * @brief Returns the thread that currently holds the looper lock.
	 * @return The thread_id of the locking thread, or -1 if unlocked.
	 * @see Lock(), CountLocks()
	 */
			thread_id		LockingThread() const;

	/**
	 * @brief Returns the number of recursive locks held on this looper.
	 * @return The lock nesting count.
	 * @see Lock(), LockingThread()
	 */
			int32			CountLocks() const;

	/**
	 * @brief Returns the total number of lock requests made on this looper.
	 * @return The cumulative count of lock requests.
	 */
			int32			CountLockRequests() const;

	/**
	 * @brief Returns the semaphore used for locking this looper.
	 * @return The sem_id of the lock semaphore.
	 */
			sem_id			Sem() const;

	// Scripting

	/**
	 * @brief Resolves a scripting specifier for this looper.
	 *
	 * Extends BHandler's scripting support to handle looper-specific
	 * properties such as "Handler" and "Window".
	 *
	 * @param message The scripting message being resolved.
	 * @param index The current index into the specifier stack.
	 * @param specifier The current specifier message.
	 * @param what The specifier's "what" code.
	 * @param property The name of the property being accessed.
	 * @return The BHandler that should handle this specifier.
	 * @see GetSupportedSuites(), BHandler::ResolveSpecifier()
	 */
	virtual BHandler*		ResolveSpecifier(BMessage* message, int32 index,
								BMessage* specifier, int32 what,
								const char* property);

	/**
	 * @brief Reports the scripting suites supported by this looper.
	 * @param data The BMessage to populate with suite information.
	 * @return B_OK on success, or an error code on failure.
	 * @see ResolveSpecifier()
	 */
	virtual status_t		GetSupportedSuites(BMessage* data);

	// Message filters (also see BHandler).

	/**
	 * @brief Adds a common message filter that applies to all handlers.
	 *
	 * Common filters are checked before handler-specific filters. The
	 * looper takes ownership of the filter.
	 *
	 * @param filter The BMessageFilter to add. Must not be NULL.
	 * @see RemoveCommonFilter(), SetCommonFilterList()
	 */
	virtual	void			AddCommonFilter(BMessageFilter* filter);

	/**
	 * @brief Removes a common message filter.
	 * @param filter The BMessageFilter to remove.
	 * @return true if the filter was found and removed, false otherwise.
	 *         The caller regains ownership of the filter.
	 * @see AddCommonFilter()
	 */
	virtual	bool			RemoveCommonFilter(BMessageFilter* filter);

	/**
	 * @brief Replaces the entire list of common message filters.
	 *
	 * Any previously installed common filters are deleted.
	 *
	 * @param filters A BList of BMessageFilter pointers, or NULL to clear
	 *        all common filters.
	 * @see CommonFilterList(), AddCommonFilter()
	 */
	virtual	void			SetCommonFilterList(BList* filters);

	/**
	 * @brief Returns the list of common message filters.
	 * @return A BList of BMessageFilter pointers, or NULL if none are
	 *         installed.
	 * @see AddCommonFilter(), SetCommonFilterList()
	 */
			BList*			CommonFilterList() const;

	// Private or reserved

	/**
	 * @brief Reserved virtual function for binary compatibility.
	 * @param d The perform code identifying the operation.
	 * @param arg Operation-specific argument.
	 * @return B_OK or an error code depending on the operation.
	 * @note This method is for internal use and binary compatibility.
	 */
	virtual status_t		Perform(perform_code d, void* arg);

protected:
		// called from overridden task_looper

	/**
	 * @brief Reads a message from the looper's port.
	 *
	 * This is a low-level method used by the message loop implementation.
	 * Subclasses that override task_looper() may call this to retrieve
	 * messages.
	 *
	 * @param timeout Maximum time in microseconds to wait for a message.
	 *        Defaults to B_INFINITE_TIMEOUT.
	 * @return The next BMessage from the port, or NULL if the timeout
	 *         expired or an error occurred.
	 */
			BMessage*		MessageFromPort(bigtime_t = B_INFINITE_TIMEOUT);

private:
	typedef BHandler _inherited;
	friend class BWindow;
	friend class BApplication;
	friend class BMessenger;
	friend class BView;
	friend class BHandler;
	friend class ::BPrivate::BLooperList;
	friend port_id _get_looper_port_(const BLooper* );

	virtual	void			_ReservedLooper1();
	virtual	void			_ReservedLooper2();
	virtual	void			_ReservedLooper3();
	virtual	void			_ReservedLooper4();
	virtual	void			_ReservedLooper5();
	virtual	void			_ReservedLooper6();

							BLooper(const BLooper&);
			BLooper&		operator=(const BLooper&);

							BLooper(int32 priority, port_id port,
								const char* name);

			status_t		_PostMessage(BMessage* msg, BHandler* handler,
								BHandler* reply_to);

	static	status_t		_Lock(BLooper* loop, port_id port,
								bigtime_t timeout);
	static	status_t		_LockComplete(BLooper* loop, int32 old,
								thread_id this_tid, sem_id sem,
								bigtime_t timeout);
			void			_InitData(const char* name, int32 priority,
								port_id port, int32 capacity);
			void			AddMessage(BMessage* msg);
			void			_AddMessagePriv(BMessage* msg);
	static	status_t		_task0_(void* arg);

			void*			ReadRawFromPort(int32* code,
								bigtime_t timeout = B_INFINITE_TIMEOUT);
			BMessage*		ReadMessageFromPort(
								bigtime_t timeout = B_INFINITE_TIMEOUT);
	virtual	BMessage*		ConvertToMessage(void* raw, int32 code);
	virtual	void			task_looper();
			void			_QuitRequested(BMessage* msg);
			bool			AssertLocked() const;
			BHandler*		_TopLevelFilter(BMessage* msg, BHandler* target);
			BHandler*		_HandlerFilter(BMessage* msg, BHandler* target);
			BHandler*		_ApplyFilters(BList* list, BMessage* msg,
								BHandler* target);
			void			check_lock();
			BHandler*		resolve_specifier(BHandler* target, BMessage* msg);
			void			UnlockFully();

			::BPrivate::BDirectMessageTarget* fDirectTarget;
			BMessage*		fLastMessage;
			port_id			fMsgPort;
			int32			fAtomicCount;
			sem_id			fLockSem;
			int32			fOwnerCount;
			thread_id		fOwner;
			thread_id		fThread;
			addr_t			fCachedStack;
			int32			fInitPriority;
			BHandler*		fPreferred;
			BList			fHandlers;
			BList*			fCommonFilters;
			bool			fTerminating;
			bool			fRunCalled;
			bool			fOwnsPort;
			uint32			_reserved[11];
};

#endif	// _LOOPER_H
