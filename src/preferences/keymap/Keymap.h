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
 * MIT License. Copyright 2004-2011, Haiku Inc.
 * Original authors: Jerome Duval, Axel Doerfler (axeld@pinc-software.de).
 */

/** @file Keymap.h
    @brief Editable Keymap subclass owned by the Keymap preferences app. */

#ifndef KEYMAP_H
#define KEYMAP_H


#include <Keymap.h>

#include <Entry.h>
#include <Messenger.h>
#include <String.h>


/** @brief Index of a dead key, matching the order of fields in @c key_map. */
enum dead_key_index {
	kDeadKeyAcute = 1,
	kDeadKeyGrave,
	kDeadKeyCircumflex,
	kDeadKeyDiaeresis,
	kDeadKeyTilde
};


/**
 * @brief Editable extension of BKeymap used by the Keymap preferences app.
 *
 * Adds load / save / use operations on the underlying binary keymap blob,
 * mutators for individual key codes, dead-key trigger management, and a
 * change-notification message that fires whenever the keymap is mutated.
 */
class Keymap : public BKeymap {
public:
								Keymap();
								~Keymap();

			void				SetTarget(BMessenger target,
									BMessage* modificationMessage);

			status_t			Load(const entry_ref& ref);
			status_t			Save(const entry_ref& ref);

			void				DumpKeymap();

			status_t			SetModifier(uint32 keyCode, uint32 modifier);

			void				SetDeadKeyEnabled(uint32 keyCode,
									uint32 modifiers, bool enabled);
			void				GetDeadKeyTrigger(dead_key_index deadKeyIndex,
									BString& outTrigger);
			void				SetDeadKeyTrigger(dead_key_index deadKeyIndex,
									const BString& trigger);

			status_t			RestoreSystemDefault();
			status_t			Use();

			void				SetKey(uint32 keyCode, uint32 modifiers,
									int8 deadKey, const char* bytes,
									int32 numBytes = -1);

			void				SetName(const char* name);

			/** @brief Read-only access to the in-memory key map. */
			const key_map&		Map() const { return fKeys; }
			/** @brief Mutable access to the in-memory key map. */
			key_map&			Map() { return fKeys; }

			Keymap&				operator=(const Keymap& other);

private:
			bool				_SetChars(int32 offset, const char* bytes,
									int32 numBytes);

private:
			char				fName[B_FILE_NAME_LENGTH];

			BMessenger			fTarget;
			BMessage*			fModificationMessage;
};


#endif	// KEYMAP_H
