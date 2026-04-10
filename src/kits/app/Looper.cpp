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
 *   Copyright 2001-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm, bpmagic@columbus.rr.com
 *       Axel Dörfler, axeld@pinc-software.de
 *       Erik Jaesler, erik@cgsoftware.com
 *       Ingo Weinhold, bonefish@@users.sf.net
 */

/**
 * @file Looper.cpp
 * @brief Implementation of BLooper, the message loop thread class.
 *
 * BLooper spawns a dedicated thread that receives messages from a port and an
 * internal queue, dispatches them to the appropriate BHandler, and applies
 * message filters at both the looper and handler levels. It also provides
 * locking primitives, handler management, and scripting support.
 */


#include <Looper.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Autolock.h>
#include <Message.h>
#include <MessageFilter.h>
#include <MessageQueue.h>
#include <Messenger.h>
#include <PropertyInfo.h>

#include <AppMisc.h>
#include <AutoLocker.h>
#include <DirectMessageTarget.h>
#include <LooperList.h>
#include <MessagePrivate.h>
#include <TokenSpace.h>


// debugging
//#define DBG(x) x
#define DBG(x)	;
#define PRINT(x)	DBG({ printf("[%6" B_PRId32 "] ", find_thread(NULL)); printf x; })


#define FILTER_LIST_BLOCK_SIZE	5
#define DATA_BLOCK_SIZE			5


using BPrivate::gDefaultTokens;
using BPrivate::gLooperList;
using BPrivate::BLooperList;

port_id _get_looper_port_(const BLooper* looper);

enum {
	BLOOPER_PROCESS_INTERNALLY = 0,
	BLOOPER_HANDLER_BY_INDEX
};

static property_info sLooperPropInfo[] = {
	{
		"Handler",
			{},
			{B_INDEX_SPECIFIER, B_REVERSE_INDEX_SPECIFIER},
			NULL, BLOOPER_HANDLER_BY_INDEX,
			{},
			{},
			{}
	},
	{
		"Handlers",
			{B_GET_PROPERTY},
			{B_DIRECT_SPECIFIER},
			NULL, BLOOPER_PROCESS_INTERNALLY,
			{B_MESSENGER_TYPE},
			{},
			{}
	},
	{
		"Handler",
			{B_COUNT_PROPERTIES},
			{B_DIRECT_SPECIFIER},
			NULL, BLOOPER_PROCESS_INTERNALLY,
			{B_INT32_TYPE},
			{},
			{}
	},

	{ 0 }
};

struct _loop_data_ {
	BLooper*	looper;
	thread_id	thread;
};


//	#pragma mark -


/** @brief Construct a BLooper with a name, thread priority, and port capacity.
 *  @param name Human-readable name for this looper and its thread.
 *  @param priority Thread priority for the looper's message loop thread.
 *  @param portCapacity Maximum number of messages the looper's port can hold.
 *                      Defaults to B_LOOPER_PORT_DEFAULT_CAPACITY if <= 0.
 *  @note The looper is locked upon construction. Call Run() to start the
 *        message loop thread.
 *  @see Run()
 *  @see _InitData()
 */
BLooper::BLooper(const char* name, int32 priority, int32 portCapacity)
	:
	BHandler(name)
{
	_InitData(name, priority, -1, portCapacity);
}


/** @brief Destroy the BLooper.
 *
 *  Cleans up the message port, drains remaining messages (auto-replying),
 *  removes and detaches all child handlers, deletes common filters, and
 *  releases the lock semaphore. The looper is removed from the global looper
 *  list.
 *
 *  @note Calling delete on a running looper triggers a debugger call. Use
 *        Quit() instead, which will delete the looper at the appropriate time.
 *  @see Quit()
 */
BLooper::~BLooper()
{
	if (fRunCalled && !fTerminating) {
		debugger("You can't call delete on a BLooper object "
			"once it is running.");
	}

	Lock();

	// In case the looper thread calls Quit() fLastMessage is not deleted.
	if (fLastMessage) {
		delete fLastMessage;
		fLastMessage = NULL;
	}

	// Close the message port and read and reply to the remaining messages.
	if (fMsgPort >= 0 && fOwnsPort)
		close_port(fMsgPort);

	// Clear the queue so our call to IsMessageWaiting() below doesn't give
	// us bogus info
	fDirectTarget->Close();

	BMessage* message;
	while ((message = fDirectTarget->Queue()->NextMessage()) != NULL) {
		delete message;
			// msg will automagically post generic reply
	}

	if (fOwnsPort) {
		do {
			delete ReadMessageFromPort(0);
				// msg will automagically post generic reply
		} while (IsMessageWaiting());

		delete_port(fMsgPort);
	}
	fDirectTarget->Release();

	// Clean up our filters
	SetCommonFilterList(NULL);

	AutoLocker<BLooperList> ListLock(gLooperList);
	RemoveHandler(this);

	// Remove all the "child" handlers
	int32 count = fHandlers.CountItems();
	for (int32 i = 0; i < count; i++) {
		BHandler* handler = (BHandler*)fHandlers.ItemAtFast(i);
		handler->SetNextHandler(NULL);
		handler->SetLooper(NULL);
	}
	fHandlers.MakeEmpty();

	Unlock();
	gLooperList.RemoveLooper(this);
	delete_sem(fLockSem);
}


/** @brief Construct a BLooper from an archived BMessage.
 *  @param data The archive message. The port capacity is read from "_port_cap"
 *              and the thread priority from "_prio". Missing fields use
 *              defaults.
 *  @see Instantiate()
 *  @see Archive()
 */
BLooper::BLooper(BMessage* data)
	: BHandler(data)
{
	int32 portCapacity;
	if (data->FindInt32("_port_cap", &portCapacity) != B_OK || portCapacity < 0)
		portCapacity = B_LOOPER_PORT_DEFAULT_CAPACITY;

	int32 priority;
	if (data->FindInt32("_prio", &priority) != B_OK)
		priority = B_NORMAL_PRIORITY;

	_InitData(Name(), priority, -1, portCapacity);
}


/** @brief Create a new BLooper from an archived BMessage.
 *  @param data The archive message to instantiate from.
 *  @return A new BLooper if \a data is a valid BLooper archive, or NULL if
 *          validation fails.
 *  @see Archive()
 */
BArchivable*
BLooper::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BLooper"))
		return new BLooper(data);

	return NULL;
}


/** @brief Archive the BLooper into a BMessage.
 *  @param data The message to archive into.
 *  @param deep If true, child objects are archived recursively.
 *  @return B_OK on success, or an error code on failure.
 *  @note Archives the port capacity ("_port_cap") and thread priority ("_prio")
 *        in addition to the base BHandler fields.
 *  @see Instantiate()
 */
status_t
BLooper::Archive(BMessage* data, bool deep) const
{
	status_t status = BHandler::Archive(data, deep);
	if (status < B_OK)
		return status;

	port_info info;
	status = get_port_info(fMsgPort, &info);
	if (status == B_OK)
		status = data->AddInt32("_port_cap", info.capacity);

	thread_info threadInfo;
	if (get_thread_info(Thread(), &threadInfo) == B_OK)
		status = data->AddInt32("_prio", threadInfo.priority);

	return status;
}


/** @brief Post a message to this looper by command code.
 *  @param command The message command code.
 *  @return B_OK on success, or an error code on failure.
 *  @note A temporary BMessage is constructed from \a command and posted to this
 *        looper's own handler.
 *  @see PostMessage(BMessage*)
 *  @see _PostMessage()
 */
