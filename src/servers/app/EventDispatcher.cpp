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
 *   Copyright 2005-2009, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file EventDispatcher.cpp
 *  @brief Per-desktop input event dispatcher that routes events to client windows. */


#include "EventDispatcher.h"

#include "BitmapManager.h"
#include "Desktop.h"
#include "EventStream.h"
#include "HWInterface.h"
#include "InputManager.h"
#include "ServerBitmap.h"

#include <MessagePrivate.h>
#include <MessengerPrivate.h>
#include <ServerProtocol.h>
#include <TokenSpace.h>

#include <Autolock.h>
#include <ToolTipManager.h>
#include <View.h>

#include <new>
#include <stdio.h>
#include <string.h>


//#define TRACE_EVENTS
#ifdef TRACE_EVENTS
#	define ETRACE(x) printf x
#else
#	define ETRACE(x) ;
#endif


/*!
	The EventDispatcher is a per Desktop object that handles all input
	events for that desktop.

	The events are processed as needed in the Desktop class (via EventFilters),
	and then forwarded to the actual target of the event, a client window
	(or more correctly, to its EventTarget).
	You cannot set the target of an event directly - the event filters need
	to specify the targets.
	The event loop will make sure that every target and interested listener
	will get the event - it also delivers mouse moved events to the previous
	target once so that this target can then spread the B_EXITED_VIEW transit
	to the local target handler (usually a BView).

	If you look at the event_listener structure below, the differentiation
	between target and token may look odd, but it really has a reason as
	well:
	All events are sent to the preferred window handler only - the window
	may then use the token or token list to identify the specific target
	view(s). This makes it possible to send every event only once, no
	matter how many local target handlers there are.
*/

struct event_listener {
	int32		token;
	uint32		event_mask;
	uint32		options;
	uint32		temporary_event_mask;
	uint32		temporary_options;

	uint32		EffectiveEventMask() const { return event_mask | temporary_event_mask; }
	uint32		EffectiveOptions() const { return options | temporary_options; }
};

static const char* kTokenName = "_token";

static const uint32 kFakeMouseMoved = 'fake';

static const float kMouseMovedImportance = 0.1f;
static const float kMouseTransitImportance = 1.0f;
static const float kStandardImportance = 0.9f;
static const float kListenerImportance = 0.8f;


/**
 * @brief Constructs an empty EventTarget with no listeners.
 */
EventTarget::EventTarget()
	:
	fListeners(2)
{
}


/**
 * @brief Destroys the EventTarget.
 */
EventTarget::~EventTarget()
{
}


/**
 * @brief Associates this target with the given messenger.
 * @param messenger The BMessenger used to send events to the target.
 */
void
EventTarget::SetTo(const BMessenger& messenger)
{
	fMessenger = messenger;
}


/**
 * @brief Searches for a listener with the given token.
 * @param token  The listener token to search for.
 * @param _index If non-NULL, receives the index of the found listener.
 * @return Pointer to the matching event_listener, or NULL if not found.
 */
event_listener*
EventTarget::FindListener(int32 token, int32* _index)
{
	for (int32 i = fListeners.CountItems(); i-- > 0;) {
		event_listener* listener = fListeners.ItemAt(i);

		if (listener->token == token) {
			if (_index)
				*_index = i;
			return listener;
		}
	}

	return NULL;
}


/**
 * @brief Removes a temporary listener or clears its temporary fields.
 *
 * If the listener has no permanent event mask it is deleted; otherwise only
 * its temporary fields are cleared.
 *
 * @param listener The listener to process.
 * @param index    The index of the listener in the internal list.
 * @return true if the listener was removed and deleted, false otherwise.
 */
bool
EventTarget::_RemoveTemporaryListener(event_listener* listener, int32 index)
{
	if (listener->event_mask == 0) {
		// this is only a temporary target
		ETRACE(("events: remove temp. listener: token %ld, eventMask = %ld, options = %ld\n",
			listener->token, listener->temporary_event_mask, listener->temporary_options));

		fListeners.RemoveItemAt(index);
		delete listener;
		return true;
	}

	if (listener->temporary_event_mask != 0) {
		ETRACE(("events: clear temp. listener: token %ld, eventMask = %ld, "
				"options = %ld\n",
			listener->token, listener->temporary_event_mask,
			listener->temporary_options));

		listener->temporary_event_mask = 0;
		listener->temporary_options = 0;
	}

	return false;
}


