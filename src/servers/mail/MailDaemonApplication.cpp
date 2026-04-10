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
   Copyright 2007-2016, Haiku, Inc. All rights reserved.
   Copyright 2001-2002 Dr. Zoidberg Enterprises. All rights reserved.
   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
   Distributed under the terms of the MIT License.
 */
/** @file MailDaemonApplication.cpp
 *  @brief Main mail daemon application managing accounts and delivery. */
#include "MailDaemonApplication.h"

#include <stdio.h>
#include <stdlib.h>
#include <vector>

#include <Beep.h>
#include <Catalog.h>
#include <ControlLook.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <fs_index.h>
#include <IconUtils.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <Notification.h>
#include <Path.h>
#include <Roster.h>
#include <StringList.h>
#include <StringFormat.h>
#include <VolumeRoster.h>

#include <E-mail.h>
#include <MailDaemon.h>
#include <MailMessage.h>
#include <MailSettings.h>

#include <MailPrivate.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MailDaemon"


/** @brief Message code to trigger the initial delayed auto-check. */
static const uint32 kMsgStartAutoCheck = 'stAC';
/** @brief Message code for periodic automatic mail checking. */
static const uint32 kMsgAutoCheck = 'moto';

static const bigtime_t kStartAutoCheckDelay = 30000000;
	// Wait 30 seconds before the first auto check - this usually lets the
	// boot process settle down, and give the network a chance to come up.


struct send_mails_info {
	send_mails_info()
	{
		bytes = 0;
	}

	BMessage	files;
	off_t		bytes;
};


class InboundMessenger : public BMessenger {
public:
	InboundMessenger(BInboundMailProtocol* protocol)
		:
		BMessenger(protocol)
	{
	}

	status_t MarkAsRead(const entry_ref& ref, read_flags flag)
	{
		BMessage message(kMsgMarkMessageAsRead);
		message.AddRef("ref", &ref);
		message.AddInt32("read", flag);

		return SendMessage(&message);
	}

	status_t SynchronizeMessages()
	{
		BMessage message(kMsgSyncMessages);
		return SendMessage(&message);
	}
};


// #pragma mark -


/**
 * @brief Creates filesystem indices for mail-related attributes on all queryable volumes.
 */
static void
makeIndices()
{
	const char* stringIndices[] = {
		B_MAIL_ATTR_CC, B_MAIL_ATTR_FROM, B_MAIL_ATTR_NAME,
		B_MAIL_ATTR_PRIORITY, B_MAIL_ATTR_REPLY, B_MAIL_ATTR_STATUS,
		B_MAIL_ATTR_SUBJECT, B_MAIL_ATTR_TO, B_MAIL_ATTR_THREAD,
		B_MAIL_ATTR_ACCOUNT, NULL
	};

	// add mail indices for all devices capable of querying

	int32 cookie = 0;
	dev_t device;
	while ((device = next_dev(&cookie)) >= B_OK) {
		fs_info info;
		if (fs_stat_dev(device, &info) < 0
			|| (info.flags & B_FS_HAS_QUERY) == 0)
			continue;

		for (int32 i = 0; stringIndices[i]; i++)
			fs_create_index(device, stringIndices[i], B_STRING_TYPE, 0);

		fs_create_index(device, "MAIL:draft", B_INT32_TYPE, 0);
		fs_create_index(device, B_MAIL_ATTR_WHEN, B_INT32_TYPE, 0);
		fs_create_index(device, B_MAIL_ATTR_FLAGS, B_INT32_TYPE, 0);
		fs_create_index(device, B_MAIL_ATTR_ACCOUNT_ID, B_INT32_TYPE, 0);
		fs_create_index(device, B_MAIL_ATTR_READ, B_INT32_TYPE, 0);
	}
}


/**
 * @brief Adds a Tracker attribute definition to a MIME type info message.
 *
 * @param msg        The BMessage to add the attribute to.
 * @param name       Internal attribute name.
 * @param publicName Display name shown in Tracker columns.
 * @param type       Attribute data type.
 * @param viewable   Whether the attribute is visible in Tracker.
 * @param editable   Whether the attribute is editable in Tracker.
 * @param width      Default column width in pixels.
 */