status_t
BLooper::PostMessage(uint32 command)
{
	BMessage message(command);
	return _PostMessage(&message, this, NULL);
}


/** @brief Post a BMessage to this looper.
 *  @param message The message to post. The message is copied; the caller
 *                 retains ownership.
 *  @return B_OK on success, or an error code on failure.
 *  @see PostMessage(uint32)
 *  @see PostMessage(BMessage*, BHandler*, BHandler*)
 */
status_t
BLooper::PostMessage(BMessage* message)
{
	return _PostMessage(message, this, NULL);
}


/** @brief Post a message by command code to a specific handler with a reply target.
 *  @param command The message command code.
 *  @param handler The target handler for the message; must belong to this looper.
 *  @param replyTo The handler to receive any reply, or NULL.
 *  @return B_OK on success, or an error code on failure.
 *  @see PostMessage(BMessage*, BHandler*, BHandler*)
 */
status_t
BLooper::PostMessage(uint32 command, BHandler* handler, BHandler* replyTo)
{
	BMessage message(command);
	return _PostMessage(&message, handler, replyTo);
}


/** @brief Post a BMessage to a specific handler with a reply target.
 *  @param message The message to post. The message is copied; the caller
 *                 retains ownership.
 *  @param handler The target handler for the message; must belong to this looper.
 *  @param replyTo The handler to receive any reply, or NULL.
 *  @return B_OK on success, or an error code on failure.
 *  @see _PostMessage()
 */
status_t
BLooper::PostMessage(BMessage* message, BHandler* handler, BHandler* replyTo)
{
	return _PostMessage(message, handler, replyTo);
}


/** @brief Dispatch a message to a target handler.
 *  @param message The message to dispatch.
 *  @param handler The handler that should process the message.
 *  @note Handles _QUIT_ internally by setting the terminating flag. For
 *        B_QUIT_REQUESTED directed at the looper itself, calls _QuitRequested().
 *        All other messages are forwarded to handler->MessageReceived().
 *  @see MessageReceived()
 *  @see _QuitRequested()
 */
void
BLooper::DispatchMessage(BMessage* message, BHandler* handler)
{
	PRINT(("BLooper::DispatchMessage(%.4s)\n", (char*)&message->what));

	switch (message->what) {
		case _QUIT_:
			// Can't call Quit() to do this, because of the slight chance
			// another thread with have us locked between now and then.
			fTerminating = true;

			// After returning from DispatchMessage(), the looper will be
			// deleted in _task0_()
			break;

		case B_QUIT_REQUESTED:
			if (handler == this) {
				_QuitRequested(message);
				break;
			}

			// fall through

		default:
			handler->MessageReceived(message);
			break;
	}
	PRINT(("BLooper::DispatchMessage() done\n"));
}


/** @brief Handle an incoming message for the looper itself.
 *  @param message The message to process.
 *  @note Handles scripting property queries for "Handlers" (GET) and "Handler"
 *        (COUNT). Messages without specifiers or unrecognized scripting messages
 *        are forwarded to BHandler::MessageReceived().
 *  @see BHandler::MessageReceived()
 *  @see ResolveSpecifier()
 */
void
BLooper::MessageReceived(BMessage* message)
{
	if (!message->HasSpecifiers()) {
		BHandler::MessageReceived(message);
		return;
	}

	BMessage replyMsg(B_REPLY);
	status_t err = B_BAD_SCRIPT_SYNTAX;
	int32 index;
	BMessage specifier;
	int32 what;
	const char* property;

	if (message->GetCurrentSpecifier(&index, &specifier, &what, &property)
			!= B_OK) {
		return BHandler::MessageReceived(message);
	}

	BPropertyInfo propertyInfo(sLooperPropInfo);
	switch (propertyInfo.FindMatch(message, index, &specifier, what,
			property)) {
		case 1: // Handlers: GET
			if (message->what == B_GET_PROPERTY) {
				int32 count = CountHandlers();
				err = B_OK;
				for (int32 i = 0; err == B_OK && i < count; i++) {
					BMessenger messenger(HandlerAt(i));
					err = replyMsg.AddMessenger("result", messenger);
				}
			}
			break;
		case 2: // Handler: COUNT
			if (message->what == B_COUNT_PROPERTIES)
				err = replyMsg.AddInt32("result", CountHandlers());
			break;

		default:
			return BHandler::MessageReceived(message);
	}

	if (err != B_OK) {
		replyMsg.what = B_MESSAGE_NOT_UNDERSTOOD;

		if (err == B_BAD_SCRIPT_SYNTAX)
			replyMsg.AddString("message", "Didn't understand the specifier(s)");
		else
			replyMsg.AddString("message", strerror(err));
	}

	replyMsg.AddInt32("error", err);
	message->SendReply(&replyMsg);
}


/** @brief Return the message currently being dispatched.
 *  @return The current BMessage, or NULL if no message is being dispatched.
 *  @note The returned pointer is valid only during the current dispatch cycle.
 *  @see DetachCurrentMessage()
 */
BMessage*
BLooper::CurrentMessage() const
{
	return fLastMessage;
}


/** @brief Detach and return the message currently being dispatched.
 *  @return The current BMessage. The caller takes ownership. Returns NULL if
 *          no message is being dispatched.
 *  @note After calling this, CurrentMessage() will return NULL and the looper
 *        will not delete the message when dispatch completes.
 *  @see CurrentMessage()
 */
BMessage*
BLooper::DetachCurrentMessage()
{
	BMessage* message = fLastMessage;
	fLastMessage = NULL;
	return message;
}


/** @brief Dispatch a message that originated outside the normal looper queue.
 *  @param message The externally supplied message to dispatch.
 *  @param handler The handler that should process the message.
 *  @param _detached Set to true on return if the message was detached during
 *                   dispatch (i.e., DetachCurrentMessage() was called).
 *  @note The looper must be locked. The previous fLastMessage is saved and
 *        restored after dispatch.
 *  @see DispatchMessage()
 *  @see DetachCurrentMessage()
 */
void
BLooper::DispatchExternalMessage(BMessage* message, BHandler* handler,
	bool& _detached)
{
	AssertLocked();

	BMessage* previousMessage = fLastMessage;
	fLastMessage = message;

	DispatchMessage(message, handler);

	_detached = fLastMessage == NULL;
	fLastMessage = previousMessage;
}


/** @brief Return the looper's message queue.
 *  @return The BMessageQueue used by this looper's direct target.
 *  @see IsMessageWaiting()
 */
BMessageQueue*
BLooper::MessageQueue() const
{
	return fDirectTarget->Queue();
}


/** @brief Check whether there are messages waiting to be processed.
 *  @return true if the message queue is not empty or there is data on the
 *          message port, false otherwise.
 *  @note The looper must be locked before calling this method.
 *  @see MessageQueue()
 */
bool
BLooper::IsMessageWaiting() const
{
	AssertLocked();

	if (!fDirectTarget->Queue()->IsEmpty())
		return true;

	int32 count;
	do {
		count = port_buffer_size_etc(fMsgPort, B_RELATIVE_TIMEOUT, 0);
	} while (count == B_INTERRUPTED);

	return count > 0;
}


/** @brief Add a handler to this looper.
 *  @param handler The handler to add. Must not already belong to another looper.
 *                 If NULL, this method does nothing.
 *  @note The looper must be locked. The handler's next handler is set to this
 *        looper (unless the handler is the looper itself, to avoid a cycle).
 *  @see RemoveHandler()
 *  @see CountHandlers()
 */
