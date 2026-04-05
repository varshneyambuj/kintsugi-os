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
 *   Copyright 2001-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Erik Jaesler, erik@cgsoftware.com
 */

/**
 * @file Handler.cpp
 * @brief Implementation of BHandler, the base class for message handling in the
 *        application framework.
 *
 * BHandler provides the foundation for receiving and dispatching BMessage
 * objects. Handlers are typically attached to a BLooper, which delivers
 * messages to them. This file also contains the BPrivate::ObserverList helper
 * class used to implement the observer notification pattern.
 */


#include <TokenSpace.h>

#include <AppDefs.h>
#include <Handler.h>
#include <Looper.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Messenger.h>
#include <PropertyInfo.h>

#include <algorithm>
#include <new>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <vector>

using std::map;
using std::vector;
using BPrivate::gDefaultTokens;


/** @brief Archive field name used to store the handler's name. */
static const char* kArchiveNameField = "_name";

/** @brief Internal message code used to register a new observer. */
static const uint32 kMsgStartObserving = '_OBS';
/** @brief Internal message code used to unregister an existing observer. */
static const uint32 kMsgStopObserving = '_OBP';
/** @brief Field name carrying the messenger target in observer messages. */
static const char* kObserveTarget = "be:observe_target";


static property_info sHandlerPropInfo[] = {
	{
		"Suites",					// name
		{ B_GET_PROPERTY },			// commands
		{ B_DIRECT_SPECIFIER },		// specifiers
		NULL,						// usage
		0,							// extra data
		{ 0 },						// types
		{							// ctypes (compound_type)
			{						// ctypes[0]
				{					// pairs[0]
					{
						"suites",		// name
						B_STRING_TYPE	// type
					}
				}
			},
			{						// ctypes[1]
				{					// pairs[0]
					{
						"messages",
						B_PROPERTY_INFO_TYPE
					}
				}
			}
		},
		{}		// reserved
	},
	{
		"Messenger",
			{ B_GET_PROPERTY },
			{ B_DIRECT_SPECIFIER },
			NULL, 0,
			{ B_MESSENGER_TYPE },
			{},
			{}
	},
	{
		"InternalName",
			{ B_GET_PROPERTY },
			{ B_DIRECT_SPECIFIER },
			NULL, 0,
			{ B_STRING_TYPE },
			{},
			{}
	},

	{ 0 }
};

/** @brief Callback that deletes a BMessageFilter; used with BList::DoForEach().
 *  @param filter Pointer to the BMessageFilter to delete.
 *  @return Always returns false so iteration continues through the entire list.
 */
bool FilterDeleter(void* filter);

namespace BPrivate {

class ObserverList {
	public:
		ObserverList();
		~ObserverList();

		status_t SendNotices(uint32 what, const BMessage* notice);
		status_t Add(const BHandler* handler, uint32 what);
		status_t Add(const BMessenger& messenger, uint32 what);
		status_t Remove(const BHandler* handler, uint32 what);
		status_t Remove(const BMessenger& messenger, uint32 what);
		bool IsEmpty();

	private:
		typedef map<uint32, vector<const BHandler*> > HandlerObserverMap;
		typedef map<uint32, vector<BMessenger> > MessengerObserverMap;

		void _ValidateHandlers(uint32 what);
		void _SendNotices(uint32 what, BMessage* notice);

		HandlerObserverMap		fHandlerMap;
		MessengerObserverMap	fMessengerMap;
};

}	// namespace BPrivate

using namespace BPrivate;


//	#pragma mark -


/** @brief Construct a BHandler with an optional name.
 *  @param name Human-readable name for this handler, or NULL for unnamed.
 *  @note The handler is not attached to any looper after construction; it must
 *        be added to a BLooper via BLooper::AddHandler() before it can receive
 *        messages.
 *  @see BLooper::AddHandler()
 */
BHandler::BHandler(const char* name)
	: BArchivable(),
	fName(NULL)
{
	_InitData(name);
}


/** @brief Destroy the BHandler.
 *
 *  Removes the handler from its looper (if any), deletes all owned message
 *  filters and the observer list, and releases the token allocated during
 *  construction.
 *
 *  @note The handler's looper is locked and unlocked during destruction if
 *        the handler still belongs to one.
 *  @see BLooper::RemoveHandler()
 */
