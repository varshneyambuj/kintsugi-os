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
 *   Copyright 2001-2009, Haiku Inc.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Gabe Yoder (gyoder@stny.rr.com)
 */


/** @file Clipboard.cpp
 *  @brief Implementation of BClipboard, the system clipboard interface.
 *
 *  BClipboard provides access to named system clipboards, allowing
 *  applications to copy and paste data. It communicates with the
 *  registrar service to synchronize clipboard contents across
 *  applications.
 */


#include <Clipboard.h>

#include <Application.h>
#include <RegistrarDefs.h>
#include <RosterPrivate.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RUN_WITHOUT_REGISTRAR
	static BClipboard sClipboard(NULL);
	BClipboard *be_clipboard = &sClipboard;
#else
	BClipboard *be_clipboard = NULL;
#endif


using namespace BPrivate;


/** @brief Constructs a BClipboard object for the named clipboard.
 *
 *  If \a name is NULL, the clipboard defaults to "system". The constructor
 *  registers the clipboard with the registrar service and obtains a
 *  messenger for clipboard communication.
 *
 *  @param name      The name of the clipboard, or NULL for the system clipboard.
 *  @param transient Whether the clipboard is transient (not currently used).
 */
BClipboard::BClipboard(const char *name, bool transient)
	:
	fLock("clipboard")
{
	if (name != NULL)
		fName = strdup(name);
	else
		fName = strdup("system");

	fData = new BMessage();
	fCount = 0;

	BMessage message(B_REG_GET_CLIPBOARD_MESSENGER), reply;
	if (BRoster::Private().SendTo(&message, &reply, false) == B_OK
		&& reply.what == B_REG_SUCCESS
		&& reply.FindMessenger("messenger", &fClipHandler) == B_OK) {
		BMessage handlerMessage(B_REG_ADD_CLIPBOARD), handlerReply;
		int32 result;
		if (handlerMessage.AddString("name", fName) == B_OK
			&& fClipHandler.SendMessage(&handlerMessage, &handlerReply) == B_OK)
			handlerReply.FindInt32("result", &result);
	}
}


/** @brief Destroys the BClipboard object and frees associated resources.
 */
BClipboard::~BClipboard()
{
	free(fName);
	delete fData;
}


/** @brief Returns the name of this clipboard.
 *  @return The clipboard name as a C string.
 */
const char *
BClipboard::Name() const
{
	return (const char *)fName;
}


/*!	\brief Returns the (locally cached) number of commits to the clipboard.

	The returned value is the number of successful Commit() invocations for
	the clipboard represented by this object, either invoked on this object
	or another (even from another application). This method returns a locally
	cached value, which might already be obsolete. For an up-to-date value
	SystemCount() can be invoked.

	\return The number of commits to the clipboard.
*/
uint32
BClipboard::LocalCount() const
{
	return fCount;
}


/*!	\brief Returns the number of commits to the clipboard.

	The returned value is the number of successful Commit() invocations for
	the clipboard represented by this object, either invoked on this object
	or another (even from another application). This method retrieves the
	value directly from the system service managing the clipboards, so it is
	more expensive, but more up-to-date than LocalCount(), which returns a
	locally cached value.

	\return The number of commits to the clipboard.
*/
uint32
BClipboard::SystemCount() const
{
	int32 value;
	BMessage message(B_REG_GET_CLIPBOARD_COUNT), reply;
	if (message.AddString("name", fName) == B_OK
		&& fClipHandler.SendMessage(&message, &reply) == B_OK
		&& reply.FindInt32("count", &value) == B_OK)
		return (uint32)value;

	return 0;
}


/** @brief Registers a messenger to receive notifications when the clipboard changes.
 *  @param target The BMessenger that will receive clipboard change notifications.
 *  @return B_OK on success, B_BAD_VALUE if target is invalid, or B_ERROR on failure.
 */
status_t
BClipboard::StartWatching(BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;

	BMessage message(B_REG_CLIPBOARD_START_WATCHING), reply;
	if (message.AddString("name", fName) == B_OK
		&& message.AddMessenger("target", target) == B_OK
		&& fClipHandler.SendMessage(&message, &reply) == B_OK) {
		int32 result;
		reply.FindInt32("result", &result);
		return result;
	}
	return B_ERROR;
}


/** @brief Unregisters a messenger from receiving clipboard change notifications.
 *  @param target The BMessenger to stop notifying.
 *  @return B_OK on success, B_BAD_VALUE if target is invalid, or B_ERROR on failure.
 */
status_t
BClipboard::StopWatching(BMessenger target)
{
	if (!target.IsValid())
		return B_BAD_VALUE;

	BMessage message(B_REG_CLIPBOARD_STOP_WATCHING), reply;
	if (message.AddString("name", fName) == B_OK
		&& message.AddMessenger("target", target) == B_OK
		&& fClipHandler.SendMessage(&message, &reply) == B_OK) {
		int32 result;
		if (reply.FindInt32("result", &result) == B_OK)
			return result;
	}
	return B_ERROR;
}


/** @brief Locks the clipboard for exclusive access and downloads current data.
 *
 *  The clipboard must be locked before reading or modifying its data.
 *  When running with a registrar, the current clipboard contents are
 *  downloaded from the system after acquiring the lock.
 *
 *  @return true if the lock was acquired successfully, false otherwise.
 */
