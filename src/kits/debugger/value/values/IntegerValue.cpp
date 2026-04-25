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
 * @file IntegerValue.cpp
 * @brief Implementation of IntegerValue, a Value holding any signed/unsigned integer width.
 *
 * The width and signedness live inside the embedded BVariant; consumers ask
 * IsSigned() to distinguish, and ToString() picks ToInt64() vs ToUInt64()
 * accordingly. AddressValue derives from this class to share the integer
 * plumbing while overriding the textual format.
 *
 * @see Value, AddressValue, EnumerationValue
 */


#include "IntegerValue.h"


/**
 * @brief Constructs an IntegerValue from a typed BVariant payload.
 *
 * @param value  BVariant holding an integer of any width/signedness.
 */
IntegerValue::IntegerValue(const BVariant& value)
	:
	fValue(value)
{
}


/**
 * @brief Trivial destructor.
 */
IntegerValue::~IntegerValue()
{
}


/**
 * @brief Reports whether the payload is a signed integer.
 *
 * @return true when the embedded BVariant is a signed integer; false for
 *         unsigned or non-integer payloads.
 */
bool
IntegerValue::IsSigned() const
{
	bool isSigned;
	return fValue.IsInteger(&isSigned) && isSigned;
}


/**
 * @brief Renders the payload as a base-10 integer string.
 *
 * @param _string  Receives the formatted decimal representation.
 * @return true on success, false when the payload is not an integer or the
 *         resulting string is empty.
 */
bool
IntegerValue::ToString(BString& _string) const
{
	bool isSigned;
	if (!fValue.IsInteger(&isSigned))
		return false;

	BString string;
	if (isSigned)
		string << fValue.ToInt64();
	else
		string << fValue.ToUInt64();

	if (string.Length() == 0)
		return false;

	_string = string;
	return true;
}


/**
 * @brief Boxes the integer payload into a BVariant.
 *
 * @param _value  Receives a copy of the embedded variant.
 * @return Always true.
 */
bool
IntegerValue::ToVariant(BVariant& _value) const
{
	_value = fValue;
	return true;
}


/**
 * @brief Equality comparison against another Value.
 *
 * @param other  Other value to compare.
 * @return true when @a other is an IntegerValue with the same payload.
 */
bool
IntegerValue::operator==(const Value& other) const
{
	const IntegerValue* otherInt = dynamic_cast<const IntegerValue*>(&other);
	return otherInt != NULL ? fValue == otherInt->fValue : false;
}
