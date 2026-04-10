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
   Copyright 2004-2018, Haiku Inc. All Rights Reserved.
   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
   
   Distributed under the terms of the MIT License.
 */
/** @file DeskbarView.cpp
 *  @brief Mail daemon deskbar replicant view and popup menu. */
//!	mail_daemon's deskbar menu and view


#include "DeskbarView.h"

#include <stdio.h>
#include <malloc.h>

#include <Bitmap.h>
#include <Catalog.h>
#include <Deskbar.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <IconUtils.h>
#include <kernel/fs_info.h>
#include <kernel/fs_index.h>
#include <MenuItem.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <NodeMonitor.h>
#include <OpenWithTracker.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <Query.h>
#include <Rect.h>
#include <Resources.h>
#include <Roster.h>
#include <String.h>
#include <StringFormat.h>
#include <SymLink.h>
#include <VolumeRoster.h>
#include <Window.h>

#include <E-mail.h>
#include <MailDaemon.h>
#include <MailSettings.h>

#include <MailPrivate.h>

#include "DeskbarViewIcons.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeskbarView"


const char* kTrackerSignature = "application/x-vnd.Be-TRAK";


extern "C" _EXPORT BView* instantiate_deskbar_item(float maxWidth,
	float maxHeight);


/**
 * @brief Finds the image_info for the add-on containing this function.
 *
 * @param image Output: the image_info for the current add-on.
 * @return B_OK on success, B_ERROR if not found.
 */
static status_t
our_image(image_info& image)
{
	int32 cookie = 0;
	while (get_next_image_info(B_CURRENT_TEAM, &cookie, &image) == B_OK) {
		if ((char *)our_image >= (char *)image.text
			&& (char *)our_image <= (char *)image.text + image.text_size)
			return B_OK;
	}

	return B_ERROR;
}


/**
 * @brief Factory function called by the Deskbar to create the mail replicant.
 *
 * @param maxWidth  Maximum width available in the Deskbar tray.
 * @param maxHeight Maximum height available in the Deskbar tray.
 * @return A new DeskbarView sized to fit the tray slot.
 */
BView*
instantiate_deskbar_item(float maxWidth, float maxHeight)
{
	return new DeskbarView(BRect(0, 0, maxHeight - 1, maxHeight - 1));
}


//	#pragma mark -


/**
 * @brief Constructs the deskbar view with the given frame rectangle.
 *
 * @param frame The view's frame rectangle.
 */
DeskbarView::DeskbarView(BRect frame)
	:
	BView(frame, "mail_daemon", B_FOLLOW_NONE, B_WILL_DRAW | B_PULSE_NEEDED),
	fStatus(kStatusNoMail),
	fLastButtons(0)
{
	_InitBitmaps();
}


/**
 * @brief Constructs the deskbar view from an archived BMessage.
 *
 * @param message The archived message.
 */
DeskbarView::DeskbarView(BMessage *message)
	:
	BView(message),
	fStatus(kStatusNoMail),
	fLastButtons(0)
{
	_InitBitmaps();
}


/**
 * @brief Destroys the deskbar view, freeing bitmaps and active queries.
 */
DeskbarView::~DeskbarView()
{
	for (int i = 0; i < kStatusCount; i++)
		delete fBitmaps[i];

	for (int32 i = 0; i < fNewMailQueries.CountItems(); i++)
		delete ((BQuery *)(fNewMailQueries.ItemAt(i)));
}


/**
 * @brief Called when the view is attached to a window; starts mail queries.
 *
 * Adopts parent colors and refreshes the new-mail query. If the mail
 * daemon is not running, removes the deskbar item.
 */
void DeskbarView::AttachedToWindow()
{
	BView::AttachedToWindow();
	AdoptParentColors();

	if (ViewUIColor() == B_NO_COLOR)
		SetLowColor(ViewColor());
	else
		SetLowUIColor(ViewUIColor());

	if (be_roster->IsRunning(B_MAIL_DAEMON_SIGNATURE)) {
		_RefreshMailQuery();
	} else {
		BDeskbar deskbar;
		deskbar.RemoveItem("mail_daemon");
	}
}


/**
 * @brief Checks whether a given entry resides in the Trash.
 *
 * @param ref The entry reference to check.
 * @return @c true if the entry is in the Trash directory.
 */