/**
 * @brief Removes or clears temporary state for all listeners on this target.
 */
void
EventTarget::RemoveTemporaryListeners()
{
	for (int32 index = CountListeners(); index-- > 0;) {
		event_listener* listener = ListenerAt(index);

		_RemoveTemporaryListener(listener, index);
	}
}


/**
 * @brief Removes the temporary listener identified by @a token.
 * @param token The token of the listener to remove.
 * @return true if a listener was removed, false if not found.
 */
bool
EventTarget::RemoveTemporaryListener(int32 token)
{
	int32 index;
	event_listener* listener = FindListener(token, &index);
	if (listener == NULL)
		return false;

	return _RemoveTemporaryListener(listener, index);
}


/**
 * @brief Removes the listener identified by @a token.
 *
 * If the listener still has a temporary event mask it is not deleted;
 * instead its permanent fields are cleared.
 *
 * @param token The token of the listener to remove.
 * @return true if the listener was fully removed and deleted, false otherwise.
 */
bool
EventTarget::RemoveListener(int32 token)
{
	int32 index;
	event_listener* listener = FindListener(token, &index);
	if (listener == NULL)
		return false;

	if (listener->temporary_event_mask != 0) {
		// we still need this event
		listener->event_mask = 0;
		listener->options = 0;
		return false;
	}

	fListeners.RemoveItemAt(index);
	delete listener;
	return true;
}


/**
 * @brief Adds a listener to this target, or updates it if already present.
 * @param token     The token identifying the listener (view token).
 * @param eventMask Bitmask of event types to listen for.
 * @param options   Listener option flags.
 * @param temporary Whether this is a temporary (mouse-grab) listener.
 * @return true on success, false if allocation fails.
 */
bool
EventTarget::AddListener(int32 token, uint32 eventMask,
	uint32 options, bool temporary)
{
	event_listener* listener = new (std::nothrow) event_listener;
	if (listener == NULL)
		return false;

	listener->token = token;

	if (temporary) {
		listener->event_mask = 0;
		listener->options = 0;
		listener->temporary_event_mask = eventMask;
		listener->temporary_options = options;
	} else {
		listener->event_mask = eventMask;
		listener->options = options;
		listener->temporary_event_mask = 0;
		listener->temporary_options = 0;
	}

	bool success = fListeners.AddItem(listener);
	if (!success)
		delete listener;

	return success;
}


//	#pragma mark -


/**
 * @brief Default implementation; removes nothing.
 * @param target The EventTarget to remove (ignored by the base class).
 */
void
EventFilter::RemoveTarget(EventTarget* target)
{
}


//	#pragma mark -


/**
 * @brief Constructs an EventDispatcher with no active stream or filters.
 */
EventDispatcher::EventDispatcher()
	:
	BLocker("event dispatcher"),
	fStream(NULL),
	fThread(-1),
	fCursorThread(-1),
	fPreviousMouseTarget(NULL),
	fFocus(NULL),
	fSuspendFocus(false),
	fMouseFilter(NULL),
	fKeyboardFilter(NULL),
	fTargets(10),
	fNextLatestMouseMoved(NULL),
	fLastButtons(0),
	fLastUpdate(system_time()),
	fDraggingMessage(false),
	fCursorLock("cursor loop lock"),
	fHWInterface(NULL),
	fDesktop(NULL)
{
}


/**
 * @brief Destroys the EventDispatcher, stopping the event and cursor threads.
 */
EventDispatcher::~EventDispatcher()
{
	_Unset();
}


/**
 * @brief Attaches the dispatcher to the given event stream and starts its threads.
 * @param stream The EventStream to read events from (NULL disassociates).
 * @return B_OK on success, or an error code if thread creation fails.
 */
status_t
EventDispatcher::SetTo(EventStream* stream)
{
	ETRACE(("event dispatcher: stream = %p\n", stream));

	_Unset();

	if (stream == NULL)
		return B_OK;

	fStream = stream;
	return _Run();
}


/**
 * @brief Checks whether the dispatcher is fully initialized and running.
 * @return B_OK if the stream is set and threads are running, B_NO_INIT otherwise.
 */
status_t
EventDispatcher::InitCheck()
{
	if (fStream != NULL) {
		if (fThread < B_OK)
			return fThread;

		return B_OK;
	}
	return B_NO_INIT;
}


