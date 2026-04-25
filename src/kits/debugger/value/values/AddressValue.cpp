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
 *   Copyright 2009-2012, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file AddressValue.cpp
 * @brief Implementation of AddressValue, an IntegerValue rendered as a hex pointer.
 *
 * AddressValue represents a pointer or a memory address (function pointers,
 * pointer-typed locals, raw addresses) and prints itself as "0x..." rather
 * than as a decimal integer.
 *
 * @see IntegerValue, AddressValueNode
 */


#include "AddressValue.h"

#include <stdio.h>


/**
 * @brief Constructs an AddressValue from an integer-typed BVariant payload.
 *
 * @param value  BVariant holding the raw address.
 */
AddressValue::AddressValue(const BVariant& value)
	:
	IntegerValue(value)
{
}


/**
 * @brief Trivial destructor.
 */
AddressValue::~AddressValue()
{
}


/**
 * @brief Renders the payload as a hex literal of the form "0x...".
 *
 * @param _string  Receives the formatted hex representation.
 * @return true on success, false when the payload is not an integer.
 */
bool
AddressValue::ToString(BString& _string) const
{
	if (!fValue.IsInteger())
		return false;

	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%#" B_PRIx64, fValue.ToUInt64());

	BString string(buffer);
	if (string.Length() == 0)
		return false;

	_string = string;
	return true;
}
