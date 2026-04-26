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
 *   Copyright 1999-2009 Jeremy Friesner
 *   Copyright 2009-2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jeremy Friesner
 */


/**
 * @file ShortcutsSpec.cpp
 * @brief Implementation of ShortcutsSpec, a single hotkey row.
 *
 * Holds modifier-state indices, key code, and command string, and provides
 * editing entry points called from the column list view as well as
 * archiving routines used to ship hotkey definitions to the input_server
 * filter.
 */


#include "ShortcutsSpec.h"

#include <ctype.h>
#include <stdio.h>

#include <Beep.h>
#include <Catalog.h>
#include <ColumnTypes.h>
#include <Directory.h>
#include <Locale.h>
#include <NodeInfo.h>
#include <Path.h>
#include <Region.h>
#include <Window.h>

#include "ColumnListView.h"

#include "BitFieldTesters.h"
#include "CommandActuators.h"
#include "KeyInfos.h"
#include "MetaKeyStateMap.h"
#include "ParseCommandLine.h"


#define CLASS "ShortcutsSpec : "

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "ShortcutsSpec"

/** @brief Default row height for the shortcuts list view. */
const float _height = 20.0f;

/** @brief Per-modifier shared state-map table indexed by column. */
static MetaKeyStateMap sMetaMaps[ShortcutsSpec::NUM_META_COLUMNS];

/** @brief True once sViewFont has been populated from a host BView. */
static bool sFontCached = false;
/** @brief Cached BFont taken from the first row's owning view. */
static BFont sViewFont;
/** @brief Cached font height (ascent - descent) of sViewFont. */
static float sFontHeight;

const char* ShortcutsSpec::sShiftName;
const char* ShortcutsSpec::sControlName;
const char* ShortcutsSpec::sOptionName;
const char* ShortcutsSpec::sCommandName;


#define ICON_BITMAP_RECT BRect(0.0f, 0.0f, 15.0f, 15.0f)
#define ICON_BITMAP_SPACE B_RGBA32


/**
 * @brief Returns the character at offset @a pos within @a str.
 *
 * Walks the string one byte at a time so the caller does not have to
 * pre-validate the length.
 *
 * @param str NUL-terminated source string.
 * @param pos Zero-based byte offset.
 * @return The character at the offset, or '\0' if @a pos lies past the end.
 */
// Returns the (pos)'th char in the string, or '\0' if (pos) if off the end of
// the string
static char
GetLetterAt(const char* str, int pos)
{
	for (int i = 0; i < pos; i++) {
		if (str[i] == '\0')
			return '\0';
	}
	return str[pos];
}


/**
 * @brief Populates a MetaKeyStateMap with the standard five chord states.
 *
 * Adds None / Either / Left / Right / Both states for the supplied paired
 * modifier bits. The map name is stored verbatim from @a name.
 *
 * @param map   Map to configure.
 * @param name  Display name (e.g. "Shift").
 * @param both  Modifier bit set when either side is pressed.
 * @param left  Modifier bit set when the left side alone is pressed.
 * @param right Modifier bit set when the right side alone is pressed.
 */
// Setup the states in a standard manner for a pair of meta-keys.
static void
SetupStandardMap(MetaKeyStateMap& map, const char* name, uint32 both,
	uint32 left, uint32 right)
{
	map.SetInfo(name);

	// In this state, neither key may be pressed.
	map.AddState(B_TRANSLATE("(None)"), new HasBitsFieldTester(0, both));

	// Here, either may be pressed. (Remember both is NOT a 2-bit chord, it's
	// another bit entirely)
	map.AddState(B_TRANSLATE("Either"), new HasBitsFieldTester(both));

	// Here, only the left may be pressed
	map.AddState(B_TRANSLATE("Left"), new HasBitsFieldTester(left, right));

	// Here, only the right may be pressed
	map.AddState(B_TRANSLATE("Right"), new HasBitsFieldTester(right, left));

	// Here, both must be pressed.
	map.AddState(B_TRANSLATE("Both"), new HasBitsFieldTester(left | right));
}