/**
 * @brief Stops the event/cursor threads and releases the current event stream.
 */
void
EventDispatcher::_Unset()
{
	if (fStream == NULL)
		return;

	fStream->SendQuit();

	status_t status;
	wait_for_thread(fThread, &status);
	wait_for_thread(fCursorThread, &status);

	fThread = fCursorThread = -1;

	gInputManager->PutStream(fStream);
	fStream = NULL;
}


/**
 * @brief Spawns the event loop thread (and optionally the cursor thread).
 * @return B_OK on success, or the error returned by spawn_thread/resume_thread.
 */
status_t
EventDispatcher::_Run()
{
	fThread = spawn_thread(_event_looper, "event loop",
		B_REAL_TIME_DISPLAY_PRIORITY - 10, this);
	if (fThread < B_OK)
		return fThread;

	if (fStream->SupportsCursorThread()) {
		ETRACE(("event stream supports cursor thread!\n"));

		fCursorThread = spawn_thread(_cursor_looper, "cursor loop",
			B_REAL_TIME_DISPLAY_PRIORITY - 5, this);
		if (resume_thread(fCursorThread) != B_OK) {
			kill_thread(fCursorThread);
			fCursorThread = -1;
		}
	}

	return resume_thread(fThread);
}


/**
 * @brief Removes any reference to the target without deleting it.
 * @param target The EventTarget to deregister from focus, mouse, and listener lists.
 */
void
EventDispatcher::RemoveTarget(EventTarget& target)
{
	BAutolock _(this);

	if (fFocus == &target)
		fFocus = NULL;
	if (fPreviousMouseTarget == &target)
		fPreviousMouseTarget = NULL;

	if (fKeyboardFilter.IsSet())
		fKeyboardFilter->RemoveTarget(&target);
	if (fMouseFilter.IsSet())
		fMouseFilter->RemoveTarget(&target);

	fTargets.RemoveItem(&target);
}


/**
 * @brief Adds the specified listener or updates its event mask and options
 *        if already added.
 *
 * Follows BView semantics: specifying an event mask of zero leaves the event
 * mask untouched and only updates the options.
 *
 * @param target    The EventTarget to add or update.
 * @param token     Token identifying the specific view listener.
 * @param eventMask Bitmask of events to listen for.
 * @param options   Listener option flags.
 * @param temporary Whether this is a temporary listener (requires mouse button down).
 * @return true if the listener was added or updated, false on failure.
 */
bool
EventDispatcher::_AddListener(EventTarget& target, int32 token,
	uint32 eventMask, uint32 options, bool temporary)
{
	BAutolock _(this);

	if (temporary && fLastButtons == 0) {
		// only allow to add temporary listeners in case a buttons is pressed
		return false;
	}

	if (!fTargets.HasItem(&target))
		fTargets.AddItem(&target);

	event_listener* listener = target.FindListener(token);
	if (listener != NULL) {
		// we already have this target, update its event mask
		if (temporary) {
			if (eventMask != 0)
				listener->temporary_event_mask = eventMask;
			listener->temporary_options = options;
		} else {
			if (eventMask != 0)
				listener->event_mask = eventMask;
			listener->options = options;
		}

		return true;
	}

	if (eventMask == 0)
		return false;

	ETRACE(("events: add listener: token %ld, eventMask = %ld, options = %ld,"
			"%s\n",
		token, eventMask, options, temporary ? "temporary" : "permanent"));

	// we need a new target

	bool success = target.AddListener(token, eventMask, options, temporary);
	if (!success) {
		if (target.IsEmpty())
			fTargets.RemoveItem(&target);
	} else {
		if (options & B_SUSPEND_VIEW_FOCUS)
			fSuspendFocus = true;
	}

	return success;
}


/**
 * @brief Removes all temporary listeners from every registered target.
 */
void
EventDispatcher::_RemoveTemporaryListeners()
{
	for (int32 i = fTargets.CountItems(); i-- > 0;) {
		EventTarget* target = fTargets.ItemAt(i);

		target->RemoveTemporaryListeners();
	}
}


/**
 * @brief Adds a permanent listener to @a target.
 * @param target    The EventTarget receiving the listener.
 * @param token     Token identifying the specific view.
 * @param eventMask Bitmask of events to listen for.
 * @param options   Listener option flags (only B_NO_POINTER_HISTORY is honored).
 * @return true on success, false on failure.
 */
