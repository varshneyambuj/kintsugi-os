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
 * MIT License. Copyright 2006, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file TypeListWindow.h
 * @brief Modal browser window that lets the user pick a MIME type from the
 *        full system database and reports the selection to a target window.
 */

#ifndef TYPE_LIST_WINDOW_H
#define TYPE_LIST_WINDOW_H


#include <Messenger.h>
#include <Window.h>

class BButton;
class BMenu;
class BTextControl;

class MimeTypeListView;


/**
 * @brief Window that presents the MIME type tree for selection and posts
 *        the chosen type back to a caller-supplied messenger.
 */
class TypeListWindow : public BWindow {
	public:
		TypeListWindow(const char* currentType, uint32 what, BWindow* target);
		virtual ~TypeListWindow();

		virtual void MessageReceived(BMessage* message);

	private:
		BMessenger			fTarget;
		uint32				fWhat;
		MimeTypeListView*	fListView;
		BButton*			fSelectButton;
};

#endif	// TYPE_LIST_WINDOW_H