bool DeskbarView::_EntryInTrash(const entry_ref* ref)
{
	BEntry entry(ref);
	BVolume volume(ref->device);
	BPath path;
	if (volume.InitCheck() != B_OK
		|| find_directory(B_TRASH_DIRECTORY, &path, false, &volume) != B_OK)
		return false;

	BDirectory trash(path.Path());
	return trash.Contains(&entry);
}


/**
 * @brief Refreshes the new-mail queries across all mounted volumes.
 *
 * Clears existing queries, creates new ones filtering for "New" status,
 * counts unread messages not in the Trash, and updates the icon status.
 */
void DeskbarView::_RefreshMailQuery()
{
	for (int32 i = 0; i < fNewMailQueries.CountItems(); i++)
		delete ((BQuery *)(fNewMailQueries.ItemAt(i)));
	fNewMailQueries.MakeEmpty();

	BVolumeRoster volumes;
	BVolume volume;
	fNewMessages = 0;

	while (volumes.GetNextVolume(&volume) == B_OK) {
		BQuery *newMailQuery = new BQuery;
		newMailQuery->SetTarget(this);
		newMailQuery->SetVolume(&volume);
		newMailQuery->PushAttr(B_MAIL_ATTR_STATUS);
		newMailQuery->PushString("New");
		newMailQuery->PushOp(B_EQ);
		newMailQuery->Fetch();

		BEntry entry;
		while (newMailQuery->GetNextEntry(&entry) == B_OK) {
			if (entry.InitCheck() == B_OK) {
				entry_ref ref;
				entry.GetRef(&ref);
				if (!_EntryInTrash(&ref))
					fNewMessages++;
			}
		}

		fNewMailQueries.AddItem(newMailQuery);
	}

	fStatus = (fNewMessages > 0) ? kStatusNewMail : kStatusNoMail;
	Invalidate();
}


/**
 * @brief Instantiates a DeskbarView from an archived BMessage.
 *
 * @param data The archive message.
 * @return A new DeskbarView, or NULL if instantiation validation fails.
 */
DeskbarView* DeskbarView::Instantiate(BMessage *data)
{
	if (!validate_instantiation(data, "DeskbarView"))
		return NULL;

	return new DeskbarView(data);
}


/**
 * @brief Archives the deskbar view into a BMessage.
 *
 * @param data The destination message.
 * @param deep If true, archives child views as well.
 * @return B_NO_ERROR on success.
 */
status_t DeskbarView::Archive(BMessage *data,bool deep) const
{
	BView::Archive(data, deep);

	data->AddString("add_on", B_MAIL_DAEMON_SIGNATURE);
	return B_NO_ERROR;
}


/** @brief Draws the current status icon using alpha blending. */
void
DeskbarView::Draw(BRect /*updateRect*/)
{
	if (fBitmaps[fStatus] == NULL)
		return;

	SetDrawingMode(B_OP_ALPHA);
	DrawBitmap(fBitmaps[fStatus]);
	SetDrawingMode(B_OP_COPY);
}


/**
 * @brief Dispatches incoming messages for mail checks, sends, and query updates.
 *
 * @param message The incoming message.
 */
void
DeskbarView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case MD_CHECK_SEND_NOW:
			// also happens in DeskbarView::MouseUp() with
			// B_TERTIARY_MOUSE_BUTTON pressed
			BMailDaemon().CheckAndSendQueuedMail();
			break;
		case MD_CHECK_FOR_MAILS:
			BMailDaemon().CheckMail(message->FindInt32("account"));
			break;
		case MD_SEND_MAILS:
			BMailDaemon().SendQueuedMail();
			break;

		case MD_OPEN_NEW:
		{
			char* argv[] = {(char *)"New Message", (char *)"mailto:"};
			be_roster->Launch("text/x-email", 2, argv);
			break;
		}
		case MD_OPEN_PREFS:
			be_roster->Launch("application/x-vnd.Haiku-Mail");
			break;

		case MD_REFRESH_QUERY:
			_RefreshMailQuery();
			break;

		case B_QUERY_UPDATE:
		{
			int32 opcode;
			message->FindInt32("opcode", &opcode);

			switch (opcode) {
				case B_ENTRY_CREATED:
				case B_ENTRY_REMOVED:
				{
					entry_ref ref;
					message->FindInt32("device", &ref.device);
					message->FindInt64("directory", &ref.directory);

					if (!_EntryInTrash(&ref)) {
						if (opcode == B_ENTRY_CREATED)
							fNewMessages++;
						else
							fNewMessages--;
					}
					break;
				}
			}

			fStatus = fNewMessages > 0 ? kStatusNewMail : kStatusNoMail;
			Invalidate();
			break;
		}
		case B_QUIT_REQUESTED:
			BMailDaemon().Quit();
			break;

		// open received files in the standard mail application
		case B_REFS_RECEIVED:
		{
			BMessage argv(B_ARGV_RECEIVED);
			argv.AddString("argv", "E-mail");

			entry_ref ref;
			BPath path;
			int i = 0;

			while (message->FindRef("refs", i++, &ref) == B_OK
				&& path.SetTo(&ref) == B_OK) {
				//fprintf(stderr,"got %s\n", path.Path());
				argv.AddString("argv", path.Path());
			}

			if (i > 1) {
				argv.AddInt32("argc", i);
				be_roster->Launch("text/x-email", &argv);
			}
			break;
		}
		default:
			BView::MessageReceived(message);
	}
}


