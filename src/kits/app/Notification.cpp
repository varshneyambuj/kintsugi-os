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
 *
 *   Authors:
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *       Stephan Aßmus, superstippi@gmx.de
 *       Brian Hill, supernova@tycho.email
 */


/** @file Notification.cpp
 *  @brief Implementation of BNotification, the system notification interface.
 *
 *  BNotification allows applications to display system notifications to the
 *  user. Notifications can include a title, content, icon, progress bar, and
 *  on-click actions such as launching an application or opening a file.
 */


#include <Notification.h>

#include <new>

#include <stdlib.h>
#include <string.h>

#include <notification/Notifications.h>

#include <Bitmap.h>
#include <Message.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Roster.h>


/** @brief Constructs a new notification of the specified type.
 *
 *  Automatically determines the source application signature, name,
 *  and icon from the running application's info.
 *
 *  @param type The notification type (e.g., B_INFORMATION_NOTIFICATION,
 *              B_IMPORTANT_NOTIFICATION, B_ERROR_NOTIFICATION,
 *              B_PROGRESS_NOTIFICATION).
 */
BNotification::BNotification(notification_type type)
	:
	BArchivable(),
	fInitStatus(B_OK),
	fType(type),
	fProgress(0.f),
	fFile(NULL),
	fBitmap(NULL)
{
	team_info teamInfo;
	get_team_info(B_CURRENT_TEAM, &teamInfo);
	app_info appInfo;
	be_roster->GetRunningAppInfo(teamInfo.team, &appInfo);

	int32 iconSize = B_LARGE_ICON;
	fBitmap = new BBitmap(BRect(0, 0, iconSize - 1, iconSize - 1), 0, B_RGBA32);
	if (fBitmap) {
		if (BNodeInfo::GetTrackerIcon(&appInfo.ref, fBitmap,
			icon_size(iconSize)) != B_OK) {
			delete fBitmap;
			fBitmap = NULL;
		}
	}
	fSourceSignature = appInfo.signature;
	BPath path(&appInfo.ref);
	if (path.InitCheck() == B_OK)
		fSourceName = path.Leaf();
}


/** @brief Constructs a notification from an archived BMessage.
 *
 *  Restores all notification properties from the archive, including type,
 *  title, content, group, icon, on-click references, and arguments.
 *
 *  @param archive The archived message to restore from.
 */
BNotification::BNotification(BMessage* archive)
	:
	BArchivable(archive),
	fInitStatus(B_OK),
	fProgress(0.0f),
	fFile(NULL),
	fBitmap(NULL)
{
	BString appName;
	if (archive->FindString("_appname", &appName) == B_OK)
		fSourceName = appName;

	BString signature;
	if (archive->FindString("_signature", &signature) == B_OK)
		fSourceSignature = signature;

	int32 type;
	if (archive->FindInt32("_type", &type) == B_OK)
		fType = (notification_type)type;
	else
		fInitStatus = B_ERROR;

	BString group;
	if (archive->FindString("_group", &group) == B_OK)
		SetGroup(group);

	BString title;
	if (archive->FindString("_title", &title) == B_OK)
		SetTitle(title);

	BString content;
	if (archive->FindString("_content", &content) == B_OK)
		SetContent(content);

	BString messageID;
	if (archive->FindString("_messageID", &messageID) == B_OK)
		SetMessageID(messageID);

	float progress;
	if (type == B_PROGRESS_NOTIFICATION
		&& archive->FindFloat("_progress", &progress) == B_OK)
		SetProgress(progress);

	BString onClickApp;
	if (archive->FindString("_onClickApp", &onClickApp) == B_OK)
		SetOnClickApp(onClickApp);

	entry_ref onClickFile;
	if (archive->FindRef("_onClickFile", &onClickFile) == B_OK)
		SetOnClickFile(&onClickFile);

	entry_ref onClickRef;
	int32 index = 0;
	while (archive->FindRef("_onClickRef", index++, &onClickRef) == B_OK)
		AddOnClickRef(&onClickRef);

	BString onClickArgv;
	index = 0;
	while (archive->FindString("_onClickArgv", index++, &onClickArgv) == B_OK)
		AddOnClickArg(onClickArgv);

	status_t ret = B_OK;
	BMessage icon;
	if ((ret = archive->FindMessage("_icon", &icon)) == B_OK) {
		BBitmap bitmap(&icon);
		ret = bitmap.InitCheck();
		if (ret == B_OK)
			ret = SetIcon(&bitmap);
	}
}