BHandler::~BHandler()
{
	if (LockLooper()) {
		BLooper* looper = Looper();
		looper->RemoveHandler(this);
		looper->Unlock();
	}

	// remove all filters
	if (fFilters) {
		int32 count = fFilters->CountItems();
		for (int32 i = 0; i < count; i++)
			delete (BMessageFilter*)fFilters->ItemAtFast(i);
		delete fFilters;
	}

	// remove all observers (the observer list manages itself)
	delete fObserverList;

	// free rest
	free(fName);
	gDefaultTokens.RemoveToken(fToken);
}


/** @brief Construct a BHandler from an archived BMessage.
 *  @param data The archive message containing the handler's state. The handler
 *              name is read from the "_name" field if present.
 *  @note This is the unarchiving constructor used by Instantiate().
 *  @see Instantiate()
 *  @see Archive()
 */
BHandler::BHandler(BMessage* data)
	: BArchivable(data),
	fName(NULL)
{
	const char* name = NULL;

	if (data)
		data->FindString(kArchiveNameField, &name);

	_InitData(name);
}


/** @brief Create a new BHandler from an archived BMessage.
 *  @param data The archive message to instantiate from.
 *  @return A new BHandler if \a data is a valid BHandler archive, or NULL if
 *          validation fails.
 *  @see Archive()
 */
BArchivable*
BHandler::Instantiate(BMessage* data)
{
	if (!validate_instantiation(data, "BHandler"))
		return NULL;

	return new BHandler(data);
}


/** @brief Archive the BHandler into a BMessage.
 *  @param data The message to archive into.
 *  @param deep If true, child objects are archived as well (unused for
 *              BHandler, passed to BArchivable).
 *  @return B_OK on success, or an error code on failure.
 *  @see Instantiate()
 */
status_t
BHandler::Archive(BMessage* data, bool deep) const
{
	status_t status = BArchivable::Archive(data, deep);
	if (status < B_OK)
		return status;

	if (fName == NULL)
		return B_OK;

	return data->AddString(kArchiveNameField, fName);
}


/** @brief Handle an incoming message.
 *  @param message The message to process.
 *
 *  Default implementation handles observer registration/deregistration,
 *  scripting property queries (B_GET_PROPERTY for Messenger, Suites, and
 *  InternalName), and B_GET_SUPPORTED_SUITES. Unrecognized messages are
 *  forwarded to the next handler in the chain, or replied to with
 *  B_MESSAGE_NOT_UNDERSTOOD if no next handler exists.
 *
 *  @note Subclasses should call the base implementation for any messages they
 *        do not handle.
 *  @see SetNextHandler()
 *  @see GetSupportedSuites()
 */
void
BHandler::MessageReceived(BMessage* message)
{
	BMessage reply(B_REPLY);

	switch (message->what) {
		case kMsgStartObserving:
		case kMsgStopObserving:
		{
			BMessenger target;
			uint32 what;
			if (message->FindMessenger(kObserveTarget, &target) != B_OK
				|| message->FindInt32(B_OBSERVE_WHAT_CHANGE, (int32*)&what)
					!= B_OK) {
				break;
			}

			ObserverList* list = _ObserverList();
			if (list != NULL) {
				if (message->what == kMsgStartObserving)
					list->Add(target, what);
				else
					list->Remove(target, what);
			}
			break;
		}

		case B_GET_PROPERTY:
		{
			int32 cur;
			BMessage specifier;
			int32 form;
			const char* prop;

			status_t err = message->GetCurrentSpecifier(&cur, &specifier,
				&form, &prop);
			if (err != B_OK && err != B_BAD_SCRIPT_SYNTAX)
				break;
			bool known = false;
			// B_BAD_SCRIPT_SYNTAX defaults to the Messenger property
			if (err == B_BAD_SCRIPT_SYNTAX || cur < 0
				|| (strcmp(prop, "Messenger") == 0)) {
				err = reply.AddMessenger("result", this);
				known = true;
			} else if (strcmp(prop, "Suites") == 0) {
				err = GetSupportedSuites(&reply);
				known = true;
			} else if (strcmp(prop, "InternalName") == 0) {
				err = reply.AddString("result", Name());
				known = true;
			}

			if (known) {
				reply.AddInt32("error", B_OK);
				message->SendReply(&reply);
				return;
			}
			// let's try next handler
			break;
		}

		case B_GET_SUPPORTED_SUITES:
		{
			reply.AddInt32("error", GetSupportedSuites(&reply));
			message->SendReply(&reply);
			return;
		}
	}

	// ToDo: there is some more work needed here
	// (someone in the know should fill in)!

	if (fNextHandler) {
		// we need to apply the next handler's filters here, too
		BHandler* target = Looper()->_HandlerFilter(message, fNextHandler);
		if (target != NULL && target != this) {
			// TODO: we also need to make sure that "target" is not before
			//	us in the handler chain - at least in case it wasn't before
			//	the handler actually targeted with this message - this could
			//	get ugly, though.
			target->MessageReceived(message);
		}
	} else if (message->what != B_MESSAGE_NOT_UNDERSTOOD
		&& (message->WasDropped() || message->HasSpecifiers())) {
		printf("BHandler %s: MessageReceived() couldn't understand the message:\n", Name());
		message->PrintToStream();
		message->SendReply(B_MESSAGE_NOT_UNDERSTOOD);
	}
}


