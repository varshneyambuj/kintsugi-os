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
 *   Copyright 2004-2011 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Sandor Vroemisse
 *       Jerome Duval
 *       Axel Doerfler, axeld@pinc-software.de
 */


/**
 * @file Keymap.cpp
 * @brief Editable Keymap that loads/saves the binary keymap blob and
 *        notifies observers of mutations.
 *
 * The on-disk format is the BeOS/Haiku binary key_map plus a UTF-8 string
 * table; this class reads it from disk, exposes mutators for individual
 * keys and modifier roles, and writes the result back, byte-swapping
 * to big-endian on save and back to host byte order after.
 */


#include "Keymap.h"

#include <new>
#include <stdio.h>
#include <string.h>

#include <ByteOrder.h>
#include <File.h>
#include <FindDirectory.h>
#include <Path.h>

#include <input_globals.h>


/** @brief Bitmask of all "side-agnostic" modifier flags. */
static const uint32 kModifierKeys = B_SHIFT_KEY | B_CAPS_LOCK | B_CONTROL_KEY
	| B_OPTION_KEY | B_COMMAND_KEY | B_MENU_KEY;


/**
 * @brief Helper used by DumpKeymap() to print a single mapped UTF-8 character.
 *
 * The string table @a chars stores each entry prefixed by a one-byte
 * length, followed by the UTF-8 bytes. A length of zero means the slot
 * is unmapped and is printed as "N/A".
 *
 * @param chars  String table from the active key_map.
 * @param offset Offset of the entry to print.
 * @param last   When true, suppress the trailing tab separator.
 */
static void
print_key(char* chars, int32 offset, bool last = false)
{
	int size = chars[offset++];

	switch (size) {
		case 0:
			// Not mapped
			fputs("N/A", stdout);
			break;

		case 1:
			// single-byte UTF-8/ASCII character
			fputc(chars[offset], stdout);
			break;

		default:
		{
			// 2-, 3-, or 4-byte UTF-8 character
			char* str = new char[size + 1];
			strncpy(str, &chars[offset], size);
			str[size] = 0;
			fputs(str, stdout);
			delete[] str;
			break;
		}
	}

	if (!last)
		fputs("\t", stdout);
}


//	#pragma mark -


/** @brief Construct an empty Keymap with no modification target. */
Keymap::Keymap()
	:
	fModificationMessage(NULL)
{
}


/** @brief Free the optional modification-notification message. */
Keymap::~Keymap()
{
	delete fModificationMessage;
}


/**
 * @brief Configure where modification notifications should be sent.
 *
 * Each mutator (SetKey, SetModifier, SetDeadKey...) posts @a modificationMessage
 * to @a target after a successful change so the UI can refresh.
 *
 * @param target              Messenger that will receive change notifications.
 * @param modificationMessage Message to send on each change. Ownership
 *                            transfers to the Keymap; the previous message
 *                            (if any) is freed.
 */
void
Keymap::SetTarget(BMessenger target, BMessage* modificationMessage)
{
	delete fModificationMessage;

	fTarget = target;
	fModificationMessage = modificationMessage;
}


/**
 * @brief Replace the keymap's display name.
 *
 * The name is stored in @c fName and persisted to the @c keymap:name
 * extended attribute on save.
 *
 * @param name New name; truncated to @c B_FILE_NAME_LENGTH bytes.
 */
void
Keymap::SetName(const char* name)
{
	strlcpy(fName, name, sizeof(fName));
}


/**
 * @brief Dump the entire keymap to stdout for debugging.
 *
 * Prints a 9-column chart (normal, shift, control, option, option+shift,
 * Caps, Caps+shift, Caps+option, Caps+option+shift) for keycodes 0..127.
 * Only version 3 keymaps are supported; otherwise the function is a no-op.
 */