void
BLooper::AddHandler(BHandler* handler)
{
	if (handler == NULL)
		return;

	AssertLocked();

	if (handler->Looper() == NULL) {
		fHandlers.AddItem(handler);
		handler->SetLooper(this);
		if (handler != this)	// avoid a cycle
			handler->SetNextHandler(this);
	}
}


/** @brief Remove a handler from this looper.
 *  @param handler The handler to remove. If NULL, returns false.
 *  @return true if the handler was found and removed, false otherwise.
 *  @note The looper must be locked. If the removed handler is the preferred
 *        handler, the preferred handler is cleared. The handler's looper and
 *        next handler pointers are reset to NULL.
 *  @see AddHandler()
 *  @see SetPreferredHandler()
 */
bool
BLooper::RemoveHandler(BHandler* handler)
{
	if (handler == NULL)
		return false;

	AssertLocked();

	if (handler->Looper() == this && fHandlers.RemoveItem(handler)) {
		if (handler == fPreferred)
			fPreferred = NULL;

		handler->SetNextHandler(NULL);
		handler->SetLooper(NULL);
		return true;
	}

	return false;
}


/** @brief Return the number of handlers attached to this looper.
 *  @return The handler count.
 *  @note The looper must be locked.
 *  @see AddHandler()
 *  @see HandlerAt()
 */
int32
BLooper::CountHandlers() const
{
	AssertLocked();

	return fHandlers.CountItems();
}


/** @brief Return the handler at the given index.
 *  @param index Zero-based index of the handler.
 *  @return The BHandler at \a index, or NULL if the index is out of range.
 *  @note The looper must be locked.
 *  @see CountHandlers()
 *  @see IndexOf()
 */
BHandler*
BLooper::HandlerAt(int32 index) const
{
	AssertLocked();

	return (BHandler*)fHandlers.ItemAt(index);
}


/** @brief Return the index of a handler in this looper's handler list.
 *  @param handler The handler to look up.
 *  @return The zero-based index of \a handler, or a negative value if not found.
 *  @note The looper must be locked.
 *  @see HandlerAt()
 *  @see CountHandlers()
 */
int32
BLooper::IndexOf(BHandler* handler) const
{
	AssertLocked();

	return fHandlers.IndexOf(handler);
}


/** @brief Return the preferred handler for this looper.
 *  @return The preferred BHandler, or NULL if none is set.
 *  @note Messages without a specific target handler are dispatched to the
 *        preferred handler.
 *  @see SetPreferredHandler()
 */
BHandler*
BLooper::PreferredHandler() const
{
	return fPreferred;
}


/** @brief Set the preferred handler for this looper.
 *  @param handler The handler to designate as preferred. Must belong to this
 *                 looper and be in the handler list. Pass NULL to clear.
 *  @note If \a handler does not belong to this looper or is not in the handler
 *        list, the preferred handler is set to NULL.
 *  @see PreferredHandler()
 */
void
BLooper::SetPreferredHandler(BHandler* handler)
{
	if (handler && handler->Looper() == this && IndexOf(handler) >= 0) {
		fPreferred = handler;
	} else {
		fPreferred = NULL;
	}
}


/** @brief Spawn the message loop thread and start processing messages.
 *  @return The thread_id of the newly spawned thread, or a negative error code.
 *  @note The looper must be locked when Run() is called. It is unlocked before
 *        the thread is resumed. Calling Run() more than once triggers a
 *        debugger call.
 *  @see Loop()
 *  @see Quit()
 *  @see _task0_()
 */
thread_id
BLooper::Run()
{
	AssertLocked();

	if (fRunCalled) {
		// Not allowed to call Run() more than once
		debugger("can't call BLooper::Run twice!");
		return fThread;
	}

	fThread = spawn_thread(_task0_, Name(), fInitPriority, this);
	if (fThread < B_OK)
		return fThread;

	if (fMsgPort < B_OK)
		return fMsgPort;

	fRunCalled = true;
	Unlock();

	status_t err = resume_thread(fThread);
	if (err < B_OK)
		return err;

	return fThread;
}


/** @brief Run the message loop in the calling thread.
 *  @note Unlike Run(), this does not spawn a new thread. The calling thread
 *        becomes the looper thread and blocks in the message loop. The looper
 *        must be locked. Calling Loop() or Run() more than once triggers a
 *        debugger call.
 *  @see Run()
 *  @see task_looper()
 */
void
BLooper::Loop()
{
	AssertLocked();

	if (fRunCalled) {
		// Not allowed to call Loop() or Run() more than once
		debugger("can't call BLooper::Loop twice!");
		return;
	}

	fThread = find_thread(NULL);
	fRunCalled = true;

	task_looper();
}


/** @brief Request the looper to quit.
 *
 *  If called before Run(), deletes the looper immediately. If called from the
 *  looper's own thread, sets the terminating flag and deletes the looper. If
 *  called from another thread, posts a _QUIT_ message and waits for the looper
 *  thread to finish processing remaining messages and exit.
 *
 *  @note The looper must be locked before calling Quit(). When called from
 *        another thread, QuitRequested() is not invoked -- the looper simply
 *        terminates after draining its queue.
 *  @see QuitRequested()
 *  @see Run()
 */
void
BLooper::Quit()
{
	PRINT(("BLooper::Quit()\n"));

	if (!IsLocked()) {
		printf("ERROR - you must Lock a looper before calling Quit(), "
			"team=%" B_PRId32 ", looper=%s\n", Team(),
			Name() ? Name() : "unnamed");
	}

	// Try to lock
	if (!Lock()) {
		// We're toast already
		return;
	}

	PRINT(("  is locked\n"));

	if (!fRunCalled) {
		PRINT(("  Run() has not been called yet\n"));
		fTerminating = true;
		delete this;
	} else if (find_thread(NULL) == fThread) {
		PRINT(("  We are the looper thread\n"));
		fTerminating = true;
		delete this;
		exit_thread(0);
	} else {
		PRINT(("  Run() has already been called and we are not the looper thread\n"));

		// As with sem in _Lock(), we need to cache this here in case the looper
		// disappears before we get to the wait_for_thread() below
		thread_id thread = Thread();

		// We need to unlock here. Otherwise the looper thread can't
		// dispatch the _QUIT_ message we're going to post.
		UnlockFully();

		// As per the BeBook, if we've been called by a thread other than
		// our own, the rest of the message queue has to get processed.  So
		// we put this in the queue, and when it shows up, we'll call Quit()
		// from our own thread.
		// QuitRequested() will not be called in this case.
		PostMessage(_QUIT_);

		// We have to wait until the looper is done processing any remaining
		// messages.
		status_t status;
		while (wait_for_thread(thread, &status) == B_INTERRUPTED)
			;
	}

	PRINT(("BLooper::Quit() done\n"));
}


/** @brief Hook called to determine whether the looper may quit.
 *  @return true to allow quitting, false to deny. The default implementation
 *          always returns true.
 *  @note Subclasses can override this to perform cleanup or deny quit requests.
 *  @see Quit()
 *  @see _QuitRequested()
 */
bool
BLooper::QuitRequested()
{
	return true;
}