static void
addAttribute(BMessage& msg, const char* name, const char* publicName,
	int32 type = B_STRING_TYPE, bool viewable = true, bool editable = false,
	int32 width = 200)
{
	msg.AddString("attr:name", name);
	msg.AddString("attr:public_name", publicName);
	msg.AddInt32("attr:type", type);
	msg.AddBool("attr:viewable", viewable);
	msg.AddBool("attr:editable", editable);
	msg.AddInt32("attr:width", width);
	msg.AddInt32("attr:alignment", B_ALIGN_LEFT);
}


// #pragma mark -


/** @brief Constructs an account_protocols with no loaded protocol images. */
account_protocols::account_protocols()
	:
	inboundImage(-1),
	inboundProtocol(NULL),
	outboundImage(-1),
	outboundProtocol(NULL)
{
}


// #pragma mark -


/**
 * @brief Constructs the mail daemon application.
 *
 * Creates the error log window, installs MIME types, creates filesystem
 * indices, and registers the "New E-mail" system beep event.
 */
MailDaemonApplication::MailDaemonApplication()
	:
	BServer(B_MAIL_DAEMON_SIGNATURE, true, NULL),
	fAutoCheckRunner(NULL)
{
	fErrorLogWindow = new ErrorLogWindow(BRect(200, 200, 500, 250),
		B_TRANSLATE("Mail daemon status log"), B_TITLED_WINDOW);
	// install MimeTypes, attributes, indices, and the
	// system beep add startup
	MakeMimeTypes();
	makeIndices();
	add_system_beep_event("New E-mail");
}


/**
 * @brief Destroys the mail daemon, cleaning up accounts, queries, and animations.
 */
MailDaemonApplication::~MailDaemonApplication()
{
	delete fAutoCheckRunner;

	for (int32 i = 0; i < fQueries.CountItems(); i++)
		delete fQueries.ItemAt(i);

	while (!fAccounts.empty()) {
		_RemoveAccount(fAccounts.begin()->second);
		fAccounts.erase(fAccounts.begin());
	}

	delete fLEDAnimation;
	delete fNotification;
}


/**
 * @brief Called when the application is ready to run.
 *
 * Installs the deskbar icon, initialises mail accounts, schedules the
 * delayed auto-check, sets up new message counting, and creates the
 * notification and LED animation objects.
 */
void
MailDaemonApplication::ReadyToRun()
{
	InstallDeskbarIcon();

	_InitAccounts();

	// Start auto mail check with a delay
	BMessage startAutoCheck(kMsgStartAutoCheck);
	BMessageRunner::StartSending(this, &startAutoCheck,
		kStartAutoCheckDelay, 1);

	_InitNewMessagesCount();

	fCentralBeep = false;

	fNotification = new BNotification(B_INFORMATION_NOTIFICATION);
	fNotification->SetGroup(B_TRANSLATE("Mail status"));
	fNotification->SetMessageID("daemon_status");

	BPath path;
	if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
		path.Append("Mail/New E-mail");

		entry_ref ref;
		if (get_ref_for_path(path.Path(), &ref) == B_OK) {
			fNotification->SetOnClickApp("application/x-vnd.Be-TRAK");
			fNotification->AddOnClickRef(&ref);
		}
	}

	_UpdateNewMessagesNotification();

	app_info info;
	be_roster->GetAppInfo(B_MAIL_DAEMON_SIGNATURE, &info);

	BBitmap icon(BRect(B_ORIGIN, be_control_look->ComposeIconSize(32)), B_RGBA32);
	BNode node(&info.ref);
	BIconUtils::GetVectorIcon(&node, "BEOS:ICON", &icon);
	fNotification->SetIcon(&icon);

	fLEDAnimation = new LEDAnimation();
	SetPulseRate(1000000);
}


