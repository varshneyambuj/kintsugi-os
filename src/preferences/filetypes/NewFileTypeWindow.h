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
 * @file NewFileTypeWindow.h
 * @brief Dialog that registers a new MIME type by combining a chosen
 *        super-type with a user-supplied subtype name.
 */

#ifndef NEW_FILE_TYPE_WINDOW_H
#define NEW_FILE_TYPE_WINDOW_H


#include <Messenger.h>
#include <Window.h>

class BButton;
class BMenu;
class BTextControl;

class FileTypesWindow;


/**
 * @brief Window for creating a new entry in the system MIME database.
 */
class NewFileTypeWindow : public BWindow {
	public:
		NewFileTypeWindow(FileTypesWindow* target, const char* currentType);
		virtual ~NewFileTypeWindow();

		virtual void MessageReceived(BMessage* message);
		virtual bool QuitRequested();

	private:
		BMessenger		fTarget;
		BMenu*			fSupertypesMenu;
		BTextControl*	fNameControl;
		BButton*		fAddButton;
};

#endif	// NEW_FILE_TYPE_WINDOW_H
