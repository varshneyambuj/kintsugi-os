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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2014-2017, Haiku.
 */

/** @file MidiSettingsView.h
    @brief Settings view that lists installed SoundFonts and selects the active one. */

#ifndef MIDIVIEW_H_
#define MIDIVIEW_H_


#include <String.h>

#include "MediaViews.h"

class BButton;
class BListView;
class BStringView;

/**
 * @brief Settings pane for choosing the SoundFont used by the MIDI subsystem.
 *
 * Loads the current selection from the shared midi_settings file, scans
 * the @c synth data directories for available SoundFonts, and watches
 * those directories so newly installed files appear automatically.
 */
class MidiSettingsView : public SettingsView {
public:
					MidiSettingsView();

	virtual void	AttachedToWindow();
	virtual void	DetachedFromWindow();
	virtual void	MessageReceived(BMessage* message);

private:
	void 			_RetrieveSoundFontList();
	void 			_LoadSettings();
	void 			_SaveSettings();
	void 			_WatchFolders();
	void			_SelectActiveSoundFont();
	BString			_SelectedSoundFont() const;
	void			_UpdateSoundFontStatus();

	BListView*		fListView;
	BString			fActiveSoundFont;
	BStringView*	fSoundFontStatus;
};

#endif /* MIDIVIEW_H_ */