/** @brief Destroys the notification and frees all associated resources.
 *
 *  Releases the on-click file, icon bitmap, entry references, and
 *  argument strings.
 */
BNotification::~BNotification()
{
	delete fFile;
	delete fBitmap;

	for (int32 i = fRefs.CountItems() - 1; i >= 0; i--)
		delete (entry_ref*)fRefs.ItemAtFast(i);

	for (int32 i = fArgv.CountItems() - 1; i >= 0; i--)
		free(fArgv.ItemAtFast(i));
}


/** @brief Returns the initialization status of the notification.
 *  @return B_OK if the notification was initialized successfully, or an error code.
 */
status_t
BNotification::InitCheck() const
{
	return fInitStatus;
}


/** @brief Creates a new BNotification from an archived message.
 *  @param archive The archived message to instantiate from.
 *  @return A new BNotification object, or NULL if instantiation validation fails.
 */
BArchivable*
BNotification::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BNotification"))
		return new(std::nothrow) BNotification(archive);

	return NULL;
}


/** @brief Archives the notification into a BMessage.
 *
 *  Stores all notification properties including source info, type, group,
 *  title, content, message ID, progress, on-click targets, and icon.
 *
 *  @param archive The message to archive into.
 *  @param deep    Whether to perform a deep archive (includes child objects).
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BNotification::Archive(BMessage* archive, bool deep) const
{
	status_t status = BArchivable::Archive(archive, deep);

	if (status == B_OK)
		status = archive->AddString("_appname", fSourceName);

	if (status == B_OK)
		status = archive->AddString("_signature", fSourceSignature);

	if (status == B_OK)
		status = archive->AddInt32("_type", (int32)fType);

	if (status == B_OK && Group() != NULL)
		status = archive->AddString("_group", Group());

	if (status == B_OK && Title() != NULL)
		status = archive->AddString("_title", Title());

	if (status == B_OK && Content() != NULL)
		status = archive->AddString("_content", Content());

	if (status == B_OK && MessageID() != NULL)
		status = archive->AddString("_messageID", MessageID());

	if (status == B_OK && Type() == B_PROGRESS_NOTIFICATION)
		status = archive->AddFloat("_progress", Progress());

	if (status == B_OK && OnClickApp() != NULL)
		status = archive->AddString("_onClickApp", OnClickApp());

	if (status == B_OK && OnClickFile() != NULL)
		status = archive->AddRef("_onClickFile", OnClickFile());

	if (status == B_OK) {
		for (int32 i = 0; i < CountOnClickRefs(); i++) {
			status = archive->AddRef("_onClickRef", OnClickRefAt(i));
			if (status != B_OK)
				break;
		}
	}

	if (status == B_OK) {
		for (int32 i = 0; i < CountOnClickArgs(); i++) {
			status = archive->AddString("_onClickArgv", OnClickArgAt(i));
			if (status != B_OK)
				break;
		}
	}

	if (status == B_OK) {
		const BBitmap* icon = Icon();
		if (icon != NULL) {
			BMessage iconArchive;
			status = icon->Archive(&iconArchive);
			if (status == B_OK)
				archive->AddMessage("_icon", &iconArchive);
		}
	}

	return status;
}


/** @brief Returns the MIME signature of the source application.
 *  @return The source application's MIME signature string.
 */
