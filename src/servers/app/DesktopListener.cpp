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
 *   Copyright 2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 */

/** @file DesktopListener.cpp
    @brief Implements the DesktopListener interface and DesktopObservable notification hub. */


#include "DesktopListener.h"


/** @brief Virtual destructor for DesktopListener.
 *
 * Ensures proper cleanup of derived listener objects when destroyed through
 * a base-class pointer.
 */
DesktopListener::~DesktopListener()
{

}


/** @brief Constructs a DesktopObservable with no active invocation in progress. */
DesktopObservable::DesktopObservable()
	:
	fWeAreInvoking(false)
{

}


/** @brief Registers a listener with this observable and notifies it of registration.
 *
 * @param listener The DesktopListener to register.
 * @param desktop  The Desktop instance passed to the listener's ListenerRegistered callback.
 */
void
DesktopObservable::RegisterListener(DesktopListener* listener, Desktop* desktop)
{
	fDesktopListenerList.Add(listener);
	listener->ListenerRegistered(desktop);
}


/** @brief Unregisters a previously registered listener.
 *
 * @param listener The DesktopListener to remove.
 */
void
DesktopObservable::UnregisterListener(DesktopListener* listener)
{
	fDesktopListenerList.Remove(listener);
	listener->ListenerUnregistered();
}


/** @brief Returns a read-only reference to the list of registered listeners.
 *
 * @return Const reference to the internal DesktopListenerDLList.
 */
const DesktopListenerDLList&
DesktopObservable::GetDesktopListenerList()
{
	return fDesktopListenerList;
}


/** @brief Routes an incoming message to the listener identified by its integer identifier.
 *
 * Reads the listener identifier from the link and iterates the listener list
 * to find a matching listener, then delegates message handling to it.
 *
 * @param sender  The Window that is the source of the message.
 * @param link    The LinkReceiver carrying the raw message data.
 * @param reply   The LinkSender used to send a reply back to the caller.
 * @return        true if a matching listener handled the message, false otherwise.
 */
bool
DesktopObservable::MessageForListener(Window* sender,
	BPrivate::LinkReceiver& link, BPrivate::LinkSender& reply)
{
	int32 identifier;
	link.Read<int32>(&identifier);
	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener)) {
		if (listener->Identifier() == identifier) {
			if (!listener->HandleMessage(sender, link, reply))
				break;
			return true;
		}
	}
	return false;
}


/** @brief Notifies all registered listeners that a window has been added to the desktop.
 *
 * @param window The Window that was added.
 */
void
DesktopObservable::NotifyWindowAdded(Window* window)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowAdded(window);
}


/** @brief Notifies all registered listeners that a window has been removed from the desktop.
 *
 * @param window The Window that was removed.
 */
void
DesktopObservable::NotifyWindowRemoved(Window* window)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowRemoved(window);
}


/** @brief Notifies all registered listeners of a key-press event.
 *
 * @param what      The message code (e.g. B_KEY_DOWN).
 * @param key       The key code that was pressed.
 * @param modifiers Active modifier key flags at the time of the event.
 * @return          true if at least one listener requested the event be skipped,
 *                  false otherwise.
 */
bool
DesktopObservable::NotifyKeyPressed(uint32 what, int32 key, int32 modifiers)
{
	if (fWeAreInvoking)
		return false;
	InvokeGuard invokeGuard(fWeAreInvoking);

	bool skipEvent = false;
	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener)) {
		if (listener->KeyPressed(what, key, modifiers))
			skipEvent = true;
	}
	return skipEvent;
}


/** @brief Notifies all registered listeners of a mouse event message.
 *
 * @param message The BMessage describing the mouse event.
 */
void
DesktopObservable::NotifyMouseEvent(BMessage* message)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->MouseEvent(message);
}


/** @brief Notifies all registered listeners of a mouse-button-down event.
 *
 * @param window  The Window under the mouse pointer.
 * @param message The BMessage containing mouse-down data.
 * @param where   The screen position of the mouse pointer.
 */
void
DesktopObservable::NotifyMouseDown(Window* window, BMessage* message,
	const BPoint& where)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->MouseDown(window, message, where);
}


/** @brief Notifies all registered listeners of a mouse-button-up event.
 *
 * @param window  The Window under the mouse pointer.
 * @param message The BMessage containing mouse-up data.
 * @param where   The screen position of the mouse pointer.
 */
void
DesktopObservable::NotifyMouseUp(Window* window, BMessage* message,
	const BPoint& where)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->MouseUp(window, message, where);
}


/** @brief Notifies all registered listeners of a mouse-moved event.
 *
 * @param window  The Window currently under the mouse pointer.
 * @param message The BMessage containing mouse-moved data.
 * @param where   The new screen position of the mouse pointer.
 */
void
DesktopObservable::NotifyMouseMoved(Window* window, BMessage* message,
	const BPoint& where)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->MouseMoved(window, message, where);
}


/** @brief Notifies all registered listeners that a window has been moved.
 *
 * @param window The Window that moved.
 */
void
DesktopObservable::NotifyWindowMoved(Window* window)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowMoved(window);
}


