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
 *   Copyright 2001 Dr. Zoidberg Enterprises. All rights reserved.
 *
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file FileConfigView.cpp
 * @brief File-path configuration widgets for mail filter add-ons.
 *
 * Provides FileControl, a self-contained BView combining a BTextControl and a
 * "Select..." BButton that opens a BFilePanel, and MailFileConfigView which
 * wraps FileControl to integrate with the BMailAddOnSettings save/load
 * lifecycle. Add-on configuration panels use these views to let users pick
 * file-system paths for filters such as the sieve script filter.
 *
 * @see BMailSettingsView, BMailAddOnSettings
 */


#include <FileConfigView.h>

#include <stdio.h>

#include <Button.h>
#include <Catalog.h>
#include <GroupLayout.h>
#include <MailSettingsView.h>
#include <Message.h>
#include <Path.h>
#include <String.h>
#include <TextControl.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "MailKit"


/** @brief Message constant sent when the user clicks the Select button. */
static const uint32 kMsgSelectButton = 'fsel';


namespace BPrivate {


/**
 * @brief Constructs a FileControl view with a text field and a file-picker button.
 *
 * Creates a horizontal group layout containing a BTextControl pre-populated
 * with \a pathOfFile and a "Select…" BButton. A BFilePanel restricted to
 * \a flavors is allocated but not shown until the button is pressed.
 *
 * @param name        BView name for this control.
 * @param label       Human-readable label displayed next to the text field.
 * @param pathOfFile  Initial path string shown in the text field.
 * @param flavors     BFilePanel entry-flavors constant (e.g. B_FILE_NODE).
 */
FileControl::FileControl(const char* name, const char* label,
	const char* pathOfFile, uint32 flavors)
	:
	BView(name, 0)
{
	SetViewUIColor(B_PANEL_BACKGROUND_COLOR);
	SetLayout(new BGroupLayout(B_HORIZONTAL));

	fText = new BTextControl("file_path", label, pathOfFile, NULL);
	AddChild(fText);

	fButton = new BButton("select_file", B_TRANSLATE("Select" B_UTF8_ELLIPSIS),
		new BMessage(kMsgSelectButton));
	AddChild(fButton);

	fPanel = new BFilePanel(B_OPEN_PANEL, NULL, NULL, flavors, false);
}


/**
 * @brief Destroys the FileControl and its file panel.
 */
FileControl::~FileControl()
{
	delete fPanel;
}


/**
 * @brief Connects button and file-panel targets after the view is attached to a window.
 */
void
FileControl::AttachedToWindow()
{
	fButton->SetTarget(this);
	fPanel->SetTarget(this);
}


/**
 * @brief Handles the Select button and file-panel ref-received messages.
 *
 * When kMsgSelectButton is received, the file panel is repositioned to the
 * current text-field directory and shown. When B_REFS_RECEIVED is received
 * (user selected a file), the text field is updated with the selected path.
 *
 * @param msg  Incoming BMessage from the button or file panel.
 */
void
FileControl::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kMsgSelectButton:
		{
			fPanel->Hide();

			BPath path(fText->Text());
			if (path.InitCheck() == B_OK && path.GetParent(&path) == B_OK)
				fPanel->SetPanelDirectory(path.Path());

			fPanel->Show();
			break;
		}
		case B_REFS_RECEIVED:
		{
			entry_ref ref;
			if (msg->FindRef("refs", &ref) == B_OK) {
				BEntry entry(&ref);
				if (entry.InitCheck() == B_OK) {
					BPath path;
					entry.GetPath(&path);

					fText->SetText(path.Path());
				}
			}
			break;
		}

		default:
			BView::MessageReceived(msg);
			break;
	}
}


/**
 * @brief Sets the text field to the specified path string.
 *
 * @param pathOfFile  New path value to display.
 */
void
FileControl::SetText(const char* pathOfFile)
{
	fText->SetText(pathOfFile);
}


/**
 * @brief Returns the current path string shown in the text field.
 *
 * @return Pointer to the text field's content; valid for the lifetime of this view.
 */
const char*
FileControl::Text() const
{
	return fText->Text();
}


/**
 * @brief Enables or disables both the text field and the Select button.
 *
 * @param enabled  true to enable, false to disable.
 */
void
FileControl::SetEnabled(bool enabled)
{
	fText->SetEnabled(enabled);
	fButton->SetEnabled(enabled);
}


//	#pragma mark -


/**
 * @brief Constructs a MailFileConfigView tied to a named settings key.
 *
 * @param label        Human-readable label for the text field.
 * @param name         Settings key name used when loading and saving.
 * @param useMeta      If true, reads/writes from the meta (account) message;
 *                     otherwise uses the filter's own settings message.
 * @param defaultPath  Initial path shown before any settings are loaded.
 * @param flavors      BFilePanel entry-flavors constant.
 */
MailFileConfigView::MailFileConfigView(const char* label, const char* name,
	bool useMeta, const char* defaultPath, uint32 flavors)
	:
	FileControl(name, label, defaultPath, flavors),
	fUseMeta(useMeta),
	fName(name)
{
}


/**
 * @brief Populates the view from the appropriate settings message.
 *
 * Reads the string value stored under fName from either \a archive or
 * \a meta depending on the fUseMeta flag set at construction time.
 *
 * @param archive  The filter's own add-on settings message.
 * @param meta     The account-level meta settings message.
 */
void
MailFileConfigView::SetTo(const BMessage* archive, BMessage* meta)
{
	SetText((fUseMeta ? meta : archive)->FindString(fName));
	fMeta = meta;
}


/**
 * @brief Saves the current path value into the appropriate settings message.
 *
 * Writes the text field content under fName to either the meta message or
 * \a settings depending on fUseMeta.
 *
 * @param settings  The BMailAddOnSettings object to persist the value into.
 * @return B_OK on success, or a BMessage error code on failure.
 */
status_t
MailFileConfigView::SaveInto(BMailAddOnSettings& settings) const
{
	BMessage* archive = fUseMeta ? fMeta : &settings;
	return archive->SetString(fName, Text());
}


}	// namespace BPrivate