const char*
BNotification::SourceSignature() const
{
	return fSourceSignature;
}


/** @brief Returns the name of the source application.
 *  @return The source application name string.
 */
const char*
BNotification::SourceName() const
{
	return fSourceName;
}


/** @brief Returns the type of this notification.
 *  @return The notification_type value.
 */
notification_type
BNotification::Type() const
{
	return fType;
}


/** @brief Returns the group name of this notification.
 *  @return The group name string, or NULL if no group is set.
 */
const char*
BNotification::Group() const
{
	if (fGroup == "")
		return NULL;
	return fGroup;
}


/** @brief Sets the group name for this notification.
 *  @param group The group name to assign.
 */
void
BNotification::SetGroup(const BString& group)
{
	fGroup = group;
}


/** @brief Returns the title of this notification.
 *  @return The title string, or NULL if no title is set.
 */
const char*
BNotification::Title() const
{
	if (fTitle == "")
		return NULL;
	return fTitle;
}


/** @brief Sets the title of this notification.
 *  @param title The title string to assign.
 */
void
BNotification::SetTitle(const BString& title)
{
	fTitle = title;
}


/** @brief Returns the content text of this notification.
 *  @return The content string, or NULL if no content is set.
 */
const char*
BNotification::Content() const
{
	if (fContent == "")
		return NULL;
	return fContent;
}


/** @brief Sets the content text of this notification.
 *  @param content The content string to assign.
 */
void
BNotification::SetContent(const BString& content)
{
	fContent = content;
}


/** @brief Returns the message ID of this notification.
 *
 *  The message ID can be used to update or replace an existing notification.
 *
 *  @return The message ID string, or NULL if no ID is set.
 */
const char*
BNotification::MessageID() const
{
	if (fID == "")
		return NULL;
	return fID;
}


/** @brief Sets the message ID for this notification.
 *  @param id The message ID string to assign.
 */
void
BNotification::SetMessageID(const BString& id)
{
	fID = id;
}


/** @brief Returns the progress value for a progress notification.
 *
 *  Only meaningful for notifications of type B_PROGRESS_NOTIFICATION.
 *
 *  @return The progress value between 0.0 and 1.0, or -1 if not a progress notification.
 */
float
BNotification::Progress() const
{
	if (fType != B_PROGRESS_NOTIFICATION)
		return -1;
	return fProgress;
}


/** @brief Sets the progress value for a progress notification.
 *
 *  The value is clamped to the range [0.0, 1.0].
 *
 *  @param progress The progress value to set.
 */
void
BNotification::SetProgress(float progress)
{
	if (progress < 0)
		fProgress = 0;
	else if (progress > 1)
		fProgress = 1;
	else
		fProgress = progress;
}


/** @brief Returns the MIME signature of the application to launch on click.
 *  @return The on-click application signature, or NULL if not set.
 */
const char*
BNotification::OnClickApp() const
{
	if (fApp == "")
		return NULL;
	return fApp;
}


/** @brief Sets the application to launch when the notification is clicked.
 *  @param app The MIME signature of the target application.
 */
void
BNotification::SetOnClickApp(const BString& app)
{
	fApp = app;
}


/** @brief Returns the file reference to open when the notification is clicked.
 *  @return A pointer to the on-click entry_ref, or NULL if not set.
 */
const entry_ref*
BNotification::OnClickFile() const
{
	return fFile;
}


/** @brief Sets the file to open when the notification is clicked.
 *  @param file The entry_ref of the file, or NULL to clear.
 *  @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
status_t
BNotification::SetOnClickFile(const entry_ref* file)
{
	delete fFile;

	if (file != NULL) {
		fFile = new(std::nothrow) entry_ref(*file);
		if (fFile == NULL)
			return B_NO_MEMORY;
	} else
		fFile = NULL;

	return B_OK;
}


/** @brief Adds an entry reference to the on-click reference list.
 *  @param ref The entry_ref to add.
 *  @return B_OK on success, B_BAD_VALUE if ref is NULL, or B_NO_MEMORY on failure.
 */
