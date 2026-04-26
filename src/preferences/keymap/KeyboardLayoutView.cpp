/*
 * Copyright 2026 Kintsugi OS Project. All rights reserved.
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
 * Authors:
 *     Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2009-2010, Axel Dörfler, axeld@pinc-software.de.
 *   Copyright 2013-2014 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       John Scipione, jscipione@gmail.com
 */


/**
 * @file KeyboardLayoutView.cpp
 * @brief Implementation of KeyboardLayoutView, the editable on-screen keyboard.
 *
 * Maps a KeyboardLayout to screen pixels, draws each key with labels
 * resolved through the bound Keymap, and lets the user remap keys via
 * drag-and-drop or popup menus. Also tracks live keyboard state so the
 * pressed keys are highlighted in real time, and exposes dead-key
 * handling and modifier-key swap operations.
 */


#include "KeyboardLayoutView.h"

#include <stdio.h>
#include <stdlib.h>

#include <Beep.h>
#include <Bitmap.h>
#include <ControlLook.h>
#include <LayoutUtils.h>
#include <InputServerDevice.h>
#include <MenuItem.h>
#include <PopUpMenu.h>
#include <Region.h>
#include <Window.h>

#include "Keymap.h"
#include "KeymapApplication.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Keyboard Layout View"


/** @brief Background tint for keys flagged "dark" in the layout. */
static const rgb_color kDarkColor = {200, 200, 200, 255};
/** @brief Highlight colour for the second key of a dead-key sequence. */
static const rgb_color kIdealSecondDeadKeyColor = {190, 190, 100, 255};
/** @brief Highlight colour for an active dead key awaiting completion. */
static const rgb_color kIdealDeadKeyColor = {102, 153, 205, 255};
/** @brief Fill colour for an LED indicator that is currently lit. */
static const rgb_color kLitIndicatorColor = {116, 212, 83, 255};


/**
 * @brief Returns true if @a keyCode is a left-side modifier key.
 *
 * @param keyCode  Hardware scancode to test.
 */
static bool
is_left_modifier_key(uint32 keyCode)
{
	return keyCode == 0x4b	// left shift
		|| keyCode == 0x5d	// left command
		|| keyCode == 0x5c	// left control
		|| keyCode == 0x66;	// left option
}


/**
 * @brief Returns true if @a keyCode is a right-side modifier or menu key.
 *
 * @param keyCode  Hardware scancode to test.
 */
static bool
is_right_modifier_key(uint32 keyCode)
{
	return keyCode == 0x56	// right shift
		|| keyCode == 0x5f	// right command
		|| keyCode == 0x60	// right control
		|| keyCode == 0x67	// right option
		|| keyCode == 0x68;	// menu
}


/**
 * @brief Returns true if @a keyCode is one of Caps/Num/Scroll Lock.
 *
 * @param keyCode  Hardware scancode to test.
 */
static bool
is_lock_key(uint32 keyCode)
{
	return keyCode == 0x3b	// caps lock
		|| keyCode == 0x22	// num lock
		|| keyCode == 0x0f;	// scroll lock
}


/**
 * @brief Returns true if @a keyCode can be reassigned to a modifier role.
 *
 * @param keyCode  Hardware scancode to test.
 */
static bool
is_mappable_to_modifier(uint32 keyCode)
{
	return is_left_modifier_key(keyCode)
		|| is_right_modifier_key(keyCode)
		|| is_lock_key(keyCode);
}


//	#pragma mark - KeyboardLayoutView


/**
 * @brief Constructs a layout view, optionally bound to an input device.
 *
 * When @a dev is non-NULL the view runs as a virtual on-screen
 * keyboard that injects key events through the provided device, and
 * editing is disabled. When @a dev is NULL the view operates in
 * editor mode and is editable by default.
 *
 * @param name  BView name (also used as the layout name on the wire).
 * @param dev   Optional input-server device for virtual-keyboard mode.
 */
KeyboardLayoutView::KeyboardLayoutView(const char* name, BInputServerDevice* dev)
	:
	BView(name, B_WILL_DRAW | B_FULL_UPDATE_ON_RESIZE | B_FRAME_EVENTS | B_TRANSPARENT_BACKGROUND),
	fKeymap(NULL),
	fEditable(dev == NULL),
	fModifiers(0),
	fDeadKey(0),
	fButtons(0),
	fDragKey(NULL),
	fDropTarget(NULL),
	fOldSize(0, 0),
	fDevice(dev)
{
	fLayout = new KeyboardLayout;
	memset(fKeyState, 0, sizeof(fKeyState));

	SetEventMask(B_KEYBOARD_EVENTS);

	SetViewColor(B_TRANSPARENT_COLOR);
}


/**
 * @brief Destroys the view. The layout, keymap, and device are not owned.
 */
KeyboardLayoutView::~KeyboardLayoutView()
{
}


/**
 * @brief Replaces the displayed layout and recomputes the on-screen geometry.
 *
 * @param layout  New keyboard layout. Ownership is not taken.
 */
void
KeyboardLayoutView::SetKeyboardLayout(KeyboardLayout* layout)
{
	fLayout = layout;
	_LayoutKeyboard();
	Invalidate();
}


/**
 * @brief Replaces the keymap used to label keys and resolve dead-key state.
 *
 * @param keymap  New keymap. Ownership is not taken.
 */
void
KeyboardLayoutView::SetKeymap(Keymap* keymap)
{
	fKeymap = keymap;
	Invalidate();
}


/**
 * @brief Sets the destination for synthesised B_KEY_DOWN messages from clicks.
 *
 * @param target  Messenger that receives generated key-down events.
 */
void
KeyboardLayoutView::SetTarget(BMessenger target)
{
	fTarget = target;
}


/**
 * @brief Sets the base font used for normal key labels and re-measures it.
 *
 * @param font  Font to use; its metrics are cached for label sizing.
 */
void
KeyboardLayoutView::SetBaseFont(const BFont& font)
{
	fBaseFont = font;

	font_height fontHeight;
	fBaseFont.GetHeight(&fontHeight);
	fBaseFontHeight = fontHeight.ascent + fontHeight.descent;
	fBaseFontSize = fBaseFont.Size();

	Invalidate();
}


/**
 * @brief Sets the default fonts and snapshot of current modifiers on attach.
 */
void
KeyboardLayoutView::AttachedToWindow()
{
	SetBaseFont(*be_plain_font);
	fSpecialFont = *be_fixed_font;
	fModifiers = modifiers();
}


/**
 * @brief Recomputes scale factor and offsets when the view is resized.
 *
 * @param width   New width (unused; bounds are read inside _LayoutKeyboard()).
 * @param height  New height (unused; bounds are read inside _LayoutKeyboard()).
 */
void
KeyboardLayoutView::FrameResized(float width, float height)
{
	_LayoutKeyboard();
}


/**
 * @brief Forces a redraw when the window gains focus so highlights stay accurate.
 *
 * @param active  Whether the window is becoming active.
 */
void
KeyboardLayoutView::WindowActivated(bool active)
{
	if (active)
		Invalidate();
}


/**
 * @brief Returns the minimum size, clamped to 100 x 50 pixels.
 */
BSize
KeyboardLayoutView::MinSize()
{
	return BLayoutUtils::ComposeSize(ExplicitMinSize(), BSize(100, 50));
}


/**
 * @brief Refreshes the live key-state map for any pressed key.
 *
 * @param bytes     Unused.
 * @param numBytes  Unused.
 */
