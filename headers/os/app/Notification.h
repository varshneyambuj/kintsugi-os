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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */
#ifndef _NOTIFICATION_H
#define _NOTIFICATION_H

/**
 * @file Notification.h
 * @brief Provides the BNotification class for displaying system notifications
 *        and the notification_type enum for classifying notification severity.
 */


#include <Archivable.h>
#include <Entry.h>
#include <List.h>
#include <String.h>


/**
 * @enum notification_type
 * @brief Specifies the visual style and urgency of a BNotification.
 */
enum notification_type {
	B_INFORMATION_NOTIFICATION,  /**< General informational notification. */
	B_IMPORTANT_NOTIFICATION,    /**< Important notification requiring attention. */
	B_ERROR_NOTIFICATION,        /**< Error notification indicating a problem. */
	B_PROGRESS_NOTIFICATION      /**< Notification with a progress bar (0.0 to 1.0). */
};

class BBitmap;


/**
 * @brief Represents a system notification that can be displayed to the user.
 *
 * BNotification allows applications to post notifications to the system
 * notification panel. Notifications can carry a title, content text, an icon,
 * and optional on-click actions. Progress notifications can also display a
 * progress bar.
 *
 * Typical usage:
 * @code
 *   BNotification notification(B_INFORMATION_NOTIFICATION);
 *   notification.SetTitle("Download Complete");
 *   notification.SetContent("myfile.zip has been saved.");
 *   notification.Send();
 * @endcode
 *
 * BNotification inherits from BArchivable and can be serialized to/from a
 * BMessage.
 *
 * @see notification_type
 * @see BArchivable
 */
class BNotification : public BArchivable {
public:
	/**
	 * @brief Constructs a new notification of the specified type.
	 * @param type The notification_type controlling its visual style.
	 */
								BNotification(notification_type type);

	/**
	 * @brief Constructs a BNotification from an archived BMessage.
	 * @param archive Pointer to a BMessage containing archived notification
	 *        data.
	 * @see Archive
	 * @see Instantiate
	 */
								BNotification(BMessage* archive);

	/**
	 * @brief Destroys the BNotification and frees associated resources.
	 */
	virtual						~BNotification();

	/**
	 * @brief Checks whether the notification was properly initialized.
	 * @return @c B_OK if valid, or an error code describing the problem.
	 */
			status_t			InitCheck() const;

	/**
	 * @brief Instantiates a BNotification from an archived BMessage.
	 * @param archive Pointer to the archived BMessage.
	 * @return A pointer to the new BArchivable object, or @c NULL on failure.
	 * @see Archive
	 */
	static	BArchivable*		Instantiate(BMessage* archive);

	/**
	 * @brief Archives the notification into a BMessage.
	 * @param archive Pointer to the BMessage to receive the archived data.
	 * @param deep If @c true, child objects are also archived.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see Instantiate
	 */
	virtual	status_t			Archive(BMessage* archive, bool deep = true) const;

	/**
	 * @brief Returns the MIME signature of the sending application.
	 * @return A null-terminated string with the source application's signature.
	 */
			const char*			SourceSignature() const;

	/**
	 * @brief Returns the name of the sending application.
	 * @return A null-terminated string with the source application's name.
	 */
			const char*			SourceName() const;

	/**
	 * @brief Returns the notification type.
	 * @return The notification_type of this notification.
	 */
			notification_type	Type() const;

	/**
	 * @brief Returns the notification group name.
	 *
	 * Notifications in the same group may be visually grouped together.
	 *
	 * @return The group name string, or @c NULL if not set.
	 * @see SetGroup
	 */
			const char*			Group() const;

	/**
	 * @brief Sets the notification group name.
	 * @param group The group name to assign.
	 * @see Group
	 */
			void				SetGroup(const BString& group);

	/**
	 * @brief Returns the notification title.
	 * @return The title string, or @c NULL if not set.
	 * @see SetTitle
	 */
			const char*			Title() const;

	/**
	 * @brief Sets the notification title.
	 * @param title The title string to display.
	 * @see Title
	 */
			void				SetTitle(const BString& title);

	/**
	 * @brief Returns the notification content text.
	 * @return The content string, or @c NULL if not set.
	 * @see SetContent
	 */
			const char*			Content() const;

	/**
	 * @brief Sets the notification content text.
	 * @param content The body text to display in the notification.
	 * @see Content
	 */
			void				SetContent(const BString& content);

	/**
	 * @brief Returns the unique message identifier.
	 *
	 * The message ID can be used to update or replace an existing notification.
	 *
	 * @return The message ID string, or @c NULL if not set.
	 * @see SetMessageID
	 */
			const char*			MessageID() const;

