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
 * MIT License. Copyright 2008, Andrej Spielmann.
 */

/** @file AntialiasingSettingsView.h
    @brief Tab pane that exposes font antialiasing controls. */

#ifndef ANTIALIASING_SETTINGS_VIEW_H
#define ANTIALIASING_SETTINGS_VIEW_H


#include <View.h>

class BBox;
class BMenuField;
class BPopUpMenu;
class BSlider;
class BTextView;


/**
 * @brief BView subclass that drives subpixel, hinting and weight settings.
 */
class AntialiasingSettingsView : public BView {
public:
							AntialiasingSettingsView(const char* name);
	virtual					~AntialiasingSettingsView();

	virtual	void			AttachedToWindow();
	virtual	void			MessageReceived(BMessage* message);

			void			SetDefaults();
			void			Revert();
			bool			IsDefaultable();
			bool			IsRevertable();

private:
			void			_BuildAntialiasingMenu();
			void			_SetCurrentAntialiasing();
			void			_BuildHintingMenu();
			void			_SetCurrentHinting();
			void			_SetCurrentAverageWeight();

protected:
			float			fDivider;

			BMenuField*		fAntialiasingMenuField;
			BPopUpMenu*		fAntialiasingMenu;
			BMenuField*		fHintingMenuField;
			BPopUpMenu*		fHintingMenu;
			BSlider*		fAverageWeightControl;

			bool			fSavedSubpixelAntialiasing;
			bool			fCurrentSubpixelAntialiasing;
			uint8			fSavedHinting;
			uint8			fCurrentHinting;
			unsigned char	fSavedAverageWeight;
			unsigned char	fCurrentAverageWeight;
};

#endif // ANTIALIASING_SETTINGS_VIEW_H
