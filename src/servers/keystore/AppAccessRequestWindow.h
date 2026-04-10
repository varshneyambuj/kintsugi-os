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
   Copyright 2012, Michael Lotz, mmlr@mlotz.ch. All Rights Reserved.
   Distributed under the terms of the MIT License.
 */
/** @file AppAccessRequestWindow.h
 *  @brief Window that prompts the user to allow keyring access. */
#ifndef _APP_ACCESS_REQUEST_WINDOW_H
#define _APP_ACCESS_REQUEST_WINDOW_H

#include <Bitmap.h>
#include <Button.h>
#include <Message.h>
#include <Window.h>

#include "StripeView.h"

class AppAccessRequestView;


/** @brief Dialog prompting the user to authorize keyring access. */
class AppAccessRequestWindow : public BWindow {
public:
									/** @brief Construct the access-request dialog. */
									AppAccessRequestWindow(
										const char* keyringName,
										const char* signature,
										const char* path,
										const char* accessString, bool appIsNew,
										bool appWasUpdated);
/** @brief Destructor. */
virtual								~AppAccessRequestWindow();

/** @brief Disallow access if the window is closed. */
virtual	bool						QuitRequested();
/** @brief Handle disallow, once, and always button clicks. */
virtual	void						MessageReceived(BMessage* message);

		/** @brief Show the dialog and block until the user responds. */
		status_t					RequestAppAccess(bool& allowAlways);
		/** @brief Retrieve the calling application icon at a given size. */
		BBitmap						GetIcon(int32 iconSize);
private:
		AppAccessRequestView*		fRequestView;
		sem_id						fDoneSem; /**< Semaphore signaling dialog completion */
		uint32						fResult; /**< The user-chosen result message code */
		BButton* 					fDisallowButton;
		BButton* 					fOnceButton;
		BButton* 					fAlwaysButton;
		BStripeView*				fStripeView;

};


#endif // _APP_ACCESS_REQUEST_WINDOW_H