/** @brief Lock the looper.
 *  @return true if the lock was acquired, false on failure (e.g., the looper
 *          has been deleted).
 *  @note Supports nested locking -- a thread that already holds the lock can
 *        call Lock() again without deadlocking.
 *  @see Unlock()
 *  @see LockWithTimeout()
 *  @see _Lock()
 */
bool
BLooper::Lock()
{
	// Defer to global _Lock(); see notes there
	return _Lock(this, -1, B_INFINITE_TIMEOUT) == B_OK;
}


/** @brief Unlock the looper.
 *  @note Must be called once for each successful Lock() call. When the owner
 *        count reaches zero, the lock semaphore is released so that other
 *        threads may acquire it.
 *  @see Lock()
 *  @see LockWithTimeout()
 */
void
BLooper::Unlock()
{
PRINT(("BLooper::Unlock()\n"));
	//	Make sure we're locked to begin with
	AssertLocked();

	//	Decrement fOwnerCount
	--fOwnerCount;
PRINT(("  fOwnerCount now: %ld\n", fOwnerCount));
	//	Check to see if the owner still wants a lock
	if (fOwnerCount == 0) {
		//	Set fOwner to invalid thread_id (< 0)
		fOwner = -1;
		fCachedStack = 0;

#if DEBUG < 1
		//	Decrement requested lock count (using fAtomicCount for this)
		int32 atomicCount = atomic_add(&fAtomicCount, -1);
PRINT(("  fAtomicCount now: %ld\n", fAtomicCount));

		// Check if anyone is waiting for a lock
		// and release if it's the case
		if (atomicCount > 1)
#endif
			release_sem(fLockSem);
	}
PRINT(("BLooper::Unlock() done\n"));
}


/** @brief Check whether the looper is locked by the calling thread.
 *  @return true if the calling thread holds the lock, false otherwise (including
 *          if the looper has been deleted).
 *  @note Uses both a cached stack page check and a thread ID comparison for
 *        fast lock verification.
 *  @see Lock()
 */
bool
BLooper::IsLocked() const
{
	if (!gLooperList.IsLooperValid(this)) {
		// The looper is gone, so of course it's not locked
		return false;
	}

	uint32 stack;
	return ((addr_t)&stack & ~(B_PAGE_SIZE - 1)) == fCachedStack
		|| find_thread(NULL) == fOwner;
}


/** @brief Lock the looper with a timeout.
 *  @param timeout Maximum time in microseconds to wait for the lock.
 *  @return B_OK if the lock was acquired, or an error code (e.g., B_TIMED_OUT).
 *  @see Lock()
 *  @see Unlock()
 *  @see _Lock()
 */
status_t
BLooper::LockWithTimeout(bigtime_t timeout)
{
	return _Lock(this, -1, timeout);
}


/** @brief Return the thread ID of the looper's message loop thread.
 *  @return The thread_id, or B_ERROR if the looper has not been started.
 *  @see Run()
 *  @see Team()
 */
thread_id
BLooper::Thread() const
{
	return fThread;
}


/** @brief Return the team ID of the process that owns this looper.
 *  @return The team_id of the current team.
 *  @see Thread()
 */
team_id
BLooper::Team() const
{
	return BPrivate::current_team();
}


/** @brief Find the looper whose message loop is running on the given thread.
 *  @param thread The thread_id to look up.
 *  @return The BLooper running on \a thread, or NULL if none is found.
 *  @see Thread()
 */
BLooper*
BLooper::LooperForThread(thread_id thread)
{
	return gLooperList.LooperForThread(thread);
}


/** @brief Return the thread that currently holds the looper lock.
 *  @return The thread_id of the locking thread, or a negative value if unlocked.
 *  @see Lock()
 *  @see CountLocks()
 */
thread_id
BLooper::LockingThread() const
{
	return fOwner;
}


/** @brief Return the number of nested locks held by the owning thread.
 *  @return The current lock nesting count.
 *  @see Lock()
 *  @see LockingThread()
 */
int32
BLooper::CountLocks() const
{
	return fOwnerCount;
}


/** @brief Return the total number of pending lock requests.
 *  @return The atomic count of threads waiting for or holding the lock.
 *  @see CountLocks()
 *  @see Sem()
 */
int32
BLooper::CountLockRequests() const
{
	return fAtomicCount;
}


/** @brief Return the semaphore used for locking this looper.
 *  @return The sem_id of the lock semaphore.
 *  @see Lock()
 *  @see CountLockRequests()
 */
sem_id
BLooper::Sem() const
{
	return fLockSem;
}


/** @brief Determine the handler for a scripting message.
 *  @param message The scripting message being resolved.
 *  @param index Current index into the specifier stack.
 *  @param specifier The current specifier message.
 *  @param what The specifier form (e.g., B_DIRECT_SPECIFIER,
 *              B_INDEX_SPECIFIER).
 *  @param property The property name being targeted.
 *  @return A pointer to the BHandler that should handle the message, or NULL if
 *          the specifier could not be resolved.
 *  @note Handles "Handler" (by index) and looper-internal properties. Falls
 *        through to BHandler::ResolveSpecifier() for unrecognized properties.
 *  @see GetSupportedSuites()
 *  @see BHandler::ResolveSpecifier()
 */
BHandler*
BLooper::ResolveSpecifier(BMessage* message, int32 index, BMessage* specifier,
	int32 what, const char* property)
{
/**
	@note	When I was first dumping the results of GetSupportedSuites() from
			various classes, the use of the extra_data field was quite
			mysterious to me.  Then I dumped BApplication and compared the
			result against the BeBook's docs for scripting BApplication.  A
			bunch of it isn't documented, but what is tipped me to the idea
			that the extra_data is being used as a quick and dirty way to tell
			what scripting "command" has been sent, e.g., for easy use in a
			switch statement.  Would certainly be a lot faster than a bunch of
			string comparisons -- which wouldn't tell the whole story anyway,
			because of the same name being used for multiple properties.
 */
 	BPropertyInfo propertyInfo(sLooperPropInfo);
	uint32 data;
	status_t err = B_OK;
	const char* errMsg = "";
	if (propertyInfo.FindMatch(message, index, specifier, what, property, &data)
			>= 0) {
		switch (data) {
			case BLOOPER_PROCESS_INTERNALLY:
				return this;

			case BLOOPER_HANDLER_BY_INDEX:
			{
				int32 index = specifier->FindInt32("index");
				if (what == B_REVERSE_INDEX_SPECIFIER) {
					index = CountHandlers() - index;
				}
				BHandler* target = HandlerAt(index);
				if (target) {
					// Specifier has been fully handled
					message->PopSpecifier();
					return target;
				} else {
					err = B_BAD_INDEX;
					errMsg = "handler index out of range";
				}
				break;
			}

			default:
				err = B_BAD_SCRIPT_SYNTAX;
				errMsg = "Didn't understand the specifier(s)";
		}
	} else {
		return BHandler::ResolveSpecifier(message, index, specifier, what,
			property);
	}

	BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
	reply.AddInt32("error", err);
	reply.AddString("message", errMsg);
	message->SendReply(&reply);

	return NULL;
}


/** @brief Report the scripting suites supported by this looper.
 *  @param data The message to populate with suite information. Receives a
 *              "suites" string field ("suite/vnd.Be-looper") and a "messages"
 *              flattened BPropertyInfo. Also includes suites from
 *              BHandler::GetSupportedSuites().
 *  @return B_OK on success, B_BAD_VALUE if \a data is NULL, or another error
 *          code on failure.
 *  @see ResolveSpecifier()
 *  @see BHandler::GetSupportedSuites()
 */