bool
EventDispatcher::AddListener(EventTarget& target, int32 token,
	uint32 eventMask, uint32 options)
{
	options &= B_NO_POINTER_HISTORY;
		// that's currently the only allowed option

	return _AddListener(target, token, eventMask, options, false);
}


/**
 * @brief Adds a temporary listener to @a target (active only while mouse is pressed).
 * @param target    The EventTarget receiving the listener.
 * @param token     Token identifying the specific view.
 * @param eventMask Bitmask of events to listen for.
 * @param options   Listener option flags.
 * @return true on success, false if no mouse button is currently pressed.
 */
bool
EventDispatcher::AddTemporaryListener(EventTarget& target,
	int32 token, uint32 eventMask, uint32 options)
{
	return _AddListener(target, token, eventMask, options, true);
}


/**
 * @brief Removes a permanent listener from @a target.
 * @param target The EventTarget from which to remove the listener.
 * @param token  Token of the listener to remove.
 */
void
EventDispatcher::RemoveListener(EventTarget& target, int32 token)
{
	BAutolock _(this);
	ETRACE(("events: remove listener token %ld\n", token));

	if (target.RemoveListener(token) && target.IsEmpty())
		fTargets.RemoveItem(&target);
}


/**
 * @brief Removes a temporary listener from @a target.
 * @param target The EventTarget from which to remove the listener.
 * @param token  Token of the temporary listener to remove.
 */
void
EventDispatcher::RemoveTemporaryListener(EventTarget& target, int32 token)
{
	BAutolock _(this);
	ETRACE(("events: remove temporary listener token %ld\n", token));

	if (target.RemoveTemporaryListener(token) && target.IsEmpty())
		fTargets.RemoveItem(&target);
}


/**
 * @brief Sets the mouse event filter.
 * @param filter The EventFilter to use for mouse events (ownership transferred).
 */
void
EventDispatcher::SetMouseFilter(EventFilter* filter)
{
	BAutolock _(this);

	if (fMouseFilter.Get() == filter)
		return;

	fMouseFilter.SetTo(filter);
}


/**
 * @brief Sets the keyboard event filter.
 * @param filter The EventFilter to use for keyboard events (ownership transferred).
 */
void
EventDispatcher::SetKeyboardFilter(EventFilter* filter)
{
	BAutolock _(this);

	if (fKeyboardFilter.Get() == filter)
		return;

	fKeyboardFilter.SetTo(filter);
}


/**
 * @brief Returns the last known mouse position and button state.
 * @param where   Receives the last cursor position in screen coordinates.
 * @param buttons Receives the last button bitmask.
 */
void
EventDispatcher::GetMouse(BPoint& where, int32& buttons)
{
	BAutolock _(this);

	where = fLastCursorPosition;
	buttons = fLastButtons;
}


/**
 * @brief Injects a fake B_MOUSE_MOVED event directed at a specific target view.
 * @param target    The EventTarget to receive the fake move.
 * @param viewToken The token of the view within the target window.
 */
void
EventDispatcher::SendFakeMouseMoved(EventTarget& target, int32 viewToken)
{
	if (fStream == NULL)
		return;

	BMessage* fakeMove = new BMessage(kFakeMouseMoved);
	if (fakeMove == NULL)
		return;

	fakeMove->AddMessenger("target", target.Messenger());
	fakeMove->AddInt32("view_token", viewToken);

	fStream->InsertEvent(fakeMove);
}


/**
 * @brief Processes a fake mouse-moved message, sending transit events as needed.
 * @param message The fake mouse-moved BMessage to process.
 */
