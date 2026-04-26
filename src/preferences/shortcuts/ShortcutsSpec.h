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
 * MIT License. Copyright 1999-2010, Jeremy Friesner and Haiku, Inc.
 * Original author: Jeremy Friesner.
 */

/** @file ShortcutsSpec.h
    @brief One row of the Shortcuts list, holding modifiers, key, and command. */

#ifndef SHORTCUTS_SPEC_H
#define SHORTCUTS_SPEC_H


#include <Bitmap.h>

#include <ColumnListView.h>
#include "KeyInfos.h"


class CommandActuator;
class MetaKeyStateMap;


/**
 * @brief Returns the shared MetaKeyStateMap for the modifier column index.
 *
 * @param which Column index (one of SHIFT_COLUMN_INDEX, CONTROL_COLUMN_INDEX,
 *              COMMAND_COLUMN_INDEX, OPTION_COLUMN_INDEX).
 * @return Reference to the requested map (lifetime equals the application).
 */
MetaKeyStateMap& GetNthKeyMap(int which);

/**
 * @brief One hotkey entry in the Shortcuts preference list.
 *
 * Each ShortcutsSpec stores per-modifier state indices, the triggering key
 * code, and the command string. It can render itself as a row in the
 * preference UI and serialize itself into a BMessage so the
 * shortcut_catcher input_server filter can reconstruct the matching
 * BitFieldTester and CommandActuator.
 */
/*
 * Objects of this class represent one hotkey "entry" in the preferences
 * ListView. Each ShortcutsSpec contains the info necessary to generate both
 * the proper GUI display, and the proper BitFieldTester and CommandActuator
 * object for the ShortcutsCatcher add-on to use.
 */
class ShortcutsSpec : public BRow, public BArchivable {
public:
	static	void			InitializeMetaMaps();

							ShortcutsSpec(const char* command);
							ShortcutsSpec(const ShortcutsSpec& copyMe);
							ShortcutsSpec(BMessage* from);
							~ShortcutsSpec();

	virtual	status_t		Archive(BMessage* into, bool deep = true) const;
	static	BArchivable*	Instantiate(BMessage* from);
	const	char* 			GetCellText(int whichColumn) const;
			void			SetCommand(const char* commandStr);

	// Returns the name of the Nth Column.
	static	const char*		GetColumnName(int index);

			// Update this spec's state in response to a keystroke to the given
			// column. Returns true iff a change occurred.
			bool 			ProcessColumnKeyStroke(int whichColumn,
								const char* bytes, int32 key);

			// Same as ProcessColumnKeyStroke, but for a mouse click instead.
			bool			ProcessColumnMouseClick(int whichColumn);

			// Same as ProcessColumnKeyStroke, but for a text string instead.
			bool			ProcessColumnTextString(int whichColumn,
								const char* string);

			/** @brief Returns the column index that currently holds the focus. */
			int32 			GetSelectedColumn() const { return fSelectedColumn; }
			/** @brief Sets which column index currently holds the focus. */
			void 			SetSelectedColumn(int32 i) { fSelectedColumn = i; }

	// default layout of columns is set in here.
	enum {
		SHIFT_COLUMN_INDEX		= 0,
		CONTROL_COLUMN_INDEX	= 1,
		COMMAND_COLUMN_INDEX	= 2,
		OPTION_COLUMN_INDEX		= 3,
		NUM_META_COLUMNS		= 4, // shift, control, command, option, for now
		KEY_COLUMN_INDEX		= NUM_META_COLUMNS,
		STRING_COLUMN_INDEX		= 5
	};

private:
			void 			_CacheViewFont(BView* owner);
			bool 			_AttemptTabCompletion();

			char*			fCommand;
			uint32			fCommandLen;	// number of bytes in fCommand buffer
			uint32			fCommandNul;	// index of the NUL byte in fCommand

			// icon for associated program. Invalid if none available.
			BBitmap			fBitmap;

			char*			fLastBitmapName;
			bool			fBitmapValid;
			uint32			fKey;
			int32			fMetaCellStateIndex[NUM_META_COLUMNS];
			BPoint			fCursorPt1;
			BPoint			fCursorPt2;
			bool			fCursorPtsValid;
	mutable	char			fScratch[50];
			int32			fSelectedColumn;

private:
	static	void			_InitModifierNames();

	static	const char*		sShiftName;
	static	const char*		sControlName;
	static	const char*		sOptionName;
	static	const char*		sCommandName;
};


#endif	// SHORTCUTS_SPEC_H
