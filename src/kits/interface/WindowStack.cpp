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
 *   Copyright 2010 Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Clemens Zeidler <haiku@clemens-zeidler.de>
 */


/**
 * @file WindowStack.cpp
 * @brief Implementation of BWindowStack, a stacking window group manager
 *
 * BWindowStack manages a group of BWindow objects that are stacked together
 * and move as a unit. It provides an API for adding/removing windows from the
 * stack and querying stack membership.
 *
 * @see BWindow
 */


#include "WindowStack.h"

#include <new>

#include <Window.h>

#include <ApplicationPrivate.h>
#include <MessengerPrivate.h>
#include <PortLink.h>
#include <ServerProtocol.h>

#include "StackAndTilePrivate.h"


using namespace BPrivate;


/**
 * @brief Constructs a BWindowStack associated with @a window's server link.
 *
 * All subsequent stack operations are sent over the BPortLink that belongs to
 * @a window; the stack represents the stacking group that @a window belongs to
 * (or will belong to after AddWindow() is called).
 *
 * @param window The window whose server link is used for IPC; must be non-NULL.
 */
BWindowStack::BWindowStack(BWindow* window)
{
	fLink = window->fLink;
}


/**
 * @brief Destroys the BWindowStack object.
 *
 * The BPortLink is borrowed from the associated BWindow and is not freed here.
 */
BWindowStack::~BWindowStack()
{

}


/**
 * @brief Appends @a window to the end of this stack.
 *
 * Convenience wrapper that converts @a window to a BMessenger and delegates
 * to AddWindow(const BMessenger&).
 *
 * @param window The BWindow to add; must be non-NULL.
 *
 * @return B_OK on success, or an error code from the server.
 *
 * @see AddWindow(const BMessenger&), AddWindowAt()
 */
status_t
BWindowStack::AddWindow(const BWindow* window)
{
	BMessenger messenger(window);
	return AddWindow(messenger);
}


/**
 * @brief Appends the window identified by @a window to the end of this stack.
 *
 * Delegates to AddWindowAt() with position -1, which the server interprets
 * as "append to the end".
 *
 * @param window BMessenger targeting the window to add.
 *
 * @return B_OK on success, or an error code from the server.
 *
 * @see AddWindowAt(const BMessenger&, int32)
 */
status_t
BWindowStack::AddWindow(const BMessenger& window)
{
	return AddWindowAt(window, -1);
}


/**
 * @brief Inserts @a window into the stack at @a position.
 *
 * Convenience wrapper that converts @a window to a BMessenger and delegates
 * to AddWindowAt(const BMessenger&, int32).
 *
 * @param window   The BWindow to insert; must be non-NULL.
 * @param position Zero-based index at which to insert; -1 appends to the end.
 *
 * @return B_OK on success, or an error code from the server.
 *
 * @see AddWindowAt(const BMessenger&, int32)
 */
status_t
BWindowStack::AddWindowAt(const BWindow* window, int32 position)
{
	BMessenger messenger(window);
	return AddWindowAt(messenger, position);
}


/**
 * @brief Inserts the window identified by @a window into the stack at @a position.
 *
 * Sends the kAddWindowToStack message over the server link, attaching the
 * window's port/token/team identity and the desired insertion index.
 *
 * @param window   BMessenger targeting the window to insert.
 * @param position Zero-based index at which to insert; -1 appends to the end.
 *
 * @return B_OK on success, or an error code if the server request failed.
 *
 * @see AddWindow(), RemoveWindow()
 */
status_t
BWindowStack::AddWindowAt(const BMessenger& window, int32 position)
{
	_StartMessage(kAddWindowToStack);

	_AttachMessenger(window);
	fLink->Attach<int32>(position);

	int32 code = B_ERROR;
	if (fLink->FlushWithReply(code) != B_OK)
		return code;

	return B_OK;
}


/**
 * @brief Removes @a window from this stack.
 *
 * Convenience wrapper that converts @a window to a BMessenger and delegates
 * to RemoveWindow(const BMessenger&).
 *
 * @param window The BWindow to remove; must be non-NULL.
 *
 * @return B_OK on success, or an error code from the server.
 *
 * @see RemoveWindow(const BMessenger&), AddWindow()
 */
status_t
BWindowStack::RemoveWindow(const BWindow* window)
{
	BMessenger messenger(window);
	return RemoveWindow(messenger);
}


/**
 * @brief Removes the window identified by @a window from this stack.
 *
 * Sends the kRemoveWindowFromStack message over the server link.
 *
 * @param window BMessenger targeting the window to remove.
 *
 * @return B_OK on success, or B_ERROR if the server request failed.
 *
 * @see RemoveWindowAt(), AddWindow()
 */
status_t
BWindowStack::RemoveWindow(const BMessenger& window)
{
	_StartMessage(kRemoveWindowFromStack);
	_AttachMessenger(window);

	if (fLink->Flush() != B_OK)
		return B_ERROR;

	return B_OK;
}


/**
 * @brief Removes the window at @a position from this stack.
 *
 * Sends kRemoveWindowFromStackAt and optionally reads the removed window's
 * identity back into @a window.
 *
 * @param position Zero-based index of the window to remove.
 * @param window   If non-NULL, receives a BMessenger for the removed window.
 *
 * @return B_OK on success, or an error code from the server.
 *
 * @see RemoveWindow(), WindowAt()
 */