void
KeyboardLayoutView::KeyDown(const char* bytes, int32 numBytes)
{
	_KeyChanged(Window()->CurrentMessage());
}


/**
 * @brief Refreshes the live key-state map for any released key.
 *
 * @param bytes     Unused.
 * @param numBytes  Unused.
 */
void
KeyboardLayoutView::KeyUp(const char* bytes, int32 numBytes)
{
	_KeyChanged(Window()->CurrentMessage());
}


/**
 * @brief Routes mouse-button presses on a key to the appropriate edit action.
 *
 * Primary button highlights or toggles modifier keys; secondary
 * (or primary + Control) opens a remap popup; tertiary toggles the
 * dead-key state for keys that have one. Most behaviours only apply
 * when the view is in editable mode.
 *
 * @param point  Click position in view coordinates.
 */
void
KeyboardLayoutView::MouseDown(BPoint point)
{
	fClickPoint = point;
	fDragKey = NULL;
	fDropPoint.x = -1;

	int32 buttons = 0;
	if (Looper() != NULL && Looper()->CurrentMessage() != NULL)
		Looper()->CurrentMessage()->FindInt32("buttons", &buttons);

	Key* key = _KeyAt(point);
	if (fKeymap == NULL || key == NULL) {
		fButtons = buttons;
		return;
	}

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0
			|| ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0
		&& (modifiers() & B_CONTROL_KEY) != 0)) {
		// secondary mouse button, pop up a swap context menu
		if (fEditable && !is_mappable_to_modifier(key->code)) {
			// ToDo: Pop up a list of alternative characters to map
			// the key to. Currently we only add an option to remove the
			// current key mapping.
			BPopUpMenu* alternativesPopUp = new BPopUpMenu(
				"Alternatives pop up", true, true, B_ITEMS_IN_COLUMN);
			BMessage* message = new BMessage(kMsgUpdateNormalKeys);
			message->AddUInt32("keyCode", key->code);
			message->AddBool("unset", true);
			alternativesPopUp->AddItem(new BMenuItem(B_TRANSLATE("Remove"),
				message));
			alternativesPopUp->SetAsyncAutoDestruct(true);
			if (alternativesPopUp->SetTargetForItems(Window()) == B_OK)
				alternativesPopUp->Go(ConvertToScreen(point), true);
		} else if (fEditable) {
			// pop up the modifier keys menu
			BPopUpMenu* modifiersPopUp = new BPopUpMenu("Modifiers pop up",
				true, true, B_ITEMS_IN_COLUMN);
			const key_map& map = fKeymap->Map();
			bool isLockKey = is_lock_key(key->code);
			BMenuItem* item = NULL;

			if (is_left_modifier_key(key->code) || isLockKey) {
				item = _CreateSwapModifiersMenuItem(B_LEFT_SHIFT_KEY,
					isLockKey ? B_LEFT_SHIFT_KEY : B_SHIFT_KEY,
					map.left_shift_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.left_shift_key)
					item->SetMarked(true);

				item = _CreateSwapModifiersMenuItem(B_LEFT_CONTROL_KEY,
					isLockKey ? B_LEFT_CONTROL_KEY : B_CONTROL_KEY,
					map.left_control_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.left_control_key)
					item->SetMarked(true);

				item = _CreateSwapModifiersMenuItem(B_LEFT_OPTION_KEY,
					isLockKey ? B_LEFT_OPTION_KEY : B_OPTION_KEY,
					map.left_option_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.left_option_key)
					item->SetMarked(true);

				item = _CreateSwapModifiersMenuItem(B_LEFT_COMMAND_KEY,
					isLockKey ? B_LEFT_COMMAND_KEY : B_COMMAND_KEY,
					map.left_command_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.left_command_key)
					item->SetMarked(true);
			}

			if (is_right_modifier_key(key->code) || isLockKey) {
				if (isLockKey)
					modifiersPopUp->AddSeparatorItem();

				item = _CreateSwapModifiersMenuItem(B_RIGHT_SHIFT_KEY,
					isLockKey ? B_RIGHT_SHIFT_KEY : B_SHIFT_KEY,
					map.right_shift_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.right_shift_key)
					item->SetMarked(true);

				item = _CreateSwapModifiersMenuItem(B_RIGHT_CONTROL_KEY,
					isLockKey ? B_RIGHT_CONTROL_KEY : B_CONTROL_KEY,
					map.right_control_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.right_control_key)
					item->SetMarked(true);
			}

			item = _CreateSwapModifiersMenuItem(B_MENU_KEY, B_MENU_KEY,
				map.menu_key, key->code);
			modifiersPopUp->AddItem(item);
			if (key->code == map.menu_key)
				item->SetMarked(true);

			if (is_right_modifier_key(key->code) || isLockKey) {
				item = _CreateSwapModifiersMenuItem(B_RIGHT_OPTION_KEY,
					isLockKey ? B_RIGHT_OPTION_KEY : B_OPTION_KEY,
					map.right_option_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.right_option_key)
					item->SetMarked(true);

				item = _CreateSwapModifiersMenuItem(B_RIGHT_COMMAND_KEY,
					isLockKey ? B_RIGHT_COMMAND_KEY : B_COMMAND_KEY,
					map.right_command_key, key->code);
				modifiersPopUp->AddItem(item);
				if (key->code == map.right_command_key)
					item->SetMarked(true);
			}

			modifiersPopUp->AddSeparatorItem();

			item = _CreateSwapModifiersMenuItem(B_CAPS_LOCK, B_CAPS_LOCK,
				map.caps_key, key->code);
			modifiersPopUp->AddItem(item);
			if (key->code == map.caps_key)
				item->SetMarked(true);

			item = _CreateSwapModifiersMenuItem(B_NUM_LOCK, B_NUM_LOCK,
				map.num_key, key->code);
			modifiersPopUp->AddItem(item);
			if (key->code == map.num_key)
				item->SetMarked(true);

			item = _CreateSwapModifiersMenuItem(B_SCROLL_LOCK, B_SCROLL_LOCK,
				map.scroll_key, key->code);
			modifiersPopUp->AddItem(item);
			if (key->code == map.scroll_key)
				item->SetMarked(true);

			modifiersPopUp->SetAsyncAutoDestruct(true);
			if (modifiersPopUp->SetTargetForItems(Window()) == B_OK)
				modifiersPopUp->Go(ConvertToScreen(point), true);
		}
	} else if ((buttons & B_TERTIARY_MOUSE_BUTTON) != 0
		&& (fButtons & B_TERTIARY_MOUSE_BUTTON) == 0) {
		// tertiary mouse button, toggle the "deadness" of dead keys
		bool isEnabled = false;
		uint8 deadKey = fKeymap->DeadKey(key->code, fModifiers, &isEnabled);
		if (deadKey > 0) {
			fKeymap->SetDeadKeyEnabled(key->code, fModifiers, !isEnabled);
			_InvalidateKey(key);
		}
	} else {
		// primary mouse button
		if (fKeymap->IsModifierKey(key->code)) {
			if (_KeyState(key->code)) {
				uint32 modifier = fKeymap->Modifier(key->code);
				if ((modifier & modifiers()) == 0) {
					_SetKeyState(key->code, false);
					fModifiers &= ~modifier;
					Invalidate();
				}
			} else {
				_SetKeyState(key->code, true);
				fModifiers |= fKeymap->Modifier(key->code);
				Invalidate();
			}

			// TODO: if possible, we could handle the lock keys for real
		} else {
			_SetKeyState(key->code, true);
			_InvalidateKey(key);
		}
	}

	fButtons = buttons;
}