/** @brief Notifies all registered listeners that a window has been resized.
 *
 * @param window The Window that was resized.
 */
void
DesktopObservable::NotifyWindowResized(Window* window)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowResized(window);
}


/** @brief Notifies all registered listeners that a window has become active.
 *
 * @param window The Window that was activated.
 */
void
DesktopObservable::NotifyWindowActivated(Window* window)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowActivated(window);
}


/** @brief Notifies all registered listeners that a window has been sent behind another.
 *
 * @param window   The Window that was sent behind.
 * @param behindOf The Window it was placed behind, or NULL if sent to the back.
 */
void
DesktopObservable::NotifyWindowSentBehind(Window* window, Window* behindOf)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowSentBehind(window, behindOf);
}


/** @brief Notifies all registered listeners that a window's workspace membership changed.
 *
 * @param window     The Window whose workspaces changed.
 * @param workspaces The new workspace bitmask for the window.
 */
void
DesktopObservable::NotifyWindowWorkspacesChanged(Window* window,
	uint32 workspaces)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowWorkspacesChanged(window, workspaces);
}


/** @brief Notifies all registered listeners that a window has been hidden.
 *
 * @param window        The Window that was hidden.
 * @param fromMinimize  true if the window was hidden as a result of minimization.
 */
void
DesktopObservable::NotifyWindowHidden(Window* window, bool fromMinimize)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowHidden(window, fromMinimize);
}


/** @brief Notifies all registered listeners that a window's minimized state changed.
 *
 * @param window   The Window whose minimize state changed.
 * @param minimize true if the window is now minimized, false if restored.
 */
void
DesktopObservable::NotifyWindowMinimized(Window* window, bool minimize)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowMinimized(window, minimize);
}


/** @brief Notifies all registered listeners that a window tab's position changed.
 *
 * @param window     The Window whose tab location changed.
 * @param location   The new tab position along the title bar.
 * @param isShifting true if the tab is currently being dragged interactively.
 */
void
DesktopObservable::NotifyWindowTabLocationChanged(Window* window,
	float location, bool isShifting)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowTabLocationChanged(window, location, isShifting);
}


/** @brief Notifies all registered listeners that a window's size constraints changed.
 *
 * @param window    The Window whose size limits changed.
 * @param minWidth  New minimum width.
 * @param maxWidth  New maximum width.
 * @param minHeight New minimum height.
 * @param maxHeight New maximum height.
 */
void
DesktopObservable::NotifySizeLimitsChanged(Window* window, int32 minWidth,
	int32 maxWidth, int32 minHeight, int32 maxHeight)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->SizeLimitsChanged(window, minWidth, maxWidth, minHeight,
			maxHeight);
}


/** @brief Notifies all registered listeners that a window's look changed.
 *
 * @param window The Window whose look changed.
 * @param look   The new window_look value.
 */
void
DesktopObservable::NotifyWindowLookChanged(Window* window, window_look look)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowLookChanged(window, look);
}


/** @brief Notifies all registered listeners that a window's feel changed.
 *
 * @param window The Window whose feel changed.
 * @param feel   The new window_feel value.
 */
void
DesktopObservable::NotifyWindowFeelChanged(Window* window, window_feel feel)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->WindowFeelChanged(window, feel);
}


/** @brief Allows registered listeners to apply decorator settings to a window.
 *
 * Iterates all listeners and calls SetDecoratorSettings on each; the return
 * value is the logical OR of all individual return values.
 *
 * @param window   The Window whose decorator settings are being modified.
 * @param settings A BMessage containing the new decorator settings.
 * @return         true if at least one listener reported that settings changed.
 */
bool
DesktopObservable::SetDecoratorSettings(Window* window,
	const BMessage& settings)
{
	if (fWeAreInvoking)
		return false;
	InvokeGuard invokeGuard(fWeAreInvoking);

	bool changed = false;
	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		changed = changed | listener->SetDecoratorSettings(window, settings);

	return changed;
}


/** @brief Collects decorator settings from all registered listeners into a message.
 *
 * @param window   The Window whose decorator settings are requested.
 * @param settings A BMessage to be populated with the collected settings.
 */
void
DesktopObservable::GetDecoratorSettings(Window* window, BMessage& settings)
{
	if (fWeAreInvoking)
		return;
	InvokeGuard invokeGuard(fWeAreInvoking);

	for (DesktopListener* listener = fDesktopListenerList.First();
		listener != NULL; listener = fDesktopListenerList.GetNext(listener))
		listener->GetDecoratorSettings(window, settings);
}


/** @brief Constructs an InvokeGuard and sets the guarded flag to true.
 *
 * @param invoking Reference to the boolean flag that tracks in-progress invocation.
 */
DesktopObservable::InvokeGuard::InvokeGuard(bool& invoking)
	:
	fInvoking(invoking)
{
	fInvoking = true;
}


/** @brief Destructs the InvokeGuard and resets the guarded flag to false. */
DesktopObservable::InvokeGuard::~InvokeGuard()
{
	fInvoking = false;
}