/**
 * @brief Returns the shared MetaKeyStateMap for the requested column index.
 *
 * @param which Column index in the range [0, NUM_META_COLUMNS).
 * @return Reference to the requested map.
 */
MetaKeyStateMap&
GetNthKeyMap(int which)
{
	return sMetaMaps[which];
}


/**
 * @brief Initializes the four shared modifier state maps on first use.
 *
 * Idempotent; subsequent calls are no-ops.
 */
/*static*/ void
ShortcutsSpec::InitializeMetaMaps()
{
	static bool metaMapsInitialized = false;
	if (metaMapsInitialized)
		return;
	metaMapsInitialized = true;

	_InitModifierNames();

	SetupStandardMap(sMetaMaps[ShortcutsSpec::SHIFT_COLUMN_INDEX], sShiftName,
		B_SHIFT_KEY, B_LEFT_SHIFT_KEY, B_RIGHT_SHIFT_KEY);

	SetupStandardMap(sMetaMaps[ShortcutsSpec::CONTROL_COLUMN_INDEX],
		sControlName, B_CONTROL_KEY, B_LEFT_CONTROL_KEY, B_RIGHT_CONTROL_KEY);

	SetupStandardMap(sMetaMaps[ShortcutsSpec::COMMAND_COLUMN_INDEX],
		sCommandName, B_COMMAND_KEY, B_LEFT_COMMAND_KEY, B_RIGHT_COMMAND_KEY);

	SetupStandardMap(sMetaMaps[ShortcutsSpec::OPTION_COLUMN_INDEX], sOptionName
		, B_OPTION_KEY, B_LEFT_OPTION_KEY, B_RIGHT_OPTION_KEY);
}


/**
 * @brief Constructs a fresh hotkey row for the given command string.
 *
 * @param cmd Command line for the new entry; copied internally.
 */
ShortcutsSpec::ShortcutsSpec(const char* cmd)
	:
	BRow(),
	fCommand(NULL),
	fBitmap(ICON_BITMAP_RECT, ICON_BITMAP_SPACE),
	fLastBitmapName(NULL),
	fBitmapValid(false),
	fKey(0),
	fCursorPtsValid(false)
{
	for (int i = 0; i < NUM_META_COLUMNS; i++)
		fMetaCellStateIndex[i] = 0;
	SetCommand(cmd);
}


/**
 * @brief Copy constructor; duplicates state, command, and field strings.
 *
 * @param from Source spec to copy.
 */
ShortcutsSpec::ShortcutsSpec(const ShortcutsSpec& from)
	:
	BRow(),
	fCommand(NULL),
	fBitmap(ICON_BITMAP_RECT, ICON_BITMAP_SPACE),
	fLastBitmapName(NULL),
	fBitmapValid(false),
	fKey(from.fKey),
	fCursorPtsValid(false)
{
	for (int i = 0; i < NUM_META_COLUMNS; i++)
		fMetaCellStateIndex[i] = from.fMetaCellStateIndex[i];

	SetCommand(from.fCommand);
	SetSelectedColumn(from.GetSelectedColumn());

	for (int i = 0; i < from.CountFields(); i++)
		SetField(new BStringField(
					static_cast<const BStringField*>(from.GetField(i))->String()), i);
}


/**
 * @brief Reconstructs a hotkey row from a flattened BMessage.
 *
 * Reads the command string, key code, and per-column modifier state
 * indices. Logs a diagnostic if any expected field is missing.
 *
 * @param from Source archive message.
 */
