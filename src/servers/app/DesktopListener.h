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
 * MIT License. Copyright 2010, Haiku.
 * Original author: Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file DesktopListener.h
    @brief Observer interface for desktop events and the observable mixin that fires them. */

#ifndef DESKTOP_LISTENER_H
#define DESKTOP_LISTENER_H


#include <util/DoublyLinkedList.h>

#include <Point.h>

#include <ServerLink.h>
#include "Window.h"


class BMessage;
class Desktop;
class Window;


/** @brief Abstract observer interface for receiving notifications about desktop events
           such as window lifecycle, input events, and configuration changes. */
class DesktopListener : public DoublyLinkedListLinkImpl<DesktopListener> {
public:
	virtual						~DesktopListener();

	/** @brief Returns a unique integer identifier for this listener type.
	    @return Listener identifier. */
	virtual int32				Identifier() = 0;

	/** @brief Called immediately after this listener is registered with a desktop.
	    @param desktop The desktop this listener was registered with. */
	virtual	void				ListenerRegistered(Desktop* desktop) = 0;

	/** @brief Called immediately before this listener is unregistered. */
	virtual	void				ListenerUnregistered() = 0;

	/** @brief Allows the listener to handle an incoming protocol message.
	    @param sender The window that sent the message.
	    @param link Receiver for reading message data.
	    @param reply Sender for writing the reply.
	    @return true if the message was handled and should not be processed further. */
	virtual bool				HandleMessage(Window* sender,
									BPrivate::LinkReceiver& link,
									BPrivate::LinkSender& reply) = 0;

	/** @brief Called when a new window is added to the desktop.
	    @param window The window that was added. */
	virtual void				WindowAdded(Window* window) = 0;

	/** @brief Called when a window is removed from the desktop.
	    @param window The window that was removed. */
	virtual void				WindowRemoved(Window* window) = 0;

	/** @brief Called when a key is pressed.
	    @param what Key event type.
	    @param key Raw key code.
	    @param modifiers Current modifier state.
	    @return true to consume the event, false to pass it on. */
	virtual bool				KeyPressed(uint32 what, int32 key,
									int32 modifiers) = 0;

	/** @brief Called for every mouse event before routing.
	    @param message The raw mouse event message. */
	virtual void				MouseEvent(BMessage* message) = 0;

	/** @brief Called when a mouse button is pressed over a window.
	    @param window The window under the mouse.
	    @param message The mouse-down message.
	    @param where Screen coordinates of the press. */
	virtual void				MouseDown(Window* window, BMessage* message,
									const BPoint& where) = 0;

	/** @brief Called when a mouse button is released.
	    @param window The window under the mouse.
	    @param message The mouse-up message.
	    @param where Screen coordinates of the release. */
	virtual void				MouseUp(Window* window, BMessage* message,
									const BPoint& where) = 0;

	/** @brief Called when the mouse moves.
	    @param window The window under the mouse.
	    @param message The mouse-moved message.
	    @param where New screen coordinates of the pointer. */
	virtual void				MouseMoved(Window* window, BMessage* message,
									const BPoint& where) = 0;

	/** @brief Called after a window's position has changed.
	    @param window The window that moved. */
	virtual void				WindowMoved(Window* window) = 0;

	/** @brief Called after a window's size has changed.
	    @param window The window that was resized. */
	virtual void				WindowResized(Window* window) = 0;

	/** @brief Called when a window gains activation.
	    @param window The window that was activated. */
	virtual void				WindowActivated(Window* window) = 0;

	/** @brief Called when a window is sent behind another.
	    @param window The window that was moved back.
	    @param behindOf The window it went behind. */
	virtual void				WindowSentBehind(Window* window,
									Window* behindOf) = 0;

	/** @brief Called when a window's workspace membership changes.
	    @param window The window.
	    @param workspaces New workspace bitmask. */
	virtual void				WindowWorkspacesChanged(Window* window,
									uint32 workspaces) = 0;

	/** @brief Called when a window is hidden.
	    @param window The window that was hidden.
	    @param fromMinimize true if hiding was due to minimisation. */
	virtual void				WindowHidden(Window* window,
									bool fromMinimize) = 0;

	/** @brief Called when a window's minimised state changes.
	    @param window The window.
	    @param minimize true if being minimised. */
	virtual void				WindowMinimized(Window* window,
									bool minimize) = 0;

	/** @brief Called when a window's tab position changes.
	    @param window The window.
	    @param location New tab offset.
	    @param isShifting true during a tab-shift operation. */
	virtual void				WindowTabLocationChanged(Window* window,
									float location, bool isShifting) = 0;

	/** @brief Called when a window's size limits change.
	    @param window The window.
	    @param minWidth Minimum width.
	    @param maxWidth Maximum width.
	    @param minHeight Minimum height.
	    @param maxHeight Maximum height. */
	virtual void				SizeLimitsChanged(Window* window,
									int32 minWidth, int32 maxWidth,
									int32 minHeight, int32 maxHeight) = 0;

	/** @brief Called when a window's look changes.
	    @param window The window.
	    @param look New window_look value. */
	virtual void				WindowLookChanged(Window* window,
									window_look look) = 0;

	/** @brief Called when a window's feel changes.
	    @param window The window.
	    @param feel New window_feel value. */
	virtual void				WindowFeelChanged(Window* window,
									window_feel feel) = 0;

	/** @brief Called to apply decorator settings.
	    @param window The window.
	    @param settings BMessage with settings to apply.
	    @return true if the listener handled the settings. */
	virtual bool				SetDecoratorSettings(Window* window,
									const BMessage& settings) = 0;

	/** @brief Called to retrieve decorator settings.
	    @param window The window.
	    @param settings BMessage to populate with current settings. */
	virtual void				GetDecoratorSettings(Window* window,
									BMessage& settings) = 0;
};