status_t
BNotification::AddOnClickRef(const entry_ref* ref)
{
	if (ref == NULL)
		return B_BAD_VALUE;

	entry_ref* clonedRef = new(std::nothrow) entry_ref(*ref);
	if (clonedRef == NULL || !fRefs.AddItem(clonedRef))
		return B_NO_MEMORY;

	return B_OK;
}


/** @brief Returns the number of on-click entry references.
 *  @return The count of on-click references.
 */
int32
BNotification::CountOnClickRefs() const
{
	return fRefs.CountItems();
}


/** @brief Returns the on-click entry reference at the given index.
 *  @param index The zero-based index of the reference.
 *  @return A pointer to the entry_ref, or NULL if the index is out of range.
 */
const entry_ref*
BNotification::OnClickRefAt(int32 index) const
{
	return (entry_ref*)fRefs.ItemAt(index);
}


/** @brief Adds a command-line argument to the on-click argument list.
 *  @param arg The argument string to add.
 *  @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
status_t
BNotification::AddOnClickArg(const BString& arg)
{
	char* clonedArg = strdup(arg.String());
	if (clonedArg == NULL || !fArgv.AddItem(clonedArg))
		return B_NO_MEMORY;

	return B_OK;
}


/** @brief Returns the number of on-click command-line arguments.
 *  @return The count of on-click arguments.
 */
int32
BNotification::CountOnClickArgs() const
{
	return fArgv.CountItems();
}


/** @brief Returns the on-click argument string at the given index.
 *  @param index The zero-based index of the argument.
 *  @return The argument string, or NULL if the index is out of range.
 */
const char*
BNotification::OnClickArgAt(int32 index) const
{
	return (char*)fArgv.ItemAt(index);
}


/** @brief Returns the notification icon bitmap.
 *  @return A pointer to the icon BBitmap, or NULL if no icon is set.
 */
const BBitmap*
BNotification::Icon() const
{
	return fBitmap;
}


/** @brief Sets the notification icon.
 *
 *  A copy of the provided bitmap is made. Pass NULL to clear the icon.
 *
 *  @param icon The bitmap to use as the icon, or NULL to clear.
 *  @return B_OK on success, or B_NO_MEMORY if allocation fails.
 */
status_t
BNotification::SetIcon(const BBitmap* icon)
{
	delete fBitmap;

	if (icon != NULL) {
		fBitmap = new(std::nothrow) BBitmap(icon);
		if (fBitmap == NULL)
			return B_NO_MEMORY;
		return fBitmap->InitCheck();
	}

	fBitmap = NULL;
	return B_OK;
}


/** @brief Sends the notification to the notification server for display.
 *
 *  Archives the notification and sends it to the system notification
 *  server. An optional timeout can be specified to control how long
 *  the notification is displayed.
 *
 *  @param timeout Duration in microseconds to display the notification,
 *                 or 0 for the default duration.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
BNotification::Send(bigtime_t timeout)
{
	BMessage msg(kNotificationMessage);

	// Archive notification
	status_t ret = Archive(&msg);

	// Custom time out
	if (ret == B_OK && timeout > 0)
		ret = msg.AddInt64("timeout", timeout);

	// Send message
	if (ret == B_OK) {
		BMessenger server(kNotificationServerSignature);
		ret = server.SendMessage(&msg);
	}

	return ret;
}


void BNotification::_ReservedNotification1() {}
void BNotification::_ReservedNotification2() {}
void BNotification::_ReservedNotification3() {}
void BNotification::_ReservedNotification4() {}
void BNotification::_ReservedNotification5() {}
void BNotification::_ReservedNotification6() {}
void BNotification::_ReservedNotification7() {}
void BNotification::_ReservedNotification8() {}