ShortcutsSpec::ShortcutsSpec(BMessage* from)
	:
	BRow(),
	fCommand(NULL),
	fBitmap(ICON_BITMAP_RECT, ICON_BITMAP_SPACE),
	fLastBitmapName(NULL),
	fBitmapValid(false),
	fCursorPtsValid(false)
{
	const char* temp;
	if (from->FindString("command", &temp) != B_NO_ERROR) {
		printf(CLASS);
		printf(" Error, no command string in archive BMessage!\n");
		temp = "";
	}

	SetCommand(temp);

	if (from->FindInt32("key", (int32*) &fKey) != B_NO_ERROR) {
		printf(CLASS);
		printf(" Error, no key int32 in archive BMessage!\n");
	}

	for (int i = 0; i < NUM_META_COLUMNS; i++)
		if (from->FindInt32("mcidx", i, (int32*)&fMetaCellStateIndex[i])
			!= B_NO_ERROR) {
			printf(CLASS);
			printf(" Error, no modifiers int32 in archive BMessage!\n");
		}

	for (int i = 0; i <= STRING_COLUMN_INDEX; i++)
		SetField(new BStringField(GetCellText(i)), i);
}


/**
 * @brief Replaces the entry's command string and updates the displayed cell.
 *
 * @param command New command line; copied into the internal buffer.
 */
void
ShortcutsSpec::SetCommand(const char* command)
{
	delete[] fCommand;
		// out with the old (if any)...
	fCommandLen = strlen(command) + 1;
	fCommandNul = fCommandLen - 1;
	fCommand = new char[fCommandLen];
	strcpy(fCommand, command);
	SetField(new BStringField(command), STRING_COLUMN_INDEX);
}


/**
 * @brief Returns the display name of the Nth modifier column.
 *
 * @param i Column index in the range [0, NUM_META_COLUMNS).
 * @return Pointer to the modifier name (owned by the global state map).
 */
const char*
ShortcutsSpec::GetColumnName(int i)
{
	return sMetaMaps[i].GetName();
}


/**
 * @brief Serializes this spec into the given BMessage.
 *
 * Emits both the prefs-applet payload (command, key, mcidx fields) and the
 * input_server payload (an archived MinMatchFieldTester and CommandActuator)
 * so a single archive round-trip can drive both sides.
 *
 * @param into Destination BMessage.
 * @param deep Forwarded to BArchivable::Archive().
 * @retval B_NO_ERROR  On success.
 * @retval other       Status code from the underlying Archive() calls.
 */
status_t
ShortcutsSpec::Archive(BMessage* into, bool deep) const
{
	status_t ret = BArchivable::Archive(into, deep);
	if (ret != B_NO_ERROR)
		return ret;

	into->AddString("class", "ShortcutsSpec");

	// These fields are for our prefs panel's benefit only
	into->AddString("command", fCommand);
	into->AddInt32("key", fKey);

	// Assemble a BitFieldTester for the input_server add-on to use...
	MinMatchFieldTester test(NUM_META_COLUMNS, false);
	for (int i = 0; i < NUM_META_COLUMNS; i++) {
		// for easy parsing by prefs applet on load-in
		into->AddInt32("mcidx", fMetaCellStateIndex[i]);
		test.AddSlave(sMetaMaps[i].GetNthStateTester(fMetaCellStateIndex[i]));
	}

	BMessage testerMsg;
	ret = test.Archive(&testerMsg);
	if (ret != B_NO_ERROR)
		return ret;

	into->AddMessage("modtester", &testerMsg);

	// And also create a CommandActuator for the input_server add-on to execute
	CommandActuator* act = CreateCommandActuator(fCommand);
	BMessage actMsg;
	ret = act->Archive(&actMsg);
	if (ret != B_NO_ERROR)
		return ret;
	delete act;

	into->AddMessage("act", &actMsg);

	return ret;
}


/**
 * @brief BArchivable factory; constructs a ShortcutsSpec from a message.
 *
 * Accepts both modern "ShortcutsSpec" archives and legacy "SpicyKeysSpec"
 * archives so older settings files keep loading.
 *
 * @param from Candidate archive message.
 * @return A newly allocated spec on success, or NULL when validation fails.
 */