void
EventDispatcher::_SendFakeMouseMoved(BMessage* message)
{
	BMessenger target;
	int32 viewToken;
	if (message->FindInt32("view_token", &viewToken) != B_OK
		|| message->FindMessenger("target", &target) != B_OK)
		return;

	if (fDesktop == NULL)
		return;

	// Check if the target is still valid
	::EventTarget* eventTarget = NULL;

	fDesktop->LockSingleWindow();

	if (target.IsValid())
		eventTarget = fDesktop->FindTarget(target);

	fDesktop->UnlockSingleWindow();

	if (eventTarget == NULL)
		return;

	BMessage moved(B_MOUSE_MOVED);
	moved.AddPoint("screen_where", fLastCursorPosition);
	moved.AddInt32("buttons", fLastButtons);

	if (fDraggingMessage)
		moved.AddMessage("be:drag_message", &fDragMessage);

	if (fPreviousMouseTarget != NULL
		&& fPreviousMouseTarget->Messenger() != target) {
		_AddTokens(&moved, fPreviousMouseTarget, B_POINTER_EVENTS);
		_SendMessage(fPreviousMouseTarget->Messenger(), &moved,
			kMouseTransitImportance);

		_RemoveTokens(&moved);
	}

	moved.AddInt32("_view_token", viewToken);
		// this only belongs to the new target

	moved.AddBool("be:transit_only", true);
		// let the view know this what not user generated

	_SendMessage(target, &moved, kMouseTransitImportance);

	fPreviousMouseTarget = eventTarget;
}


/**
 * @brief Returns the time elapsed since the last input event.
 * @return Idle time in microseconds.
 */
bigtime_t
EventDispatcher::IdleTime()
{
	BAutolock _(this);
	return system_time() - fLastUpdate;
}


/**
 * @brief Returns whether a dedicated cursor thread is running.
 * @return true if the cursor thread is active, false otherwise.
 */
bool
EventDispatcher::HasCursorThread()
{
	return fCursorThread >= B_OK;
}


/**
 * @brief Sets the hardware interface used to move the mouse cursor.
 *
 * @a interface is allowed to be NULL to detach from the current hardware.
 * The last cursor position is adopted from the new interface when non-NULL.
 *
 * @param interface The HWInterface whose cursor should be moved.
 */
void
EventDispatcher::SetHWInterface(HWInterface* interface)
{
	BAutolock _(fCursorLock);

	fHWInterface = interface;

	// adopt the cursor position of the new HW interface
	if (interface != NULL)
		fLastCursorPosition = interface->CursorPosition();
}


/**
 * @brief Begins a drag-and-drop operation with the given message and cursor bitmap.
 * @param message          The drag message (copied internally).
 * @param bitmap           The bitmap to display under the cursor while dragging.
 * @param offsetFromCursor Offset of the bitmap's top-left from the cursor hotspot.
 */
void
EventDispatcher::SetDragMessage(BMessage& message,
	ServerBitmap* bitmap, const BPoint& offsetFromCursor)
{
	ETRACE(("EventDispatcher::SetDragMessage()\n"));

	BAutolock _(this);

	if (fLastButtons == 0) {
		// mouse buttons has already been released or was never pressed
		return;
	}

	fHWInterface->SetDragBitmap(bitmap, offsetFromCursor);

	fDragMessage = message;
	fDraggingMessage = true;
	fDragOffset = offsetFromCursor;
}


/**
 * @brief Associates this dispatcher with the given Desktop.
 * @param desktop The Desktop instance that owns this dispatcher.
 */
void
EventDispatcher::SetDesktop(Desktop* desktop)
{
	fDesktop = desktop;
}


//	#pragma mark - Message methods


/**
 * @brief Sends @a message to the provided @a messenger.
 *
 * TODO: If the message could not be delivered immediately, it should be included
 * in a waiting message queue with a fixed length - the least important
 * messages are removed first when that gets full.
 *
 * @param messenger  The BMessenger to deliver the message to.
 * @param message    The BMessage to send.
 * @param importance Delivery priority (higher = more important, kept longer).
 * @return false if the target port no longer exists, true otherwise.
 */
bool
EventDispatcher::_SendMessage(BMessenger& messenger, BMessage* message,
	float importance)
{
	// TODO: add failed messages to a queue, and start dropping them by importance
	//	(and use the same mechanism in ServerWindow::SendMessageToClient())

	status_t status = messenger.SendMessage(message, (BHandler*)NULL, 0);
	if (status != B_OK) {
		printf("EventDispatcher: failed to send message '%.4s' to target: %s\n",
			(char*)&message->what, strerror(status));
	}

	if (status == B_BAD_PORT_ID) {
		// the target port is gone
		return false;
	}

	return true;
}


/**
 * @brief Adds listener tokens for the given event mask to @a message.
 *
 * Optionally skips tokens for listeners that have B_NO_POINTER_HISTORY set
 * and are receiving an older mouse-moved event.
 *
 * @param message         The message to add tokens to.
 * @param target          The EventTarget whose listeners are inspected.
 * @param eventMask       Only listeners matching this mask are included.
 * @param nextMouseMoved  The latest mouse-moved event (for history filtering).
 * @param _viewToken      In/out: focus view token; cleared when skipped.
 * @return true if at least one token was added, false otherwise.
 */