	/**
	 * @brief Sets the unique message identifier.
	 * @param id The message ID string.
	 * @see MessageID
	 */
			void				SetMessageID(const BString& id);

	/**
	 * @brief Returns the current progress value.
	 *
	 * Only meaningful for B_PROGRESS_NOTIFICATION type notifications.
	 *
	 * @return A float in the range 0.0 to 1.0 representing progress.
	 * @see SetProgress
	 */
			float				Progress() const;

	/**
	 * @brief Sets the progress value for a progress notification.
	 * @param progress A float from 0.0 (0%) to 1.0 (100%).
	 * @see Progress
	 */
			void				SetProgress(float progress);

	/**
	 * @brief Returns the application signature to launch when the notification
	 *        is clicked.
	 * @return The on-click application signature, or @c NULL if not set.
	 * @see SetOnClickApp
	 */
			const char*			OnClickApp() const;

	/**
	 * @brief Sets the application to launch when the notification is clicked.
	 * @param app The MIME application signature to launch.
	 * @see OnClickApp
	 */
			void				SetOnClickApp(const BString& app);

	/**
	 * @brief Returns the file to open when the notification is clicked.
	 * @return Pointer to the on-click entry_ref, or @c NULL if not set.
	 * @see SetOnClickFile
	 */
			const entry_ref*	OnClickFile() const;

	/**
	 * @brief Sets a file to open when the notification is clicked.
	 * @param file Pointer to an entry_ref identifying the file.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see OnClickFile
	 */
			status_t			SetOnClickFile(const entry_ref* file);

	/**
	 * @brief Adds a file reference to pass to the on-click application.
	 * @param ref Pointer to an entry_ref to add.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see CountOnClickRefs
	 * @see OnClickRefAt
	 */
			status_t			AddOnClickRef(const entry_ref* ref);

	/**
	 * @brief Returns the number of on-click file references.
	 * @return The number of entry_ref items added via AddOnClickRef().
	 */
			int32				CountOnClickRefs() const;

	/**
	 * @brief Returns the on-click file reference at the given index.
	 * @param index Zero-based index of the reference.
	 * @return Pointer to the entry_ref, or @c NULL if the index is invalid.
	 */
			const entry_ref*	OnClickRefAt(int32 index) const;

	/**
	 * @brief Adds a command-line argument to pass to the on-click application.
	 * @param arg The argument string to add.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see CountOnClickArgs
	 * @see OnClickArgAt
	 */
			status_t			AddOnClickArg(const BString& arg);

	/**
	 * @brief Returns the number of on-click command-line arguments.
	 * @return The number of argument strings added via AddOnClickArg().
	 */
			int32				CountOnClickArgs() const;

	/**
	 * @brief Returns the on-click argument at the given index.
	 * @param index Zero-based index of the argument.
	 * @return The argument string, or @c NULL if the index is invalid.
	 */
			const char*			OnClickArgAt(int32 index) const;

	/**
	 * @brief Returns the notification icon.
	 * @return Pointer to the BBitmap icon, or @c NULL if no icon is set.
	 * @see SetIcon
	 */
			const BBitmap*		Icon() const;

	/**
	 * @brief Sets the notification icon.
	 *
	 * The bitmap is copied internally; the caller retains ownership of the
	 * original.
	 *
	 * @param icon Pointer to the BBitmap to use as the icon, or @c NULL to
	 *        clear.
	 * @return @c B_OK on success, or an error code on failure.
	 * @see Icon
	 */
			status_t			SetIcon(const BBitmap* icon);

	/**
	 * @brief Sends the notification to the notification server for display.
	 * @param timeout Duration in microseconds the notification is displayed.
	 *        Use -1 for the system default timeout.
	 * @return @c B_OK on success, or an error code on failure.
	 */
			status_t			Send(bigtime_t timeout = -1);

private:
	virtual	void _ReservedNotification1();
	virtual	void _ReservedNotification2();
	virtual	void _ReservedNotification3();
	virtual	void _ReservedNotification4();
	virtual	void _ReservedNotification5();
	virtual	void _ReservedNotification6();
	virtual	void _ReservedNotification7();
	virtual	void _ReservedNotification8();

			status_t			fInitStatus;

			BString				fSourceSignature;
			BString				fSourceName;
			notification_type	fType;
			BString				fGroup;
			BString				fTitle;
			BString				fContent;
			BString				fID;
			float				fProgress;

			BString				fApp;
			entry_ref*			fFile;
			BList				fRefs;
			BList				fArgv;
			BBitmap*			fBitmap;

			uint32				_reserved[8];
};


#endif	// _NOTIFICATION_H