BArchivable*
ShortcutsSpec::Instantiate(BMessage* from)
{
	bool validateOK = false;
	if (validate_instantiation(from, "ShortcutsSpec"))
		validateOK = true;
	else // test the old one.
		if (validate_instantiation(from, "SpicyKeysSpec"))
			validateOK = true;

	if (!validateOK)
		return NULL;

	return new ShortcutsSpec(from);
}


/**
 * @brief Destructor; releases the command string and cached bitmap name.
 */
ShortcutsSpec::~ShortcutsSpec()
{
	delete[] fCommand;
	delete[] fLastBitmapName;
}


/**
 * @brief Caches the owning view's font on first invocation.
 *
 * @param owner The BView whose font should be sampled.
 */
void
ShortcutsSpec::_CacheViewFont(BView* owner)
{
	if (sFontCached == false) {
		sFontCached = true;
		owner->GetFont(&sViewFont);
		font_height fh;
		sViewFont.GetHeight(&fh);
		sFontHeight = fh.ascent - fh.descent;
	}
}


/**
 * @brief Returns the display text for a given column.
 *
 * Branches on the well-known column indices: KEY produces a key name (or a
 * hexadecimal fallback for high USB-HID codes), STRING returns the command
 * line, and the remaining columns return the active modifier state's
 * description (with parenthesized states blanked).
 *
 * @param whichColumn Column index.
 * @return Pointer to the text to render; never NULL.
 */
const char*
ShortcutsSpec::GetCellText(int whichColumn) const
{
	const char* temp = ""; // default
	switch (whichColumn) {
		case KEY_COLUMN_INDEX:
		{
			if ((fKey > 0) && (fKey <= 0xFF)) {
				temp = GetKeyName(fKey);
				if (temp == NULL)
					temp = "";
			} else if (fKey > 0xFF) {
				sprintf(fScratch, "#%" B_PRIx32, fKey);
				return fScratch;
			}
			break;
		}

		case STRING_COLUMN_INDEX:
			temp = fCommand;
			break;

		default:
			if ((whichColumn >= 0) && (whichColumn < NUM_META_COLUMNS))
				temp = sMetaMaps[whichColumn].GetNthStateDesc(
							fMetaCellStateIndex[whichColumn]);
			if (temp[0] == '(')
				temp = "";
			break;
	}
	return temp;
}


/**
 * @brief Translates a mouse click on a modifier column into a state cycle.
 *
 * @param whichColumn Column the user clicked.
 * @return True when the click changed the spec; false otherwise.
 */
bool
ShortcutsSpec::ProcessColumnMouseClick(int whichColumn)
{
	if ((whichColumn >= 0) && (whichColumn < NUM_META_COLUMNS)) {
		// same as hitting space for these columns: cycle entry
		const char temp = B_SPACE;

		// 3rd arg isn't correct but it isn't read for this case anyway
		return ProcessColumnKeyStroke(whichColumn, &temp, 0);
	}
	return false;
}


/**
 * @brief Applies a string-typed update to a column.
 *
 * The STRING column receives the new command, the KEY column resolves the
 * string to a key code, and modifier columns delegate to
 * ProcessColumnKeyStroke().
 *
 * @param whichColumn Column to update.
 * @param string      New text value.
 * @return True when the spec changed.
 */
bool
ShortcutsSpec::ProcessColumnTextString(int whichColumn, const char* string)
{
	switch (whichColumn) {
		case STRING_COLUMN_INDEX:
			SetCommand(string);
			return true;
			break;

		case KEY_COLUMN_INDEX:
		{
			fKey = FindKeyCode(string);
			SetField(new BStringField(GetCellText(whichColumn)),
				KEY_COLUMN_INDEX);
			return true;
			break;
		}

		default:
			return ProcessColumnKeyStroke(whichColumn, string, 0);
	}
}


/**
 * @brief Performs Bash-style tab completion on the trailing command argument.
 *
 * Splits the command line, finds the last argument, treats anything after
 * the last '/' as a filename fragment, and extends the fragment to the
 * longest common prefix among matching directory entries. Beeps and gives
 * up if no unique extension exists.
 *
 * @return True when the command string was modified.
 */
