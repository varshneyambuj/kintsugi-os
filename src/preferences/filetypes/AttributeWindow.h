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
 * MIT License. Copyright 2006-2007, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file AttributeWindow.h
 * @brief Editor window for a single Tracker attribute description on a
 *        MIME type (name, type code, alignment, width, visibility flags).
 */

#ifndef ATTRIBUTE_WINDOW_H
#define ATTRIBUTE_WINDOW_H


#include "AttributeListView.h"

#include <Messenger.h>
#include <Mime.h>
#include <String.h>
#include <Window.h>

class BButton;
class BCheckBox;
class BMenu;
class BMenuField;
class BTextControl;

class FileTypesWindow;


/**
 * @brief Modal dialog that adds or edits a Tracker attribute entry on a
 *        MIME type.
 */
class AttributeWindow : public BWindow {
	public:
		AttributeWindow(FileTypesWindow* target, BMimeType& type,
			AttributeItem* item);
		virtual ~AttributeWindow();

		virtual void MessageReceived(BMessage* message);
		virtual bool QuitRequested();

	private:
		type_code _CurrentType() const;
		BMenuItem* _DefaultDisplayAs() const;
		void _CheckDisplayAs();
		void _CheckAcceptable();
		AttributeItem* _NewItemFromCurrent();

	private:
		BMessenger		fTarget;
		BMimeType		fMimeType;
		AttributeItem	fAttribute;
		BTextControl*	fPublicNameControl;
		BTextControl*	fAttributeControl;
		BMenu*			fTypeMenu;
		BMenuField*		fDisplayAsMenuField;
		BMenuField*		fAlignmentMenuField;
		BCheckBox*		fVisibleCheckBox;
		BCheckBox*		fEditableCheckBox;
		BTextControl*	fSpecialControl;
		BTextControl*	fWidthControl;
		BButton*		fAcceptButton;
};

#endif	// ATTRIBUTE_WINDOW_H