/** @brief Doubly-linked list of DesktopListener pointers. */
typedef DoublyLinkedList<DesktopListener> DesktopListenerDLList;


/** @brief Mixin that manages a list of DesktopListener objects and dispatches
           desktop events to all of them. */
class DesktopObservable {
public:
								DesktopObservable();

			/** @brief Registers a listener and notifies it that registration is complete.
			    @param listener The DesktopListener to add.
			    @param desktop The Desktop being observed. */
			void				RegisterListener(DesktopListener* listener,
									Desktop* desktop);

			/** @brief Unregisters a listener and notifies it before removal.
			    @param listener The DesktopListener to remove. */
			void				UnregisterListener(DesktopListener* listener);

			/** @brief Returns the current list of registered desktop listeners.
			    @return Const reference to the listener list. */
	const DesktopListenerDLList&	GetDesktopListenerList();

			/** @brief Forwards a protocol message to listeners until one handles it.
			    @param sender The window that sent the message.
			    @param link Receiver for reading message data.
			    @param reply Sender for writing the reply.
			    @return true if a listener handled the message. */
			bool				MessageForListener(Window* sender,
									BPrivate::LinkReceiver& link,
									BPrivate::LinkSender& reply);

			/** @brief Notifies all listeners that a window was added.
			    @param window The window that was added. */
			void				NotifyWindowAdded(Window* window);

			/** @brief Notifies all listeners that a window was removed.
			    @param window The window that was removed. */
			void				NotifyWindowRemoved(Window* window);

			/** @brief Notifies all listeners of a key press.
			    @param what Key event type.
			    @param key Raw key code.
			    @param modifiers Current modifier state.
			    @return true if a listener consumed the event. */
			bool				NotifyKeyPressed(uint32 what, int32 key,
									int32 modifiers);

			/** @brief Notifies all listeners of a mouse event.
			    @param message The raw mouse event message. */
			void				NotifyMouseEvent(BMessage* message);

			/** @brief Notifies all listeners of a mouse button press.
			    @param window Window under the mouse.
			    @param message The mouse-down message.
			    @param where Screen coordinates. */
			void				NotifyMouseDown(Window* window,
									BMessage* message, const BPoint& where);

			/** @brief Notifies all listeners of a mouse button release.
			    @param window Window under the mouse.
			    @param message The mouse-up message.
			    @param where Screen coordinates. */
			void				NotifyMouseUp(Window* window, BMessage* message,
										const BPoint& where);

			/** @brief Notifies all listeners of mouse movement.
			    @param window Window under the mouse.
			    @param message The mouse-moved message.
			    @param where New screen coordinates. */
			void				NotifyMouseMoved(Window* window,
									BMessage* message, const BPoint& where);

			/** @brief Notifies all listeners that a window moved.
			    @param window The window. */
			void				NotifyWindowMoved(Window* window);

			/** @brief Notifies all listeners that a window was resized.
			    @param window The window. */
			void				NotifyWindowResized(Window* window);

			/** @brief Notifies all listeners that a window was activated.
			    @param window The window. */
			void				NotifyWindowActivated(Window* window);

			/** @brief Notifies all listeners that a window was sent behind another.
			    @param window The window moved back.
			    @param behindOf The window it went behind. */
			void				NotifyWindowSentBehind(Window* window,
									Window* behindOf);

			/** @brief Notifies all listeners that a window's workspace membership changed.
			    @param window The window.
			    @param workspaces New workspace bitmask. */
			void				NotifyWindowWorkspacesChanged(Window* window,
									uint32 workspaces);

			/** @brief Notifies all listeners that a window was hidden.
			    @param window The window.
			    @param fromMinimize true if hidden due to minimisation. */
			void				NotifyWindowHidden(Window* window,
									bool fromMinimize);

			/** @brief Notifies all listeners of a window minimise state change.
			    @param window The window.
			    @param minimize true if now minimised. */
			void				NotifyWindowMinimized(Window* window,
									bool minimize);

			/** @brief Notifies all listeners that a window tab position changed.
			    @param window The window.
			    @param location New tab offset.
			    @param isShifting true during shift operation. */
			void				NotifyWindowTabLocationChanged(Window* window,
									float location, bool isShifting);

			/** @brief Notifies all listeners that window size limits changed.
			    @param window The window.
			    @param minWidth Minimum width.
			    @param maxWidth Maximum width.
			    @param minHeight Minimum height.
			    @param maxHeight Maximum height. */
			void				NotifySizeLimitsChanged(Window* window,
									int32 minWidth, int32 maxWidth,
									int32 minHeight, int32 maxHeight);

			/** @brief Notifies all listeners that a window's look changed.
			    @param window The window.
			    @param look New window_look value. */
			void				NotifyWindowLookChanged(Window* window,
									window_look look);

			/** @brief Notifies all listeners that a window's feel changed.
			    @param window The window.
			    @param feel New window_feel value. */
			void				NotifyWindowFeelChanged(Window* window,
									window_feel feel);

			/** @brief Forwards a decorator settings change to all listeners.
			    @param window The window.
			    @param settings BMessage with new settings.
			    @return true if a listener applied the settings. */
			bool				SetDecoratorSettings(Window* window,
									const BMessage& settings);

			/** @brief Collects decorator settings from all listeners into a message.
			    @param window The window.
			    @param settings BMessage to populate. */
			void				GetDecoratorSettings(Window* window,
									BMessage& settings);

private:
		class InvokeGuard {
			public:
				InvokeGuard(bool& invoking);
				~InvokeGuard();
			private:
				bool&	fInvoking;
		};

		DesktopListenerDLList	fDesktopListenerList;

		// prevent recursive invokes
		bool					fWeAreInvoking;
};

#endif