/** @brief Return the looper this handler is attached to.
 *  @return The owning BLooper, or NULL if the handler is not attached to any
 *          looper.
 *  @see BLooper::AddHandler()
 */
BLooper*
BHandler::Looper() const
{
	return fLooper;
}


/** @brief Set the handler's name.
 *  @param name The new name, or NULL to clear the name. The string is copied.
 *  @see Name()
 */
void
BHandler::SetName(const char* name)
{
	if (fName != NULL) {
		free(fName);
		fName = NULL;
	}

	if (name != NULL)
		fName = strdup(name);
}


/** @brief Return the handler's name.
 *  @return The handler name, or NULL if no name has been set.
 *  @see SetName()
 */
const char*
BHandler::Name() const
{
	return fName;
}


/** @brief Set the next handler in the message-forwarding chain.
 *  @param handler The handler to forward unrecognized messages to, or NULL to
 *                 end the chain. Must belong to the same looper as this handler.
 *  @note The handler must already belong to a looper, and that looper must be
 *        locked when calling this method.
 *  @see NextHandler()
 *  @see MessageReceived()
 */
void
BHandler::SetNextHandler(BHandler* handler)
{
	if (fLooper == NULL) {
		debugger("handler must belong to looper before setting NextHandler");
		return;
	}

	if (!fLooper->IsLocked()) {
		debugger("The handler's looper must be locked before setting NextHandler");
		return;
	}

	if (handler != NULL && fLooper != handler->Looper()) {
		debugger("The handler and its NextHandler must have the same looper");
		return;
	}

	fNextHandler = handler;
}


/** @brief Return the next handler in the message-forwarding chain.
 *  @return The next BHandler, or NULL if none has been set.
 *  @see SetNextHandler()
 */
BHandler*
BHandler::NextHandler() const
{
	return fNextHandler;
}


/** @brief Add a message filter to this handler.
 *  @param filter The message filter to add. The handler takes ownership.
 *  @note The owning looper (if any) must be locked before calling this method.
 *  @see RemoveFilter()
 *  @see SetFilterList()
 *  @see FilterList()
 */
void
BHandler::AddFilter(BMessageFilter* filter)
{
	BLooper* looper = fLooper;
	if (looper != NULL && !looper->IsLocked()) {
		debugger("Owning Looper must be locked before calling SetFilterList");
		return;
	}

	if (looper != NULL)
		filter->SetLooper(looper);

	if (fFilters == NULL)
		fFilters = new BList;

	fFilters->AddItem(filter);
}


/** @brief Remove a message filter from this handler.
 *  @param filter The message filter to remove. Ownership is returned to the
 *                caller.
 *  @return true if the filter was found and removed, false otherwise.
 *  @note The owning looper (if any) must be locked before calling this method.
 *  @see AddFilter()
 *  @see SetFilterList()
 */
bool
BHandler::RemoveFilter(BMessageFilter* filter)
{
	BLooper* looper = fLooper;
	if (looper != NULL && !looper->IsLocked()) {
		debugger("Owning Looper must be locked before calling SetFilterList");
		return false;
	}

	if (fFilters != NULL && fFilters->RemoveItem((void*)filter)) {
		filter->SetLooper(NULL);
		return true;
	}

	return false;
}


