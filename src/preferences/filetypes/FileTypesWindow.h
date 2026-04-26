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
 * @file FileTypesWindow.h
 * @brief Main browser window for the system MIME database with editors
 *        for icon, extensions, sniffer rule, preferred app, and
 *        Tracker attributes.
 */

#ifndef FILE_TYPES_WINDOW_H
#define FILE_TYPES_WINDOW_H


#include <Alert.h>
#include <Mime.h>
#include <Window.h>


class BBox;
class BButton;
class BListView;
class BMenuField;
class BMimeType;
class BOutlineListView;
class BSplitView;
class BTextControl;

class AttributeListView;
class ExtensionListView;
class MimeTypeListView;
class StringView;
class TypeIconView;


/** @brief Internal message: a new MIME type was just added; select it. */
static const uint32 kMsgSelectNewType = 'slnt';
/** @brief Internal message: the modal NewFileTypeWindow has closed. */
static const uint32 kMsgNewTypeWindowClosed = 'ntwc';


/**
 * @brief Top-level FileTypes browser; owns the MIME tree on the left and
 *        the per-type editor panels on the right.
 */
class FileTypesWindow : public BWindow {
public:
								FileTypesWindow(const BMessage& settings);
	virtual						~FileTypesWindow();

	virtual	void				MessageReceived(BMessage* message);
	virtual	bool				QuitRequested();

			void				SelectType(const char* type);

			void				PlaceSubWindow(BWindow* window);

private:
			BRect				_Frame(const BMessage& settings) const;
			void				_ShowSnifferRule(bool show);
			void				_UpdateExtensions(BMimeType* type);
			void				_AdoptPreferredApplication(BMessage* message,
									bool sameAs);
			void				_UpdatePreferredApps(BMimeType* type);
			void				_UpdateIcon(BMimeType* type);
			void				_SetType(BMimeType* type,
									int32 forceUpdate = 0);
			void				_MoveUpAttributeIndex(int32 index);

private:
			BMimeType			fCurrentType;

			BSplitView*			fMainSplitView;

			MimeTypeListView*	fTypeListView;
			BButton*			fRemoveTypeButton;

			BBox*				fIconBox;
			TypeIconView*		fIconView;

			BBox*				fRecognitionBox;
			StringView*			fExtensionLabel;
			ExtensionListView*	fExtensionListView;
			BButton*			fAddExtensionButton;
			BButton*			fRemoveExtensionButton;
			BTextControl*		fRuleControl;

			BBox*				fDescriptionBox;
			StringView*			fInternalNameView;
			BTextControl*		fTypeNameControl;
			BTextControl*		fDescriptionControl;

			BBox*				fPreferredBox;
			BMenuField*			fPreferredField;
			BButton*			fSelectButton;
			BButton*			fSameAsButton;

			BBox*				fAttributeBox;
			AttributeListView*	fAttributeListView;
			BButton*			fAddAttributeButton;
			BButton*			fRemoveAttributeButton;
			BButton*			fMoveUpAttributeButton;
			BButton*			fMoveDownAttributeButton;

			BWindow*			fNewTypeWindow;
};


#endif	// FILE_TYPES_WINDOW_H