status_t
BLooper::GetSupportedSuites(BMessage* data)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = data->AddString("suites", "suite/vnd.Be-looper");
	if (status == B_OK) {
		BPropertyInfo PropertyInfo(sLooperPropInfo);
		status = data->AddFlat("messages", &PropertyInfo);
		if (status == B_OK)
			status = BHandler::GetSupportedSuites(data);
	}

	return status;
}


/** @brief Add a common message filter to this looper.
 *  @param filter The message filter to add. Must not already belong to another
 *                looper. The looper takes ownership. If NULL, does nothing.
 *  @note The looper must be locked. Common filters are applied to all messages
 *        before handler-specific filters.
 *  @see RemoveCommonFilter()
 *  @see SetCommonFilterList()
 *  @see CommonFilterList()
 */
void
BLooper::AddCommonFilter(BMessageFilter* filter)
{
	if (filter == NULL)
		return;

	AssertLocked();

	if (filter->Looper()) {
		debugger("A MessageFilter can only be used once.");
		return;
	}

	if (fCommonFilters == NULL)
		fCommonFilters = new BList(FILTER_LIST_BLOCK_SIZE);

	filter->SetLooper(this);
	fCommonFilters->AddItem(filter);
}


/** @brief Remove a common message filter from this looper.
 *  @param filter The filter to remove. Ownership is returned to the caller.
 *  @return true if the filter was found and removed, false otherwise.
 *  @note The looper must be locked.
 *  @see AddCommonFilter()
 *  @see SetCommonFilterList()
 */
bool
BLooper::RemoveCommonFilter(BMessageFilter* filter)
{
	AssertLocked();

	if (fCommonFilters == NULL)
		return false;

	bool result = fCommonFilters->RemoveItem(filter);
	if (result)
		filter->SetLooper(NULL);

	return result;
}


/** @brief Replace the entire common filter list for this looper.
 *  @param filters The new list of BMessageFilter pointers, or NULL to clear.
 *                 The looper takes ownership of both the list and its filters.
 *                 Any previously installed common filters are deleted.
 *  @note The looper must be locked. Each filter in \a filters must not already
 *        belong to a looper, otherwise a debugger call is triggered.
 *  @see AddCommonFilter()
 *  @see RemoveCommonFilter()
 *  @see CommonFilterList()
 */
void
BLooper::SetCommonFilterList(BList* filters)
{
	AssertLocked();

	BMessageFilter* filter;
	if (filters) {
		// Check for ownership issues - a filter can only have one owner
		for (int32 i = 0; i < filters->CountItems(); ++i) {
			filter = (BMessageFilter*)filters->ItemAt(i);
			if (filter->Looper()) {
				debugger("A MessageFilter can only be used once.");
				return;
			}
		}
	}

	if (fCommonFilters) {
		for (int32 i = 0; i < fCommonFilters->CountItems(); ++i) {
			delete (BMessageFilter*)fCommonFilters->ItemAt(i);
		}

		delete fCommonFilters;
		fCommonFilters = NULL;
	}

	// Per the BeBook, we take ownership of the list
	fCommonFilters = filters;
	if (fCommonFilters) {
		for (int32 i = 0; i < fCommonFilters->CountItems(); ++i) {
			filter = (BMessageFilter*)fCommonFilters->ItemAt(i);
			filter->SetLooper(this);
		}
	}
}


/** @brief Return the list of common message filters for this looper.
 *  @return The BList of BMessageFilter pointers, or NULL if none are installed.
 *  @see AddCommonFilter()
 *  @see RemoveCommonFilter()
 *  @see SetCommonFilterList()
 */
BList*
BLooper::CommonFilterList() const
{
	return fCommonFilters;
}


/** @brief Reserved virtual hook for binary compatibility.
 *  @param d The perform code identifying the operation.
 *  @param arg Pointer to operation-specific data.
 *  @return The result of BHandler::Perform().
 */
status_t
BLooper::Perform(perform_code d, void* arg)
{
	// This is sort of what we're doing for this function everywhere
	return BHandler::Perform(d, arg);
}


/** @brief Read a message from the looper's port.
 *  @param timeout Maximum time in microseconds to wait for a message.
 *  @return A new BMessage read from the port, or NULL on timeout or error.
 *  @note This is a convenience wrapper around ReadMessageFromPort().
 *  @see ReadMessageFromPort()
 *  @see ReadRawFromPort()
 */
BMessage*
BLooper::MessageFromPort(bigtime_t timeout)
{
	return ReadMessageFromPort(timeout);
}


void BLooper::_ReservedLooper1() {}
void BLooper::_ReservedLooper2() {}
void BLooper::_ReservedLooper3() {}
void BLooper::_ReservedLooper4() {}
void BLooper::_ReservedLooper5() {}
void BLooper::_ReservedLooper6() {}


#ifdef __HAIKU_BEOS_COMPATIBLE
BLooper::BLooper(const BLooper& other)
{
	// Copy construction not allowed
}


BLooper&
BLooper::operator=(const BLooper& other)
{
	// Looper copying not allowed
	return *this;
}
#endif


/** @brief Private constructor used internally to create a looper on an existing
 *         port.
 *  @param priority Thread priority for the looper.
 *  @param port An existing port_id to use, or a negative value to create a new
 *              port.
 *  @param name The looper name.
 *  @note This constructor is not part of the public API.
 *  @see _InitData()
 */
BLooper::BLooper(int32 priority, port_id port, const char* name)
{
	_InitData(name, priority, port, B_LOOPER_PORT_DEFAULT_CAPACITY);
}


/** @brief Internal implementation for all PostMessage() variants.
 *  @param msg The message to post.
 *  @param handler The target handler for the message.
 *  @param replyTo The handler to receive any reply, or NULL.
 *  @return B_OK on success, or an error code on failure.
 *  @note Constructs a BMessenger targeting \a handler within this looper and
 *        sends the message through it.
 *  @see PostMessage()
 */
status_t
BLooper::_PostMessage(BMessage* msg, BHandler* handler, BHandler* replyTo)
{
	status_t status;
	BMessenger messenger(handler, this, &status);
	if (status == B_OK)
		return messenger.SendMessage(msg, replyTo, 0);

	return status;
}


/*!
	Locks a looper either by port or using a direct pointer to the looper.

	\param looper looper to lock, if not NULL
	\param port port to identify the looper in case \a looper is NULL
	\param timeout timeout for acquiring the lock
*/
status_t
BLooper::_Lock(BLooper* looper, port_id port, bigtime_t timeout)
{
	PRINT(("BLooper::_Lock(%p, %lx)\n", looper, port));

	//	Check params (loop, port)
	if (looper == NULL && port < 0) {
		PRINT(("BLooper::_Lock() done 1\n"));
		return B_BAD_VALUE;
	}

	thread_id currentThread = find_thread(NULL);
	int32 oldCount;
	sem_id sem;

	{
		AutoLocker<BLooperList> ListLock(gLooperList);
		if (!ListLock.IsLocked())
			return B_BAD_VALUE;

		// Look up looper by port_id, if necessary
		if (looper == NULL) {
			looper = gLooperList.LooperForPort(port);
			if (looper == NULL) {
				PRINT(("BLooper::_Lock() done 3\n"));
				return B_BAD_VALUE;
			}
		} else if (!gLooperList.IsLooperValid(looper)) {
			// Check looper validity
			PRINT(("BLooper::_Lock() done 4\n"));
			return B_BAD_VALUE;
		}

		// Check for nested lock attempt
		if (currentThread == looper->fOwner) {
			++looper->fOwnerCount;
			PRINT(("BLooper::_Lock() done 5: fOwnerCount: %ld\n", looper->fOwnerCount));
			return B_OK;
		}

		// Cache the semaphore, so that we can safely access it after having
		// unlocked the looper list
		sem = looper->fLockSem;
		if (sem < 0) {
			PRINT(("BLooper::_Lock() done 6\n"));
			return B_BAD_VALUE;
		}

		// Bump the requested lock count (using fAtomicCount for this)
		oldCount = atomic_add(&looper->fAtomicCount, 1);
	}

	return _LockComplete(looper, oldCount, currentThread, sem, timeout);
}