bool
EventDispatcher::_AddTokens(BMessage* message, EventTarget* target,
	uint32 eventMask, BMessage* nextMouseMoved, int32* _viewToken)
{
	_RemoveTokens(message);

	int32 count = target->CountListeners();
	int32 added = 0;

	for (int32 i = 0; i < count; i++) {
		event_listener* listener = target->ListenerAt(i);
		if ((listener->EffectiveEventMask() & eventMask) == 0)
			continue;

		if (nextMouseMoved != NULL && message->what == B_MOUSE_MOVED
			&& (listener->EffectiveOptions() & B_NO_POINTER_HISTORY) != 0
			&& message != nextMouseMoved
			&& _viewToken != NULL) {
			if (listener->token == *_viewToken) {
				// focus view doesn't want to get pointer history
				*_viewToken = B_NULL_TOKEN;
			}
			continue;
		}

		ETRACE(("  add token %ld\n", listener->token));

		if (message->AddInt32(kTokenName, listener->token) == B_OK)
			added++;
	}

	return added != 0;
}


/**
 * @brief Removes all listener tokens previously added to @a message.
 * @param message The message whose token field should be cleared.
 */
void
EventDispatcher::_RemoveTokens(BMessage* message)
{
	message->RemoveName(kTokenName);
}


/**
 * @brief Marks @a message so it is fed to the focus handler.
 * @param message The message to mark.
 */
void
EventDispatcher::_SetFeedFocus(BMessage* message)
{
	if (message->ReplaceBool("_feed_focus", true) != B_OK)
		message->AddBool("_feed_focus", true);
}


/**
 * @brief Clears the feed-focus mark from @a message.
 * @param message The message to unmark.
 */
void
EventDispatcher::_UnsetFeedFocus(BMessage* message)
{
	message->RemoveName("_feed_focus");
}


/**
 * @brief Delivers the pending drag message to the last mouse target as a drop.
 */
void
EventDispatcher::_DeliverDragMessage()
{
	ETRACE(("EventDispatcher::_DeliverDragMessage()\n"));

	if (fDraggingMessage && fPreviousMouseTarget != NULL) {
		BMessage::Private(fDragMessage).SetWasDropped(true);
		fDragMessage.RemoveName("_original_what");
		fDragMessage.AddInt32("_original_what", fDragMessage.what);
		fDragMessage.AddPoint("_drop_point_", fLastCursorPosition);
		fDragMessage.AddPoint("_drop_offset_", fDragOffset);
		fDragMessage.what = _MESSAGE_DROPPED_;

		_SendMessage(fPreviousMouseTarget->Messenger(),
			&fDragMessage, 100.0);
	}

	fDragMessage.MakeEmpty();
	fDragMessage.what = 0;
	fDraggingMessage = false;

	fHWInterface->SetDragBitmap(NULL, B_ORIGIN);
}


//	#pragma mark - Event loops


/**
 * @brief Main event processing loop; runs in the event thread.
 *
 * Continuously dequeues events from the stream, routes them through the
 * appropriate filters, and dispatches them to target windows and listeners.
 * The loop exits when the stream signals quit (input server died), after
 * which the desktop is notified.
 */