/**
 * @brief Handles file references by fetching the full message body.
 *
 * Reads the account ID from each referenced node's attributes and
 * delegates to the appropriate inbound protocol to fetch the body.
 *
 * @param message BMessage containing "refs" entry_ref fields.
 */
void
MailDaemonApplication::RefsReceived(BMessage* message)
{
	entry_ref ref;
	for (int32 i = 0; message->FindRef("refs", i, &ref) == B_OK; i++) {
		BNode node(&ref);
		if (node.InitCheck() != B_OK)
			continue;

		int32 account;
		if (node.ReadAttr(B_MAIL_ATTR_ACCOUNT_ID, B_INT32_TYPE, 0, &account,
				sizeof(account)) < 0)
			continue;

		BInboundMailProtocol* protocol = _InboundProtocol(account);
		if (protocol == NULL)
			continue;

		BMessenger target;
		BMessenger* replyTo = &target;
		if (message->FindMessenger("target", &target) != B_OK)
			replyTo = NULL;

		protocol->FetchBody(ref, replyTo);
	}
}


/**
 * @brief Dispatches incoming messages for mail operations and settings changes.
 *
 * Handles auto-check triggers, manual check/send requests, settings
 * updates, account reloads, message marking, body fetches, new message
 * queries, LED blinking, and various UI callbacks.
 *
 * @param msg The incoming message.
 */
void
MailDaemonApplication::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgStartAutoCheck:
			_UpdateAutoCheckRunner();
			break;

		case kMsgAutoCheck:
			// TODO: check whether internet is up and running!
			// supposed to fall through
		case kMsgCheckAndSend:	// check & send messages
			msg->what = kMsgSendMessages;
			PostMessage(msg);
			// supposed to fall trough
		case kMsgCheckMessage:	// check messages
			GetNewMessages(msg);
			break;

		case kMsgSendMessages:	// send messages
			SendPendingMessages(msg);
			break;

		case kMsgSettingsUpdated:
			fSettingsFile.Reload();
			_UpdateAutoCheckRunner();
			break;

		case kMsgAccountsChanged:
			_ReloadAccounts(msg);
			break;

		case kMsgMarkMessageAsRead:
		{
			int32 account = msg->FindInt32("account");
			entry_ref ref;
			if (msg->FindRef("ref", &ref) != B_OK)
				break;
			read_flags read = (read_flags)msg->FindInt32("read");

			BInboundMailProtocol* protocol = _InboundProtocol(account);
			if (protocol != NULL)
				InboundMessenger(protocol).MarkAsRead(ref, read);
			break;
		}

		case kMsgFetchBody:
			RefsReceived(msg);
			break;

		case 'stwg':	// Status window gone
		{
			BMessage reply('mnuc');
			reply.AddInt32("num_new_messages", fNewMessages);

			while ((msg = fFetchDoneRespondents.RemoveItemAt(0))) {
				msg->SendReply(&reply);
				delete msg;
			}

			if (fAlertString != B_EMPTY_STRING) {
				fAlertString.Truncate(fAlertString.Length() - 1);
				BAlert* alert = new BAlert(B_TRANSLATE("New Messages"),
					fAlertString.String(), "OK", NULL, NULL, B_WIDTH_AS_USUAL);
				alert->SetFeel(B_NORMAL_WINDOW_FEEL);
				alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
				alert->Go(NULL);
				fAlertString = B_EMPTY_STRING;
			}

			if (fCentralBeep) {
				system_beep("New E-mail");
				fCentralBeep = false;
			}
			break;
		}

		case 'mcbp':
			if (fNewMessages > 0)
				fCentralBeep = true;
			break;

		case kMsgCountNewMessages:	// Number of new messages
		{
			BMessage reply('mnuc');	// Mail New message Count
			if (msg->FindBool("wait_for_fetch_done")) {
				fFetchDoneRespondents.AddItem(DetachCurrentMessage());
				break;
			}

			reply.AddInt32("num_new_messages", fNewMessages);
			msg->SendReply(&reply);
			break;
		}

		case 'mblk':	// Mail Blink
			if (fNewMessages > 0)
				fLEDAnimation->Start();
			break;

		case 'enda':	// End Auto Check
			delete fAutoCheckRunner;
			fAutoCheckRunner = NULL;
			break;

		case 'numg':
		{
			static BStringFormat format(B_TRANSLATE("{0, plural, "
				"one{# new message} other{# new messages}} for %name\n"));

			int32 numMessages = msg->FindInt32("num_messages");
			fAlertString.Truncate(0);
			format.Format(fAlertString, numMessages);
			fAlertString.ReplaceFirst("%name", msg->FindString("name"));
			break;
		}

		case B_QUERY_UPDATE:
		{
			int32 previousCount = fNewMessages;

			int32 opcode;
			msg->FindInt32("opcode", &opcode);

			switch (opcode) {
				case B_ENTRY_CREATED:
				case B_ENTRY_REMOVED:
				{
					entry_ref ref;
					msg->FindInt32("device", &ref.device);
					msg->FindInt64("directory", &ref.directory);
					BEntry entry(&ref);

					if (!_IsEntryInTrash(entry)) {
						if (opcode == B_ENTRY_CREATED)
							fNewMessages++;
						else
							fNewMessages--;
					}
					break;
				}
				default:
					return;
			}

			_UpdateNewMessagesNotification();

			if (fSettingsFile.ShowStatusWindow()
					!= B_MAIL_SHOW_STATUS_WINDOW_NEVER
				&& previousCount < fNewMessages) {
				fNotification->Send();
			}
			break;
		}

		default:
			BApplication::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Periodic pulse that stops the LED animation when user activity is detected.
 */