/** @brief Replace the entire filter list for this handler.
 *  @param filters The new list of BMessageFilter pointers, or NULL to clear.
 *                 The handler takes ownership of both the list and its filters.
 *                 Any previously installed filters are deleted.
 *  @note The owning looper (if any) must be locked before calling this method.
 *  @see AddFilter()
 *  @see RemoveFilter()
 *  @see FilterList()
 */
void
BHandler::SetFilterList(BList* filters)
{
	BLooper* looper = fLooper;
	if (looper != NULL && !looper->IsLocked()) {
		debugger("Owning Looper must be locked before calling SetFilterList");
		return;
	}

	/**
		@note	I would like to use BObjectList internally, but this function is
				spec'd such that fFilters would get deleted and then assigned
				'filters', which would obviously mess this up.  Wondering if
				anyone ever assigns a list of filters and then checks against
				FilterList() to see if they are the same.
	 */

	// TODO: Explore issues with using BObjectList
	if (fFilters != NULL) {
		fFilters->DoForEach(FilterDeleter);
		delete fFilters;
	}

	fFilters = filters;
	if (fFilters) {
		for (int32 i = 0; i < fFilters->CountItems(); ++i) {
			BMessageFilter* filter =
				static_cast<BMessageFilter*>(fFilters->ItemAt(i));
			if (filter != NULL)
				filter->SetLooper(looper);
		}
	}
}


/** @brief Return the list of message filters installed on this handler.
 *  @return The BList of BMessageFilter pointers, or NULL if no filters are
 *          installed.
 *  @see AddFilter()
 *  @see RemoveFilter()
 *  @see SetFilterList()
 */
BList*
BHandler::FilterList()
{
	return fFilters;
}


/** @brief Lock the handler's looper.
 *  @return true if the looper was successfully locked, false if the handler has
 *          no looper or the looper could not be locked.
 *  @note This performs a "pseudo-atomic" operation: it caches the looper
 *        pointer, locks it, and then verifies that the looper has not changed.
 *        If it has, the lock is released and false is returned.
 *  @see LockLooperWithTimeout()
 *  @see UnlockLooper()
 */
bool
BHandler::LockLooper()
{
	BLooper* looper = fLooper;
	// Locking the looper also makes sure that the looper is valid
	if (looper != NULL && looper->Lock()) {
		// Have we locked the right looper? That's as far as the
		// "pseudo-atomic" operation mentioned in the BeBook.
		if (fLooper == looper)
			return true;

		// we locked the wrong looper, bail out
		looper->Unlock();
	}

	return false;
}


/** @brief Lock the handler's looper with a timeout.
 *  @param timeout Maximum time in microseconds to wait for the lock.
 *  @return B_OK if the looper was locked, B_BAD_VALUE if the handler has no
 *          looper, B_MISMATCHED_VALUES if the looper changed during locking,
 *          or another error code from BLooper::LockWithTimeout().
 *  @see LockLooper()
 *  @see UnlockLooper()
 */
status_t
BHandler::LockLooperWithTimeout(bigtime_t timeout)
{
	BLooper* looper = fLooper;
	if (looper == NULL)
		return B_BAD_VALUE;

	status_t status = looper->LockWithTimeout(timeout);
	if (status != B_OK)
		return status;

	if (fLooper != looper) {
		// we locked the wrong looper, bail out
		looper->Unlock();
		return B_MISMATCHED_VALUES;
	}

	return B_OK;
}


/** @brief Unlock the handler's looper.
 *  @note The looper must have been previously locked by LockLooper() or
 *        LockLooperWithTimeout().
 *  @see LockLooper()
 *  @see LockLooperWithTimeout()
 */
void
BHandler::UnlockLooper()
{
	fLooper->Unlock();
}


/** @brief Determine the handler for a scripting message.
 *  @param message The scripting message being resolved.
 *  @param index Current index into the specifier stack.
 *  @param specifier The current specifier message.
 *  @param what The specifier form (e.g., B_DIRECT_SPECIFIER).
 *  @param property The property name being targeted.
 *  @return A pointer to the BHandler that should handle the message, or NULL if
 *          the specifier could not be resolved.
 *  @note If the property matches one of the built-in handler properties
 *        (Suites, Messenger, InternalName), this handler is returned. Otherwise
 *        a B_MESSAGE_NOT_UNDERSTOOD reply is sent.
 *  @see GetSupportedSuites()
 */