void
EventDispatcher::_EventLoop()
{
	BMessage* event;
	while (fStream->GetNextEvent(&event)) {
		BAutolock _(this);
		fLastUpdate = system_time();

		EventTarget* current = NULL;
		EventTarget* previous = NULL;
		bool pointerEvent = false;
		bool keyboardEvent = false;
		bool addedTokens = false;

		switch (event->what) {
			case kFakeMouseMoved:
				_SendFakeMouseMoved(event);
				break;
			case B_MOUSE_MOVED:
			{
				BPoint where;
				if (event->FindPoint("where", &where) == B_OK)
					fLastCursorPosition = where;

				if (fDraggingMessage)
					event->AddMessage("be:drag_message", &fDragMessage);

				if (!HasCursorThread()) {
					// There is no cursor thread, we need to move the cursor
					// ourselves
					BAutolock _(fCursorLock);

					if (fHWInterface != NULL) {
						fHWInterface->MoveCursorTo(fLastCursorPosition.x,
							fLastCursorPosition.y);
					}
				}

				// This is for B_NO_POINTER_HISTORY - we always want the
				// latest mouse moved event in the queue only
				if (fNextLatestMouseMoved == NULL)
					fNextLatestMouseMoved = fStream->PeekLatestMouseMoved();
				else if (fNextLatestMouseMoved != event) {
					// Drop older mouse moved messages if the server is lagging
					// too much (if the message is older than 100 msecs)
					bigtime_t eventTime;
					if (event->FindInt64("when", &eventTime) == B_OK) {
						if (system_time() - eventTime > 100000)
							break;
					}
				}

				// supposed to fall through
			}
			case B_MOUSE_DOWN:
			case B_MOUSE_UP:
			case B_MOUSE_IDLE:
			{
#ifdef TRACE_EVENTS
				if (event->what != B_MOUSE_MOVED)
					printf("mouse up/down event, previous target = %p\n", fPreviousMouseTarget);
#endif
				pointerEvent = true;

				if (!fMouseFilter.IsSet())
					break;

				EventTarget* mouseTarget = fPreviousMouseTarget;
				int32 viewToken = B_NULL_TOKEN;
				if (fMouseFilter->Filter(event, &mouseTarget, &viewToken,
						fNextLatestMouseMoved) == B_SKIP_MESSAGE) {
					// this is a work-around if the wrong B_MOUSE_UP
					// event is filtered out
					if (event->what == B_MOUSE_UP
						&& event->FindInt32("buttons") == 0) {
						fSuspendFocus = false;
						_RemoveTemporaryListeners();
					}
					break;
				}

				int32 buttons;
				if (event->FindInt32("buttons", &buttons) == B_OK)
					fLastButtons = buttons;
				else
					fLastButtons = 0;

				// The "where" field will be filled in by the receiver
				// (it's supposed to be expressed in local window coordinates)
				event->RemoveName("where");
				event->AddPoint("screen_where", fLastCursorPosition);

				if (event->what == B_MOUSE_MOVED
					&& fPreviousMouseTarget != NULL
					&& mouseTarget != fPreviousMouseTarget) {
					// Target has changed, we need to notify the previous target
					// that the mouse has exited its views
					addedTokens = _AddTokens(event, fPreviousMouseTarget,
						B_POINTER_EVENTS);
					if (addedTokens)
						_SetFeedFocus(event);

					_SendMessage(fPreviousMouseTarget->Messenger(), event,
						kMouseTransitImportance);
					previous = fPreviousMouseTarget;
				}

				current = fPreviousMouseTarget = mouseTarget;

				if (current != NULL) {
					int32 focusView = viewToken;
					addedTokens |= _AddTokens(event, current, B_POINTER_EVENTS,
						fNextLatestMouseMoved, &focusView);

					bool noPointerHistoryFocus = focusView != viewToken;

					if (viewToken != B_NULL_TOKEN)
						event->AddInt32("_view_token", viewToken);

					if (addedTokens && !noPointerHistoryFocus)
						_SetFeedFocus(event);
					else if (noPointerHistoryFocus) {
						// No tokens were added or the focus shouldn't get a
						// mouse moved
						break;
					}

					_SendMessage(current->Messenger(), event,
						event->what == B_MOUSE_MOVED
							? kMouseMovedImportance : kStandardImportance);
				}
				break;
			}

			case B_KEY_DOWN:
			case B_KEY_UP:
			case B_UNMAPPED_KEY_DOWN:
			case B_UNMAPPED_KEY_UP:
			case B_MODIFIERS_CHANGED:
			case B_INPUT_METHOD_EVENT:
				ETRACE(("key event, focus = %p\n", fFocus));

				if (fKeyboardFilter.IsSet()
					&& fKeyboardFilter->Filter(event, &fFocus)
						== B_SKIP_MESSAGE) {
					break;
				}

				keyboardEvent = true;

				if (fFocus != NULL && _AddTokens(event, fFocus,
						B_KEYBOARD_EVENTS)) {
					// if tokens were added, we need to explicetly suspend
					// focus in the event - if not, the event is simply not
					// forwarded to the target
					addedTokens = true;

					if (!fSuspendFocus)
						_SetFeedFocus(event);
				}

				// supposed to fall through

			default:
				// TODO: the keyboard filter sets the focus - ie. no other
				//	focus messages that go through the event dispatcher can
				//	go through.
				if (event->what == B_MOUSE_WHEEL_CHANGED)
					current = fPreviousMouseTarget;
				else
					current = fFocus;

				if (current != NULL && (!fSuspendFocus || addedTokens)) {
					_SendMessage(current->Messenger(), event,
						kStandardImportance);
				}
				break;
		}

		if (keyboardEvent || pointerEvent) {
			// send the event to the additional listeners

			if (addedTokens) {
				_RemoveTokens(event);
				_UnsetFeedFocus(event);
			}
			if (pointerEvent) {
				// this is added in the Desktop mouse processing
				// but it's only intended for the focus view
				event->RemoveName("_view_token");
			}

			for (int32 i = fTargets.CountItems(); i-- > 0;) {
				EventTarget* target = fTargets.ItemAt(i);

				// We already sent the event to the all focus and last focus
				// tokens
				if (current == target || previous == target)
					continue;

				// Don't send the message if there are no tokens for this event
				if (!_AddTokens(event, target,
						keyboardEvent ? B_KEYBOARD_EVENTS : B_POINTER_EVENTS,
						event->what == B_MOUSE_MOVED
							? fNextLatestMouseMoved : NULL))
					continue;

				if (!_SendMessage(target->Messenger(), event,
						event->what == B_MOUSE_MOVED
							? kMouseMovedImportance : kListenerImportance)) {
					// the target doesn't seem to exist anymore, let's remove it
					fTargets.RemoveItemAt(i);
				}
			}

			if (event->what == B_MOUSE_UP && fLastButtons == 0) {
				// no buttons are pressed anymore
				fSuspendFocus = false;
				_RemoveTemporaryListeners();
				if (fDraggingMessage)
					_DeliverDragMessage();
			}
		}

		if (fNextLatestMouseMoved == event)
			fNextLatestMouseMoved = NULL;
		delete event;
	}

	// The loop quit, therefore no more events are coming from the input
	// server, it must have died. Unset ourselves and notify the desktop.
	fThread = -1;
		// Needed to avoid problems with wait_for_thread in _Unset()
	_Unset();

	if (fDesktop)
		fDesktop->PostMessage(AS_EVENT_STREAM_CLOSED);
}


