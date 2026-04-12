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
 *   Copyright 2004-2012, Haiku Inc. All Rights Reserved.
 *   Copyright 2001-2002 Dr. Zoidberg Enterprises. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MailDaemon.cpp
 * @brief BMailDaemon — IPC proxy for communicating with the mail daemon process.
 *
 * BMailDaemon wraps a BMessenger connected to the background mail daemon
 * (B_MAIL_DAEMON_SIGNATURE) and provides a typed API for requesting mail
 * checks, queue flushes, body fetches, read-status updates, and daemon
 * lifecycle management. Callers should check IsRunning() before invoking
 * other methods to avoid silent failures when the daemon is not active.
 *
 * @see BMailProtocol, BMailSettings, BMailAccounts
 */


#include <MailDaemon.h>

#include <List.h>
#include <MailSettings.h>
#include <Messenger.h>
#include <Message.h>
#include <Roster.h>

#include <MailPrivate.h>


using namespace BPrivate;


/**
 * @brief Constructs a BMailDaemon proxy and connects to the running daemon.
 *
 * Initialises the internal BMessenger with B_MAIL_DAEMON_SIGNATURE.
 * IsRunning() will return false if the daemon is not currently active.
 */
BMailDaemon::BMailDaemon()
	:
	fDaemon(B_MAIL_DAEMON_SIGNATURE)
{
}


/**
 * @brief Destroys the BMailDaemon proxy.
 */
BMailDaemon::~BMailDaemon()
{
}


/**
 * @brief Returns true if the mail daemon is currently running.
 *
 * @return true if the internal messenger is valid (daemon is reachable).
 */
bool
BMailDaemon::IsRunning()
{
	return fDaemon.IsValid();
}


/**
 * @brief Asks the daemon to check for new mail on the specified account.
 *
 * Sends kMsgCheckMessage with the account ID. Passing -1 (or the default)
 * checks all accounts.
 *
 * @param accountID  ID of the account to check, or -1 for all accounts.
 * @return B_OK on successful delivery, B_MAIL_NO_DAEMON if the daemon is not
 *         running, or another BMessenger error code.
 */
status_t
BMailDaemon::CheckMail(int32 accountID)
{
	if (!fDaemon.IsValid())
		return B_MAIL_NO_DAEMON;

	BMessage message(kMsgCheckMessage);
	message.AddInt32("account", accountID);
	return fDaemon.SendMessage(&message);
}


/**
 * @brief Asks the daemon to check for new mail and send queued outbound messages.
 *
 * @param accountID  ID of the account to act on, or -1 for all accounts.
 * @return B_OK on success, B_MAIL_NO_DAEMON if the daemon is not running.
 */
status_t
BMailDaemon::CheckAndSendQueuedMail(int32 accountID)
{
	if (!fDaemon.IsValid())
		return B_MAIL_NO_DAEMON;

	BMessage message(kMsgCheckAndSend);
	message.AddInt32("account", accountID);
	return fDaemon.SendMessage(&message);
}


/**
 * @brief Asks the daemon to send all messages currently waiting in the outbox.
 *
 * @return B_OK on success, B_MAIL_NO_DAEMON if the daemon is not running.
 */
status_t
BMailDaemon::SendQueuedMail()
{
	if (!fDaemon.IsValid())
		return B_MAIL_NO_DAEMON;

	return fDaemon.SendMessage(kMsgSendMessages);
}


/**
 * @brief Returns the number of new (unread) messages available.
 *
 * @param waitForFetchCompletion  If true, blocks until any in-progress mail
 *                                fetch has finished before returning the count.
 * @return Number of new messages, or B_MAIL_NO_DAEMON (negative) on error.
 */
int32
BMailDaemon::CountNewMessages(bool waitForFetchCompletion)
{
	if (!fDaemon.IsValid())
		return B_MAIL_NO_DAEMON;

	BMessage reply;
	BMessage first(kMsgCountNewMessages);

	if (waitForFetchCompletion)
		first.AddBool("wait_for_fetch_done",true);

	fDaemon.SendMessage(&first, &reply);

	return reply.FindInt32("num_new_messages");
}


/**
 * @brief Marks a mail file as read, seen, or unread in the daemon's index.
 *
 * @param account  Account ID that owns the message.
 * @param ref      entry_ref of the mail file to update.
 * @param flag     Read status flag (B_READ, B_SEEN, or B_UNREAD).
 * @return B_OK on success, B_MAIL_NO_DAEMON if the daemon is not running.
 */
status_t
BMailDaemon::MarkAsRead(int32 account, const entry_ref& ref, read_flags flag)
{
	if (!fDaemon.IsValid())
		return B_MAIL_NO_DAEMON;

	BMessage message(kMsgMarkMessageAsRead);
	message.AddInt32("account", account);
	message.AddRef("ref", &ref);
	message.AddInt32("read", flag);

	return fDaemon.SendMessage(&message);
}


/**
 * @brief Requests that the daemon fetch the full body of a partially downloaded message.
 *
 * Sends kMsgFetchBody and waits synchronously for the daemon's reply.
 * An optional \a listener messenger receives the B_MAIL_BODY_FETCHED notification.
 *
 * @param ref       entry_ref of the partially fetched mail file.
 * @param listener  Optional BMessenger to notify when the fetch is complete.
 * @return B_OK on success, B_MAIL_NO_DAEMON if the daemon is not running, or
 *         a BMessenger error code.
 */
status_t
BMailDaemon::FetchBody(const entry_ref& ref, BMessenger* listener)
{
	if (!fDaemon.IsValid())
		return B_MAIL_NO_DAEMON;

	BMessage message(kMsgFetchBody);
	message.AddRef("refs", &ref);
	if (listener != NULL)
		message.AddMessenger("target", *listener);

	BMessage reply;
	return fDaemon.SendMessage(&message, &reply);
}


/**
 * @brief Sends B_QUIT_REQUESTED to the mail daemon, asking it to shut down.
 *
 * @return B_OK on success, B_MAIL_NO_DAEMON if the daemon is not running.
 */
status_t
BMailDaemon::Quit()
{
	if (!fDaemon.IsValid())
		return B_MAIL_NO_DAEMON;

	return fDaemon.SendMessage(B_QUIT_REQUESTED);
}


/**
 * @brief Launches the mail daemon if it is not already running.
 *
 * Uses be_roster to launch B_MAIL_DAEMON_SIGNATURE, then re-initialises the
 * internal messenger if the launch succeeded.
 *
 * @return B_OK if the daemon was launched successfully, or an error code
 *         from BRoster::Launch().
 */
status_t
BMailDaemon::Launch()
{
	status_t status = be_roster->Launch(B_MAIL_DAEMON_SIGNATURE);
	if (status == B_OK)
		fDaemon = BMessenger(B_MAIL_DAEMON_SIGNATURE);

	return status;
}