BHandler*
BHandler::ResolveSpecifier(BMessage* message, int32 index,
	BMessage* specifier, int32 what, const char* property)
{
	// Straight from the BeBook
	BPropertyInfo propertyInfo(sHandlerPropInfo);
	if (propertyInfo.FindMatch(message, index, specifier, what, property) >= 0)
		return this;

	BMessage reply(B_MESSAGE_NOT_UNDERSTOOD);
	reply.AddInt32("error", B_BAD_SCRIPT_SYNTAX);
	reply.AddString("message", "Didn't understand the specifier(s)");
	message->SendReply(&reply);

	return NULL;
}


/** @brief Report the scripting suites supported by this handler.
 *  @param data The message to populate with suite information. Receives a
 *              "suites" string field ("suite/vnd.Be-handler") and a "messages"
 *              flattened BPropertyInfo.
 *  @return B_OK on success, B_BAD_VALUE if \a data is NULL, or another error
 *          code on failure.
 *  @see ResolveSpecifier()
 */
status_t
BHandler::GetSupportedSuites(BMessage* data)
{
/**
	@note	This is the output from the original implementation (calling
			PrintToStream() on both data and the contained BPropertyInfo):

BMessage: what =  (0x0, or 0)
	entry         suites, type='CSTR', c=1, size=21, data[0]: "suite/vnd.Be-handler"
	entry       messages, type='SCTD', c=1, size= 0,
	  property   commands                       types                specifiers
--------------------------------------------------------------------------------
		Suites   PGET                                               1
				 (RTSC,suites)
				 (DTCS,messages)

	 Messenger   PGET                          GNSM                 1
  InternalName   PGET                          RTSC                 1

			With a good deal of trial and error, I determined that the
			parenthetical clauses are entries in the 'ctypes' field of
			property_info.  'ctypes' is an array of 'compound_type', which
			contains an array of 'field_pair's.  I haven't the foggiest what
			either 'compound_type' or 'field_pair' is for, being as the
			scripting docs are so bloody horrible.  The corresponding
			property_info array is declared in the globals section.
 */

	if (data == NULL)
		return B_BAD_VALUE;

	status_t result = data->AddString("suites", "suite/vnd.Be-handler");
	if (result == B_OK) {
		BPropertyInfo propertyInfo(sHandlerPropInfo);
		result = data->AddFlat("messages", &propertyInfo);
	}

	return result;
}


/** @brief Start watching a target for a specific state change via BMessenger.
 *  @param target The messenger identifying the handler to watch.
 *  @param what The change code to observe.
 *  @return B_OK on success, or an error code if the notification message could
 *          not be delivered.
 *  @note This sends a kMsgStartObserving message to \a target, causing it to
 *        add this handler's messenger to its observer list.
 *  @see StopWatching(BMessenger, uint32)
 *  @see StartWatchingAll(BMessenger)
 *  @see SendNotices()
 */
status_t
BHandler::StartWatching(BMessenger target, uint32 what)
{
	BMessage message(kMsgStartObserving);
	message.AddMessenger(kObserveTarget, this);
	message.AddInt32(B_OBSERVE_WHAT_CHANGE, what);

	return target.SendMessage(&message);
}


/** @brief Start watching a target for all state changes via BMessenger.
 *  @param target The messenger identifying the handler to watch.
 *  @return B_OK on success, or an error code if the notification message could
 *          not be delivered.
 *  @see StartWatching(BMessenger, uint32)
 *  @see StopWatchingAll(BMessenger)
 */
status_t
BHandler::StartWatchingAll(BMessenger target)
{
	return StartWatching(target, B_OBSERVER_OBSERVE_ALL);
}


/** @brief Stop watching a target for a specific state change via BMessenger.
 *  @param target The messenger identifying the handler to stop watching.
 *  @param what The change code to stop observing.
 *  @return B_OK on success, or an error code if the notification message could
 *          not be delivered.
 *  @see StartWatching(BMessenger, uint32)
 *  @see StopWatchingAll(BMessenger)
 */
status_t
BHandler::StopWatching(BMessenger target, uint32 what)
{
	BMessage message(kMsgStopObserving);
	message.AddMessenger(kObserveTarget, this);
	message.AddInt32(B_OBSERVE_WHAT_CHANGE, what);

	return target.SendMessage(&message);
}