void
Keymap::DumpKeymap()
{
	if (fKeys.version != 3)
		return;

	// Print a chart of the normal, shift, control, option, option+shift,
	// Caps, Caps+shift, Caps+option, and Caps+option+shift keys.
	puts("Key #\tn\ts\tc\to\tos\tC\tCs\tCo\tCos\n");

	for (uint8 i = 0; i < 128; i++) {
		printf(" 0x%02x\t", i);
		print_key(fChars, fKeys.normal_map[i]);
		print_key(fChars, fKeys.shift_map[i]);
		print_key(fChars, fKeys.control_map[i]);
		print_key(fChars, fKeys.option_map[i]);
		print_key(fChars, fKeys.option_shift_map[i]);
		print_key(fChars, fKeys.caps_map[i]);
		print_key(fChars, fKeys.caps_shift_map[i]);
		print_key(fChars, fKeys.option_caps_map[i]);
		print_key(fChars, fKeys.option_caps_shift_map[i], true);
		fputs("\n", stdout);
	}
}


/**
 * @brief Load a keymap from a file.
 *
 * Parses the binary key_map blob plus the UTF-8 string table from
 * @a ref, and reads the keymap's display name from the @c keymap:name
 * extended attribute (falling back to the file name).
 *
 * @param ref entry_ref of the keymap file to load.
 * @return    @c B_OK on success; an I/O or parse error otherwise.
 */
status_t
Keymap::Load(const entry_ref& ref)
{
	BEntry entry;
	status_t status = entry.SetTo(&ref, true);
	if (status != B_OK)
		return status;

	BFile file(&entry, B_READ_ONLY);
	status = SetTo(file);
	if (status != B_OK)
		return status;

	// fetch name from attribute and fall back to filename

	ssize_t bytesRead = file.ReadAttr("keymap:name", B_STRING_TYPE, 0, fName,
		sizeof(fName));
	if (bytesRead > 0)
		fName[bytesRead] = '\0';
	else
		strlcpy(fName, ref.name, sizeof(fName));

	return B_OK;
}


/**
 * @brief Serialize the keymap to a file.
 *
 * Writes the key_map struct (byte-swapped to big-endian for portability),
 * the size of the UTF-8 string table, the table itself, and finally the
 * @c keymap:name attribute. The in-memory key_map is restored to host byte
 * order before the function returns.
 *
 * @param ref entry_ref of the destination file. The file is truncated.
 * @return    @c B_OK on success; otherwise an I/O error.
 */
