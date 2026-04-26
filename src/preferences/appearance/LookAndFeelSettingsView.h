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
 * MIT License. Copyright 2010-2020 Haiku, Inc.
 * Original authors: Stephan Aßmus, Alexander von Gluck, Ryan Leavengood,
 *                   John Scipione.
 */

/** @file LookAndFeelSettingsView.h
    @brief Tab pane that picks decorator, ControlLook and arrow style. */

#ifndef LOOK_AND_FEEL_SETTINGS_VIEW_H
#define LOOK_AND_FEEL_SETTINGS_VIEW_H


#include <DecorInfo.h>
#include <String.h>
#include <View.h>


class BButton;
class BCheckBox;
class BMenuField;
class BPopUpMenu;
class FakeScrollBar;


/**
 * @brief BView subclass that drives the Look-and-feel tab.
 */
class LookAndFeelSettingsView : public BView {
public:
								LookAndFeelSettingsView(const char* name);
	virtual						~LookAndFeelSettingsView();

	virtual	void				AttachedToWindow();
	virtual	void				MessageReceived(BMessage* message);

			bool				IsDefaultable();
			void				SetDefaults();

			bool				IsRevertable();
			void				Revert();

private:
			void				_SetDecor(const BString& name);
			void				_SetDecor(BPrivate::DecorInfo* decorInfo);
			void				_BuildDecorMenu();
			const char*			_DecorLabel(const BString& name);

			void				_SetControlLook(const BString& path);
			void				_BuildControlLookMenu();
			const char*			_ControlLookLabel(const char* name);

			bool				_DoubleScrollBarArrows();
			void				_SetDoubleScrollBarArrows(bool doubleArrows);

private:
			DecorInfoUtility	fDecorUtility;

			BButton*			fDecorInfoButton;
			BMenuField*			fDecorMenuField;
			BPopUpMenu*			fDecorMenu;

			BButton*			fControlLookInfoButton;
			BMenuField*			fControlLookMenuField;
			BPopUpMenu*			fControlLookMenu;

			FakeScrollBar*		fArrowStyleSingle;
			FakeScrollBar*		fArrowStyleDouble;

			BString				fSavedDecor;
			BString				fCurrentDecor;

			BString				fSavedControlLook;
			BString				fCurrentControlLook;

			bool				fSavedDoubleArrowsValue : 1;
};


#endif // LOOK_AND_FEEL_SETTINGS_VIEW_H