void
MailDaemonApplication::Pulse()
{
	bigtime_t idle = idle_time();
	if (fLEDAnimation->IsRunning() && idle < 100000)
		fLEDAnimation->Stop();
}


/**
 * @brief Handles the quit request by removing the deskbar icon.
 *
 * @return Always returns @c true to allow the application to quit.
 */
bool
MailDaemonApplication::QuitRequested()
{
	RemoveDeskbarIcon();
	return true;
}


/**
 * @brief Adds the mail daemon replicant to the Deskbar tray if not already present.
 */
void
MailDaemonApplication::InstallDeskbarIcon()
{
	BDeskbar deskbar;

	if (!deskbar.HasItem("mail_daemon")) {
		BRoster roster;
		entry_ref ref;

		status_t status = roster.FindApp(B_MAIL_DAEMON_SIGNATURE, &ref);
		if (status < B_OK) {
			fprintf(stderr, "Can't find application to tell deskbar: %s\n",
				strerror(status));
			return;
		}

		status = deskbar.AddItem(&ref);
		if (status < B_OK) {
			fprintf(stderr, "Can't add deskbar replicant: %s\n",
				strerror(status));
			return;
		}
	}
}


/**
 * @brief Removes the mail daemon replicant from the Deskbar tray if present.
 */
void
MailDaemonApplication::RemoveDeskbarIcon()
{
	BDeskbar deskbar;
	if (deskbar.HasItem("mail_daemon"))
		deskbar.RemoveItem("mail_daemon");
}


/**
 * @brief Checks for new messages on one or all configured accounts.
 *
 * If the message contains an "account" field, only that account is
 * checked; otherwise all inbound accounts are synchronised.
 *
 * @param msg Message optionally containing an "account" int32 field.
 */
void
MailDaemonApplication::GetNewMessages(BMessage* msg)
{
	int32 account = -1;
	if (msg->FindInt32("account", &account) == B_OK && account >= 0) {
		// Check the single requested account
		BInboundMailProtocol* protocol = _InboundProtocol(account);
		if (protocol != NULL)
			InboundMessenger(protocol).SynchronizeMessages();
		return;
	}

	// Check all accounts

	AccountMap::const_iterator iterator = fAccounts.begin();
	for (; iterator != fAccounts.end(); iterator++) {
		BInboundMailProtocol* protocol = iterator->second.inboundProtocol;
		if (protocol != NULL)
			InboundMessenger(protocol).SynchronizeMessages();
	}
}


