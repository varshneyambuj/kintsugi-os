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
 *   Copyright 2004-2012, Haiku, Inc. All rights reserved.
 *   Copyright 2001, Dr. Zoidberg Enterprises. All rights reserved.
 *   Copyright 2011, Clemens Zeidler. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file c_mail_api.cpp
 * @brief C-style compatibility stubs for the legacy BeOS mail kit.
 *
 * Implements the flat C functions declared in E-mail.h so that applications
 * written against the original BeOS R5 mail API continue to link without
 * modification. Most functions delegate directly to BMailDaemon,
 * BMailSettings, or BMailAccounts; write operations that lack a modern
 * equivalent return B_NO_REPLY.
 *
 * @see BMailDaemon, BMailSettings, BMailAccounts
 */


#include <stdlib.h>
#include <string.h>

#include <Directory.h>
#include <E-mail.h>
#include <File.h>
#include <FindDirectory.h>
#include <List.h>
#include <Path.h>

#include <crypt.h>
#include <MailDaemon.h>
#include <MailMessage.h>
#include <MailSettings.h>


/**
 * @brief Triggers a mail check and optionally returns the new message count.
 *
 * Asks the mail daemon to check all accounts for new mail. If
 * \a _incomingCount is non-NULL it is filled with the number of new messages
 * available after the check completes.
 *
 * @param _incomingCount  Output pointer for the new message count, or NULL.
 * @return B_OK on success, or a daemon error code on failure.
 */
_EXPORT status_t
check_for_mail(int32* _incomingCount)
{
	status_t status = BMailDaemon().CheckMail();
	if (status != B_OK)
		return status;

	if (_incomingCount != NULL)
		*_incomingCount = BMailDaemon().CountNewMessages(true);

	return B_OK;
}


/**
 * @brief Sends all messages currently queued in the outbox.
 *
 * @return B_OK on success, or a daemon error code on failure.
 */
_EXPORT status_t
send_queued_mail(void)
{
	return BMailDaemon().SendQueuedMail();
}


/**
 * @brief Returns the number of configured POP/inbound accounts.
 *
 * @return Number of inbound mail accounts known to the mail settings.
 */
_EXPORT int32
count_pop_accounts(void)
{
	BMailAccounts accounts;
	return accounts.CountAccounts();
}


/**
 * @brief Retrieves the current mail notification settings.
 *
 * Always returns alert=true and beep=false as a stub implementation.
 *
 * @param notification  Output structure to fill with notification preferences.
 * @return B_OK always.
 */
_EXPORT status_t
get_mail_notification(mail_notification *notification)
{
	notification->alert = true;
	notification->beep = false;
	return B_OK;
}


/**
 * @brief Sets the mail notification preferences (stub — not implemented).
 *
 * @return B_NO_REPLY always; write-back is not supported by the new mail kit.
 */
_EXPORT status_t
set_mail_notification(mail_notification *, bool)
{
	return B_NO_REPLY;
}


/**
 * @brief Retrieves the settings for a POP account by index.
 *
 * Reads account credentials from the BMailAccountSettings at \a index.
 * The encrypted password stored in "cpasswd" is decrypted before copying;
 * the caller does not need to free the returned data.
 *
 * @param account  Output structure to receive the account parameters.
 * @param index    Zero-based index into the list of configured accounts.
 * @return B_OK on success, B_BAD_INDEX if \a index is out of range.
 */
_EXPORT status_t
get_pop_account(mail_pop_account* account, int32 index)
{
	BMailAccounts accounts;
	BMailAccountSettings* accountSettings = accounts.AccountAt(index);
	if (accountSettings == NULL)
		return B_BAD_INDEX;

	const BMessage& settings = accountSettings->InboundSettings();
	strcpy(account->pop_name, settings.FindString("username"));
	strcpy(account->pop_host, settings.FindString("server"));
	strcpy(account->real_name, accountSettings->RealName());
	strcpy(account->reply_to, accountSettings->ReturnAddress());

	const char* encryptedPassword = get_passwd(&settings, "cpasswd");
	const char* password = encryptedPassword;
	if (password == NULL)
		password = settings.FindString("password");
	strcpy(account->pop_password, password);

	delete[] encryptedPassword;
	return B_OK;
}


/**
 * @brief Sets a POP account by index (stub — not implemented).
 *
 * @return B_NO_REPLY always; write-back is not supported by the new mail kit.
 */
_EXPORT status_t
set_pop_account(mail_pop_account *, int32, bool)
{
	return B_NO_REPLY;
}


/**
 * @brief Retrieves the hostname of the default outbound (SMTP) server.
 *
 * Looks up the default outbound account from BMailSettings and copies its
 * server name into \a buffer (caller must provide at least B_MAX_HOST_NAME_LENGTH bytes).
 *
 * @param buffer  Output buffer to receive the null-terminated hostname.
 * @return B_OK on success, B_ERROR if no default account exists,
 *         B_NAME_NOT_FOUND if the account has no server configured.
 */
_EXPORT status_t
get_smtp_host(char* buffer)
{
	BMailAccounts accounts;
	BMailAccountSettings* account = accounts.AccountAt(
		BMailSettings().DefaultOutboundAccount());
	if (account == NULL)
		return B_ERROR;

	const BMessage& settings = account->OutboundSettings();

	if (!settings.HasString("server"))
		return B_NAME_NOT_FOUND;

	strcpy(buffer, settings.FindString("server"));
	return B_OK;
}


/**
 * @brief Sets the default SMTP hostname (stub — not implemented).
 *
 * @return B_NO_REPLY always; write-back is not supported by the new mail kit.
 */
_EXPORT status_t
set_smtp_host(char * /* host */, bool /* save */)
{
	return B_NO_REPLY;
}


/**
 * @brief Forwards an on-disk mail message to a new set of recipients.
 *
 * Opens the RFC 822 file identified by \a ref, creates a BEmailMessage from
 * it, replaces the To field with \a recipients, and sends it immediately or
 * queued depending on \a now.
 *
 * @param ref         entry_ref of the mail file to forward.
 * @param recipients  Comma-separated list of recipient addresses.
 * @param now         If true, send immediately; otherwise queue for later.
 * @return B_OK on success, or a file or daemon error code on failure.
 */
_EXPORT status_t
forward_mail(entry_ref *ref, const char *recipients, bool now)
{
	BFile file(ref, O_RDONLY);
	status_t status = file.InitCheck();
	if (status < B_OK)
		return status;

	BEmailMessage mail(&file);
	mail.SetTo(recipients);

	return mail.Send(now);
}