bool
ShortcutsSpec::_AttemptTabCompletion()
{
	bool result = false;

	int32 argc;
	char** argv = ParseArgvFromString(fCommand, argc);
	if (argc > 0) {
		// Try to complete the path partially expressed in the last argument!
		char* arg = argv[argc - 1];
		char* fileFragment = strrchr(arg, '/');
		if (fileFragment != NULL) {
			const char* directoryName = (fileFragment == arg) ? "/" : arg;
			*fileFragment = '\0';
			fileFragment++;
			int fragmentLength = strlen(fileFragment);

			BDirectory dir(directoryName);
			if (dir.InitCheck() == B_NO_ERROR) {
				BEntry nextEnt;
				BPath nextPath;
				BList matchList;
				int maxEntryLen = 0;

				// Read in all the files in the directory whose names start
				// with our fragment.
				while (dir.GetNextEntry(&nextEnt) == B_NO_ERROR) {
					if (nextEnt.GetPath(&nextPath) == B_NO_ERROR) {
						char* filePath = strrchr(nextPath.Path(), '/') + 1;
						if (strncmp(filePath, fileFragment, fragmentLength) == 0) {
							int len = strlen(filePath);
							if (len > maxEntryLen)
								maxEntryLen = len;
							char* newStr = new char[len + 1];
							strcpy(newStr, filePath);
							matchList.AddItem(newStr);
						}
					}
				}

				// Now slowly extend our keyword to its full length, counting
				// numbers of matches at each step. If the match list length
				// is 1, we can use that whole entry. If it's greater than one,
				// we can use just the match length.
				int matchLen = matchList.CountItems();
				if (matchLen > 0) {
					int i;
					BString result(fileFragment);
					for (i = fragmentLength; i < maxEntryLen; i++) {
						// See if all the matching entries have the same letter
						// in the next position... if so, we can go farther.
						char commonLetter = '\0';
						for (int j = 0; j < matchLen; j++) {
							char nextLetter = GetLetterAt(
								(char*)matchList.ItemAt(j), i);
							if (commonLetter == '\0')
								commonLetter = nextLetter;

							if ((commonLetter != '\0')
								&& (commonLetter != nextLetter)) {
								commonLetter = '\0';// failed;
								beep();
								break;
							}
						}
						if (commonLetter == '\0')
							break;
						else
							result.Append(commonLetter, 1);
					}

					// free all the strings we allocated
					for (int k = 0; k < matchLen; k++)
						delete [] ((char*)matchList.ItemAt(k));

					DoStandardEscapes(result);

					BString wholeLine;
					for (int l = 0; l < argc - 1; l++) {
						wholeLine += argv[l];
						wholeLine += " ";
					}

					BString file(directoryName);
					DoStandardEscapes(file);

					if (directoryName[strlen(directoryName) - 1] != '/')
						file += "/";

					file += result;

					// Remove any trailing slash...
					const char* fileStr = file.String();
					if (fileStr[strlen(fileStr) - 1] == '/')
						file.RemoveLast("/");

					// and re-append it iff the file is a dir.
					BDirectory testFileAsDir(file.String());
					if ((strcmp(file.String(), "/") != 0)
						&& (testFileAsDir.InitCheck() == B_NO_ERROR))
						file.Append("/");

					wholeLine += file;

					SetCommand(wholeLine.String());
					result = true;
				}
			}
			*(fileFragment - 1) = '/';
		}
	}
	FreeArgv(argv);

	return result;
}


/**
 * @brief Updates the spec in response to a keystroke directed at a column.
 *
 * Handles key-code capture in the KEY column, character-level editing
 * (including tab completion) in the STRING column, and modifier-state
 * cycling or letter-jumping for the modifier columns.
 *
 * @param whichColumn Column receiving the keystroke.
 * @param bytes       Typed bytes (may be NULL when only @a key matters).
 * @param key         Raw key code, or -1 when only @a bytes is meaningful.
 * @return True when the spec changed.
 */