/**
 * @brief Sends all pending outbound messages, or a specific message if specified.
 *
 * Queries all volumes for messages with B_MAIL_PENDING flags, groups
 * them by account, and delegates to the appropriate outbound protocol.
 *
 * @param msg Message optionally containing "account" and/or "message_path" fields.
 */
void
MailDaemonApplication::SendPendingMessages(BMessage* msg)
{
	BVolumeRoster roster;
	BVolume volume;
	std::map<int32, send_mails_info> messages;
	int32 account = msg->GetInt32("account", -1);

	if (!msg->HasString("message_path")) {
		while (roster.GetNextVolume(&volume) == B_OK) {
			BQuery query;
			query.SetVolume(&volume);
			query.PushAttr(B_MAIL_ATTR_FLAGS);
			query.PushInt32(B_MAIL_PENDING);
			query.PushOp(B_EQ);

			query.PushAttr(B_MAIL_ATTR_FLAGS);
			query.PushInt32(B_MAIL_PENDING | B_MAIL_SAVE);
			query.PushOp(B_EQ);

			if (account >= 0) {
				query.PushAttr(B_MAIL_ATTR_ACCOUNT_ID);
				query.PushInt32(account);
				query.PushOp(B_EQ);
				query.PushOp(B_AND);
			}

			query.PushOp(B_OR);
			query.Fetch();
			BEntry entry;
			while (query.GetNextEntry(&entry) == B_OK) {
				if (_IsEntryInTrash(entry))
					continue;

				BNode node;
				while (node.SetTo(&entry) == B_BUSY)
					snooze(1000);
				if (!_IsPending(node))
					continue;

				if (node.ReadAttr(B_MAIL_ATTR_ACCOUNT_ID, B_INT32_TYPE, 0,
						&account, sizeof(int32)) < 0)
					account = -1;

				_AddMessage(messages[account], entry, node);
			}
		}
	} else {
		// Send the requested message only
		const char* path;
		if (msg->FindString("message_path", &path) != B_OK)
			return;

		BEntry entry(path);
		_AddMessage(messages[account], entry, BNode(&entry));
	}

	std::map<int32, send_mails_info>::iterator iterator = messages.begin();
	for (; iterator != messages.end(); iterator++) {
		BOutboundMailProtocol* protocol = _OutboundProtocol(iterator->first);
		if (protocol == NULL)
			continue;

		send_mails_info& info = iterator->second;
		if (info.bytes == 0)
			continue;

		protocol->SendMessages(info.files, info.bytes);
	}
}


/**
 * @brief Installs or updates MIME type definitions for email file types.
 *
 * Registers "text/x-email" and "text/x-partial-email" with Tracker
 * column attributes for subject, from, to, status, etc.
 *
 * @param remakeMIMETypes If true, forces full reinstallation of the MIME types.
 */
