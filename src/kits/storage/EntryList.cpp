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
 *   Copyright Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file EntryList.cpp
 * @brief Implementation of the BEntryList abstract base class.
 *
 * BEntryList defines the interface for iterating over file system entries
 * (e.g. directory contents or query results). This file provides the
 * constructor, destructor, and reserved virtual method stubs that keep
 * the vtable ABI stable across library versions.
 *
 * @see BEntryList
 */

#include <EntryList.h>


/**
 * @brief Default constructor.
 */
BEntryList::BEntryList()
{
}


/**
 * @brief Destructor.
 */
BEntryList::~BEntryList()
{
}


// Currently unused reserved virtual methods for future ABI compatibility.

/** @brief Reserved virtual method slot 1 (unused). */
void BEntryList::_ReservedEntryList1() {}
/** @brief Reserved virtual method slot 2 (unused). */
void BEntryList::_ReservedEntryList2() {}
/** @brief Reserved virtual method slot 3 (unused). */
void BEntryList::_ReservedEntryList3() {}
/** @brief Reserved virtual method slot 4 (unused). */
void BEntryList::_ReservedEntryList4() {}
/** @brief Reserved virtual method slot 5 (unused). */
void BEntryList::_ReservedEntryList5() {}
/** @brief Reserved virtual method slot 6 (unused). */
void BEntryList::_ReservedEntryList6() {}
/** @brief Reserved virtual method slot 7 (unused). */
void BEntryList::_ReservedEntryList7() {}
/** @brief Reserved virtual method slot 8 (unused). */
void BEntryList::_ReservedEntryList8() {}
