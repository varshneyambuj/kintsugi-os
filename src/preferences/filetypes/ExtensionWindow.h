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
 * @file ExtensionWindow.h
 * @brief Dialog and helpers for editing the file-name extensions list of a
 *        MIME type entry.
 */

#ifndef EXTENSION_WINDOW_H
#define EXTENSION_WINDOW_H


#include <Messenger.h>
#include <Mime.h>
#include <String.h>
#include <Window.h>

class BButton;
class BTextControl;

class FileTypesWindow;


/**
 * @brief Modal window that adds or edits a single file-name extension on
 *        a MIME type's extensions list.
 */
class ExtensionWindow : public BWindow {
	public:
		ExtensionWindow(FileTypesWindow* target, BMimeType& type,
			const char* extension);
		virtual ~ExtensionWindow();

		virtual void MessageReceived(BMessage* message);

	private:
		BMessenger		fTarget;
		BMimeType		fMimeType;
		BString			fExtension;
		BTextControl*	fExtensionControl;
		BButton*		fAcceptButton;
};

extern status_t merge_extensions(BMimeType& type, const BList& newExtensions,
	const char* removeExtension = NULL);
extern status_t replace_extension(BMimeType& type, const char* newExtension,
	const char* oldExtension);

#endif	// EXTENSION_WINDOW_H