/**
 * @brief Finalises a click sequence by sending a key-down or releasing state.
 *
 * Cancels in-progress drags, releases pressed-state for non-modifier
 * keys, and synthesises a B_KEY_DOWN to the target if a real click
 * (not a drag) was completed on a non-modifier key. Dead-key handling
 * is invoked here so chained keypresses produce composed characters.
 *
 * @param point  Release position in view coordinates.
 */
void
KeyboardLayoutView::MouseUp(BPoint point)
{
	Key* key = _KeyAt(fClickPoint);

	int32 buttons = 0;
	if (Looper() != NULL && Looper()->CurrentMessage() != NULL)
		Looper()->CurrentMessage()->FindInt32("buttons", &buttons);

	if (fKeymap == NULL || key == NULL) {
		fDragKey = NULL;
		return;
	}

	if ((buttons & B_SECONDARY_MOUSE_BUTTON) != 0
		|| ((buttons & B_PRIMARY_MOUSE_BUTTON) != 0
			&& (modifiers() & B_CONTROL_KEY) != 0)) {
		; // do nothing
	} else if ((buttons & B_TERTIARY_MOUSE_BUTTON) != 0
		&& (fButtons & B_TERTIARY_MOUSE_BUTTON) == 0) {
		// toggle the "deadness" of dead keys via middle mouse button
		_SetKeyState(key->code, false);
		_InvalidateKey(key);
		fButtons = buttons;
	} else {
		// primary mouse button
		fButtons = buttons;

		// modifier keys are sticky when used with the mouse
		if (fKeymap->IsModifierKey(key->code))
			return;

		_SetKeyState(key->code, false);

		if (_HandleDeadKey(key->code, fModifiers) && fDeadKey != 0)
			return;

		_InvalidateKey(key);

		if (fDragKey == NULL)
			_SendKeyDown(key);
	}

	fDragKey = NULL;
}


/**
 * @brief Tracks drop-target highlighting and starts a drag once the threshold is crossed.
 *
 * While a drag message is in flight, the hovered key is highlighted as
 * the drop target. Otherwise, if the primary mouse button has been
 * held and the cursor has moved enough pixels, this method bakes a
 * bitmap of the source key and starts a drag carrying its scancode
 * and current text mapping.
 *
 * @param point        Current cursor position in view coordinates.
 * @param transit      Standard BView transit code.
 * @param dragMessage  Drag message in flight, or NULL for a plain move.
 */
void
KeyboardLayoutView::MouseMoved(BPoint point, uint32 transit,
	const BMessage* dragMessage)
{
	if (fKeymap == NULL)
		return;

	// Ignore mouse-moved events if we are acting as a real input device.
	if (fDevice != NULL)
		return;

	// prevent dragging for tertiary mouse button
	if ((fButtons & B_TERTIARY_MOUSE_BUTTON) != 0)
		return;

	if (dragMessage != NULL) {
		if (fEditable) {
			_InvalidateKey(fDropTarget);
			fDropPoint = point;

			_EvaluateDropTarget(point);
		}

		return;
	}

	int32 buttons;
	if (Window()->CurrentMessage() == NULL
		|| Window()->CurrentMessage()->FindInt32("buttons", &buttons) != B_OK
		|| buttons == 0) {
		return;
	}

	if (fDragKey != NULL || !(fabs(point.x - fClickPoint.x) > 4
		|| fabs(point.y - fClickPoint.y) > 4)) {
		return;
	}

	// start dragging
	Key* key = _KeyAt(fClickPoint);
	if (key == NULL)
		return;

	BRect frame = _FrameFor(key);
	BPoint offset = fClickPoint - frame.LeftTop();
	frame.OffsetTo(B_ORIGIN);

	BRect rect = frame;
	rect.right--;
	rect.bottom--;
	BBitmap* bitmap = new BBitmap(rect, B_RGBA32, true);
	bitmap->Lock();

	BView* view = new BView(rect, "drag", B_FOLLOW_NONE, 0);
	bitmap->AddChild(view);

	view->SetHighColor(0, 0, 0, 0);
	view->FillRect(view->Bounds());
	view->SetDrawingMode(B_OP_ALPHA);
	view->SetHighColor(0, 0, 0, 128);
	// set the level of transparency by value
	view->SetBlendingMode(B_CONSTANT_ALPHA, B_ALPHA_COMPOSITE);
	_DrawKey(view, frame, key, frame, false);

	view->Sync();
	bitmap->Unlock();

	BMessage drag(B_MIME_DATA);
	drag.AddInt32("key", key->code);

	char* string;
	int32 numBytes;
	fKeymap->GetChars(key->code, fModifiers, fDeadKey, &string,
		&numBytes);
	if (string != NULL) {
		drag.AddData("text/plain", B_MIME_DATA, string, numBytes);
		delete[] string;
	}

	DragMessage(&drag, bitmap, B_OP_ALPHA, offset);
	fDragKey = key;
	fDragModifiers = fModifiers;

	fKeyState[key->code / 8] &= ~(1 << (7 - (key->code & 7)));
	_InvalidateKey(key);
}


/**
 * @brief Paints every key and LED indicator in the layout.
 *
 * Recomputes the on-screen geometry first if the view was resized
 * since the previous draw cycle.
 *
 * @param updateRect  Region requested for redraw.
 */
void
KeyboardLayoutView::Draw(BRect updateRect)
{
	if (fOldSize != BSize(Bounds().Width(), Bounds().Height())) {
		_LayoutKeyboard();
	}

	// Draw keys

	for (int32 i = 0; i < fLayout->CountKeys(); i++) {
		Key* key = fLayout->KeyAt(i);

		_DrawKey(this, updateRect, key, _FrameFor(key),
			_IsKeyPressed(key->code));
	}

	// Draw LED indicators

	for (int32 i = 0; i < fLayout->CountIndicators(); i++) {
		Indicator* indicator = fLayout->IndicatorAt(i);

		_DrawIndicator(this, updateRect, indicator, _FrameFor(indicator->frame),
			(fModifiers & indicator->modifier) != 0);
	}
}


/**
 * @brief Handles drops, modifier change notifications, and unmapped key events.
 *
 * Drops carrying text rebind the drop-target key's character mapping.
 * Drops carrying a "key" field swap mappings between the two keys
 * (with special handling for modifier keys). Modifier-changed events
 * update the live highlight; unmapped key events feed _KeyChanged().
 *
 * @param message  Incoming message.
 */