void
MailDaemonApplication::MakeMimeTypes(bool remakeMIMETypes)
{
	// Add MIME database entries for the e-mail file types we handle.  Either
	// do a full rebuild from nothing, or just add on the new attributes that
	// we support which the regular BeOS mail daemon didn't have.

	const uint8 kNTypes = 2;
	const char* types[kNTypes] = {"text/x-email", "text/x-partial-email"};

	for (size_t i = 0; i < kNTypes; i++) {
		BMessage info;
		BMimeType mime(types[i]);
		if (mime.InitCheck() != B_OK) {
			fputs("could not init mime type.\n", stderr);
			return;
		}

		if (!mime.IsInstalled() || remakeMIMETypes) {
			// install the full mime type
			mime.Delete();
			mime.Install();

			// Set up the list of e-mail related attributes that Tracker will
			// let you display in columns for e-mail messages.
			addAttribute(info, B_MAIL_ATTR_NAME, "Name");
			addAttribute(info, B_MAIL_ATTR_SUBJECT, "Subject");
			addAttribute(info, B_MAIL_ATTR_TO, "To");
			addAttribute(info, B_MAIL_ATTR_CC, "Cc");
			addAttribute(info, B_MAIL_ATTR_FROM, "From");
			addAttribute(info, B_MAIL_ATTR_REPLY, "Reply To");
			addAttribute(info, B_MAIL_ATTR_STATUS, "Status");
			addAttribute(info, B_MAIL_ATTR_PRIORITY, "Priority", B_STRING_TYPE,
				true, true, 40);
			addAttribute(info, B_MAIL_ATTR_WHEN, "When", B_TIME_TYPE, true,
				false, 150);
			addAttribute(info, B_MAIL_ATTR_THREAD, "Thread");
			addAttribute(info, B_MAIL_ATTR_ACCOUNT, "Account", B_STRING_TYPE,
				true, false, 100);
			addAttribute(info, B_MAIL_ATTR_READ, "Read", B_INT32_TYPE,
				true, false, 70);
			mime.SetAttrInfo(&info);

			if (i == 0) {
				mime.SetShortDescription("E-mail");
				mime.SetLongDescription("Electronic Mail Message");
				mime.SetPreferredApp("application/x-vnd.Be-MAIL");
			} else {
				mime.SetShortDescription("Partial E-mail");
				mime.SetLongDescription("A Partially Downloaded E-mail");
				mime.SetPreferredApp("application/x-vnd.Be-MAIL");
			}
		}
	}
}


/**
 * @brief Initialises all configured mail accounts from settings.
 */
void
MailDaemonApplication::_InitAccounts()
{
	BMailAccounts accounts;
	for (int i = 0; i < accounts.CountAccounts(); i++)
		_InitAccount(*accounts.AccountAt(i));
}


/**
 * @brief Initialises a single mail account, loading inbound and outbound protocol add-ons.
 *
 * Creates the protocol objects, attaches notifiers, and starts their
 * message loops.
 *
 * @param settings The account settings to initialise from.
 */
void
MailDaemonApplication::_InitAccount(BMailAccountSettings& settings)
{
	account_protocols account;

	// inbound
	if (settings.IsInboundEnabled()) {
		account.inboundProtocol = _CreateInboundProtocol(settings,
			account.inboundImage);
	}
	if (account.inboundProtocol != NULL) {
		DefaultNotifier* notifier = new DefaultNotifier(settings.Name(), true,
			fErrorLogWindow, fSettingsFile.ShowStatusWindow());
		account.inboundProtocol->SetMailNotifier(notifier);
		account.inboundProtocol->Run();
	}

	// outbound
	if (settings.IsOutboundEnabled()) {
		account.outboundProtocol = _CreateOutboundProtocol(settings,
			account.outboundImage);
	}
	if (account.outboundProtocol != NULL) {
		DefaultNotifier* notifier = new DefaultNotifier(settings.Name(), false,
			fErrorLogWindow, fSettingsFile.ShowStatusWindow());
		account.outboundProtocol->SetMailNotifier(notifier);
		account.outboundProtocol->Run();
	}

	printf("account name %s, id %i, in %p, out %p\n", settings.Name(),
		(int)settings.AccountID(), account.inboundProtocol,
		account.outboundProtocol);

	if (account.inboundProtocol != NULL || account.outboundProtocol != NULL)
		fAccounts[settings.AccountID()] = account;
}


/**
 * @brief Reloads changed accounts by removing and re-initialising them.
 *
 * @param message BMessage containing "account" int32 fields identifying changed accounts.
 */
void
MailDaemonApplication::_ReloadAccounts(BMessage* message)
{
	type_code typeFound;
	int32 countFound;
	message->GetInfo("account", &typeFound, &countFound);
	if (typeFound != B_INT32_TYPE)
		return;

	// reload accounts
	BMailAccounts accounts;

	for (int i = 0; i < countFound; i++) {
		int32 account = message->FindInt32("account", i);
		AccountMap::iterator found = fAccounts.find(account);
		if (found != fAccounts.end()) {
			_RemoveAccount(found->second);
			fAccounts.erase(found);
		}

		BMailAccountSettings* settings = accounts.AccountByID(account);
		if (settings != NULL)
			_InitAccount(*settings);
	}
}


