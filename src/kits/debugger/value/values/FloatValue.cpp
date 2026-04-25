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
 * @file FloatValue.cpp
 * @brief Implementation of FloatValue, a Value holding either a float or a double.
 *
 * The width is determined at construction time by the BVariant's type code;
 * formatting and comparison delegate to BVariant accordingly.
 *
 * @see Value, FloatValueFormatter
 */


#include "FloatValue.h"

#include <stdio.h>


/**
 * @brief Constructs a FloatValue from a typed BVariant payload.
 *
 * @param value  BVariant whose type is B_FLOAT_TYPE or B_DOUBLE_TYPE.
 */
FloatValue::FloatValue(const BVariant& value)
	:
	fValue(value)
{
}


/**
 * @brief Trivial destructor.
 */
FloatValue::~FloatValue()
{
}


/**
 * @brief Renders the payload as a textual numeric literal.
 *
 * Floats are printed with %f (fixed point), doubles with %g (shortest form).
 *
 * @param _string  Receives the formatted text.
 * @return true on success, false when the payload type is not float/double or
 *         the resulting string is empty.
 */
bool
FloatValue::ToString(BString& _string) const
{
	char buffer[128];

	switch (fValue.Type()) {
		case B_FLOAT_TYPE:
		{
			snprintf(buffer, sizeof(buffer), "%f", fValue.ToFloat());
			break;
		}
		case B_DOUBLE_TYPE:
		{
			snprintf(buffer, sizeof(buffer), "%g", fValue.ToDouble());
			break;
		}
		default:
			return false;
	}

	BString string(buffer);
	if (string.Length() == 0)
		return false;

	_string = string;
	return true;
}


/**
 * @brief Boxes the float payload into a BVariant.
 *
 * @param _value  Receives a copy of the embedded variant.
 * @return Always true.
 */
bool
FloatValue::ToVariant(BVariant& _value) const
{
	_value = fValue;
	return true;
}


/**
 * @brief Equality comparison against another Value.
 *
 * @param other  Other value to compare.
 * @return true when @a other is a FloatValue with the same payload.
 */
bool
FloatValue::operator==(const Value& other) const
{
	const FloatValue* otherFloat = dynamic_cast<const FloatValue*>(&other);
	return otherFloat != NULL ? fValue == otherFloat->fValue : false;
}