void
KeyboardLayoutView::MessageReceived(BMessage* message)
{
	if (message->WasDropped() && fEditable && fDropTarget != NULL
		&& fKeymap != NULL) {
		int32 keyCode;
		const char* data;
		ssize_t size;
		if (message->FindData("text/plain", B_MIME_DATA,
				(const void**)&data, &size) == B_OK) {
			// Automatically convert UTF-8 escaped strings (for example from
			// CharacterMap)
			int32 dataSize = 0;
			uint8 buffer[16];
			if (size > 3 && data[0] == '\\' && data[1] == 'x') {
				char tempBuffer[16];
				if (size > 15)
					size = 15;
				memcpy(tempBuffer, data, size);
				tempBuffer[size] = '\0';
				data = tempBuffer;

				while (size > 3 && data[0] == '\\' && data[1] == 'x') {
					buffer[dataSize++] = strtoul(&data[2], NULL, 16);
					if ((buffer[dataSize - 1] & 0x80) == 0)
						break;

					size -= 4;
					data += 4;
				}
				data = (const char*)buffer;
			} else if ((data[0] & 0xc0) != 0x80 && (data[0] & 0x80) != 0) {
				// only accept the first character UTF-8 character
				while (dataSize < size && (data[dataSize] & 0x80) != 0) {
					dataSize++;
				}
			} else if ((data[0] & 0x80) == 0) {
				// an ASCII character
				dataSize = 1;
			} else {
				// no valid character
				beep();
				return;
			}

			int32 buttons;
			if (!message->IsSourceRemote()
				&& message->FindInt32("buttons", &buttons) == B_OK
				&& (buttons & B_PRIMARY_MOUSE_BUTTON) != 0
				&& message->FindInt32("key", &keyCode) == B_OK) {
				// switch keys if the dropped object came from us
				Key* key = _KeyForCode(keyCode);
				if (key == NULL
					|| (key == fDropTarget && fDragModifiers == fModifiers)) {
					return;
				}

				char* string;
				int32 numBytes;
				fKeymap->GetChars(fDropTarget->code, fModifiers, fDeadKey,
					&string, &numBytes);
				if (string != NULL) {
					// switch keys
					fKeymap->SetKey(fDropTarget->code, fModifiers, fDeadKey,
						(const char*)data, dataSize);
					fKeymap->SetKey(key->code, fDragModifiers, fDeadKey,
						string, numBytes);
					delete[] string;
				} else if (fKeymap->IsModifierKey(fDropTarget->code)) {
					// switch key with modifier
					fKeymap->SetModifier(key->code,
						fKeymap->Modifier(fDropTarget->code));
					fKeymap->SetKey(fDropTarget->code, fModifiers, fDeadKey,
						(const char*)data, dataSize);
				}
			} else {
				// Send the old key to the target, so it's not lost entirely
				_SendKeyDown(fDropTarget);

				fKeymap->SetKey(fDropTarget->code, fModifiers, fDeadKey,
					(const char*)data, dataSize);
			}
		} else if (!message->IsSourceRemote()
			&& message->FindInt32("key", &keyCode) == B_OK) {
			// Switch an unmapped key

			Key* key = _KeyForCode(keyCode);
			if (key != NULL && key == fDropTarget)
				return;

			uint32 modifier = fKeymap->Modifier(keyCode);

			char* string;
			int32 numBytes;
			fKeymap->GetChars(fDropTarget->code, fModifiers, fDeadKey,
				&string, &numBytes);
			if (string != NULL) {
				// switch key with modifier
				fKeymap->SetModifier(fDropTarget->code, modifier);
				fKeymap->SetKey(keyCode, fDragModifiers, fDeadKey,
					string, numBytes);
				delete[] string;
			} else {
				// switch modifier keys
				fKeymap->SetModifier(keyCode,
					fKeymap->Modifier(fDropTarget->code));
				fKeymap->SetModifier(fDropTarget->code, modifier);
			}

			_InvalidateKey(fDragKey);
		}

		_InvalidateKey(fDropTarget);
		fDropTarget = NULL;
		fDropPoint.x = -1;
		return;
	}

	switch (message->what) {
		case B_UNMAPPED_KEY_DOWN:
		case B_UNMAPPED_KEY_UP:
			_KeyChanged(message);
			break;

		case B_MODIFIERS_CHANGED:
		{
			int32 newModifiers;
			if (message->FindInt32("modifiers", &newModifiers) == B_OK
				&& fModifiers != newModifiers) {
				fModifiers = newModifiers;
				_EvaluateDropTarget(fDropPoint);
				if (Window()->IsActive())
					Invalidate();
			}
			break;
		}

		default:
			BView::MessageReceived(message);
			break;
	}
}


/**
 * @brief Recomputes the scaling factor and centring offset for the layout.
 *
 * Picks the smaller of the two axis ratios so the layout fits in the
 * view without distortion, centres it horizontally and vertically,
 * and chooses an inter-key gap based on the layout's default key size.
 */
void
KeyboardLayoutView::_LayoutKeyboard()
{
	float factorX = Bounds().Width() / fLayout->Bounds().Width();
	float factorY = Bounds().Height() / fLayout->Bounds().Height();

	fFactor = min_c(factorX, factorY);
	fOffset = BPoint(floorf((Bounds().Width() - fLayout->Bounds().Width()
			* fFactor) / 2),
		floorf((Bounds().Height() - fLayout->Bounds().Height() * fFactor) / 2));

	if (fLayout->DefaultKeySize().width < 11)
		fGap = 1;
	else
		fGap = 2;

	fOldSize.width = Bounds().Width();
	fOldSize.height = Bounds().Height();
}


/**
 * @brief Draws the standard rectangular key button frame and background.
 *
 * @param view        Target view (this view, or a drag bitmap helper).
 * @param rect        Key frame; ControlLook may shrink it for borders.
 * @param updateRect  Clip region passed through to BControlLook.
 * @param base        Base colour for the key.
 * @param background  Background colour behind the key frame.
 * @param pressed     Whether the key should appear pressed.
 */
void
KeyboardLayoutView::_DrawKeyButton(BView* view, BRect& rect, BRect updateRect,
	rgb_color base, rgb_color background, bool pressed)
{
	uint32 flags = pressed ? BControlLook::B_ACTIVATED : 0;

	be_control_look->DrawButtonFrame(view, rect, updateRect, 4.0f, base,
		background, flags);
	be_control_look->DrawButtonBackground(view, rect, updateRect, 4.0f,
		base, flags);
}


/**
 * @brief Renders one key, including label, dead-key tinting, and highlights.
 *
 * Resolves the label from the bound keymap (or uses the bare scancode
 * if there is no keymap), tints the background to indicate dead-key
 * state, and dispatches to the rectangle or enter-shaped drawing
 * paths depending on the key's geometry.
 *
 * @param view        Target view.
 * @param updateRect  Clip region.
 * @param key         Key being drawn.
 * @param rect        On-screen frame for the key.
 * @param pressed     Whether the key is currently held down.
 */
