/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2011-2023 Haiku, Inc.
 * Original authors: John Scipione, Jorge Acereda.
 */

/** @file ModifierKeysWindow.h
    @brief Floating window for remapping Caps/Shift/Control/Option/Command roles. */

#ifndef MODIFIER_KEYS_WINDOW_H
#define MODIFIER_KEYS_WINDOW_H


#include <Window.h>


class BButton;
class BMenuField;
class BPopUpMenu;
class StatusMenuField;


/**
 * @brief Floating editor that lets the user reassign modifier-key roles.
 *
 * For each of Caps Lock, Shift, Control, Option, and Command the user
 * picks which physical key should fulfil that role. Detects duplicate
 * assignments, disables OK while invalid, and posts a
 * kMsgUpdateModifierKeys message back to the main keymap window when
 * the user accepts the changes.
 */
class ModifierKeysWindow : public BWindow {
public:
									ModifierKeysWindow();
	virtual							~ModifierKeysWindow();

	virtual	void					MessageReceived(BMessage* message);

private:
			void					_CreateMenuField(BPopUpMenu** _menu, BMenuField** _field,
										uint32 key, const char* label);
			void					_MarkMenuItems();
			bool					_MarkMenuItem(const char*, BPopUpMenu*, uint32 l, uint32 r);
			const char*				_KeyToString(int32 key);
			int32					_KeyToKeyCode(int32 key, bool right = false);
			int32					_LastKey();
			void					_ValidateDuplicateKeys();
			void					_ValidateDuplicateKey(StatusMenuField*, uint32);
			uint32					_DuplicateKeys();
			void					_UpdateStatus();

			BPopUpMenu*				fCapsMenu;
			BPopUpMenu*				fShiftMenu;
			BPopUpMenu*				fControlMenu;
			BPopUpMenu*				fOptionMenu;
			BPopUpMenu*				fCommandMenu;

			StatusMenuField*		fCapsField;
			StatusMenuField*		fShiftField;
			StatusMenuField*		fControlField;
			StatusMenuField*		fOptionField;
			StatusMenuField*		fCommandField;

			BButton*				fRevertButton;
			BButton*				fCancelButton;
			BButton*				fOkButton;

			key_map*				fCurrentMap;
			key_map*				fSavedMap;

			char*					fCurrentBuffer;
			char*					fSavedBuffer;
};


#endif	// MODIFIER_KEYS_WINDOW_H