/**
 * @brief Dedicated cursor movement loop; runs in the cursor thread.
 *
 * Reads cursor positions from the stream at high priority and moves the
 * hardware cursor immediately. Also inserts B_MOUSE_IDLE events when the
 * cursor has not moved for the tool-tip delay duration.
 */
void
EventDispatcher::_CursorLoop()
{
	BPoint where;
	const bigtime_t toolTipDelay = BToolTipManager::Manager()->ShowDelay();
	bool mouseIdleSent = true;
	status_t status = B_OK;

	while (status != B_ERROR) {
		const bigtime_t timeout = mouseIdleSent ?
			B_INFINITE_TIMEOUT : toolTipDelay;
		status = fStream->GetNextCursorPosition(where, timeout);

		if (status == B_OK) {
			mouseIdleSent = false;
			BAutolock _(fCursorLock);

			if (fHWInterface != NULL)
				fHWInterface->MoveCursorTo(where.x, where.y);
		} else if (status == B_TIMED_OUT) {
			mouseIdleSent = true;
			BMessage* mouseIdle = new BMessage(B_MOUSE_IDLE);
			fStream->InsertEvent(mouseIdle);
		}
	}

	fCursorThread = -1;
}


/**
 * @brief Thread entry point for the event loop thread.
 * @param _dispatcher Pointer to the owning EventDispatcher.
 * @return B_OK always.
 */
/*static*/
status_t
EventDispatcher::_event_looper(void* _dispatcher)
{
	EventDispatcher* dispatcher = (EventDispatcher*)_dispatcher;

	ETRACE(("Start event loop\n"));
	dispatcher->_EventLoop();
	return B_OK;
}


/**
 * @brief Thread entry point for the cursor loop thread.
 * @param _dispatcher Pointer to the owning EventDispatcher.
 * @return B_OK always.
 */
/*static*/
status_t
EventDispatcher::_cursor_looper(void* _dispatcher)
{
	EventDispatcher* dispatcher = (EventDispatcher*)_dispatcher;

	ETRACE(("Start cursor loop\n"));
	dispatcher->_CursorLoop();
	return B_OK;
}