/** @brief Complete the lock acquisition by waiting on the semaphore.
 *  @param looper The looper being locked.
 *  @param oldCount The previous atomic count before this lock request.
 *  @param thread The thread_id of the requesting thread.
 *  @param sem The lock semaphore to acquire.
 *  @param timeout Maximum time in microseconds to wait.
 *  @return B_OK on success, or an error code (e.g., B_TIMED_OUT).
 *  @note On success, sets the looper's owner, cached stack, and owner count.
 *  @see _Lock()
 */
status_t
BLooper::_LockComplete(BLooper* looper, int32 oldCount, thread_id thread,
	sem_id sem, bigtime_t timeout)
{
	status_t err = B_OK;

#if DEBUG < 1
	if (oldCount > 0) {
#endif
		do {
			err = acquire_sem_etc(sem, 1, B_RELATIVE_TIMEOUT, timeout);
		} while (err == B_INTERRUPTED);
#if DEBUG < 1
	}
#endif
	if (err == B_OK) {
		looper->fOwner = thread;
		looper->fCachedStack = (addr_t)&err & ~(B_PAGE_SIZE - 1);
		looper->fOwnerCount = 1;
	}

	PRINT(("BLooper::_LockComplete() done: %lx\n", err));
	return err;
}


/** @brief Initialize the looper's internal data structures.
 *  @param name The looper name (defaults to "anonymous looper" if NULL).
 *  @param priority The thread priority for the message loop.
 *  @param port An existing port_id to use, or a negative value to create one.
 *  @param portCapacity The maximum port message capacity.
 *  @note Called by all constructors. Creates the lock semaphore, the message
 *        port, registers the looper in the global looper list (which also locks
 *        it), and adds this looper as its own first handler.
 *  @see Run()
 */
void
BLooper::_InitData(const char* name, int32 priority, port_id port,
	int32 portCapacity)
{
	fOwner = B_ERROR;
	fCachedStack = 0;
	fRunCalled = false;
	fDirectTarget = new (std::nothrow) BPrivate::BDirectMessageTarget();
	fCommonFilters = NULL;
	fLastMessage = NULL;
	fPreferred = NULL;
	fThread = B_ERROR;
	fTerminating = false;
	fOwnsPort = true;
	fMsgPort = -1;
	fAtomicCount = 0;

	if (name == NULL)
		name = "anonymous looper";

#if DEBUG
	fLockSem = create_sem(1, name);
#else
	fLockSem = create_sem(0, name);
#endif

	if (portCapacity <= 0)
		portCapacity = B_LOOPER_PORT_DEFAULT_CAPACITY;

	if (port >= 0)
		fMsgPort = port;
	else
		fMsgPort = create_port(portCapacity, name);

	fInitPriority = priority;

	gLooperList.AddLooper(this);
		// this will also lock this looper

	AddHandler(this);
}


/** @brief Add a message to the looper's internal message queue.
 *  @param message The message to enqueue.
 *  @note If called from a different thread than the looper's own thread and the
 *        message becomes the next to be processed while the port is empty, a
 *        zero-length write is issued to the port to wake up the looper.
 *  @see _AddMessagePriv()
 */
void
BLooper::AddMessage(BMessage* message)
{
	_AddMessagePriv(message);

	// wakeup looper when being called from other threads if necessary
	if (find_thread(NULL) != Thread()
		&& fDirectTarget->Queue()->IsNextMessage(message)
		&& port_count(fMsgPort) <= 0) {
		// there is currently no message waiting, and we need to wakeup the
		// looper
		write_port_etc(fMsgPort, 0, NULL, 0, B_RELATIVE_TIMEOUT, 0);
	}
}


/** @brief Add a message directly to the internal queue without waking the port.
 *  @param message The message to enqueue.
 *  @note This is the low-level enqueue operation. It does not perform any
 *        port-level wakeup signaling.
 *  @see AddMessage()
 */
void
BLooper::_AddMessagePriv(BMessage* message)
{
	// ToDo: if no target token is specified, set to preferred handler
	// Others may want to peek into our message queue, so the preferred
	// handler must be set correctly already if no token was given

	fDirectTarget->Queue()->AddMessage(message);
}


/** @brief Static thread entry point for the looper's message loop.
 *  @param arg Pointer to the BLooper instance (cast to void*).
 *  @return B_OK always.
 *  @note Locks the looper, calls task_looper() to run the message loop, and
 *        deletes the looper when the loop exits.
 *  @see Run()
 *  @see task_looper()
 */
status_t
BLooper::_task0_(void* arg)
{
	BLooper* looper = (BLooper*)arg;

	PRINT(("LOOPER: _task0_()\n"));

	if (looper->Lock()) {
		PRINT(("LOOPER: looper locked\n"));
		looper->task_looper();

		delete looper;
	}

	PRINT(("LOOPER: _task0_() done: thread %ld\n", find_thread(NULL)));
	return B_OK;
}


/** @brief Read raw data from the looper's message port.
 *  @param msgCode Output parameter receiving the port message code.
 *  @param timeout Maximum time in microseconds to wait for data.
 *  @return A newly allocated buffer containing the raw message data, or NULL on
 *          timeout or error. The caller must free the returned buffer.
 *  @see ReadMessageFromPort()
 *  @see ConvertToMessage()
 */
void*
BLooper::ReadRawFromPort(int32* msgCode, bigtime_t timeout)
{
	PRINT(("BLooper::ReadRawFromPort()\n"));
	uint8* buffer = NULL;
	ssize_t bufferSize;

	do {
		bufferSize = port_buffer_size_etc(fMsgPort, B_RELATIVE_TIMEOUT, timeout);
	} while (bufferSize == B_INTERRUPTED);

	if (bufferSize < B_OK) {
		PRINT(("BLooper::ReadRawFromPort(): failed: %ld\n", bufferSize));
		return NULL;
	}

	if (bufferSize > 0)
		buffer = (uint8*)malloc(bufferSize);

	// we don't want to wait again here, since that can only mean
	// that someone else has read our message and our bufferSize
	// is now probably wrong
	PRINT(("read_port()...\n"));
	bufferSize = read_port_etc(fMsgPort, msgCode, buffer, bufferSize,
		B_RELATIVE_TIMEOUT, 0);

	if (bufferSize < B_OK) {
		free(buffer);
		return NULL;
	}

	PRINT(("BLooper::ReadRawFromPort() read: %.4s, %p (%d bytes)\n",
		(char*)msgCode, buffer, bufferSize));

	return buffer;
}


