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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file BoolValue.cpp
 * @brief Implementation of BoolValue, a Value holding a single bool.
 *
 * Used to represent variables typed as @c bool (and other primitives that the
 * loader has narrowed to a boolean) in the variables view.
 *
 * @see Value, BoolValueFormatter
 */


#include "BoolValue.h"


/**
 * @brief Constructs a BoolValue wrapping the given bool.
 *
 * @param value  Initial boolean payload.
 */
BoolValue::BoolValue(bool value)
	:
	fValue(value)
{
}


/**
 * @brief Trivial destructor.
 */
BoolValue::~BoolValue()
{
}


/**
 * @brief Returns the literal string "true" or "false".
 *
 * @param _string  Receives the textual representation.
 * @return true on success, false if the produced string is empty
 *         (cannot happen for a bool, kept for Value contract symmetry).
 */
bool
BoolValue::ToString(BString& _string) const
{
	BString string = fValue ? "true" : "false";
	if (string.Length() == 0)
		return false;

	_string = string;
	return true;
}


/**
 * @brief Boxes the bool payload into a BVariant.
 *
 * @param _value  Receives the BVariant copy.
 * @return Always true.
 */
bool
BoolValue::ToVariant(BVariant& _value) const
{
	_value = fValue;
	return true;
}


/**
 * @brief Equality comparison against another Value.
 *
 * @param other  Other value to compare.
 * @return true when @a other is a BoolValue with the same payload.
 */
bool
BoolValue::operator==(const Value& other) const
{
	const BoolValue* otherBool = dynamic_cast<const BoolValue*>(&other);
	return otherBool != NULL ? fValue == otherBool->fValue : false;
}