/**
 * @brief Removes an account by quitting its protocol threads and unloading add-ons.
 *
 * @param account The account_protocols structure to tear down.
 */
void
MailDaemonApplication::_RemoveAccount(const account_protocols& account)
{
	if (account.inboundProtocol != NULL) {
		account.inboundProtocol->Lock();
		account.inboundProtocol->Quit();

		unload_add_on(account.inboundImage);
	}

	if (account.outboundProtocol != NULL) {
		account.outboundProtocol->Lock();
		account.outboundProtocol->Quit();

		unload_add_on(account.outboundImage);
	}
}


/**
 * @brief Loads the inbound mail protocol add-on and instantiates the protocol.
 *
 * @param settings The account settings containing the add-on reference.
 * @param image    Output: the image_id of the loaded add-on.
 * @return The instantiated protocol, or NULL on failure.
 */
BInboundMailProtocol*
MailDaemonApplication::_CreateInboundProtocol(BMailAccountSettings& settings,
	image_id& image)
{
	const entry_ref& entry = settings.InboundAddOnRef();
	BInboundMailProtocol* (*instantiateProtocol)(BMailAccountSettings*);

	BPath path(&entry);
	image = load_add_on(path.Path());
	if (image < 0)
		return NULL;

	if (get_image_symbol(image, "instantiate_inbound_protocol",
			B_SYMBOL_TYPE_TEXT, (void**)&instantiateProtocol) != B_OK) {
		unload_add_on(image);
		image = -1;
		return NULL;
	}
	return instantiateProtocol(&settings);
}


/**
 * @brief Loads the outbound mail protocol add-on and instantiates the protocol.
 *
 * @param settings The account settings containing the add-on reference.
 * @param image    Output: the image_id of the loaded add-on.
 * @return The instantiated protocol, or NULL on failure.
 */
BOutboundMailProtocol*
MailDaemonApplication::_CreateOutboundProtocol(BMailAccountSettings& settings,
	image_id& image)
{
	const entry_ref& entry = settings.OutboundAddOnRef();
	BOutboundMailProtocol* (*instantiateProtocol)(BMailAccountSettings*);

	BPath path(&entry);
	image = load_add_on(path.Path());
	if (image < 0)
		return NULL;

	if (get_image_symbol(image, "instantiate_outbound_protocol",
			B_SYMBOL_TYPE_TEXT, (void**)&instantiateProtocol) != B_OK) {
		unload_add_on(image);
		image = -1;
		return NULL;
	}
	return instantiateProtocol(&settings);
}


/**
 * @brief Looks up the inbound protocol for a given account ID.
 *
 * @param account The account identifier.
 * @return The inbound protocol, or NULL if the account is not found.
 */
BInboundMailProtocol*
MailDaemonApplication::_InboundProtocol(int32 account)
{
	AccountMap::iterator found = fAccounts.find(account);
	if (found == fAccounts.end())
		return NULL;
	return found->second.inboundProtocol;
}


/**
 * @brief Looks up the outbound protocol for a given account ID.
 *
 * Falls back to the default outbound account if @a account is negative.
 *
 * @param account The account identifier, or -1 for the default.
 * @return The outbound protocol, or NULL if not found.
 */
BOutboundMailProtocol*
MailDaemonApplication::_OutboundProtocol(int32 account)
{
	if (account < 0)
		account = BMailSettings().DefaultOutboundAccount();

	AccountMap::iterator found = fAccounts.find(account);
	if (found == fAccounts.end())
		return NULL;
	return found->second.outboundProtocol;
}


/**
 * @brief Counts new (unread) messages across all volumes using live queries.
 *
 * Sets up persistent queries that will deliver B_QUERY_UPDATE notifications
 * to this application as messages arrive or are read.
 */
