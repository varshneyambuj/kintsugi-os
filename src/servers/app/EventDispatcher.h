/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2005-2007, Haiku, Inc. All Rights Reserved.
 * Original author: Axel Dörfler, axeld@pinc-software.de.
 */

/** @file EventDispatcher.h
    @brief Routes input events from the EventStream to registered client view targets. */

#ifndef EVENT_DISPATCHER_H
#define EVENT_DISPATCHER_H


#include <AutoDeleter.h>
#include <Locker.h>
#include <Message.h>
#include <MessageFilter.h>
#include <Messenger.h>
#include <ObjectList.h>


class Desktop;
class EventStream;
class HWInterface;
class ServerBitmap;

struct event_listener;


/** @brief Associates a BMessenger with a list of view tokens that should receive events. */
class EventTarget {
	public:
		EventTarget();
		~EventTarget();

		/** @brief Sets the BMessenger used to deliver events to the client.
		    @param messenger The client-side BMessenger. */
		void SetTo(const BMessenger& messenger);

		/** @brief Returns the BMessenger for this target.
		    @return Reference to the BMessenger. */
		BMessenger& Messenger() { return fMessenger; }

		/** @brief Finds a listener entry by view token.
		    @param token The view token to search for.
		    @param _index Optional output index in the listener list.
		    @return Pointer to the matching event_listener, or NULL. */
		event_listener* FindListener(int32 token, int32* _index = NULL);

		/** @brief Adds a view token listener with the given event mask and options.
		    @param token View token to listen for.
		    @param eventMask Bitmask of event types to deliver.
		    @param options Delivery option flags.
		    @param temporary true if this listener should be removed after one event.
		    @return true on success. */
		bool AddListener(int32 token, uint32 eventMask, uint32 options,
				bool temporary);

		/** @brief Removes a listener entry.
		    @param listener Pointer to the event_listener to remove.
		    @param temporary true if removing a temporary listener. */
		void RemoveListener(event_listener* listener, bool temporary);

		/** @brief Removes a permanent listener by view token.
		    @param token View token of the listener to remove.
		    @return true if a listener was found and removed. */
		bool RemoveListener(int32 token);

		/** @brief Removes a temporary listener by view token.
		    @param token View token of the temporary listener.
		    @return true if found and removed. */
		bool RemoveTemporaryListener(int32 token);

		/** @brief Removes all temporary listeners from this target. */
		void RemoveTemporaryListeners();

		/** @brief Returns true if this target has no listeners.
		    @return true if the listener list is empty. */
		bool IsEmpty() const { return fListeners.IsEmpty(); }

		/** @brief Returns the number of registered listeners.
		    @return Listener count. */
		int32 CountListeners() const { return fListeners.CountItems(); }

		/** @brief Returns the listener at the given index.
		    @param index Zero-based index.
		    @return Pointer to the event_listener, or NULL. */
		event_listener* ListenerAt(int32 index) const
				{ return fListeners.ItemAt(index); }

	private:
		bool _RemoveTemporaryListener(event_listener* listener, int32 index);

		BObjectList<event_listener, true> fListeners;
		BMessenger	fMessenger;
};

/** @brief Abstract filter that may intercept and redirect events before delivery. */
class EventFilter {
	public:
		virtual ~EventFilter() {};

		/** @brief Evaluates an event and optionally redirects it.
		    @param event The event message.
		    @param _target In/out pointer to the intended EventTarget.
		    @param _viewToken Optional in/out view token.
		    @param latestMouseMoved Most recent mouse-moved event for coalescing.
		    @return B_DISPATCH_MESSAGE to deliver, B_SKIP_MESSAGE to discard. */
		virtual filter_result Filter(BMessage* event, EventTarget** _target,
			int32* _viewToken = NULL, BMessage* latestMouseMoved = NULL) = 0;

		/** @brief Called when an EventTarget is being removed from the dispatcher.
		    @param target The target being removed. */
		virtual void RemoveTarget(EventTarget* target);
};

/** @brief Reads events from an EventStream and routes them to the appropriate EventTarget
           objects, managing mouse, keyboard, and drag-and-drop delivery. */
class EventDispatcher : public BLocker {
	public:
		EventDispatcher();
		~EventDispatcher();

		/** @brief Attaches the dispatcher to an EventStream and starts the event loop.
		    @param stream The EventStream to read events from.
		    @return B_OK on success, or an error code. */
		status_t SetTo(EventStream* stream);

		/** @brief Returns B_OK if the dispatcher is properly initialised.
		    @return B_OK if running, or an error code. */
		status_t InitCheck();

		/** @brief Removes all listener registrations for a given EventTarget.
		    @param target The EventTarget to deregister. */
		void RemoveTarget(EventTarget& target);