/** @brief Stop watching a target for all state changes via BMessenger.
 *  @param target The messenger identifying the handler to stop watching.
 *  @return B_OK on success, or an error code if the notification message could
 *          not be delivered.
 *  @see StopWatching(BMessenger, uint32)
 *  @see StartWatchingAll(BMessenger)
 */
status_t
BHandler::StopWatchingAll(BMessenger target)
{
	return StopWatching(target, B_OBSERVER_OBSERVE_ALL);
}


/** @brief Start watching this handler for a specific state change via direct
 *         BHandler pointer.
 *  @param handler The handler that wants to observe changes on this handler.
 *  @param what The change code to observe.
 *  @return B_OK on success, B_NO_MEMORY if the observer list could not be
 *          allocated.
 *  @note This is the local (in-process) variant that registers the observer
 *        directly in this handler's observer list.
 *  @see StopWatching(BHandler*, uint32)
 *  @see SendNotices()
 */
status_t
BHandler::StartWatching(BHandler* handler, uint32 what)
{
	ObserverList* list = _ObserverList();
	if (list == NULL)
		return B_NO_MEMORY;

	return list->Add(handler, what);
}


/** @brief Start watching this handler for all state changes via direct
 *         BHandler pointer.
 *  @param handler The handler that wants to observe all changes on this handler.
 *  @return B_OK on success, B_NO_MEMORY if the observer list could not be
 *          allocated.
 *  @see StartWatching(BHandler*, uint32)
 *  @see StopWatchingAll(BHandler*)
 */
status_t
BHandler::StartWatchingAll(BHandler* handler)
{
	return StartWatching(handler, B_OBSERVER_OBSERVE_ALL);
}


/** @brief Stop watching this handler for a specific state change via direct
 *         BHandler pointer.
 *  @param handler The handler to remove from the observer list.
 *  @param what The change code to stop observing.
 *  @return B_OK on success, B_NO_MEMORY if the observer list could not be
 *          allocated.
 *  @see StartWatching(BHandler*, uint32)
 *  @see StopWatchingAll(BHandler*)
 */
status_t
BHandler::StopWatching(BHandler* handler, uint32 what)
{
	ObserverList* list = _ObserverList();
	if (list == NULL)
		return B_NO_MEMORY;

	return list->Remove(handler, what);
}


/** @brief Stop watching this handler for all state changes via direct
 *         BHandler pointer.
 *  @param handler The handler to remove from the observer list.
 *  @return B_OK on success, B_NO_MEMORY if the observer list could not be
 *          allocated.
 *  @see StopWatching(BHandler*, uint32)
 *  @see StartWatchingAll(BHandler*)
 */
status_t
BHandler::StopWatchingAll(BHandler* handler)
{
	return StopWatching(handler, B_OBSERVER_OBSERVE_ALL);
}


/** @brief Reserved virtual hook for binary compatibility.
 *  @param d The perform code identifying the operation.
 *  @param arg Pointer to operation-specific data.
 *  @return The result of BArchivable::Perform().
 */
status_t
BHandler::Perform(perform_code d, void* arg)
{
	return BArchivable::Perform(d, arg);
}


/** @brief Send change notifications to all registered observers.
 *  @param what The change code identifying the type of change.
 *  @param notice An optional message containing additional information about
 *                the change. May be NULL.
 *  @note Observers registered for \a what and those registered for all changes
 *        (B_OBSERVER_OBSERVE_ALL) will both be notified.
 *  @see StartWatching()
 *  @see IsWatched()
 */
void
BHandler::SendNotices(uint32 what, const BMessage* notice)
{
	if (fObserverList != NULL)
		fObserverList->SendNotices(what, notice);
}


/** @brief Check whether this handler has any registered observers.
 *  @return true if at least one observer is registered, false otherwise.
 *  @see StartWatching()
 *  @see SendNotices()
 */
bool
BHandler::IsWatched() const
{
	return fObserverList && !fObserverList->IsEmpty();
}


/** @brief Initialize internal handler data structures.
 *  @param name The handler's name (may be NULL).
 *  @note Called by all constructors. Sets up the name, clears the looper,
 *        next handler, filter list, and observer list, and acquires a new
 *        token from the default token space.
 */
void
BHandler::_InitData(const char* name)
{
	SetName(name);

	fLooper = NULL;
	fNextHandler = NULL;
	fFilters = NULL;
	fObserverList = NULL;

	fToken = gDefaultTokens.NewToken(B_HANDLER_TOKEN, this);
}