void
KeyboardLayoutView::_DrawKey(BView* view, BRect updateRect, const Key* key,
	BRect rect, bool pressed)
{
	rgb_color base;
	if (ui_color(B_CONTROL_BACKGROUND_COLOR).IsLight()) {
		base = key->dark ? tint_color(ui_color(B_CONTROL_BACKGROUND_COLOR), B_DARKEN_1_TINT)
			: ui_color(B_CONTROL_BACKGROUND_COLOR);
	} else {
		base = key->dark ? tint_color(ui_color(B_CONTROL_BACKGROUND_COLOR), 0.8)
			: ui_color(B_CONTROL_BACKGROUND_COLOR);
	}
	rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color keyLabelColor = ui_color(B_CONTROL_TEXT_COLOR);
	key_kind keyKind = kNormalKey;
	int32 deadKey = 0;
	bool secondDeadKey = false;
	bool isDeadKeyEnabled = true;

	char text[32];
	if (fKeymap != NULL) {
		_GetKeyLabel(key, text, sizeof(text), keyKind);
		deadKey = fKeymap->DeadKey(key->code, fModifiers, &isDeadKeyEnabled);
		secondDeadKey = fKeymap->IsDeadSecondKey(key->code, fModifiers,
			fDeadKey);
	} else {
		// Show the key code if there is no keymap
		snprintf(text, sizeof(text), "%02" B_PRIx32, key->code);
	}

	_SetFontSize(view, keyKind);

	uint32 flags = pressed ? BControlLook::B_ACTIVATED : 0;

	if (secondDeadKey)
		base = mix_color(ui_color(B_CONTROL_BACKGROUND_COLOR), kIdealSecondDeadKeyColor, 100);
	else if (deadKey > 0 && isDeadKeyEnabled)
		base = mix_color(ui_color(B_CONTROL_BACKGROUND_COLOR), kIdealDeadKeyColor, 100);

	if (key->shape == kRectangleKeyShape) {
		_DrawKeyButton(view, rect, updateRect, base, background, pressed);

		rect.InsetBy(1, 1);

		_GetAbbreviatedKeyLabelIfNeeded(view, rect, key, text, sizeof(text));
		be_control_look->DrawLabel(view, text, rect, updateRect,
			base, flags, BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE),
			&keyLabelColor);
	} else if (key->shape == kEnterKeyShape) {
		BRect topLeft = rect;
		BRect topRight = rect;
		BRect bottomLeft = rect;
		BRect bottomRight = rect;

		// TODO: for some reason, this does not always equal the bottom of
		// the other keys...
		bottomLeft.top = floorf(rect.top
			+ fLayout->DefaultKeySize().height * fFactor - fGap - 1);
		bottomLeft.right = floorf(rect.left
			+ (key->frame.Width() - key->second_row) * fFactor - fGap - 2);

		topLeft.bottom = bottomLeft.top;
		topLeft.right = bottomLeft.right + 1;
			// add one to make the borders meet

		topRight.bottom = topLeft.bottom;
		topRight.left = topLeft.right;

		bottomRight.top = bottomLeft.top;
		bottomRight.left = bottomLeft.right;

		// draw top left corner
		be_control_look->DrawButtonFrame(view, topLeft, updateRect,
			4.0f, 0.0f, 4.0f, 0.0f, base, background, flags,
			BControlLook::B_LEFT_BORDER | BControlLook::B_TOP_BORDER
				| BControlLook::B_BOTTOM_BORDER);
		be_control_look->DrawButtonBackground(view, topLeft, updateRect,
			4.0f, 0.0f, 4.0f, 0.0f, base, flags,
			BControlLook::B_LEFT_BORDER | BControlLook::B_TOP_BORDER
				| BControlLook::B_BOTTOM_BORDER);

		// draw top right corner
		be_control_look->DrawButtonFrame(view, topRight, updateRect,
			0.0f, 4.0f, 0.0f, 0.0f, base, background, flags,
			BControlLook::B_TOP_BORDER | BControlLook::B_RIGHT_BORDER);
		be_control_look->DrawButtonBackground(view, topRight, updateRect,
			0.0f, 4.0f, 0.0f, 0.0f, base, flags,
			BControlLook::B_TOP_BORDER | BControlLook::B_RIGHT_BORDER);

		// draw bottom right corner
		be_control_look->DrawButtonFrame(view, bottomRight, updateRect,
			0.0f, 0.0f, 4.0f, 4.0f, base, background, flags,
			BControlLook::B_LEFT_BORDER | BControlLook::B_RIGHT_BORDER
				| BControlLook::B_BOTTOM_BORDER);
		be_control_look->DrawButtonBackground(view, bottomRight, updateRect,
			0.0f, 0.0f, 4.0f, 4.0f, base, flags,
			BControlLook::B_LEFT_BORDER | BControlLook::B_RIGHT_BORDER
				| BControlLook::B_BOTTOM_BORDER);

		// clip out the bottom left corner
		bottomLeft.right += 1;
		bottomLeft.top -= 2;
		BRegion region(rect);
		region.Exclude(bottomLeft);
		view->ConstrainClippingRegion(&region);

		// draw the button background
		BRect bgRect = rect.InsetByCopy(2, 2);
		be_control_look->DrawButtonBackground(view, bgRect, updateRect,
			4.0f, 4.0f, 0.0f, 4.0f, base, flags);

		rect.left = bottomLeft.right;
		_GetAbbreviatedKeyLabelIfNeeded(view, rect, key, text, sizeof(text));

		// draw the button label
		be_control_look->DrawLabel(view, text, rect, updateRect,
			base, flags, BAlignment(B_ALIGN_CENTER, B_ALIGN_MIDDLE),
			&keyLabelColor);

		// reset the clipping region
		view->ConstrainClippingRegion(NULL);
	}
}


/**
 * @brief Draws a Caps/Num/Scroll Lock LED indicator with optional label.
 *
 * @param view        Target view.
 * @param updateRect  Clip region.
 * @param indicator   Indicator describing which modifier this LED tracks.
 * @param rect        On-screen frame for the indicator.
 * @param lit         Whether the LED should be drawn as lit.
 */
void
KeyboardLayoutView::_DrawIndicator(BView* view, BRect updateRect,
	const Indicator* indicator, BRect rect, bool lit)
{
	float rectTop = rect.top;
	rect.top += 2 * rect.Height() / 3;

	const char* label = NULL;
	if (indicator->modifier == B_CAPS_LOCK)
		label = "caps";
	else if (indicator->modifier == B_NUM_LOCK)
		label = "num";
	else if (indicator->modifier == B_SCROLL_LOCK)
		label = "scroll";
	if (label != NULL) {
		_SetFontSize(view, kIndicator);

		font_height fontHeight;
		GetFontHeight(&fontHeight);
		if (ceilf(rect.top - fontHeight.ascent + fontHeight.descent - 2)
				>= rectTop) {
			view->SetHighUIColor(B_PANEL_TEXT_COLOR);
			view->SetLowColor(ViewColor());

			BString text(label);
			view->TruncateString(&text, B_TRUNCATE_END, rect.Width());
			view->DrawString(text.String(),
				BPoint(ceilf(rect.left + (rect.Width()
						- StringWidth(text.String())) / 2),
					ceilf(rect.top - fontHeight.descent - 2)));
		}
	}

	rect.left += rect.Width() / 4;
	rect.right -= rect.Width() / 3;

	rgb_color background = ui_color(B_PANEL_BACKGROUND_COLOR);
	rgb_color base = lit ? kLitIndicatorColor : kDarkColor;

	be_control_look->DrawButtonFrame(view, rect, updateRect, base,
		background, BControlLook::B_DISABLED);
	be_control_look->DrawButtonBackground(view, rect, updateRect,
		base, BControlLook::B_DISABLED);
}


/**
 * @brief Returns a localised label for modifier and lock keys.
 *
 * @param map          Keymap defining which scancodes act as modifiers.
 * @param code         Scancode to label.
 * @param abbreviated  When true, return short forms (e.g. "CMD" vs "COMMAND").
 * @return             Localised label, or NULL if @a code is not a modifier.
 */