		/** @brief Registers a permanent listener for mouse or keyboard events.
		    @param target The EventTarget to deliver events to.
		    @param token View token of the listening view.
		    @param eventMask Bitmask of event types.
		    @param options Delivery option flags.
		    @return true on success. */
		bool AddListener(EventTarget& target, int32 token,
				uint32 eventMask, uint32 options);

		/** @brief Registers a temporary listener removed after the next matching event.
		    @param target The EventTarget to deliver events to.
		    @param token View token of the listening view.
		    @param eventMask Bitmask of event types.
		    @param options Delivery option flags.
		    @return true on success. */
		bool AddTemporaryListener(EventTarget& target,
				int32 token, uint32 eventMask, uint32 options);

		/** @brief Removes a permanent listener for a view token.
		    @param target The owning EventTarget.
		    @param token View token to deregister. */
		void RemoveListener(EventTarget& target, int32 token);

		/** @brief Removes a temporary listener for a view token.
		    @param target The owning EventTarget.
		    @param token View token to deregister. */
		void RemoveTemporaryListener(EventTarget& target, int32 token);

		/** @brief Installs a filter that intercepts mouse events before routing.
		    @param filter Pointer to the EventFilter; takes ownership. */
		void SetMouseFilter(EventFilter* filter);

		/** @brief Installs a filter that intercepts keyboard events before routing.
		    @param filter Pointer to the EventFilter; takes ownership. */
		void SetKeyboardFilter(EventFilter* filter);

		/** @brief Returns the current mouse position and button state.
		    @param where Output current mouse position.
		    @param buttons Output current button state bitmask. */
		void GetMouse(BPoint& where, int32& buttons);

		/** @brief Synthesises and delivers a mouse-moved event to the given target.
		    @param target The EventTarget to receive the fake event.
		    @param viewToken View token to target. */
		void SendFakeMouseMoved(EventTarget& target, int32 viewToken);

		/** @brief Returns the time in microseconds since the last input event.
		    @return Idle time in microseconds. */
		bigtime_t IdleTime();

		/** @brief Returns true if a dedicated cursor thread is running.
		    @return true if the cursor thread is active. */
		bool HasCursorThread();

		/** @brief Associates a hardware interface for cursor rendering.
		    @param interface The HWInterface used to draw the cursor. */
		void SetHWInterface(HWInterface* interface);

		/** @brief Sets a drag message to be delivered on the next mouse-up event.
		    @param message The drag BMessage payload.
		    @param bitmap Optional drag image bitmap.
		    @param offsetFromCursor Offset of the bitmap from the cursor hotspot. */
		void SetDragMessage(BMessage& message, ServerBitmap* bitmap,
				const BPoint& offsetFromCursor);
			// the message should be delivered on the next
			// "mouse up".
			// if the mouse is not pressed, it should
			// be delivered to the "current" target right away.

		/** @brief Associates this dispatcher with a Desktop for focus management.
		    @param desktop The owning Desktop. */
		void SetDesktop(Desktop* desktop);

	private:
		status_t _Run();
		void _Unset();

		void _SendFakeMouseMoved(BMessage* message);
		bool _SendMessage(BMessenger& messenger, BMessage* message,
				float importance);

		bool _AddTokens(BMessage* message, EventTarget* target,
				uint32 eventMask, BMessage* nextMouseMoved = NULL,
				int32* _viewToken = NULL);
		void _RemoveTokens(BMessage* message);
		void _SetFeedFocus(BMessage* message);
		void _UnsetFeedFocus(BMessage* message);

		void _SetMouseTarget(const BMessenger* messenger);
		void _UnsetLastMouseTarget();

		bool _AddListener(EventTarget& target, int32 token,
				uint32 eventMask, uint32 options, bool temporary);
		void _RemoveTemporaryListeners();

		void _DeliverDragMessage();

		void _EventLoop();
		void _CursorLoop();

		static status_t _event_looper(void* dispatcher);
		static status_t _cursor_looper(void* dispatcher);

	private:
		EventStream*	fStream;
		thread_id		fThread;
		thread_id		fCursorThread;

		EventTarget*	fPreviousMouseTarget;
		EventTarget*	fFocus;
		bool			fSuspendFocus;

		ObjectDeleter <EventFilter>
						fMouseFilter;
		ObjectDeleter<EventFilter>
						fKeyboardFilter;

		BObjectList<EventTarget> fTargets;

		BMessage*		fNextLatestMouseMoved;
		BPoint			fLastCursorPosition;
		int32			fLastButtons;
		bigtime_t		fLastUpdate;

		BMessage		fDragMessage;
		bool			fDraggingMessage;
		BPoint			fDragOffset;

		BLocker			fCursorLock;
		HWInterface*	fHWInterface;
		Desktop*		fDesktop;
};

#endif	/* EVENT_DISPATCHER_H */
