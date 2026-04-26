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
 * MIT License. Copyright 2003-2010, Haiku Inc.
 * Original authors: Jérôme Duval, Oliver Ruiz Dorantes, Atsushi Takamatsu.
 */

/** @file HWindow.h
    @brief Main window for the Sounds preferences app. */

#ifndef __HWINDOW_H__
#define __HWINDOW_H__


#include "SoundFilePanel.h"


#include <FileGameSound.h>
#include <FilePanel.h>
#include <Window.h>


class HEventList;
class HTypeList;


/** @brief 'MPLM' Play the sound bound to the selected event. */
/** @brief 'MSTO' Stop the currently playing sound. */
/** @brief 'MREM' Remove the binding of the selected event (legacy). */
/** @brief 'MITE' A sound-file menu item was chosen. */
/** @brief 'MOTH' "Other..." menu item; opens the file panel. */
/** @brief 'MNON' "<none>" menu item; clears the binding. */
/** @brief 'MADE' Reserved for adding a new event entry. */
/** @brief 'MREE' Reserved for removing an event entry. */
/** @brief 'MOPW' Reserved for "Open With..." action. */
enum{
	M_PLAY_MESSAGE = 'MPLM',
	M_STOP_MESSAGE = 'MSTO',
	M_REMOVE_MESSAGE = 'MREM',
	M_ITEM_MESSAGE = 'MITE',
	M_OTHER_MESSAGE = 'MOTH',
	M_NONE_MESSAGE = 'MNON',
	M_ADD_EVENT = 'MADE',
	M_REMOVE_EVENT = 'MREE',
	M_OPEN_WITH = 'MOPW'
};


/**
 * @brief Main BWindow for the Sounds preferences app.
 *
 * Owns the HEventList, the SoundFilePanel for browsing custom wav files,
 * and the BFileGameSound used for previewing the bound sound. Persists its
 * frame and last-used directory via a flattened BMessage in the user
 * settings directory.
 */
class HWindow : public BWindow {
public:
								HWindow(BRect rect, const char* name);
	virtual						~HWindow();

	virtual	void				DispatchMessage(BMessage* message,
									BHandler* handler);
	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

private:
			void				_InitGUI();
			void				_Pulse();
			void				_SetupMenuField();
			void				_UpdateZoomLimits();

private:
			HEventList*			fEventList;
			SoundFilePanel*		fFilePanel;
			BButton*			fPlayButton;
			BFileGameSound*		fPlayer;
			BRect				fFrame;
			entry_ref			fPathRef;
};


#endif	// __HWINDOW_H__
