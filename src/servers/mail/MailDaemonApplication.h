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
   Copyright 2007-2015, Haiku, Inc. All rights reserved.
   Copyright 2001-2002 Dr. Zoidberg Enterprises. All rights reserved.
   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
   Distributed under the terms of the MIT License.
 */
/** @file MailDaemonApplication.h
 *  @brief Mail daemon server application with account management. */
#ifndef MAIL_DAEMON_APPLICATION_H
#define MAIL_DAEMON_APPLICATION_H


#include <map>

#include <ObjectList.h>
#include <Message.h>
#include <MessageRunner.h>
#include <Node.h>
#include <Query.h>
#include <Server.h>
#include <String.h>

#include <MailProtocol.h>

#include "LEDAnimation.h"
#include "DefaultNotifier.h"


class BNotification;
struct send_mails_info;


/** @brief Holds inbound and outbound protocol instances for a mail account. */
struct account_protocols {
	account_protocols();

	image_id				inboundImage;
	BInboundMailProtocol*	inboundProtocol;
	image_id				outboundImage;
	BOutboundMailProtocol*	outboundProtocol;
};


typedef std::map<int32, account_protocols> AccountMap;


/** @brief Central mail server managing accounts, fetching, and sending. */
class MailDaemonApplication : public BServer {
public:
								/** @brief Construct the mail daemon server. */
								MailDaemonApplication();
	/** @brief Destructor; releases accounts and resources. */
	virtual						~MailDaemonApplication();

	/** @brief Install deskbar icon and initialize accounts. */
	virtual void				ReadyToRun();
	/** @brief Fetch message bodies for the given refs. */
	virtual	void				RefsReceived(BMessage* message);
	/** @brief Dispatch mail check, send, and settings messages. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Stop LED animation on user activity. */
	virtual void				Pulse();
	/** @brief Remove deskbar icon before quitting. */
	virtual bool				QuitRequested();

			/** @brief Add the mail status replicant to the deskbar. */
			void				InstallDeskbarIcon();
			/** @brief Remove the mail status replicant from the deskbar. */
			void				RemoveDeskbarIcon();

			/** @brief Check for new messages on one or all accounts. */
			void				GetNewMessages(BMessage* message);
			/** @brief Send all queued outgoing messages. */
			void				SendPendingMessages(BMessage* message);

			/** @brief Register e-mail MIME types and attributes. */
			void				MakeMimeTypes(bool remakeMIMETypes = false);

private:
			void				_InitAccounts();
			void				_InitAccount(BMailAccountSettings& settings);
			void				_ReloadAccounts(BMessage* message);
			void				_RemoveAccount(
									const account_protocols& account);

			BInboundMailProtocol* _CreateInboundProtocol(
									BMailAccountSettings& settings,
									image_id& image);
			BOutboundMailProtocol* _CreateOutboundProtocol(
									BMailAccountSettings& settings,
									image_id& image);

			BInboundMailProtocol* _InboundProtocol(int32 account);
			BOutboundMailProtocol* _OutboundProtocol(int32 account);

			void				_InitNewMessagesCount();
			void				_UpdateNewMessagesNotification();
			void				_UpdateAutoCheckRunner();

			void				_AddMessage(send_mails_info& info,
									const BEntry& entry, const BNode& node);

	static	bool				_IsPending(BNode& node);
	static	bool				_IsEntryInTrash(BEntry& entry);

private:
			BMessageRunner*		fAutoCheckRunner; /**< Periodic auto-check message runner */
			BMailSettings		fSettingsFile; /**< Mail daemon settings */

			int32				fNewMessages; /**< Current count of new (unread) messages */
			bool				fCentralBeep; /**< Deferred beep flag for batch notification */
				// TRUE to do a beep when the status window closes. This happens
				// when all mail has been received, so you get one beep for
				// everything rather than individual beeps for each mail
				// account.
				// Set to TRUE by the 'mcbp' message that the mail Notification
				// filter sends us, cleared when the beep is done.
			BObjectList<BMessage> fFetchDoneRespondents; /**< Pending replies waiting for fetch completion */
			BObjectList<BQuery>	fQueries; /**< Live queries tracking new messages */

			LEDAnimation*		fLEDAnimation; /**< Keyboard LED animation instance */

			BString				fAlertString; /**< Accumulated alert text for new messages */

			AccountMap			fAccounts; /**< Map of account ID to protocol instances */

			ErrorLogWindow*		fErrorLogWindow; /**< Error log window reference */
			BNotification*		fNotification; /**< System notification for new messages */
};


#endif // MAIL_DAEMON_APPLICATION_H