/** @brief Read a BMessage from the looper's message port.
 *  @param timeout Maximum time in microseconds to wait for a message.
 *  @return A new BMessage read from the port, or NULL on timeout or error.
 *          The caller takes ownership of the returned message.
 *  @note Reads raw data via ReadRawFromPort() and converts it to a BMessage
 *        using ConvertToMessage().
 *  @see ReadRawFromPort()
 *  @see ConvertToMessage()
 *  @see MessageFromPort()
 */
BMessage*
BLooper::ReadMessageFromPort(bigtime_t timeout)
{
	PRINT(("BLooper::ReadMessageFromPort()\n"));
	int32 msgCode;
	BMessage* message = NULL;

	void* buffer = ReadRawFromPort(&msgCode, timeout);
	if (buffer == NULL)
		return NULL;

	message = ConvertToMessage(buffer, msgCode);
	free(buffer);

	PRINT(("BLooper::ReadMessageFromPort() done: %p\n", message));
	return message;
}


/** @brief Convert raw port data into a BMessage.
 *  @param buffer The raw buffer previously read from the port.
 *  @param code The port message code (unused in current implementation).
 *  @return A new BMessage unflattened from \a buffer, or NULL if \a buffer is
 *          NULL or unflattening fails.
 *  @see ReadRawFromPort()
 *  @see ReadMessageFromPort()
 */
BMessage*
BLooper::ConvertToMessage(void* buffer, int32 code)
{
	PRINT(("BLooper::ConvertToMessage()\n"));
	if (buffer == NULL)
		return NULL;

	BMessage* message = new BMessage();
	if (message->Unflatten((const char*)buffer) != B_OK) {
		PRINT(("BLooper::ConvertToMessage(): unflattening message failed\n"));
		delete message;
		message = NULL;
	}

	PRINT(("BLooper::ConvertToMessage(): %p\n", message));
	return message;
}


/** @brief Main message loop implementation.
 *
 *  Unlocks the looper, then enters a two-level loop: the outer loop reads
 *  messages from the port and enqueues them; the inner loop dequeues messages,
 *  resolves target handlers, applies message filters, and dispatches. The loop
 *  continues until the fTerminating flag is set.
 *
 *  @note The looper must be locked on entry; it is unlocked before entering the
 *        loop. The looper is left locked when terminating.
 *  @see _task0_()
 *  @see Run()
 *  @see DispatchMessage()
 *  @see _TopLevelFilter()
 */
void
BLooper::task_looper()
{
	PRINT(("BLooper::task_looper()\n"));
	// Check that looper is locked (should be)
	AssertLocked();
	// Unlock the looper
	Unlock();

	if (IsLocked())
		debugger("looper must not be locked!");

	// loop: As long as we are not terminating.
	while (!fTerminating) {
		PRINT(("LOOPER: outer loop\n"));
		// TODO: timeout determination algo
		//	Read from message port (how do we determine what the timeout is?)
		PRINT(("LOOPER: MessageFromPort()...\n"));
		BMessage* msg = MessageFromPort();
		PRINT(("LOOPER: ...done\n"));

		//	Did we get a message?
		if (msg)
			_AddMessagePriv(msg);

		// Get message count from port
		int32 msgCount = port_count(fMsgPort);
		for (int32 i = 0; i < msgCount; ++i) {
			// Read 'count' messages from port (so we will not block)
			// We use zero as our timeout since we know there is stuff there
			msg = MessageFromPort(0);
			if (msg)
				_AddMessagePriv(msg);
		}

		// loop: As long as there are messages in the queue and the port is
		//		 empty... and we are not terminating, of course.
		bool dispatchNextMessage = true;
		while (!fTerminating && dispatchNextMessage) {
			PRINT(("LOOPER: inner loop\n"));
			// Get next message from queue (assign to fLastMessage after
			// locking)
			BMessage* message = fDirectTarget->Queue()->NextMessage();

			Lock();

			fLastMessage = message;

			if (fLastMessage == NULL) {
				// No more messages: Unlock the looper and terminate the
				// dispatch loop.
				dispatchNextMessage = false;
			} else {
				PRINT(("LOOPER: fLastMessage: 0x%lx: %.4s\n", fLastMessage->what,
					(char*)&fLastMessage->what));
				DBG(fLastMessage->PrintToStream());

				// Get the target handler
				BHandler* handler = NULL;
				BMessage::Private messagePrivate(fLastMessage);
				bool usePreferred = messagePrivate.UsePreferredTarget();

				if (usePreferred) {
					PRINT(("LOOPER: use preferred target\n"));
					handler = fPreferred;
					if (handler == NULL)
						handler = this;
				} else {
					gDefaultTokens.GetToken(messagePrivate.GetTarget(),
						B_HANDLER_TOKEN, (void**)&handler);

					// if this handler doesn't belong to us, we drop the message
					if (handler != NULL && handler->Looper() != this)
						handler = NULL;

					PRINT(("LOOPER: use %ld, handler: %p, this: %p\n",
						messagePrivate.GetTarget(), handler, this));
				}

				// Is this a scripting message? (BMessage::HasSpecifiers())
				if (handler != NULL && fLastMessage->HasSpecifiers()) {
					int32 index = 0;
					// Make sure the current specifier is kosher
					if (fLastMessage->GetCurrentSpecifier(&index) == B_OK)
						handler = resolve_specifier(handler, fLastMessage);
				}

				if (handler) {
					// Do filtering
					handler = _TopLevelFilter(fLastMessage, handler);
					PRINT(("LOOPER: _TopLevelFilter(): %p\n", handler));
					if (handler && handler->Looper() == this)
						DispatchMessage(fLastMessage, handler);
				}
			}

			if (fTerminating) {
				// we leave the looper locked when we quit
				return;
			}

			message = fLastMessage;
			fLastMessage = NULL;

			// Unlock the looper
			Unlock();

			// Delete the current message (fLastMessage)
			if (message != NULL)
				delete message;

			// Are any messages on the port?
			if (port_count(fMsgPort) > 0) {
				// Do outer loop
				dispatchNextMessage = false;
			}
		}
	}
	PRINT(("BLooper::task_looper() done\n"));
}


/** @brief Handle a B_QUIT_REQUESTED message internally.
 *  @param message The quit request message.
 *  @note Calls QuitRequested(). If it returns true, calls Quit(). A reply is
 *        sent to the message source if the source is waiting or the message
 *        contains a "_shutdown_" field from the registrar.
 *  @see QuitRequested()
 *  @see Quit()
 */
void
BLooper::_QuitRequested(BMessage* message)
{
	bool isQuitting = QuitRequested();
	int32 thread = fThread;

	if (isQuitting)
		Quit();

	// We send a reply to the sender, when they're waiting for a reply or
	// if the request message contains a boolean "_shutdown_" field with value
	// true. In the latter case the message came from the registrar, asking
	// the application to shut down.
	bool shutdown;
	if (message->IsSourceWaiting()
		|| (message->FindBool("_shutdown_", &shutdown) == B_OK && shutdown)) {
		BMessage replyMsg(B_REPLY);
		replyMsg.AddBool("result", isQuitting);
		replyMsg.AddInt32("thread", thread);
		message->SendReply(&replyMsg);
	}
}


/** @brief Assert that the looper is locked by the calling thread.
 *  @return true if the looper is locked, false otherwise. On failure, triggers
 *          a debugger call.
 *  @see Lock()
 *  @see IsLocked()
 */
bool
BLooper::AssertLocked() const
{
	if (!IsLocked()) {
		debugger("looper must be locked before proceeding\n");
		return false;
	}

	return true;
}