status_t
BWindowStack::RemoveWindowAt(int32 position, BMessenger* window)
{
	_StartMessage(kRemoveWindowFromStackAt);
	fLink->Attach<int32>(position);

	int32 code = B_ERROR;
	if (fLink->FlushWithReply(code) != B_OK)
		return code;

	if (window == NULL)
		return B_OK;

	return _ReadMessenger(*window);
}


/**
 * @brief Returns the number of windows currently in this stack.
 *
 * Sends kCountWindowsOnStack over the server link and reads back the count.
 *
 * @return The number of windows in the stack, or -1 if the request failed.
 */
int32
BWindowStack::CountWindows()
{
	_StartMessage(kCountWindowsOnStack);

	int32 code = B_ERROR;
	fLink->FlushWithReply(code);
	if (code != B_OK)
		return -1;

	int32 count;
	if (fLink->Read<int32>(&count) != B_OK)
		return -1;

	return count;
}


/**
 * @brief Retrieves a BMessenger for the window at @a position in the stack.
 *
 * @param position  Zero-based index of the window to retrieve.
 * @param messenger On success, receives a BMessenger targeting the window.
 *
 * @return B_OK on success, or an error code if the index is out of range or
 *         the server request failed.
 *
 * @see CountWindows(), HasWindow()
 */
status_t
BWindowStack::WindowAt(int32 position, BMessenger& messenger)
{
	_StartMessage(kWindowOnStackAt);
	fLink->Attach<int32>(position);

	int32 code = B_ERROR;
	fLink->FlushWithReply(code);
	if (code != B_OK)
		return code;

	return _ReadMessenger(messenger);
}


/**
 * @brief Returns whether @a window is a member of this stack.
 *
 * Convenience wrapper that converts @a window to a BMessenger and delegates
 * to HasWindow(const BMessenger&).
 *
 * @param window The BWindow to check; must be non-NULL.
 *
 * @return True if the window is in the stack, false otherwise.
 *
 * @see HasWindow(const BMessenger&)
 */
bool
BWindowStack::HasWindow(const BWindow* window)
{
	BMessenger messenger(window);
	return HasWindow(messenger);
}


/**
 * @brief Returns whether the window identified by @a window is a member of this stack.
 *
 * Sends kStackHasWindow over the server link and reads back a boolean result.
 *
 * @param window BMessenger targeting the window to check.
 *
 * @return True if the window is in the stack, false if it is not or if the
 *         server request failed.
 *
 * @see HasWindow(const BWindow*), CountWindows()
 */
bool
BWindowStack::HasWindow(const BMessenger& window)
{
	_StartMessage(kStackHasWindow);
	_AttachMessenger(window);

	int32 code = B_ERROR;
	fLink->FlushWithReply(code);
	if (code != B_OK)
		return false;

	bool hasWindow;
	if (fLink->Read<bool>(&hasWindow) != B_OK)
		return false;

	return hasWindow;
}


/**
 * @brief Serialises a BMessenger's identity (port, token, team) onto the server link.
 *
 * Used internally before flushing any stack-management message that targets a
 * specific window by messenger.
 *
 * @param window The BMessenger to serialise.
 *
 * @return B_OK if all three fields were attached successfully, or an error code.
 *
 * @see _ReadMessenger()
 */
status_t
BWindowStack::_AttachMessenger(const BMessenger& window)
{
	BMessenger::Private messengerPrivate(const_cast<BMessenger&>(window));
	fLink->Attach<port_id>(messengerPrivate.Port());
	fLink->Attach<int32>(messengerPrivate.Token());
	return fLink->Attach<team_id>(messengerPrivate.Team());
}


/**
 * @brief Deserialises a BMessenger's identity (port, token, team) from the server link.
 *
 * Used internally after receiving a reply that includes a window messenger, such
 * as the response to kRemoveWindowFromStackAt or kWindowOnStackAt.
 *
 * @param window The BMessenger to populate with the received identity.
 *
 * @return B_OK on success, or an error code if a read failed.
 *
 * @see _AttachMessenger()
 */
status_t
BWindowStack::_ReadMessenger(BMessenger& window)
{
	port_id port;
	int32 token;
	team_id team;
	fLink->Read<port_id>(&port);
	fLink->Read<int32>(&token);
	status_t status = fLink->Read<team_id>(&team);
	if (status != B_OK)
		return status;
	BMessenger::Private messengerPrivate(window);
	messengerPrivate.SetTo(team, port, token);
	return B_OK;
}


/**
 * @brief Begins a new stack-management message on the server link.
 *
 * Sends the AS_TALK_TO_DESKTOP_LISTENER preamble followed by the SAT
 * magic identifier, the kStacking sub-protocol tag, and the specific
 * command @a what.
 *
 * @param what The stack operation code (e.g. kAddWindowToStack).
 *
 * @return B_OK if all fields were attached successfully, or an error code.
 */
status_t
BWindowStack::_StartMessage(int32 what)
{
	fLink->StartMessage(AS_TALK_TO_DESKTOP_LISTENER);
	fLink->Attach<int32>(kMagicSATIdentifier);
	fLink->Attach<int32>(kStacking);
	return fLink->Attach<int32>(what);
}