bool
BClipboard::Lock()
{
	// Will this work correctly if clipboard is deleted while still waiting on
	// fLock.Lock() ?
	bool locked = fLock.Lock();

#ifndef RUN_WITHOUT_REGISTRAR
	if (locked && _DownloadFromSystem() != B_OK) {
		locked = false;
		fLock.Unlock();
	}
#endif

	return locked;
}


/** @brief Unlocks the clipboard, releasing exclusive access.
 */
void
BClipboard::Unlock()
{
	fLock.Unlock();
}


/** @brief Checks whether the clipboard is currently locked.
 *  @return true if the clipboard is locked, false otherwise.
 */
bool
BClipboard::IsLocked() const
{
	return fLock.IsLocked();
}


/** @brief Clears all data from the clipboard.
 *
 *  The clipboard must be locked before calling this method.
 *
 *  @return B_OK on success, or B_NOT_ALLOWED if the clipboard is not locked.
 */
status_t
BClipboard::Clear()
{
	if (!_AssertLocked())
		return B_NOT_ALLOWED;

	return fData->MakeEmpty();
}


/** @brief Commits the current clipboard data to the system.
 *
 *  This is a convenience overload that calls Commit(false), meaning
 *  the commit will not fail even if the clipboard has been modified
 *  by another application since it was last downloaded.
 *
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BClipboard::Commit()
{
	return Commit(false);
}


/** @brief Commits the current clipboard data to the system.
 *
 *  Uploads the local clipboard data to the system clipboard service.
 *  The clipboard must be locked before calling this method. On success,
 *  the local commit count is updated to match the system count.
 *
 *  @param failIfChanged If true, the commit will fail if another application
 *                       has modified the clipboard since the last download.
 *  @return B_OK on success, B_NOT_ALLOWED if not locked, or an error code.
 */
status_t
BClipboard::Commit(bool failIfChanged)
{
	if (!_AssertLocked())
		return B_NOT_ALLOWED;

	status_t status = B_ERROR;
	BMessage message(B_REG_UPLOAD_CLIPBOARD), reply;
	if (message.AddString("name", fName) == B_OK
		&& message.AddMessage("data", fData) == B_OK
		&& message.AddMessenger("data source", be_app_messenger) == B_OK
		&& message.AddInt32("count", fCount) == B_OK
		&& message.AddBool("fail if changed", failIfChanged) == B_OK)
		status = fClipHandler.SendMessage(&message, &reply);

	if (status == B_OK) {
		int32 count;
		if (reply.FindInt32("count", &count) == B_OK)
			fCount = count;
	}

	return status;
}


/** @brief Reverts the local clipboard data to the current system clipboard contents.
 *
 *  Clears local data and re-downloads the clipboard from the system service.
 *  The clipboard must be locked before calling this method.
 *
 *  @return B_OK on success, B_NOT_ALLOWED if not locked, or an error code.
 */
status_t
BClipboard::Revert()
{
	if (!_AssertLocked())
		return B_NOT_ALLOWED;

	status_t status = fData->MakeEmpty();
	if (status == B_OK)
		status = _DownloadFromSystem();

	return status;
}


/** @brief Returns the messenger of the application that last committed data.
 *  @return A BMessenger identifying the data source application.
 */
BMessenger
BClipboard::DataSource() const
{
	return fDataSource;
}


/** @brief Returns a pointer to the BMessage containing the clipboard data.
 *
 *  The clipboard must be locked before calling this method.
 *  The returned message can be used to add or read clipboard data.
 *
 *  @return A pointer to the clipboard data message, or NULL if not locked.
 */
BMessage *
BClipboard::Data() const
{
	if (!_AssertLocked())
		return NULL;

    return fData;
}


//	#pragma mark - Private methods


BClipboard::BClipboard(const BClipboard &)
{
	// This is private, and I don't use it, so I'm not going to implement it
}


BClipboard & BClipboard::operator=(const BClipboard &)
{
	// This is private, and I don't use it, so I'm not going to implement it
	return *this;
}


void BClipboard::_ReservedClipboard1() {}
void BClipboard::_ReservedClipboard2() {}
void BClipboard::_ReservedClipboard3() {}


/** @brief Asserts that the clipboard is locked, invoking the debugger if not.
 *
 *  This is a private helper used internally to validate that the clipboard
 *  lock is held before performing operations that require it.
 *
 *  @return true if the clipboard is locked, false otherwise.
 */
bool
BClipboard::_AssertLocked() const
{
	// This function is for jumping to the debugger if not locked
	if (!fLock.IsLocked()) {
		debugger("The clipboard must be locked before proceeding.");
		return false;
	}
	return true;
}


/** @brief Downloads the current clipboard contents from the system service.
 *
 *  Retrieves the clipboard data, data source messenger, and commit count
 *  from the registrar clipboard handler.
 *
 *  @param force If true, forces a re-download (currently ignored).
 *  @return B_OK on success, or B_ERROR on failure.
 */
status_t
BClipboard::_DownloadFromSystem(bool force)
{
	// Apparently, the force paramater was used in some sort of
	// optimization in R5. Currently, we ignore it.
	BMessage message(B_REG_DOWNLOAD_CLIPBOARD), reply;
	if (message.AddString("name", fName) == B_OK
		&& fClipHandler.SendMessage(&message, &reply, 10000000, 10000000) == B_OK
		&& reply.FindMessage("data", fData) == B_OK
		&& reply.FindMessenger("data source", &fDataSource) == B_OK
		&& reply.FindInt32("count", (int32 *)&fCount) == B_OK)
		return B_OK;

	return B_ERROR;
}
