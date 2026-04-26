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
 *   SndFilePanel.cpp
 *   ----------------
 *   A BFilePanel derivative that lets you select and preview
 *   audio files.
 *
 *   Copyright 2000, Be Incorporated. All Rights Reserved.
 *   This file may be used under the terms of the Be Sample Code License.
 */


/**
 * @file SoundFilePanel.cpp
 * @brief BFilePanel specialization that previews audio files in-place.
 *
 * Adds a play/stop button to the standard file panel and uses
 * BFileGameSound to preview the currently selected sound. A periodic
 * BMessageRunner repaints the button label to "play" once playback ends.
 *
 * @see HWindow, BFileGameSound
 */


#include "SoundFilePanel.h"

#include <Catalog.h>
#include <ControlLook.h>
#include <File.h>
#include <Messenger.h>
#include <NodeInfo.h>
#include <String.h>
#include <Window.h>


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "SoundFilePanel"


/** @brief Toggles play/stop on the preview button. */
const uint32 kPlayStop = 'plst';
/** @brief Periodic message that repaints the preview button label. */
const uint32 kUpdateButton = 'btup';
/** @brief UTF-8 right-pointing triangle ("Play") shown on the button. */
const char* kPlayLabel = "\xE2\x96\xB6";
/** @brief UTF-8 black square ("Stop") shown while audio is playing. */
const char* kStopLabel = "\xE2\x96\xA0";


/**
 * @brief Builds the file panel and inserts the preview play/stop button.
 *
 * Locks the underlying BFilePanel window long enough to install the new
 * button next to the panel's Cancel button and registers this object as a
 * handler so it can receive button messages.
 *
 * @param handler  Target handler to which selected refs are reported.
 */
SoundFilePanel::SoundFilePanel(BHandler* handler)
	:
	BFilePanel(B_OPEN_PANEL, new BMessenger(handler), NULL, B_FILE_NODE, false, NULL,
		new SoundFileFilter(), false, true),
	fSoundFile(NULL),
	fPlayButton(NULL),
	fButtonUpdater(NULL)
{
	BView* view;

	if (Window()->Lock()) {
		Window()->AddHandler(this);

		BView* cancel = Window()->FindView("cancel button");
		if (cancel != NULL) {
			view = Window()->ChildAt(0);
			if (view != NULL) {
				static const float spacing = be_control_look->DefaultItemSpacing();
				BRect cancelRect(cancel->Frame());
				BRect playRect(spacing, cancelRect.top, cancelRect.Height() + spacing,
					cancelRect.bottom);
				fPlayButton = new BButton(playRect, "PlayStop", kPlayLabel, new BMessage(kPlayStop),
					B_FOLLOW_LEFT | B_FOLLOW_BOTTOM);
				fPlayButton->SetTarget(this);
				fPlayButton->SetEnabled(false);
				view->AddChild(fPlayButton);
			}
		}
		Window()->Unlock();
	}

	SetButtonLabel(B_DEFAULT_BUTTON, B_TRANSLATE("Select"));
	Window()->SetTitle(B_TRANSLATE("Select a sound file"));
}


/**
 * @brief Releases the ref filter, the preview sound, and the update timer.
 */
SoundFilePanel::~SoundFilePanel()
{
	delete RefFilter();
	delete fSoundFile;
	delete fButtonUpdater;
}


/**
 * @brief Updates the previewed sound when the file selection changes.
 *
 * Tears down any previously loaded preview and, if the new selection is a
 * regular file, opens it via BFileGameSound; the play button becomes
 * enabled only when the new sound initialises successfully.
 */
void
SoundFilePanel::SelectionChanged()
{
	status_t err;
	entry_ref ref;

	if (fSoundFile) {
		delete fSoundFile;
		fSoundFile = NULL;
		fPlayButton->SetEnabled(false);
	}

	Rewind();
	err = GetNextSelectedRef(&ref);
	if (err == B_OK) {
		BNode node(&ref);
		if (!node.IsDirectory()) {
			delete fSoundFile;
			fSoundFile = new BFileGameSound(&ref, false);
			if (fSoundFile->InitCheck() == B_OK)
				fPlayButton->SetEnabled(true);
		}
	}
}


/**
 * @brief Stops the periodic button update when the panel becomes hidden.
 */
void
SoundFilePanel::WasHidden()
{
	delete fButtonUpdater;
	fButtonUpdater = NULL;
}


/**
 * @brief Handles the preview button toggle and the periodic label update.
 *
 * On kPlayStop the active BFileGameSound is started or stopped and the
 * button label flips between play and stop glyphs. On kUpdateButton (sent
 * twice per second by the BMessageRunner) the button is reset to "play"
 * once the underlying sound stops on its own.
 *
 * @param msg  Incoming BMessage; only kPlayStop and kUpdateButton are
 *             special-cased.
 */
void
SoundFilePanel::MessageReceived(BMessage* msg)
{
	switch (msg->what) {
		case kPlayStop:
		{
			if (fSoundFile != NULL) {
				if (fSoundFile->IsPlaying()) {
					fSoundFile->StopPlaying();
					fPlayButton->SetLabel(kPlayLabel);
				} else {
					fSoundFile->StartPlaying();
					fPlayButton->SetLabel(kStopLabel);
					if (fButtonUpdater == NULL) {
						fButtonUpdater = new BMessageRunner(BMessenger(this),
							new BMessage(kUpdateButton), 500000); // every .5 sec
					}
				}
			}
			break;
		}
		case kUpdateButton:
		{
			if (fSoundFile != NULL) {
				if (!fSoundFile->IsPlaying())
					fPlayButton->SetLabel(kPlayLabel);
			}
			break;
		}
	}
}


/**
 * @brief Lets only directories and audio MIME types appear in the panel.
 *
 * @param entryRef  Entry being considered (unused here).
 * @param node      Open BNode used to read the MIME type.
 * @param stat      Stat block from the file panel (unused here).
 * @param fileType  Type string supplied by the panel; the sniffed type from
 *                  BNodeInfo takes precedence.
 * @return          @c true to accept the entry, @c false to hide it.
 */
bool
SoundFileFilter::Filter(const entry_ref* entryRef, BNode* node, struct stat_beos* stat,
	const char* fileType)
{
	bool admitIt = false;
	char type[256];
	const BString mask("audio");
	BNodeInfo nodeInfo(node);

	if (node->IsDirectory()) {
		admitIt = true;
	} else {
		nodeInfo.GetType(type);
		admitIt = (mask.Compare(type, mask.CountChars()) == 0);
	}

	return admitIt;
}