/**
 * @brief Loads status icon bitmaps from the add-on's vector icon resources.
 */
void
DeskbarView::_InitBitmaps()
{
	for (int i = 0; i < kStatusCount; i++)
		fBitmaps[i] = NULL;

	image_info info;
	if (our_image(info) != B_OK)
		return;

	BFile file(info.name, B_READ_ONLY);
	if (file.InitCheck() != B_OK)
		return;

	BResources resources(&file);
	if (resources.InitCheck() != B_OK)
		return;

	for (int i = 0; i < kStatusCount; i++) {
		const void* data = NULL;
		size_t size;
		data = resources.LoadResource(B_VECTOR_ICON_TYPE,
			kIconNoMail + i, &size);
		if (data != NULL) {
			BBitmap* icon = new BBitmap(Bounds(), B_RGBA32);
			if (icon->InitCheck() == B_OK
				&& BIconUtils::GetVectorIcon((const uint8 *)data,
					size, icon) == B_OK) {
				fBitmaps[i] = icon;
			} else
				delete icon;
		}
	}
}


/** @brief Periodic pulse handler; reserved for future daemon liveness checks. */
void
DeskbarView::Pulse()
{
	// TODO: Check if mail_daemon is still running
}


/**
 * @brief Handles mouse-up events to open the mailbox or trigger a mail check.
 *
 * Primary button opens the mailbox in Tracker; tertiary button checks for mail.
 *
 * @param pos The mouse position in view coordinates.
 */
void
DeskbarView::MouseUp(BPoint pos)
{
	if ((fLastButtons & B_PRIMARY_MOUSE_BUTTON) !=0
		&& OpenWithTracker(B_USER_SETTINGS_DIRECTORY, "Mail/mailbox") != B_OK) {
		entry_ref ref;
		_GetNewQueryRef(ref);

		BMessenger trackerMessenger(kTrackerSignature);
		BMessage message(B_REFS_RECEIVED);
		message.AddRef("refs", &ref);

		trackerMessenger.SendMessage(&message);
	}

	if ((fLastButtons & B_TERTIARY_MOUSE_BUTTON) != 0)
		BMailDaemon().CheckMail();
}


/**
 * @brief Handles mouse-down events; shows a popup menu on right-click.
 *
 * @param pos The mouse position in view coordinates.
 */
void
DeskbarView::MouseDown(BPoint pos)
{
	Looper()->CurrentMessage()->FindInt32("buttons", &fLastButtons);

	if ((fLastButtons & B_SECONDARY_MOUSE_BUTTON) != 0) {
		ConvertToScreen(&pos);

		BPopUpMenu* menu = _BuildMenu();
		menu->Go(pos, true, true, BRect(pos.x - 2, pos.y - 2,
			pos.x + 2, pos.y + 2), true);
	}
}


/**
 * @brief Ensures the Menu Links directory exists, creating default links if needed.
 *
 * @param directory Output: set to the Menu Links directory on success.
 * @param path      Path to the Menu Links directory.
 * @return @c true if the directory exists or was created successfully.
 */