/** @brief Lazily create and return the observer list.
 *  @return The observer list, or NULL if allocation fails.
 *  @note The observer list is created on first access using nothrow new.
 *  @see SendNotices()
 *  @see StartWatching()
 */
ObserverList*
BHandler::_ObserverList()
{
	if (fObserverList == NULL)
		fObserverList = new (std::nothrow) BPrivate::ObserverList();

	return fObserverList;
}


/** @brief Set the looper that owns this handler.
 *  @param looper The new owning looper, or NULL to detach from any looper.
 *  @note This is an internal method called by BLooper::AddHandler() and
 *        BLooper::RemoveHandler(). It also updates the direct message target
 *        token and propagates the looper to all installed message filters.
 *  @see Looper()
 *  @see BLooper::AddHandler()
 */
void
BHandler::SetLooper(BLooper* looper)
{
	fLooper = looper;
	gDefaultTokens.SetHandlerTarget(fToken,
		looper ? looper->fDirectTarget : NULL);

	if (fFilters != NULL) {
		for (int32 i = 0; i < fFilters->CountItems(); i++) {
			static_cast<BMessageFilter*>(
				fFilters->ItemAtFast(i))->SetLooper(looper);
		}
	}
}


#if __GNUC__ < 3
// binary compatibility with R4.5

extern "C" void
_ReservedHandler1__8BHandler(BHandler* handler, uint32 what,
	const BMessage* notice)
{
	handler->BHandler::SendNotices(what, notice);
}


BHandler::BHandler(const BHandler &)
{
	// No copy construction allowed.
}


BHandler &
BHandler::operator=(const BHandler &)
{
	// No assignments allowed.
	return *this;
}
#endif

void BHandler::_ReservedHandler2() {}
void BHandler::_ReservedHandler3() {}
void BHandler::_ReservedHandler4() {}


//	#pragma mark -


/** @brief Construct an empty observer list. */
ObserverList::ObserverList()
{
}


/** @brief Destroy the observer list, releasing all observer references. */
ObserverList::~ObserverList()
{
}


/** @brief Convert raw handler pointers to messengers where possible.
 *  @param what The change code whose handler list to validate.
 *  @note Handlers that can be resolved to a valid BMessenger are migrated to
 *        the messenger map and removed from the handler map.
 */
void
ObserverList::_ValidateHandlers(uint32 what)
{
	vector<const BHandler*>& handlers = fHandlerMap[what];
	vector<const BHandler*>::iterator iterator = handlers.begin();

	while (iterator != handlers.end()) {
		BMessenger target(*iterator);
		if (!target.IsValid()) {
			iterator++;
			continue;
		}

		Add(target, what);
		iterator = handlers.erase(iterator);
	}
	if (handlers.empty())
		fHandlerMap.erase(what);
}


/** @brief Deliver a notice to all observers registered for a given change code.
 *  @param what The change code identifying which observers to notify.
 *  @param notice The notification message to send.
 *  @note Invalid messengers are pruned during delivery.
 *  @see _ValidateHandlers()
 */
void
ObserverList::_SendNotices(uint32 what, BMessage* notice)
{
	// first iterate over the list of handlers and try to make valid
	// messengers out of them
	_ValidateHandlers(what);

	// now send it to all messengers we know
	vector<BMessenger>& messengers = fMessengerMap[what];
	vector<BMessenger>::iterator iterator = messengers.begin();

	while (iterator != messengers.end()) {
		if (!(*iterator).IsValid()) {
			iterator = messengers.erase(iterator);
			continue;
		}

		(*iterator).SendMessage(notice);
		iterator++;
	}
	if (messengers.empty())
		fMessengerMap.erase(what);
}


/** @brief Send observer notifications for a state change.
 *  @param what The change code identifying the type of change.
 *  @param notice An optional message with extra information; may be NULL.
 *  @return B_OK always.
 *  @note A copy of \a notice is created with what set to
 *        B_OBSERVER_NOTICE_CHANGE. Notices are sent to observers of both the
 *        specific \a what code and B_OBSERVER_OBSERVE_ALL.
 */
