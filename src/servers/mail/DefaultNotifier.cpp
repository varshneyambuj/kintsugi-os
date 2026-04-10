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
   Copyright 2011-2012, Haiku, Inc. All rights reserved.
   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
   
   Distributed under the terms of the MIT License.
 */
/** @file DefaultNotifier.cpp
 *  @brief Default mail notification handler using system notifications. */
#include "DefaultNotifier.h"

#include <Catalog.h>
#include <IconUtils.h>
#include <MailDaemon.h>
#include <Roster.h>

#include <MailPrivate.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Notifier"


/**
 * @brief Constructs a default mail notifier for a given account.
 *
 * Sets up a progress notification with the mail daemon icon and an
 * appropriate title for inbound or outbound operations.
 *
 * @param accountName Name of the mail account.
 * @param inbound     True for incoming mail, false for outgoing.
 * @param errorWindow Window to display error/status messages.
 * @param showMode    Bitmask controlling when notifications are shown.
 */
DefaultNotifier::DefaultNotifier(const char* accountName, bool inbound,
	ErrorLogWindow* errorWindow, uint32 showMode)
	:
	fAccountName(accountName),
	fIsInbound(inbound),
	fErrorWindow(errorWindow),
	fNotification(B_PROGRESS_NOTIFICATION),
	fShowMode(showMode),
	fTotalItems(0),
	fItemsDone(0),
	fTotalSize(0),
	fSizeDone(0)
{
	BString desc(fIsInbound ? B_TRANSLATE("Fetching mail for %name")
		: B_TRANSLATE("Sending mail for %name"));
	desc.ReplaceFirst("%name", fAccountName);

	BString identifier;
	identifier << accountName << inbound;
		// Two windows for each acocunt : one for sending and the other for
		// receiving mails
	fNotification.SetMessageID(identifier);
	fNotification.SetGroup(B_TRANSLATE("Mail status"));
	fNotification.SetTitle(desc);

	app_info info;
	be_roster->GetAppInfo(B_MAIL_DAEMON_SIGNATURE, &info);
	BBitmap icon(BRect(0, 0, 32, 32), B_RGBA32);
	BNode node(&info.ref);
	BIconUtils::GetVectorIcon(&node, "BEOS:ICON", &icon);
	fNotification.SetIcon(&icon);
}


/** @brief Destroys the default notifier. */
DefaultNotifier::~DefaultNotifier()
{
}


/**
 * @brief Creates a copy of this notifier with the same settings.
 *
 * @return A new DefaultNotifier instance.
 */
BMailNotifier*
DefaultNotifier::Clone()
{
	return new DefaultNotifier(fAccountName, fIsInbound, fErrorWindow,
		fShowMode);
}


/**
 * @brief Displays an error message in the error log window.
 *
 * @param error The error message text.
 */
void
DefaultNotifier::ShowError(const char* error)
{
	fErrorWindow->AddError(B_WARNING_ALERT, error, fAccountName);
}


/**
 * @brief Displays an informational message in the error log window.
 *
 * @param message The message text.
 */
void
DefaultNotifier::ShowMessage(const char* message)
{
	fErrorWindow->AddError(B_INFO_ALERT, message, fAccountName);
}


/**
 * @brief Sets the total number of messages to process and updates the notification.
 *
 * @param items Total message count.
 */
void
DefaultNotifier::SetTotalItems(uint32 items)
{
	fTotalItems = items;
	BString progress;
	progress << fItemsDone << "/" << fTotalItems;
	fNotification.SetContent(progress);
}


/**
 * @brief Sets the total byte size of all messages and updates the progress bar.
 *
 * @param size Total size in bytes.
 */
void
DefaultNotifier::SetTotalItemsSize(uint64 size)
{
	fTotalSize = size;
	fNotification.SetProgress(fSizeDone / (float)fTotalSize);
}


/**
 * @brief Reports incremental progress in message processing.
 *
 * Updates the progress bar and notification content based on the number
 * of bytes and messages processed so far. Sends the notification if
 * the show mode allows it.
 *
 * @param messages Number of messages just completed.
 * @param bytes    Number of bytes just transferred.
 * @param message  Status text to display.
 */
void
DefaultNotifier::ReportProgress(uint32 messages, uint64 bytes,
	const char* message)
{
	fSizeDone += bytes;
	fItemsDone += messages;

	if (fTotalSize > 0)
		fNotification.SetProgress(fSizeDone / (float)fTotalSize);
	else if (fTotalItems > 0) {
		// No size information available
		// Report progress in terms of message count instead
		fNotification.SetProgress(fItemsDone / (float)fTotalItems);
	} else {
		// No message count information either
		// TODO: we should use a B_INFORMATION_NOTIFICATION here, but it is not
		// possible to change the BNotification type after creating it...
		fNotification.SetProgress(0);
	}

	BString progress;
	progress << message << "\t";

	if (fTotalItems > 0)
		progress << fItemsDone << "/" << fTotalItems;

	fNotification.SetContent(progress);

	int timeout = 0; // Default timeout
	if (fItemsDone >= fTotalItems && fTotalItems != 0)
		timeout = 1; // We're done, make the window go away faster

	_NotifyIfAllowed(timeout);
}


/**
 * @brief Resets progress counters and optionally updates the notification title.
 *
 * @param message Optional new title text, or NULL to keep the current title.
 */
void
DefaultNotifier::ResetProgress(const char* message)
{
	fSizeDone = 0;
	fItemsDone = 0;
	fNotification.SetProgress(0);
	if (message != NULL)
		fNotification.SetTitle(message);
	_NotifyIfAllowed(1); // go away faster
}


/**
 * @brief Sends the notification if the current show mode permits it.
 *
 * @param timeout Notification auto-dismiss timeout in seconds.
 */
void
DefaultNotifier::_NotifyIfAllowed(int timeout)
{
	int32 flag;
	if (fIsInbound)
		flag = B_MAIL_SHOW_STATUS_WINDOW_WHEN_ACTIVE;
	else
		flag = B_MAIL_SHOW_STATUS_WINDOW_WHEN_SENDING;

	if ((fShowMode & flag) != 0)
		fNotification.Send(timeout);
}