bool
DeskbarView::_CreateMenuLinks(BDirectory& directory, BPath& path)
{
	status_t status = directory.SetTo(path.Path());
	if (status == B_OK)
		return true;

	// Check if the directory has to be created (and do it in this case,
	// filling it with some standard links).  Normally the installer will
	// create the directory and fill it with links, so normally this doesn't
	// get used.

	BEntry entry(path.Path());
	if (status != B_ENTRY_NOT_FOUND
		|| entry.GetParent(&directory) < B_OK
		|| directory.CreateDirectory(path.Leaf(), NULL) < B_OK
		|| directory.SetTo(path.Path()) < B_OK)
		return false;

	BPath targetPath;
	find_directory(B_USER_DIRECTORY, &targetPath);
	targetPath.Append("mail/in");

	directory.CreateSymLink("Open Inbox Folder", targetPath.Path(), NULL);
	targetPath.GetParent(&targetPath);
	directory.CreateSymLink("Open Mail Folder", targetPath.Path(), NULL);

	// create the draft query

	BFile file;
	if (directory.CreateFile("Open Draft", &file) < B_OK)
		return true;

	BString string("MAIL:draft==1");
	file.WriteAttrString("_trk/qrystr", &string);
	string = "E-mail";
	file.WriteAttrString("_trk/qryinitmime", &string);
	BNodeInfo(&file).SetType("application/x-vnd.Be-query");

	return true;
}


/**
 * @brief Creates a saved query file for new (unread) email messages.
 *
 * @param query The BEntry representing the query file to create.
 */
void
DeskbarView::_CreateNewMailQuery(BEntry& query)
{
	BFile file(&query, B_READ_WRITE | B_CREATE_FILE);
	if (file.InitCheck() != B_OK)
		return;

	BString string(B_MAIL_ATTR_STATUS "==\"New\"");
	file.WriteAttrString("_trk/qrystr", &string);
	file.WriteAttrString("_trk/qryinitstr", &string);
	int32 mode = 'Fbyq';
	file.WriteAttr("_trk/qryinitmode", B_INT32_TYPE, 0, &mode, sizeof(int32));
	string = "E-mail";
	file.WriteAttrString("_trk/qryinitmime", &string);
	BNodeInfo(&file).SetType("application/x-vnd.Be-query");
}


/**
 * @brief Builds the popup context menu for the deskbar replicant.
 *
 * Includes options to create a new message, open mail links, check
 * for mail, send pending messages, open settings, and optionally
 * shut down the mail daemon.
 *
 * @return The newly built popup menu; caller takes ownership.
 */
