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
 * @file FileTypeWindow.h
 * @brief Per-file MIME-type editor window: shows the type, icon, and
 *        preferred application of one or several selected entries.
 */

#ifndef FILE_TYPE_WINDOW_H
#define FILE_TYPE_WINDOW_H


#include <Mime.h>
#include <String.h>
#include <Window.h>

#include <ObjectList.h>

class BButton;
class BMenuField;
class BTextControl;

class IconView;
class MimeTypeListView;


/**
 * @brief Window that edits the MIME type and preferred application of one
 *        or more file system entries passed as refs.
 */
class FileTypeWindow : public BWindow {
	public:
		FileTypeWindow(BPoint position, const BMessage& refs);
		virtual ~FileTypeWindow();

		virtual void MessageReceived(BMessage* message);
		virtual bool QuitRequested();

	private:
		BString _Title(const BMessage& refs);
		void _SetTo(const BMessage& refs);
		void _AdoptType(BMessage* message);
		void _AdoptType();
		void _AdoptPreferredApp(BMessage* message, bool sameAs);
		void _AdoptPreferredApp();
		void _UpdatePreferredApps();

	private:
		BObjectList<entry_ref> fEntries;
		BString			fCommonType;
		BString			fCommonPreferredApp;

		BTextControl*	fTypeControl;
		BButton*		fSelectTypeButton;
		BButton*		fSameTypeAsButton;

		IconView*		fIconView;

		BMenuField*		fPreferredField;
		BButton*		fSelectAppButton;
		BButton*		fSameAppAsButton;
};

#endif // FILE_TYPE_WINDOW_H