/** @brief Apply top-level message filtering before dispatch.
 *  @param message The message to filter.
 *  @param target The initially resolved target handler.
 *  @return The final target handler after filtering, or NULL if the message
 *          should be skipped.
 *  @note Applies common looper filters first, then handler-specific filters.
 *        Verifies that the final target belongs to this looper.
 *  @see _HandlerFilter()
 *  @see _ApplyFilters()
 *  @see CommonFilterList()
 */
BHandler*
BLooper::_TopLevelFilter(BMessage* message, BHandler* target)
{
	if (message == NULL)
		return target;

	// Apply the common filters first
	target = _ApplyFilters(CommonFilterList(), message, target);
	if (target) {
		if (target->Looper() != this) {
			debugger("Targeted handler does not belong to the looper.");
			target = NULL;
		} else {
			// Now apply handler-specific filters
			target = _HandlerFilter(message, target);
		}
	}

	return target;
}


/** @brief Apply handler-specific message filters iteratively.
 *  @param message The message to filter.
 *  @param target The target handler whose filters should be applied.
 *  @return The final target handler after filtering, or NULL if the message
 *          should be skipped.
 *  @note Repeatedly applies the target handler's filter list. If a filter
 *        redirects to a different handler, that handler's filters are applied
 *        next. Iteration stops when the target stabilizes or becomes NULL.
 *  @see _TopLevelFilter()
 *  @see _ApplyFilters()
 */
BHandler*
BLooper::_HandlerFilter(BMessage* message, BHandler* target)
{
	// Keep running filters until our handler is NULL, or until the filtering
	// handler returns itself as the designated handler
	BHandler* previousTarget = NULL;
	while (target != NULL && target != previousTarget) {
		previousTarget = target;

		target = _ApplyFilters(target->FilterList(), message, target);
		if (target != NULL && target->Looper() != this) {
			debugger("Targeted handler does not belong to the looper.");
			target = NULL;
		}
	}

	return target;
}


/** @brief Apply a list of message filters to a message.
 *  @param list The filter list to iterate (may be NULL).
 *  @param message The message being filtered.
 *  @param target The current target handler.
 *  @return The (possibly modified) target handler, or NULL if a filter returned
 *          B_SKIP_MESSAGE.
 *  @note For each filter, checks command, delivery, and source conditions. If
 *        all conditions match, the filter's function or Filter() method is
 *        called. Filters may modify the target handler.
 *  @see _TopLevelFilter()
 *  @see _HandlerFilter()
 */
BHandler*
BLooper::_ApplyFilters(BList* list, BMessage* message, BHandler* target)
{
	// This is where the action is!

	// check the parameters
	if (list == NULL || message == NULL)
		return target;

	// for each filter in the provided list
	BMessageFilter* filter = NULL;
	for (int32 i = 0; i < list->CountItems(); ++i) {
		filter = (BMessageFilter*)list->ItemAt(i);

		// check command conditions
		if (filter->FiltersAnyCommand() || filter->Command() == message->what) {
			// check delivery conditions
			message_delivery delivery = filter->MessageDelivery();
			bool dropped = message->WasDropped();
			if (delivery == B_ANY_DELIVERY
				|| (delivery == B_DROPPED_DELIVERY && dropped)
				|| (delivery == B_PROGRAMMED_DELIVERY && !dropped)) {
				// check source conditions
				message_source source = filter->MessageSource();
				bool remote = message->IsSourceRemote();
				if (source == B_ANY_SOURCE
					|| (source == B_REMOTE_SOURCE && remote)
					|| (source == B_LOCAL_SOURCE && !remote)) {
					// Are we using an "external" function?
					filter_result result;
					filter_hook filterFunction = filter->FilterFunction();
					if (filterFunction != NULL)
						result = filterFunction(message, &target, filter);
					else
						result = filter->Filter(message, &target);

					// Is further processing allowed?
					if (result == B_SKIP_MESSAGE) {
						// no, time to bail out
						return NULL;
					}
				}
			}
		}
	}

	return target;
}


/** @brief Lightweight lock assertion for use within handler dispatch.
 *  @note This is a cheaper alternative to AssertLocked() that avoids a call to
 *        gLooperList.IsLooperValid(). It is safe to use when the looper is
 *        known to be valid (e.g., during message dispatch). Triggers a debugger
 *        call if the lock is not held.
 *  @see AssertLocked()
 */
void
BLooper::check_lock()
{
	// this is a cheap variant of AssertLocked()
	// it is used in situations where it's clear that the looper is valid,
	// i.e. from handlers
	uint32 stack;
	if (((addr_t)&stack & ~(B_PAGE_SIZE - 1)) == fCachedStack
		|| fOwner == find_thread(NULL)) {
		return;
	}

	debugger("Looper must be locked.");
}


/** @brief Iteratively resolve nested scripting specifiers to a final handler.
 *  @param target The initial target handler.
 *  @param message The scripting message containing the specifier stack.
 *  @return The final resolved BHandler, or NULL if resolution fails or the
 *          resolved handler does not belong to this looper.
 *  @note Loops through the specifier stack calling ResolveSpecifier() on each
 *        handler until the target stabilizes or the specifier stack is
 *        exhausted.
 *  @see ResolveSpecifier()
 */
BHandler*
BLooper::resolve_specifier(BHandler* target, BMessage* message)
{
	// check params
	if (!target || !message)
		return NULL;

	int32 index;
	BMessage specifier;
	int32 form;
	const char* property;
	status_t err = B_OK;
	BHandler* newTarget = target;
	// loop to deal with nested specifiers
	// (e.g., the 3rd button on the 4th view)
	do {
		err = message->GetCurrentSpecifier(&index, &specifier, &form,
			&property);
		if (err != B_OK) {
			BMessage reply(B_REPLY);
			reply.AddInt32("error", err);
			message->SendReply(&reply);
			return NULL;
		}
		// current target gets what was the new target
		target = newTarget;
		newTarget = target->ResolveSpecifier(message, index, &specifier, form,
			property);
		// check that new target is owned by looper; use IndexOf() to avoid
		// dereferencing newTarget (possible race condition with object
		// destruction by another looper)
		if (newTarget == NULL || IndexOf(newTarget) < 0)
			return NULL;

		// get current specifier index (may change in ResolveSpecifier())
		err = message->GetCurrentSpecifier(&index);
	} while (newTarget && newTarget != target && err == B_OK && index >= 0);

	return newTarget;
}


/** @brief Release all nested locks, fully unlocking the looper.
 *  @note Must be called while the lock is actually held. Resets the owner count
 *        and owner thread, then releases the lock semaphore if other threads
 *        are waiting.
 *  @see Lock()
 *  @see Unlock()
 */
void
BLooper::UnlockFully()
{
	AssertLocked();

	// Clear the owner count
	fOwnerCount = 0;
	// Nobody owns the lock now
	fOwner = -1;
	fCachedStack = 0;
#if DEBUG < 1
	// There is now one less thread holding a lock on this looper
	int32 atomicCount = atomic_add(&fAtomicCount, -1);
	if (atomicCount > 1)
#endif
		release_sem(fLockSem);
}


//	#pragma mark -


/** @brief Return the message port of a BLooper.
 *  @param looper The looper whose port to retrieve.
 *  @return The port_id of the looper's message port.
 *  @note This is an internal helper function, not part of the public API.
 */
port_id
_get_looper_port_(const BLooper* looper)
{
	return looper->fMsgPort;
}