const char*
KeyboardLayoutView::_SpecialKeyLabel(const key_map& map, uint32 code,
	bool abbreviated)
{
	if (code == map.caps_key) {
		return abbreviated
			? B_TRANSLATE_COMMENT("CAPS", "Very short for 'caps lock'")
			: B_TRANSLATE("CAPS LOCK");
	}
	if (code == map.scroll_key)
		return B_TRANSLATE("SCROLL");
	if (code == map.num_key) {
		return abbreviated
			? B_TRANSLATE_COMMENT("NUM", "Very short for 'num lock'")
			: B_TRANSLATE("NUM LOCK");
	}
	if (code == map.left_shift_key || code == map.right_shift_key)
		return B_TRANSLATE("SHIFT");
	if (code == map.left_command_key || code == map.right_command_key) {
		return abbreviated
			? B_TRANSLATE_COMMENT("CMD", "Very short for 'command'")
			: B_TRANSLATE("COMMAND");
	}
	if (code == map.left_control_key || code == map.right_control_key) {
		return abbreviated
			? B_TRANSLATE_COMMENT("CTRL", "Very short for 'control'")
			: B_TRANSLATE("CONTROL");
	}
	if (code == map.left_option_key || code == map.right_option_key) {
		return abbreviated
			? B_TRANSLATE_COMMENT("OPT", "Very short for 'option'")
			: B_TRANSLATE("OPTION");
	}
	if (code == map.menu_key)
		return B_TRANSLATE("MENU");
	if (code == B_PRINT_KEY)
		return B_TRANSLATE("PRINT");
	if (code == B_PAUSE_KEY)
		return B_TRANSLATE("PAUSE");

	return NULL;
}


/**
 * @brief Returns a UTF-8 glyph for tab, enter, backspace, and arrow keys.
 *
 * @param bytes     Mapped character bytes from the keymap.
 * @param numBytes  Length of @a bytes.
 * @return          Pointer to a static UTF-8 string, or NULL if there is no symbol.
 */
const char*
KeyboardLayoutView::_SpecialMappedKeySymbol(const char* bytes, size_t numBytes)
{
	if (numBytes != 1)
		return NULL;

	if (bytes[0] == B_TAB)
		return "\xe2\x86\xb9";
	if (bytes[0] == B_ENTER)
		return "\xe2\x8f\x8e";
	if (bytes[0] == B_BACKSPACE)
		return "\xe2\x8c\xab";

	if (bytes[0] == B_UP_ARROW)
		return "\xe2\x86\x91";
	if (bytes[0] == B_LEFT_ARROW)
		return "\xe2\x86\x90";
	if (bytes[0] == B_DOWN_ARROW)
		return "\xe2\x86\x93";
	if (bytes[0] == B_RIGHT_ARROW)
		return "\xe2\x86\x92";

	return NULL;
}


/**
 * @brief Returns a localised label for navigation keys (Esc, Insert, Home, etc.).
 *
 * @param bytes        Mapped character bytes from the keymap.
 * @param numBytes     Length of @a bytes.
 * @param abbreviated  When true, return short forms (e.g. "PG up arrow").
 * @return             Localised label, or NULL if @a bytes is not a navigation key.
 */
const char*
KeyboardLayoutView::_SpecialMappedKeyLabel(const char* bytes, size_t numBytes,
	bool abbreviated)
{
	if (numBytes != 1)
		return NULL;
	if (bytes[0] == B_ESCAPE)
		return B_TRANSLATE("ESC");
	if (bytes[0] == B_INSERT)
		return B_TRANSLATE("INS");
	if (bytes[0] == B_DELETE)
		return B_TRANSLATE("DEL");
	if (bytes[0] == B_HOME)
		return B_TRANSLATE("HOME");
	if (bytes[0] == B_END)
		return B_TRANSLATE("END");
	if (bytes[0] == B_PAGE_UP) {
		return abbreviated
			? B_TRANSLATE_COMMENT("PG \xe2\x86\x91",
				"Very short for 'page up'")
			: B_TRANSLATE("PAGE \xe2\x86\x91");
	}
	if (bytes[0] == B_PAGE_DOWN) {
		return abbreviated
			? B_TRANSLATE_COMMENT("PG \xe2\x86\x93",
				"Very short for 'page down'")
			: B_TRANSLATE("PAGE \xe2\x86\x93");
	}

	return NULL;
}


/**
 * @brief Writes "F1" .. "F12" into @a text if @a code is a function key.
 *
 * @param code      Scancode to test.
 * @param text      Output buffer.
 * @param textSize  Size of @a text in bytes.
 * @return          true if a label was written, false otherwise.
 */
bool
KeyboardLayoutView::_FunctionKeyLabel(uint32 code, char* text, size_t textSize)
{
	if (code >= B_F1_KEY && code <= B_F12_KEY) {
		snprintf(text, textSize, "F%" B_PRId32, code + 1 - B_F1_KEY);
		return true;
	}

	return false;
}


/**
 * @brief Replaces @a text with a shorter label if it does not fit in @a rect.
 *
 * Falls back to abbreviated forms of modifier and navigation labels
 * when the full label would overflow the available space.
 *
 * @param view      Target view; used to measure string widths.
 * @param rect      Frame in which the label must fit.
 * @param key       Key whose label is being measured.
 * @param text      In/out: full label that may be replaced with a short form.
 * @param textSize  Size of @a text in bytes.
 */
void
KeyboardLayoutView::_GetAbbreviatedKeyLabelIfNeeded(BView* view, BRect rect,
	const Key* key, char* text, size_t textSize)
{
	if (floorf(rect.Width()) > ceilf(view->StringWidth(text)))
		return;

	// Check if we have a shorter version of this key

	const key_map& map = fKeymap->Map();

	const char* special = _SpecialKeyLabel(map, key->code, true);
	if (special != NULL) {
		strlcpy(text, special, textSize);
		return;
	}

	char* bytes = NULL;
	int32 numBytes;
	fKeymap->GetChars(key->code, fModifiers, fDeadKey, &bytes, &numBytes);
	if (bytes != NULL) {
		special = _SpecialMappedKeyLabel(bytes, numBytes, true);
		if (special != NULL)
			strlcpy(text, special, textSize);

		delete[] bytes;
	}
}


/**
 * @brief Computes the displayed label and font kind for a key.
 *
 * Tries, in order: modifier names, function-key names, mapped
 * navigation labels, mapped special symbols, and finally the literal
 * mapped character if the base font has glyphs for it.
 *
 * @param key       Key being labelled.
 * @param text      Output buffer for the label.
 * @param textSize  Size of @a text in bytes.
 * @param keyKind   Output classification driving the chosen font.
 */
void
KeyboardLayoutView::_GetKeyLabel(const Key* key, char* text, size_t textSize,
	key_kind& keyKind)
{
	const key_map& map = fKeymap->Map();
	keyKind = kNormalKey;
	text[0] = '\0';

	const char* special = _SpecialKeyLabel(map, key->code);
	if (special != NULL) {
		strlcpy(text, special, textSize);
		keyKind = kSpecialKey;
		return;
	}

	if (_FunctionKeyLabel(key->code, text, textSize)) {
		keyKind = kSpecialKey;
		return;
	}

	char* bytes = NULL;
	int32 numBytes;
	fKeymap->GetChars(key->code, fModifiers, fDeadKey, &bytes, &numBytes);
	if (bytes != NULL) {
		special = _SpecialMappedKeyLabel(bytes, numBytes);
		if (special != NULL) {
			strlcpy(text, special, textSize);
			keyKind = kSpecialKey;
		} else {
			special = _SpecialMappedKeySymbol(bytes, numBytes);
			if (special != NULL) {
				strlcpy(text, special, textSize);
				keyKind = kSymbolKey;
			} else {
				bool hasGlyphs;
				fBaseFont.GetHasGlyphs(bytes, 1, &hasGlyphs);
				if (hasGlyphs)
					strlcpy(text, bytes, textSize);
			}
		}

		delete[] bytes;
	}
}


