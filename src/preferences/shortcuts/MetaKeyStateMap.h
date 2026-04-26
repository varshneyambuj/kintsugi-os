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
 * MIT License. Copyright 1999-2009, Jeremy Friesner and Haiku, Inc.
 * Original author: Jeremy Friesner.
 */

/** @file MetaKeyStateMap.h
    @brief Defines the chording-state table for a single modifier key. */

#ifndef META_KEY_STATE_MAP_H
#define META_KEY_STATE_MAP_H


#include <List.h>
#include <SupportDefs.h>


class BitFieldTester;


/**
 * @brief Set of chording states (e.g. Left, Right, Both, Either) for a
 *        single meta-key such as Shift or Ctrl.
 *
 * Each state pairs a human-readable description string with a BitFieldTester
 * that recognizes the modifier-bit pattern matching that state.
 */
// This class defines a set of possible chording states (e.g. "Left only",
// "Right only", "Both", "Either") for a meta-key (e.g. Shift), and the
// description strings and qualifier bit-chords that go with them.
class MetaKeyStateMap {
public:
			// Note: You MUST call SetInfo() directly after using this ctor!
							MetaKeyStateMap();

			// Creates a MetaKeyStateMap with the give name
			// (e.g. "Shift" or "Ctrl")
							MetaKeyStateMap(const char* keyName);


							~MetaKeyStateMap();

			// For when you have to use the default ctor
			void			SetInfo(const char* keyName);

			// (tester) becomes property of this map!
			void			AddState(const char* desc,
								const BitFieldTester* tester);

			// Returns the name of the meta-key (e.g. "Ctrl")
	const	char*			GetName() const;

			// Returns the number of possible states contained in this
			// MetaKeyStateMap.
			int				GetNumStates() const;

			// Returns a BitFieldTester that tests for the nth state's
			// presence.
	const	BitFieldTester*	GetNthStateTester(int stateNum) const;

			// Returns a textual description of the nth state (e.g. "Left")
	const	char*			GetNthStateDesc(int stateNum) const;

private:
			// e.g. "Alt" or "Ctrl"
			char*			fKeyName;

			// list of strings e.g. "Left" or "Both"
			BList			fStateDescs;

			// list of BitFieldTesters for testing bits of modifiers in state
			BList			fStateTesters;
};


#endif	// META_KEY_STATE_MAP_H