status_t
Keymap::Save(const entry_ref& ref)
{
	BFile file;
	status_t status = file.SetTo(&ref,
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (status != B_OK) {
		printf("error %s\n", strerror(status));
		return status;
	}

	for (uint32 i = 0; i < sizeof(fKeys) / 4; i++)
		((uint32*)&fKeys)[i] = B_HOST_TO_BENDIAN_INT32(((uint32*)&fKeys)[i]);

	ssize_t bytesWritten = file.Write(&fKeys, sizeof(fKeys));
	if (bytesWritten < (ssize_t)sizeof(fKeys))
		status = bytesWritten < 0 ? bytesWritten : B_IO_ERROR;

	for (uint32 i = 0; i < sizeof(fKeys) / 4; i++)
		((uint32*)&fKeys)[i] = B_BENDIAN_TO_HOST_INT32(((uint32*)&fKeys)[i]);

	if (status == B_OK) {
		fCharsSize = B_HOST_TO_BENDIAN_INT32(fCharsSize);

		bytesWritten = file.Write(&fCharsSize, sizeof(uint32));
		if (bytesWritten < (ssize_t)sizeof(uint32))
			status = bytesWritten < 0 ? bytesWritten : B_IO_ERROR;

		fCharsSize = B_BENDIAN_TO_HOST_INT32(fCharsSize);
	}

	if (status == B_OK) {
		bytesWritten = file.Write(fChars, fCharsSize);
		if (bytesWritten < (ssize_t)fCharsSize)
			status = bytesWritten < 0 ? bytesWritten : B_IO_ERROR;
	}

	if (status == B_OK) {
		const BString name(fName);
		file.WriteAttrString("keymap:name", &name);
			// Failing would be non-fatal
	}

	return status;
}


/**
 * @brief Reassign a modifier role to a different physical key.
 *
 * Recognizes both the per-side modifiers (e.g. @c B_LEFT_SHIFT_KEY) and
 * the side-agnostic ones (e.g. @c B_SHIFT_KEY). The latter are masked
 * down to known modifiers before being applied.
 *
 * @param keyCode  Scancode of the physical key to bind.
 * @param modifier Modifier flag identifying which role to assign.
 * @retval B_OK         On success; modification message is posted.
 * @retval B_BAD_VALUE  If @a modifier is not a recognized modifier flag.
 */
status_t
Keymap::SetModifier(uint32 keyCode, uint32 modifier)
{
	const uint32 kSingleModifierKeys = B_LEFT_SHIFT_KEY | B_RIGHT_SHIFT_KEY
		| B_LEFT_COMMAND_KEY | B_RIGHT_COMMAND_KEY | B_LEFT_CONTROL_KEY
		| B_RIGHT_CONTROL_KEY | B_LEFT_OPTION_KEY | B_RIGHT_OPTION_KEY;

	if ((modifier & kSingleModifierKeys) != 0)
		modifier &= kSingleModifierKeys;
	else if ((modifier & kModifierKeys) != 0)
		modifier &= kModifierKeys;

	if (modifier == B_CAPS_LOCK)
		fKeys.caps_key = keyCode;
	else if (modifier == B_NUM_LOCK)
		fKeys.num_key = keyCode;
	else if (modifier == B_SCROLL_LOCK)
		fKeys.scroll_key = keyCode;
	else if (modifier == B_LEFT_SHIFT_KEY)
		fKeys.left_shift_key = keyCode;
	else if (modifier == B_RIGHT_SHIFT_KEY)
		fKeys.right_shift_key = keyCode;
	else if (modifier == B_LEFT_COMMAND_KEY)
		fKeys.left_command_key = keyCode;
	else if (modifier == B_RIGHT_COMMAND_KEY)
		fKeys.right_command_key = keyCode;
	else if (modifier == B_LEFT_CONTROL_KEY)
		fKeys.left_control_key = keyCode;
	else if (modifier == B_RIGHT_CONTROL_KEY)
		fKeys.right_control_key = keyCode;
	else if (modifier == B_LEFT_OPTION_KEY)
		fKeys.left_option_key = keyCode;
	else if (modifier == B_RIGHT_OPTION_KEY)
		fKeys.right_option_key = keyCode;
	else if (modifier == B_MENU_KEY)
		fKeys.menu_key = keyCode;
	else
		return B_BAD_VALUE;

	if (fModificationMessage != NULL)
		fTarget.SendMessage(fModificationMessage);

	return B_OK;
}


/**
 * @brief Toggle whether a key acts as a dead key under @a modifiers.
 *
 * The keymap maintains a per-dead-key bitmask of "active" tables; setting
 * the corresponding bit makes that combination a dead key, clearing it
 * disables it.
 *
 * @param keyCode   Scancode of the candidate dead key.
 * @param modifiers Modifier mask under which the dead-key behavior applies.
 * @param enabled   True to enable, false to disable.
 */
void
Keymap::SetDeadKeyEnabled(uint32 keyCode, uint32 modifiers, bool enabled)
{
	uint32 tableMask = 0;
	int32 offset = Offset(keyCode, modifiers, &tableMask);
	uint8 deadKeyIndex = DeadKeyIndex(offset);
	if (deadKeyIndex > 0) {
		uint32* deadTables[] = {
			&fKeys.acute_tables,
			&fKeys.grave_tables,
			&fKeys.circumflex_tables,
			&fKeys.dieresis_tables,
			&fKeys.tilde_tables
		};

		if (enabled)
			(*deadTables[deadKeyIndex - 1]) |= tableMask;
		else
			(*deadTables[deadKeyIndex - 1]) &= ~tableMask;

		if (fModificationMessage != NULL)
			fTarget.SendMessage(fModificationMessage);
	}
}


/**
 * @brief Read the trigger character associated with a dead key.
 *
 * @param deadKeyIndex Index of the dead key (1..5; see @c dead_key_index).
 * @param outTrigger   Out: receives the UTF-8 trigger string, or empty.
 */
void
Keymap::GetDeadKeyTrigger(dead_key_index deadKeyIndex, BString& outTrigger)
{
	outTrigger = "";
	if (deadKeyIndex < 1 || deadKeyIndex > 5)
		return;

	int32 deadOffsets[] = {
		fKeys.acute_dead_key[1],
		fKeys.grave_dead_key[1],
		fKeys.circumflex_dead_key[1],
		fKeys.dieresis_dead_key[1],
		fKeys.tilde_dead_key[1]
	};

	int32 offset = deadOffsets[deadKeyIndex - 1];
	if (offset < 0 || offset >= (int32)fCharsSize)
		return;

	uint32 deadNumBytes = fChars[offset];
	if (!deadNumBytes)
		return;

	outTrigger.SetTo(&fChars[offset + 1], deadNumBytes);
}


/**
 * @brief Update the trigger character of a dead key.
 *
 * Writes the new UTF-8 sequence into the string table and re-enables the
 * dead key across every modifier table so the new trigger takes effect
 * everywhere it was previously active.
 *
 * @param deadKeyIndex Index of the dead key (1..5).
 * @param trigger      New trigger string. Maximum 6 bytes per UTF-8 character.
 */
void
Keymap::SetDeadKeyTrigger(dead_key_index deadKeyIndex, const BString& trigger)
{
	if (deadKeyIndex < 1 || deadKeyIndex > 5)
		return;

	int32 deadOffsets[] = {
		fKeys.acute_dead_key[1],
		fKeys.grave_dead_key[1],
		fKeys.circumflex_dead_key[1],
		fKeys.dieresis_dead_key[1],
		fKeys.tilde_dead_key[1]
	};

	int32 offset = deadOffsets[deadKeyIndex - 1];
	if (offset < 0 || offset >= (int32)fCharsSize)
		return;

	if (_SetChars(offset, trigger.String(), trigger.Length())) {
		// reset modifier table such that new dead key is enabled wherever
		// it is available
		uint32* deadTables[] = {
			&fKeys.acute_tables,
			&fKeys.grave_tables,
			&fKeys.circumflex_tables,
			&fKeys.dieresis_tables,
			&fKeys.tilde_tables
		};
		*deadTables[deadKeyIndex - 1]
			= B_NORMAL_TABLE | B_SHIFT_TABLE | B_CONTROL_TABLE | B_OPTION_TABLE
				| B_OPTION_SHIFT_TABLE | B_CAPS_TABLE | B_CAPS_SHIFT_TABLE
				| B_OPTION_CAPS_TABLE | B_OPTION_CAPS_SHIFT_TABLE;

		if (fModificationMessage != NULL)
			fTarget.SendMessage(fModificationMessage);
	}
}


/**
 * @brief Discard the user keymap and re-activate the system default.
 *
 * Removes @c ~/config/settings/Key_map and asks the input server to
 * fall back to the system default keymap.
 *
 * @return @c B_OK on success; otherwise an error from the file system or
 *         input server.
 */
status_t
Keymap::RestoreSystemDefault()
{
	BPath path;
	status_t status = find_directory(B_USER_SETTINGS_DIRECTORY, &path);
	if (status != B_OK)
		return status;

	path.Append("Key_map");

	BEntry entry(path.Path());
	entry.Remove();

	return Use();
}


/**
 * @brief Tell the input server to activate the saved user keymap.
 *
 * Calls @c _restore_key_map_() and refreshes the keyboard lock LEDs
 * (Caps Lock, Num Lock, Scroll Lock) on success.
 *
 * @return @c B_OK on success; otherwise an error from the input server.
 */
status_t
Keymap::Use()
{
	status_t result = _restore_key_map_();
	if (result == B_OK)
		set_keyboard_locks(modifiers());
	return result;
}


/**
 * @brief Reassign the UTF-8 character produced by a key + modifier combo.
 *
 * Looks up the offset in the active modifier table for the given
 * key code, then writes @a numBytes bytes of @a bytes into the string
 * table. Posts a modification message on success.
 *
 * @param keyCode   Scancode of the key to mutate.
 * @param modifiers Modifier mask selecting the active sub-table.
 * @param deadKey   Dead-key index, or 0 for none.
 * @param bytes     UTF-8 character to assign.
 * @param numBytes  Number of bytes in @a bytes; -1 to call @c strlen.
 *                  Values above 6 are silently rejected.
 */
void
Keymap::SetKey(uint32 keyCode, uint32 modifiers, int8 deadKey,
	const char* bytes, int32 numBytes)
{
	int32 offset = Offset(keyCode, modifiers);
	if (offset < 0)
		return;

	if (numBytes < 0)
		numBytes = strlen(bytes);
	if (numBytes > 6)
		return;

	if (_SetChars(offset, bytes, numBytes)) {
		if (fModificationMessage != NULL)
			fTarget.SendMessage(fModificationMessage);
	}
}


/**
 * @brief Deep-copy assignment that duplicates the string table and message.
 *
 * @param other Source keymap to copy from.
 * @return      Reference to this Keymap.
 */
Keymap&
Keymap::operator=(const Keymap& other)
{
	if (this == &other)
		return *this;

	delete[] fChars;
	delete fModificationMessage;

	fChars = new(std::nothrow) char[other.fCharsSize];
	if (fChars != NULL) {
		memcpy(fChars, other.fChars, other.fCharsSize);
		fCharsSize = other.fCharsSize;
	} else
		fCharsSize = 0;

	memcpy(&fKeys, &other.fKeys, sizeof(key_map));
	strlcpy(fName, other.fName, sizeof(fName));

	fTarget = other.fTarget;

	if (other.fModificationMessage != NULL)
		fModificationMessage = new BMessage(*other.fModificationMessage);

	return *this;
}


/**
 * @brief Write @a numBytes bytes of @a bytes at @a offset in the string table.
 *
 * Resizes the string table when the new entry is a different length and
 * patches every offset in the key_map that points past @a offset so the
 * downstream entries remain valid.
 *
 * @param offset   Offset of the entry within @c fChars.
 * @param bytes    New UTF-8 bytes to write.
 * @param numBytes Number of bytes in @a bytes (excluding the length prefix).
 * @return         True when the table was actually changed; false when
 *                 the new content was identical to the old or when the
 *                 reallocation failed.
 */
bool
Keymap::_SetChars(int32 offset, const char* bytes, int32 numBytes)
{
	int32 oldNumBytes = fChars[offset];

	if (oldNumBytes == numBytes
		&& !memcmp(&fChars[offset + 1], bytes, numBytes)) {
		// nothing to do
		return false;
	}

	int32 diff = numBytes - oldNumBytes;
	if (diff != 0) {
		fCharsSize += diff;

		if (diff > 0) {
			// make space for the new data
			char* chars = new(std::nothrow) char[fCharsSize];
			if (chars != NULL) {
				memcpy(chars, fChars, offset + oldNumBytes + 1);
				memcpy(&chars[offset + 1 + numBytes],
					&fChars[offset + 1 + oldNumBytes],
					fCharsSize - 2 - offset - diff);
				delete[] fChars;
				fChars = chars;
			} else
				return false;
		} else if (diff < 0) {
			// shrink table
			memmove(&fChars[offset + numBytes], &fChars[offset + oldNumBytes],
				fCharsSize - offset - 2 - diff);
		}

		// update offsets
		int32* data = fKeys.control_map;
		int32 size = sizeof(fKeys.control_map) / 4 * 9
			+ sizeof(fKeys.acute_dead_key) / 4 * 5;
		for (int32 i = 0; i < size; i++) {
			if (data[i] > offset)
				data[i] += diff;
		}
	}

	memcpy(&fChars[offset + 1], bytes, numBytes);
	fChars[offset] = numBytes;

	return true;
}
