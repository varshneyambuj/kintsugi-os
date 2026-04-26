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
 *   Copyright 2009 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Jeremy Friesner
 */


/**
 * @file MetaKeyStateMap.cpp
 * @brief Implementation of MetaKeyStateMap, the chording-state table for a
 *        single modifier key.
 *
 * Stores the list of human-readable state names paired with BitFieldTester
 * objects that recognize the matching modifier-bit chords. Owns and frees
 * both lists.
 */


#include "MetaKeyStateMap.h"


#include <stdio.h>
#include <string.h>


#include "BitFieldTesters.h"


/**
 * @brief Default constructor; the caller MUST follow with SetInfo().
 */
MetaKeyStateMap::MetaKeyStateMap()
	:
	fKeyName(NULL)
{
	// User MUST call SetInfo() before using!
}


/**
 * @brief Constructs a state map with the given meta-key name.
 *
 * @param name Display name of the modifier (e.g. "Shift" or "Ctrl").
 */
MetaKeyStateMap::MetaKeyStateMap(const char* name)
{
	SetInfo(name);
}


/**
 * @brief Sets the meta-key display name after default construction.
 *
 * @param keyName Display name of the modifier; copied internally.
 */
void
MetaKeyStateMap::SetInfo(const char* keyName)
{
	fKeyName = new char[strlen(keyName) + 1];
	strcpy(fKeyName, keyName);
}


/**
 * @brief Releases the key name, all state descriptions, and all testers.
 */
MetaKeyStateMap::~MetaKeyStateMap()
{
	delete [] fKeyName;
	int nr = fStateDescs.CountItems();
	for (int i = 0; i < nr; i++)
		delete [] ((const char*) fStateDescs.ItemAt(i));

	nr = fStateTesters.CountItems();
	for (int j = 0; j < nr; j++)
		delete ((BitFieldTester*) fStateTesters.ItemAt(j));
	// _stateBits are stored in-line, no need to delete them
}


/**
 * @brief Appends a new (description, tester) pair to the state list.
 *
 * @param d Human-readable description of the state (copied internally).
 * @param t Tester that matches modifier bit-patterns for this state.
 *          Ownership transfers to this MetaKeyStateMap.
 */
void
MetaKeyStateMap::AddState(const char* d, const BitFieldTester* t)
{
	char* copy = new char[strlen(d) + 1];
	strcpy(copy, d);
	fStateDescs.AddItem(copy);
	fStateTesters.AddItem((void *)t);
}


/**
 * @brief Returns how many chording states have been registered.
 *
 * @return The number of states added via AddState().
 */
int
MetaKeyStateMap::GetNumStates() const
{
	return fStateTesters.CountItems();
}


/**
 * @brief Returns the BitFieldTester for the requested state index.
 *
 * @param stateNum Zero-based state index.
 * @return Pointer to the tester (still owned by this map), or NULL if the
 *         index is out of range.
 */
const BitFieldTester*
MetaKeyStateMap::GetNthStateTester(int stateNum) const
{
	return ((const BitFieldTester*) fStateTesters.ItemAt(stateNum));
}


/**
 * @brief Returns the description text for the requested state index.
 *
 * @param stateNum Zero-based state index.
 * @return Pointer to a NUL-terminated description (owned by this map), or
 *         NULL if the index is out of range.
 */
const char*
MetaKeyStateMap::GetNthStateDesc(int stateNum) const
{
	return ((const char*) fStateDescs.ItemAt(stateNum));
}


/**
 * @brief Returns the meta-key display name set by the constructor or
 *        SetInfo().
 *
 * @return Pointer to the NUL-terminated name string.
 */
const char*
MetaKeyStateMap::GetName() const
{
	return fKeyName;
}