/**
 * @brief Returns true if @a code should currently be rendered as pressed.
 *
 * A key is considered pressed if its bit in the live state map is set
 * or if it is the active drag drop target (so users see the swap
 * preview).
 *
 * @param code  Hardware scancode.
 */
bool
KeyboardLayoutView::_IsKeyPressed(uint32 code)
{
	if (fDropTarget != NULL && fDropTarget->code == code)
		return true;

	return _KeyState(code);
}


/**
 * @brief Returns the live pressed/not-pressed bit for @a code.
 *
 * @param code  Hardware scancode (must be < 128).
 * @return      true if the corresponding bit in fKeyState is set.
 */
bool
KeyboardLayoutView::_KeyState(uint32 code) const
{
	if (code >= 16 * 8)
		return false;

	return (fKeyState[code / 8] & (1 << (7 - (code & 7)))) != 0;
}


/**
 * @brief Sets or clears the live pressed bit for @a code.
 *
 * @param code     Hardware scancode (silently ignored if >= 128).
 * @param pressed  Whether to mark the key as pressed.
 */
void
KeyboardLayoutView::_SetKeyState(uint32 code, bool pressed)
{
	if (code >= 16 * 8)
		return;

	if (pressed)
		fKeyState[code / 8] |= (1 << (7 - (code & 7)));
	else
		fKeyState[code / 8] &= ~(1 << (7 - (code & 7)));
}


/**
 * @brief Linearly searches the layout for the Key with the given scancode.
 *
 * @param code  Hardware scancode to find.
 * @return      Pointer to the matching key, or NULL if absent.
 * @todo        Replace the linear scan with a lookup array.
 */
Key*
KeyboardLayoutView::_KeyForCode(uint32 code)
{
	// TODO: have a lookup array

	for (int32 i = 0; i < fLayout->CountKeys(); i++) {
		Key* key = fLayout->KeyAt(i);
		if (key->code == code)
			return key;
	}

	return NULL;
}


/**
 * @brief Invalidates the on-screen frame of the key with the given scancode.
 *
 * @param code  Hardware scancode.
 */
void
KeyboardLayoutView::_InvalidateKey(uint32 code)
{
	_InvalidateKey(_KeyForCode(code));
}


/**
 * @brief Invalidates the on-screen frame of @a key.
 *
 * @param key  Key whose frame should be redrawn (no-op if NULL).
 */
void
KeyboardLayoutView::_InvalidateKey(const Key* key)
{
	if (key != NULL)
		Invalidate(_FrameFor(key));
}


/**
 * @brief Updates the fDeadKey member and invalidates the view if needed.
 *
 * @param key        Scancode whose dead-key status to evaluate.
 * @param modifiers  Active modifier mask at the time of the event.
 * @return           true if the view has been invalidated.
 */
bool
KeyboardLayoutView::_HandleDeadKey(uint32 key, int32 modifiers)
{
	if (fKeymap == NULL || fKeymap->IsModifierKey(key))
		return false;

	bool isEnabled = false;
	int32 deadKey = fKeymap->DeadKey(key, modifiers, &isEnabled);
	if (fDeadKey != deadKey && isEnabled) {
		fDeadKey = deadKey;
		Invalidate();
		return true;
	} else if (fDeadKey != 0) {
		fDeadKey = 0;
		Invalidate();
		return true;
	}

	return false;
}


/**
 * @brief Synchronises live key-state from a BMessage and invalidates changed keys.
 *
 * @param message  B_KEY_DOWN/UP or B_UNMAPPED_KEY_DOWN/UP message carrying
 *                 a fresh "states" byte array and the originating "key".
 */
void
KeyboardLayoutView::_KeyChanged(const BMessage* message)
{
	const uint8* state;
	ssize_t size;
	int32 key;
	if (message->FindInt32("key", &key) != B_OK
		|| message->FindData("states", B_UINT8_TYPE,
			(const void**)&state, &size) != B_OK) {
		return;
	}

	// Update key state, and invalidate change keys

	bool checkSingle = true;

	if (message->what == B_KEY_UP || message->what == B_UNMAPPED_KEY_UP) {
		if (_HandleDeadKey(key, fModifiers))
			checkSingle = false;

		if (_KeyForCode(key) == NULL)
			printf("no key for code %" B_PRId32 "\n", key);
	}

	for (int32 i = 0; i < 16; i++) {
		if (fKeyState[i] != state[i]) {
			uint8 diff = fKeyState[i] ^ state[i];
			fKeyState[i] = state[i];

			if (!checkSingle || !Window()->IsActive())
				continue;

			for (int32 j = 7; diff != 0; j--, diff >>= 1) {
				if (diff & 1) {
					_InvalidateKey(i * 8 + j);
				}
			}
		}
	}
}


/**
 * @brief Hit-tests @a point against the on-screen layout.
 *
 * Iterates from the topmost key downward, transforming the click
 * into layout coordinates first to keep the test fast, then verifies
 * the screen frame contains the click before returning the key.
 *
 * @param point  Point in view coordinates.
 * @return       Pointer to the hit Key, or NULL if no key was clicked.
 */
Key*
KeyboardLayoutView::_KeyAt(BPoint point)
{
	// Find key candidate

	BPoint keyPoint = point;
	keyPoint -= fOffset;
	keyPoint.x /= fFactor;
	keyPoint.y /= fFactor;

	for (int32 i = fLayout->CountKeys() - 1; i >= 0; i--) {
		Key* key = fLayout->KeyAt(i);
		if (key->frame.Contains(keyPoint)) {
			BRect frame = _FrameFor(key);
			if (frame.Contains(point))
				return key;

			return NULL;
		}
	}

	return NULL;
}


/**
 * @brief Converts a layout-space rectangle to a view-space rectangle.
 *
 * Applies the global scale factor and centring offset, then shrinks
 * the result by the inter-key gap to leave space between adjacent keys.
 *
 * @param keyFrame  Rectangle in layout coordinates.
 * @return          Corresponding rectangle in view coordinates.
 */
BRect
KeyboardLayoutView::_FrameFor(BRect keyFrame)
{
	BRect rect;
	rect.left	= ceilf(keyFrame.left * fFactor);
	rect.top	= ceilf(keyFrame.top * fFactor);
	rect.right	= floorf((keyFrame.Width()) * fFactor + rect.left - fGap - 1);
	rect.bottom	= floorf((keyFrame.Height()) * fFactor + rect.top - fGap - 1);
	rect.OffsetBy(fOffset);

	return rect;
}


/**
 * @brief Convenience overload returning the view-space frame for a key.
 *
 * @param key  Key whose frame to translate.
 */
BRect
KeyboardLayoutView::_FrameFor(const Key* key)
{
	return _FrameFor(key->frame);
}


/**
 * @brief Picks an appropriate font and size for drawing a key label.
 *
 * Shrinks the base font when needed so the label height stays at
 * roughly half the on-screen key height. Special and indicator keys
 * use the fixed-width font with adjusted multipliers.
 *
 * @param view     Target view to receive the font.
 * @param keyKind  Classification of the label (normal, special, symbol, indicator).
 */
