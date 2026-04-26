/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2007-2015, Haiku, Inc. and Copyright 2011,
 * Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file AutoConfigWindow.h
    @brief Modal wizard window that walks the user through creating a new
           mail account: identity collection, automatic server discovery,
           server review, and finally writing settings into the
           BMailDaemon configuration. */

#ifndef AUTO_CONFIG_WINDOW_H
#define AUTO_CONFIG_WINDOW_H


#include <Box.h>
#include <Button.h>
#include <View.h>
#include <Window.h>

#include "MailSettings.h"

#include "AutoConfigView.h"
#include "ConfigWindow.h"


// message constants
/** @brief Posted by the "Back" button on the second page. */
const int32	kBackMsg = '?bac';
/** @brief Posted by the "Next/Finish" button to advance the wizard or
           commit the new account. */
const int32	kOkMsg = '?bok';


/**
 * @brief Two-page modal wizard that creates a new BMailAccountSettings
 *        from scratch and registers it with the parent ConfigWindow.
 */
class AutoConfigWindow : public BWindow {
public:
								AutoConfigWindow(BRect rect,
									ConfigWindow* parent);
								~AutoConfigWindow();

	virtual void				MessageReceived(BMessage* msg);
	virtual bool				QuitRequested(void);

private:
			account_info 		fAccountInfo;

			BMailAccountSettings*
								GenerateBasicAccount();

			BView*				fContainerView;
			ConfigWindow*		fParentWindow;
			BMailAccountSettings*
								fAccount;
			AutoConfigView*		fMainView;
			ServerSettingsView*	fServerView;
			BButton*			fBackButton;
			BButton*			fNextButton;

			bool				fMainConfigState;
			bool				fServerConfigState;
			bool				fAutoConfigServer;

			AutoConfig			fAutoConfig;
};


#endif	// AUTO_CONFIG_WINDOW_H