BPopUpMenu*
DeskbarView::_BuildMenu()
{
	BPopUpMenu* menu = new BPopUpMenu(B_EMPTY_STRING, false, false);
	menu->SetFont(be_plain_font);

	menu->AddItem(new BMenuItem(B_TRANSLATE("Create new message"
		B_UTF8_ELLIPSIS), new BMessage(MD_OPEN_NEW)));
	menu->AddSeparatorItem();

	BMessenger tracker(kTrackerSignature);
	BNavMenu* navMenu;
	BMenuItem* item;
	BMessage* msg;
	entry_ref ref;

	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("Mail/Menu Links");

	BDirectory directory;
	if (_CreateMenuLinks(directory, path)) {
		int32 count = 0;

		while (directory.GetNextRef(&ref) == B_OK) {
			count++;

			path.SetTo(&ref);
			// the true here dereferences the symlinks all the way :)
			BEntry entry(&ref, true);

			// do we want to use the NavMenu, or just an ordinary BMenuItem?
			// we are using the NavMenu only for directories and queries
			bool useNavMenu = false;

			if (entry.InitCheck() == B_OK) {
				if (entry.IsDirectory())
					useNavMenu = true;
				else if (entry.IsFile()) {
					// Files should use the BMenuItem unless they are queries
					char mimeString[B_MIME_TYPE_LENGTH];
					BNode node(&entry);
					BNodeInfo info(&node);
					if (info.GetType(mimeString) == B_OK
						&& strcmp(mimeString, "application/x-vnd.Be-query")
							== 0)
						useNavMenu = true;
				}
				// clobber the existing ref only if the symlink derefernces
				// completely, otherwise we'll stick with what we have
				entry.GetRef(&ref);
			}

			msg = new BMessage(B_REFS_RECEIVED);
			msg->AddRef("refs", &ref);

			if (useNavMenu) {
				item = new BMenuItem(navMenu = new BNavMenu(path.Leaf(),
					B_REFS_RECEIVED, tracker), msg);
				navMenu->SetNavDir(&ref);
			} else
				item = new BMenuItem(path.Leaf(), msg);

			menu->AddItem(item);
			if (entry.InitCheck() != B_OK)
				item->SetEnabled(false);
		}
		if (count > 0)
			menu->AddSeparatorItem();
	}

	// Hack for R5's buggy Query Notification
	#ifdef HAIKU_TARGET_PLATFORM_BEOS
		menu->AddItem(new BMenuItem(B_TRANSLATE("Refresh New Mail Count"),
			new BMessage(MD_REFRESH_QUERY)));
	#endif

	// The New E-mail query

	if (fNewMessages > 0) {
		static BStringFormat format(B_TRANSLATE(
			"{0, plural, one{# new message} other{# new messages}}"));
		BString string;
		format.Format(string, fNewMessages);

		_GetNewQueryRef(ref);

		item = new BMenuItem(navMenu = new BNavMenu(string.String(),
			B_REFS_RECEIVED, BMessenger(kTrackerSignature)),
			msg = new BMessage(B_REFS_RECEIVED));
		msg->AddRef("refs", &ref);
		navMenu->SetNavDir(&ref);

		menu->AddItem(item);
	} else {
		menu->AddItem(item = new BMenuItem(B_TRANSLATE("No new messages"),
			NULL));
		item->SetEnabled(false);
	}

	BMailAccounts accounts;
	if ((modifiers() & B_SHIFT_KEY) != 0) {
		BMenu *accountMenu = new BMenu(B_TRANSLATE("Check for mails only"));
		BFont font;
		menu->GetFont(&font);
		accountMenu->SetFont(&font);

		for (int32 i = 0; i < accounts.CountAccounts(); i++) {
			BMailAccountSettings* account = accounts.AccountAt(i);

			BMessage* message = new BMessage(MD_CHECK_FOR_MAILS);
			message->AddInt32("account", account->AccountID());

			accountMenu->AddItem(new BMenuItem(account->Name(), message));
		}
		if (accounts.CountAccounts() == 0) {
			item = new BMenuItem(B_TRANSLATE("<no accounts>"), NULL);
			item->SetEnabled(false);
			accountMenu->AddItem(item);
		}
		accountMenu->SetTargetForItems(this);
		menu->AddItem(new BMenuItem(accountMenu,
			new BMessage(MD_CHECK_FOR_MAILS)));

		// Not used:
		// menu->AddItem(new BMenuItem(B_TRANSLATE("Check For Mails Only"),
		// new BMessage(MD_CHECK_FOR_MAILS)));
		menu->AddItem(new BMenuItem(B_TRANSLATE("Send pending mails"),
			new BMessage(MD_SEND_MAILS)));
	} else {
		menu->AddItem(item = new BMenuItem(B_TRANSLATE("Check for mail now"),
			new BMessage(MD_CHECK_SEND_NOW)));
		if (accounts.CountAccounts() == 0)
			item->SetEnabled(false);
	}

	menu->AddSeparatorItem();
	menu->AddItem(new BMenuItem(B_TRANSLATE("Settings" B_UTF8_ELLIPSIS),
		new BMessage(MD_OPEN_PREFS)));

	if (modifiers() & B_SHIFT_KEY) {
		menu->AddItem(new BMenuItem(B_TRANSLATE("Shutdown mail services"),
			new BMessage(B_QUIT_REQUESTED)));
	}

	// Reset Item Targets (only those which aren't already set)

	for (int32 i = menu->CountItems(); i-- > 0;) {
		item = menu->ItemAt(i);
		if (item != NULL && (msg = item->Message()) != NULL) {
			if (msg->what == B_REFS_RECEIVED)
				item->SetTarget(tracker);
			else
				item->SetTarget(this);
		}
	}
	return menu;
}


/**
 * @brief Retrieves the entry_ref for the "New E-mail" saved query, creating it if needed.
 *
 * @param ref Output: the entry_ref for the query file.
 * @return B_OK on success.
 */
status_t
DeskbarView::_GetNewQueryRef(entry_ref& ref)
{
	BPath path;
	find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	path.Append("Mail/New E-mail");
	BEntry query(path.Path());
	if (!query.Exists())
		_CreateNewMailQuery(query);
	return query.GetRef(&ref);
}