bool
ShortcutsSpec::ProcessColumnKeyStroke(int whichColumn, const char* bytes,
	int32 key)
{
	bool result = false;

	switch (whichColumn) {
		case KEY_COLUMN_INDEX:
			if (key > -1) {
				if ((int32)fKey != key) {
					fKey = key;
					result = true;
				}
			}
			break;

		case STRING_COLUMN_INDEX:
		{
			switch (bytes[0]) {
				case B_BACKSPACE:
				case B_DELETE:
					if (fCommandNul > 0) {
						// trim a char off the string
						fCommand[fCommandNul - 1] = '\0';
						fCommandNul--;	// note new nul position
						result = true;
					}
					break;

				case B_TAB:
					if (_AttemptTabCompletion()) {
						result = true;
					} else
						beep();
					break;

				default:
				{
					uint32 newCharLen = strlen(bytes);
					if ((newCharLen > 0) && (bytes[0] >= ' ')) {
						bool reAllocString = false;
						// Make sure we have enough room in our command string
						// to add these chars...
						while (fCommandLen - fCommandNul <= newCharLen) {
							reAllocString = true;
							// enough for a while...
							fCommandLen = (fCommandLen + 10) * 2;
						}

						if (reAllocString) {
							char* temp = new char[fCommandLen];
							strcpy(temp, fCommand);
							delete [] fCommand;
							fCommand = temp;
							// fCommandNul is still valid since it's an offset
							// and the string length is the same for now
						}

						// Here we should be guaranteed enough room.
						strncat(fCommand, bytes, fCommandLen);
						fCommandNul += newCharLen;
						result = true;
					}
				}
			}
			break;
		}

		default:
			if (whichColumn < 0 || whichColumn >= NUM_META_COLUMNS)
				break;

			MetaKeyStateMap * map = &sMetaMaps[whichColumn];
			int curState = fMetaCellStateIndex[whichColumn];
			int origState = curState;
			int numStates = map->GetNumStates();

			switch(bytes[0]) {
				case B_RETURN:
					// cycle to the previous state
					curState = (curState + numStates - 1) % numStates;
					break;

				case B_SPACE:
					// cycle to the next state
					curState = (curState + 1) % numStates;
					break;

				default:
				{
					// Go to the state starting with the given letter, if
					// any
					char letter = bytes[0];
					if (islower(letter))
						letter = toupper(letter); // convert to upper case

					if ((letter == B_BACKSPACE) || (letter == B_DELETE))
						letter = '(';
							// so space bar will blank out an entry

					for (int i = 0; i < numStates; i++) {
						const char* desc = map->GetNthStateDesc(i);

						if (desc) {
							if (desc[0] == letter) {
								curState = i;
								break;
							}
						} else {
							puts(B_TRANSLATE(
								"Error, NULL state description?"));
						}
					}
				}
			}
			fMetaCellStateIndex[whichColumn] = curState;

			if (curState != origState)
				result = true;
	}

	SetField(new BStringField(GetCellText(whichColumn)), whichColumn);

	return result;
}


/**
 * @brief Initializes the static modifier-name strings via the Catalog kit.
 *
 * Called once from InitializeMetaMaps() so the names are localized at
 * runtime instead of frozen at compile time.
 */
/*static*/ void
ShortcutsSpec::_InitModifierNames()
{
	sShiftName = B_TRANSLATE_COMMENT("Shift",
		"Name for modifier on keyboard");
	sControlName = B_TRANSLATE_COMMENT("Control",
		"Name for modifier on keyboard");
	sOptionName = B_TRANSLATE_COMMENT("Option",
		"Name for modifier on keyboard");
	sCommandName = B_TRANSLATE_COMMENT("Alt",
		"Name for modifier on keyboard");
}
