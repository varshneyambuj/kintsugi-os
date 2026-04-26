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
 * MIT License. Copyright 2017, Haiku Inc.
 * Original authors: Brian Hill.
 */

/** @file AddRepoWindow.h
    @brief Modal dialog window for adding a package repository by URL. */

#ifndef ADD_REPO_WINDOW_H
#define ADD_REPO_WINDOW_H


#include <Button.h>
#include <TextControl.h>
#include <View.h>
#include <Window.h>


/**
 * @brief Window prompting the user to enter a repository URL to add.
 *
 * Presents a text control pre-populated from the clipboard when possible,
 * along with Add and Cancel buttons. Sends an ADD_REPO_URL message to the
 * supplied reply messenger when the user confirms a URL.
 */
class AddRepoWindow : public BWindow {
public:
							AddRepoWindow(BRect size,
								const BMessenger& messenger);
	virtual void			MessageReceived(BMessage*);
	virtual void			Quit();
	virtual void			FrameResized(float newWidth, float newHeight);

private:
	status_t				_GetClipboardData();

	BTextControl*			fText;
	BButton*				fAddButton;
	BButton*				fCancelButton;
	BMessenger				fReplyMessenger;
};


#endif
