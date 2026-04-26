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
 * MIT License. Copyright 2010-2017, Haiku, Inc.
 */

/** @file PrefletWin.h
    @brief Top-level window for the Notifications preflet; hosts a
           PrefletView plus the global Defaults/Revert button strip and
           implements the SettingsHost callbacks. */

#ifndef _PREFLET_WIN_H
#define _PREFLET_WIN_H


#include <GroupView.h>
#include <LayoutBuilder.h>
#include <Message.h>
#include <Window.h>

#include "SettingsHost.h"

class BButton;

class PrefletView;


/**
 * @brief Main BWindow for the Notifications preference application.
 *
 * Owns the tab view (PrefletView) and the Defaults/Revert button strip,
 * loads and saves the preflet settings file, and serves as the
 * SettingsHost that individual panes call when their state changes.
 */
class PrefletWin : public BWindow, public SettingsHost {
public:
							PrefletWin();

	virtual	bool			QuitRequested();
	virtual	void			MessageReceived(BMessage* msg);

	virtual	void			SettingChanged(bool showExample);
			void			ReloadSettings();

private:
			status_t		_Revert();
			bool			_RevertPossible();
			status_t		_Defaults();
			bool			_DefaultsPossible();
			void			_SendExampleNotification();

			PrefletView*	fMainView;
			BGroupView*		fButtonsView;
			BButton*		fDefaults;
			BButton*		fRevert;
			BGroupLayout*	fButtonsLayout;
};

#endif // _PREFLET_WIN_H
