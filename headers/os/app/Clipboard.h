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
 *   Copyright 2007-2009, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Gabe Yoder
 */
#ifndef _CLIPBOARD_H
#define	_CLIPBOARD_H

/**
 * @file Clipboard.h
 * @brief Provides the BClipboard class for system clipboard access and the
 *        B_CLIPBOARD_CHANGED notification constant.
 */


#include <BeBuild.h>
#include <Locker.h>
#include <Messenger.h>

class BMessage;


/**
 * @brief Notification message code sent when the system clipboard content
 *        changes.
 *
 * Register for this notification via BClipboard::StartWatching() to be
 * informed when another application modifies the clipboard.
 */
enum {
	B_CLIPBOARD_CHANGED = 'CLCH' /**< The clipboard contents have changed. */
};

/**
 * @brief Provides access to the system clipboard for copying and pasting data
 *        between applications.
 *
 * BClipboard supports thread-safe clipboard operations through Lock()/Unlock()
 * and a transactional model with Clear(), write data, Commit(). The clipboard
 * data is stored as a BMessage accessible via Data(). Applications can also
 * watch for changes made by other applications using StartWatching().
 *
 * A global instance for the default system clipboard is available as
 * @c be_clipboard.
 *
 * @see be_clipboard
 * @see BMessage
 */
class BClipboard {
public:
	/**
	 * @brief Constructs a BClipboard for the named clipboard.
	 * @param name The name of the clipboard (e.g. "system" for the default).
	 * @param transient If @c true, the clipboard data is not preserved across
	 *        reboots.
	 */
								BClipboard(const char* name,
									bool transient = false);

	/**
	 * @brief Destroys the BClipboard object.
	 */
	virtual						~BClipboard();

	/**
	 * @brief Returns the name of this clipboard.
	 * @return A null-terminated string identifying the clipboard.
	 */
			const char*			Name() const;

	/**
	 * @brief Returns the local data revision count.
	 *
	 * This is the count at the time the data was last downloaded from the
	 * clipboard service.
	 *
	 * @return The local clipboard revision count.
	 */
			uint32				LocalCount() const;

	/**
	 * @brief Returns the current system-wide revision count.
	 *
	 * If this differs from LocalCount(), the clipboard has been modified by
	 * another application.
	 *
	 * @return The system clipboard revision count.
	 */
			uint32				SystemCount() const;

	/**
	 * @brief Registers a target to receive B_CLIPBOARD_CHANGED notifications.
	 * @param target A BMessenger identifying the handler to notify.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see StopWatching
	 */
			status_t			StartWatching(BMessenger target);

	/**
	 * @brief Unregisters a target from clipboard change notifications.
	 * @param target The BMessenger that was previously registered.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see StartWatching
	 */
			status_t			StopWatching(BMessenger target);

	/**
	 * @brief Locks the clipboard for exclusive access.
	 *
	 * You must lock the clipboard before reading or modifying its data.
	 * Also downloads the latest clipboard contents from the system.
	 *
	 * @return @c true if the lock was acquired, @c false on failure.
	 * @see Unlock
	 */
			bool				Lock();

	/**
	 * @brief Releases the clipboard lock.
	 * @see Lock
	 */
			void				Unlock();

	/**
	 * @brief Checks whether the clipboard is currently locked by this object.
	 * @return @c true if locked, @c false otherwise.
	 */
			bool				IsLocked() const;

	/**
	 * @brief Clears all data from the clipboard.
	 *
	 * Call this before adding new data. The clipboard must be locked.
	 *
	 * @return @c B_OK on success, or an error code on failure.
	 * @see Commit
	 */
			status_t			Clear();

	/**
	 * @brief Commits the current clipboard data to the system clipboard.
	 *
	 * After calling Clear() and writing data to the BMessage returned by
	 * Data(), call Commit() to publish the changes system-wide.
	 *
	 * @return @c B_OK on success, or an error code on failure.
	 * @see Clear
	 * @see Revert
	 */
			status_t			Commit();

	/**
	 * @brief Commits the clipboard data, optionally failing if it was changed
	 *        by another application since the last download.
	 * @param failIfChanged If @c true, returns an error if the system
	 *        clipboard has been modified since the last Lock().
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Commit(bool failIfChanged);

	/**
	 * @brief Reverts the local clipboard data to the last committed state
	 *        from the system.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see Commit
	 */
			status_t			Revert();

	/**
	 * @brief Returns a BMessenger identifying the application that last
	 *        committed data to this clipboard.
	 * @return A BMessenger for the data source application.
	 */
			BMessenger			DataSource() const;

	/**
	 * @brief Returns a pointer to the BMessage holding the clipboard data.
	 *
	 * The clipboard must be locked before accessing data. Add or read fields
	 * on this message to interact with clipboard contents.
	 *
	 * @return Pointer to the clipboard data BMessage, or @c NULL if not
	 *         locked.
	 * @see Lock
	 * @see Clear
	 */
			BMessage*			Data() const;

private:
								BClipboard(const BClipboard&);
			BClipboard&			operator=(const BClipboard&);

	virtual	void				_ReservedClipboard1();
	virtual	void				_ReservedClipboard2();
	virtual	void				_ReservedClipboard3();

			bool				_AssertLocked() const;
			status_t			_DownloadFromSystem(bool force = false);
			status_t			_UploadToSystem();

			uint32				_reserved0;
			BMessage*			fData;
			BLocker				fLock;
			BMessenger			fClipHandler;
			BMessenger			fDataSource;
			uint32				fCount;
			char*				fName;
			uint32				_reserved[4];
};

/**
 * @brief Global BClipboard instance representing the default system clipboard.
 *
 * Use this pointer for standard copy/paste operations.
 */
extern BClipboard* be_clipboard;

#endif	// _CLIPBOARD_H