void
KeyboardLayoutView::_SetFontSize(BView* view, key_kind keyKind)
{
	BSize size = fLayout->DefaultKeySize();
	float fontSize = fBaseFontSize;
	if (fBaseFontHeight >= size.height * fFactor * 0.5) {
		fontSize *= (size.height * fFactor * 0.5) / fBaseFontHeight;
		if (fontSize < 8)
			fontSize = 8;
	}

	switch (keyKind) {
		case kNormalKey:
			fBaseFont.SetSize(fontSize);
			view->SetFont(&fBaseFont);
			break;
		case kSpecialKey:
			fSpecialFont.SetSize(fontSize * 0.7);
			view->SetFont(&fSpecialFont);
			break;
		case kSymbolKey:
			fSpecialFont.SetSize(fontSize * 1.6);
			view->SetFont(&fSpecialFont);
			break;

		case kIndicator:
		{
			BFont font;
			font.SetSize(fontSize * 0.8);
			view->SetFont(&font);
			break;
		}
	}
}


/**
 * @brief Recomputes which key is the current drag-and-drop target.
 *
 * Suppresses the highlight when the cursor is back over the source
 * key with the same modifier mask (which would be a no-op drop).
 *
 * @param point  Cursor position in view coordinates.
 */
void
KeyboardLayoutView::_EvaluateDropTarget(BPoint point)
{
	fDropTarget = _KeyAt(point);
	if (fDropTarget != NULL) {
		if (fDropTarget == fDragKey && fModifiers == fDragModifiers)
			fDropTarget = NULL;
		else
			_InvalidateKey(fDropTarget);
	}
}


/**
 * @brief Synthesises a B_KEY_DOWN message for @a key and dispatches it.
 *
 * Populates the standard fields (when, states, key, modifiers,
 * be:key_repeat, bytes, raw_char, byte) and either posts the message
 * to the configured target (editor mode) or enqueues it on the bound
 * input device (virtual-keyboard mode, if compiled in).
 *
 * @param key  Key being virtually pressed.
 */
void
KeyboardLayoutView::_SendKeyDown(const Key* key)
{
	BMessage message(B_KEY_DOWN);
	message.AddInt64("when", system_time());
	message.AddData("states", B_UINT8_TYPE, &fKeyState,
		sizeof(fKeyState));
	message.AddInt32("key", key->code);
	message.AddInt32("modifiers", fModifiers);
	message.AddInt32("be:key_repeat", 1);

	if (fDevice == NULL)
		message.AddPointer("keymap", fKeymap);

	char* string;
	int32 numBytes;
	fKeymap->GetChars(key->code, fModifiers, fDeadKey, &string,
		&numBytes);
	if (string != NULL) {
		message.AddString("bytes", string);
		delete[] string;
	}

	fKeymap->GetChars(key->code, 0, 0, &string, &numBytes);
	if (string != NULL) {
		message.AddInt32("raw_char", string[0]);
		message.AddInt8("byte", string[0]);
		delete[] string;
	}

	if (fDevice == NULL) {
		fTarget.SendMessage(&message);
	} else {
#if defined(VIRTUAL_KEYBOARD_DEVICE)
		BMessage* deviceMessage = new BMessage(message);
		if (fDevice->EnqueueMessage(deviceMessage) != B_OK)
			delete deviceMessage;
#endif
	}
}


/**
 * @brief Builds a popup-menu item that swaps a modifier role between two keys.
 *
 * The returned BMenuItem carries a kMsgUpdateModifierKeys message
 * encoding the swap so that KeymapWindow can apply it.
 *
 * @param modifier         Target modifier role (e.g. B_LEFT_SHIFT_KEY).
 * @param displayModifier  Modifier whose name is shown on the item label.
 * @param oldCode          Scancode currently bound to the role.
 * @param newCode          Scancode the user clicked on.
 * @return                 New BMenuItem; ownership passes to the caller.
 */
BMenuItem*
KeyboardLayoutView::_CreateSwapModifiersMenuItem(uint32 modifier,
	uint32 displayModifier, uint32 oldCode, uint32 newCode)
{
	int32 mask = B_SHIFT_KEY | B_COMMAND_KEY | B_CONTROL_KEY | B_OPTION_KEY;
	const char* oldName = _NameForModifier(oldCode == 0x00 ? modifier
		: fKeymap->Modifier(oldCode) & ~mask, false);
	const char* newName = _NameForModifier(newCode == 0x00 ? modifier
		: fKeymap->Modifier(newCode) & ~mask, false);

	BMessage* message = new BMessage(kMsgUpdateModifierKeys);
	if (newName != NULL)
		message->AddUInt32(newName, oldCode);

	if (oldName != NULL)
		message->AddUInt32(oldName, newCode);

	if (oldCode == newCode)
		message->AddBool("unset", true);

	return new BMenuItem(_NameForModifier(displayModifier, true), message);
}


/**
 * @brief Returns the canonical or pretty-printed name for a modifier mask.
 *
 * @param modifier  Single-bit modifier mask (e.g. B_LEFT_SHIFT_KEY).
 * @param pretty    When true, return a localised display name; otherwise
 *                  return the wire-protocol field name (e.g. "left_shift_key").
 * @return          Static C string, or NULL if @a modifier is unrecognised.
 */
const char*
KeyboardLayoutView::_NameForModifier(uint32 modifier, bool pretty)
{
	if (modifier == B_CAPS_LOCK)
		return pretty ? B_TRANSLATE("Caps Lock") : "caps_key";
	else if (modifier == B_NUM_LOCK)
		return pretty ? B_TRANSLATE("Num Lock") : "num_key";
	else if (modifier == B_SCROLL_LOCK)
		return pretty ? B_TRANSLATE("Scroll Lock") : "scroll_key";
	else if (modifier == B_SHIFT_KEY) {
		return pretty ? B_TRANSLATE_COMMENT("Shift", "Shift key")
			: "shift_key";
	} else if (modifier == B_LEFT_SHIFT_KEY)
		return pretty ? B_TRANSLATE("Left Shift") : "left_shift_key";
	else if (modifier == B_RIGHT_SHIFT_KEY)
		return pretty ? B_TRANSLATE("Right Shift") : "right_shift_key";
	else if (modifier == B_COMMAND_KEY) {
		return pretty ? B_TRANSLATE_COMMENT("Command", "Command key")
			: "command_key";
	} else if (modifier == B_LEFT_COMMAND_KEY)
		return pretty ? B_TRANSLATE("Left Command") : "left_command_key";
	else if (modifier == B_RIGHT_COMMAND_KEY)
		return pretty ? B_TRANSLATE("Right Command") : "right_command_key";
	else if (modifier == B_CONTROL_KEY) {
		return pretty ? B_TRANSLATE_COMMENT("Control", "Control key")
			: "control_key";
	} else if (modifier == B_LEFT_CONTROL_KEY)
		return pretty ? B_TRANSLATE("Left Control") : "left_control_key";
	else if (modifier == B_RIGHT_CONTROL_KEY)
		return pretty ? B_TRANSLATE("Right Control") : "right_control_key";
	else if (modifier == B_OPTION_KEY) {
		return pretty ? B_TRANSLATE_COMMENT("Option", "Option key")
			: "option_key";
	} else if (modifier == B_LEFT_OPTION_KEY)
		return pretty ? B_TRANSLATE("Left Option") : "left_option_key";
	else if (modifier == B_RIGHT_OPTION_KEY)
		return pretty ? B_TRANSLATE("Right Option") : "right_option_key";
	else if (modifier == B_MENU_KEY)
		return pretty ? B_TRANSLATE_COMMENT("Menu", "Menu key") : "menu_key";

	return NULL;
}
