/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work originally distributed as the Be Sample Code
 * "SndFilePanel.h": Copyright 2000, Be Incorporated. A BFilePanel
 * derivative that lets you select and preview audio files.
 */

/** @file SoundFilePanel.h
    @brief BFilePanel specialization that previews audio files in-place. */

#ifndef _SOUNDFILEPANEL_H_
#define _SOUNDFILEPANEL_H_

#include <Button.h>
#include <FileGameSound.h>
#include <FilePanel.h>
#include <Handler.h>
#include <MessageRunner.h>


/**
 * @brief BFilePanel that adds a play/stop button for previewing audio.
 *
 * Acts as both file panel and message handler; the preview is driven by a
 * BFileGameSound and the button label is repainted by a BMessageRunner so
 * it returns to the "play" state when playback ends.
 */
class SoundFilePanel : public BFilePanel, public BHandler {
public:
	SoundFilePanel(BHandler* handler);
	virtual ~SoundFilePanel();

	virtual void SelectionChanged();
	virtual void WasHidden();

	virtual void MessageReceived(BMessage* msg);

private:
	BFileGameSound* fSoundFile;
	BButton* fPlayButton;
	BMessageRunner* fButtonUpdater;
};


/**
 * @brief BRefFilter that admits only directories and audio MIME types.
 */
class SoundFileFilter : public BRefFilter {
public:
	bool Filter(const entry_ref* entryRef, BNode* node, struct stat_beos* stat,
			const char* fileType);
};

#endif // _SOUNDFILEPANEL_H_
