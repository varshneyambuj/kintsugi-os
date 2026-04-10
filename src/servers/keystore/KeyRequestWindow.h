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
/** @file KeyRequestWindow.h
 *  @brief Window that prompts the user to unlock a keyring. */
#ifndef _KEY_REQUEST_WINDOW_H
#define _KEY_REQUEST_WINDOW_H


#include <Message.h>
#include <Window.h>


class KeyRequestView;


/** @brief Dialog prompting the user to enter a keyring passphrase. */
class KeyRequestWindow : public BWindow {
public:
									/** @brief Construct the keyring unlock dialog. */
									KeyRequestWindow();
/** @brief Destructor. */
virtual								~KeyRequestWindow();

/** @brief Cancel the request if the window is closed. */
virtual	bool						QuitRequested();
/** @brief Handle cancel and unlock button clicks. */
virtual	void						MessageReceived(BMessage* message);

		/** @brief Show the dialog and block until the user provides a key. */
		status_t					RequestKey(const BString& keyringName,
										BMessage& keyMessage);

private:
		KeyRequestView*				fRequestView; /**< View containing password input controls */
		sem_id						fDoneSem; /**< Semaphore signaling dialog completion */
		status_t					fResult; /**< Dialog result status */
};


#endif // _KEY_REQUEST_WINDOW_H