void
MailDaemonApplication::_InitNewMessagesCount()
{
	BVolume volume;
	BVolumeRoster roster;

	fNewMessages = 0;

	while (roster.GetNextVolume(&volume) == B_OK) {
		BQuery* query = new BQuery;

		query->SetTarget(this);
		query->SetVolume(&volume);
		query->PushAttr(B_MAIL_ATTR_STATUS);
		query->PushString("New");
		query->PushOp(B_EQ);
		query->PushAttr("BEOS:TYPE");
		query->PushString("text/x-email");
		query->PushOp(B_EQ);
		query->PushAttr("BEOS:TYPE");
		query->PushString("text/x-partial-email");
		query->PushOp(B_EQ);
		query->PushOp(B_OR);
		query->PushOp(B_AND);
		query->Fetch();

		BEntry entry;
		while (query->GetNextEntry(&entry) == B_OK) {
			if (!_IsEntryInTrash(entry))
				fNewMessages++;
		}

		fQueries.AddItem(query);
	}
}


/**
 * @brief Updates the notification title with the current new message count.
 */
void
MailDaemonApplication::_UpdateNewMessagesNotification()
{
	BString title;
	if (fNewMessages > 0) {
		BStringFormat format(B_TRANSLATE(
			"{0, plural, one{One new message} other{# new messages}}"));

		format.Format(title, fNewMessages);
	} else
		title = B_TRANSLATE("No new messages");

	fNotification->SetTitle(title.String());
}


/**
 * @brief Creates or updates the periodic auto-check message runner.
 *
 * Reads the auto-check interval from settings. If the interval is positive,
 * starts or reconfigures the runner; if zero, stops it.
 */
void
MailDaemonApplication::_UpdateAutoCheckRunner()
{
	bigtime_t interval = fSettingsFile.AutoCheckInterval();
	if (interval > 0) {
		if (fAutoCheckRunner != NULL) {
			fAutoCheckRunner->SetInterval(interval);
			fAutoCheckRunner->SetCount(-1);
		} else {
			BMessage update(kMsgAutoCheck);
			fAutoCheckRunner = new BMessageRunner(be_app_messenger, &update,
				interval);

			// Send one right away -- the message runner will wait until the
			// first interval has passed before sending a message
			PostMessage(&update);
		}
	} else {
		delete fAutoCheckRunner;
		fAutoCheckRunner = NULL;
	}
}


/**
 * @brief Adds a pending mail entry to the send queue, accumulating byte totals.
 *
 * @param info  The send_mails_info structure to append to.
 * @param entry The file entry for the pending message.
 * @param node  The BNode for size retrieval.
 */
void
MailDaemonApplication::_AddMessage(send_mails_info& info, const BEntry& entry,
	const BNode& node)
{
	entry_ref ref;
	off_t size;
	if (node.GetSize(&size) == B_OK && entry.GetRef(&ref) == B_OK) {
		info.files.AddRef("ref", &ref);
		info.bytes += size;
	}
}


/*!	Work-around for a broken index that contains out-of-date information.
*/
/**
 * @brief Checks whether a mail node has the pending flag set in its attributes.
 *
 * Works around a potentially stale filesystem index by reading the
 * attribute directly.
 *
 * @param node The mail file node.
 * @return @c true if the B_MAIL_PENDING flag is set.
 */
/*static*/ bool
MailDaemonApplication::_IsPending(BNode& node)
{
	int32 flags;
	if (node.ReadAttr(B_MAIL_ATTR_FLAGS, B_INT32_TYPE, 0, &flags, sizeof(int32))
			!= (ssize_t)sizeof(int32))
		return false;

	return (flags & B_MAIL_PENDING) != 0;
}


/**
 * @brief Checks whether a given file entry resides in the system Trash.
 *
 * @param entry The file entry to check.
 * @return @c true if the entry is inside the Trash directory.
 */
/*static*/ bool
MailDaemonApplication::_IsEntryInTrash(BEntry& entry)
{
	entry_ref ref;
	entry.GetRef(&ref);

	BVolume volume(ref.device);
	BPath path;
	if (volume.InitCheck() != B_OK
		|| find_directory(B_TRASH_DIRECTORY, &path, false, &volume) != B_OK)
		return false;

	BDirectory trash(path.Path());
	return trash.Contains(&entry);
}