status_t
ObserverList::SendNotices(uint32 what, const BMessage* notice)
{
	BMessage* copy = NULL;
	if (notice != NULL) {
		copy = new BMessage(*notice);
		copy->what = B_OBSERVER_NOTICE_CHANGE;
		copy->AddInt32(B_OBSERVE_ORIGINAL_WHAT, notice->what);
	} else
		copy = new BMessage(B_OBSERVER_NOTICE_CHANGE);

	copy->AddInt32(B_OBSERVE_WHAT_CHANGE, what);

	_SendNotices(what, copy);
	_SendNotices(B_OBSERVER_OBSERVE_ALL, copy);

	delete copy;

	return B_OK;
}


/** @brief Register a handler as an observer for a specific change code.
 *  @param handler The handler to register.
 *  @param what The change code to observe.
 *  @return B_OK on success, B_BAD_HANDLER if \a handler is NULL.
 *  @note If the handler can already be resolved to a valid BMessenger, it is
 *        stored as a messenger instead. Duplicate registrations are silently
 *        ignored.
 */
status_t
ObserverList::Add(const BHandler* handler, uint32 what)
{
	if (handler == NULL)
		return B_BAD_HANDLER;

	// if this handler already represents a valid target, add its messenger
	BMessenger target(handler);
	if (target.IsValid())
		return Add(target, what);

	vector<const BHandler*> &handlers = fHandlerMap[what];

	vector<const BHandler*>::iterator iter;
	iter = find(handlers.begin(), handlers.end(), handler);
	if (iter != handlers.end()) {
		// TODO: do we want to have a reference count for this?
		return B_OK;
	}

	handlers.push_back(handler);
	return B_OK;
}


/** @brief Register a messenger as an observer for a specific change code.
 *  @param messenger The messenger to register.
 *  @param what The change code to observe.
 *  @return B_OK on success. Duplicate registrations are silently ignored.
 */
status_t
ObserverList::Add(const BMessenger &messenger, uint32 what)
{
	vector<BMessenger> &messengers = fMessengerMap[what];

	vector<BMessenger>::iterator iter;
	iter = find(messengers.begin(), messengers.end(), messenger);
	if (iter != messengers.end()) {
		// TODO: do we want to have a reference count for this?
		return B_OK;
	}

	messengers.push_back(messenger);
	return B_OK;
}


/** @brief Unregister a handler from observing a specific change code.
 *  @param handler The handler to remove.
 *  @param what The change code to stop observing.
 *  @return B_OK if the handler was found and removed, B_BAD_HANDLER if
 *          \a handler is NULL or was not registered.
 */
status_t
ObserverList::Remove(const BHandler* handler, uint32 what)
{
	if (handler == NULL)
		return B_BAD_HANDLER;

	// look into the list of messengers
	BMessenger target(handler);
	if (target.IsValid() && Remove(target, what) == B_OK)
		return B_OK;

	status_t status = B_BAD_HANDLER;

	vector<const BHandler*> &handlers = fHandlerMap[what];

	vector<const BHandler*>::iterator iterator = find(handlers.begin(),
		handlers.end(), handler);
	if (iterator != handlers.end()) {
		handlers.erase(iterator);
		status = B_OK;
	}
	if (handlers.empty())
		fHandlerMap.erase(what);

	return status;
}


/** @brief Unregister a messenger from observing a specific change code.
 *  @param messenger The messenger to remove.
 *  @param what The change code to stop observing.
 *  @return B_OK if the messenger was found and removed, B_BAD_HANDLER
 *          otherwise.
 */
status_t
ObserverList::Remove(const BMessenger &messenger, uint32 what)
{
	status_t status = B_BAD_HANDLER;

	vector<BMessenger> &messengers = fMessengerMap[what];

	vector<BMessenger>::iterator iterator = find(messengers.begin(),
		messengers.end(), messenger);
	if (iterator != messengers.end()) {
		messengers.erase(iterator);
		status = B_OK;
	}
	if (messengers.empty())
		fMessengerMap.erase(what);

	return status;
}


/** @brief Check whether the observer list has any registered observers.
 *  @return true if both the handler map and the messenger map are empty.
 */
bool
ObserverList::IsEmpty()
{
	return fHandlerMap.empty() && fMessengerMap.empty();
}


//	#pragma mark -


/** @brief Delete a single BMessageFilter; used as a BList::DoForEach() callback.
 *  @param _filter Pointer to the BMessageFilter to delete.
 *  @return Always returns false so the list continues iterating.
 */
bool
FilterDeleter(void* _filter)
{
	delete static_cast<BMessageFilter*>(_filter);
	return false;
}
