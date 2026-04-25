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
 *   Copyright 2010, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file StringValue.cpp
 * @brief Implementation of StringValue, a Value holding a BString payload.
 *
 * Used to represent C strings (char* / int8* / uint8*) once CStringValueNode
 * has read them out of target memory, as well as any other textual data the
 * variables view exposes.
 *
 * @see Value, CStringValueNode, StringValueFormatter
 */


#include "StringValue.h"

#include <stdio.h>


/**
 * @brief Constructs a StringValue copying the bytes from @a value.
 *
 * @param value  Null-terminated source string.
 */
StringValue::StringValue(const char* value)
	:
	fValue(value)
{
}


/**
 * @brief Trivial destructor.
 */
StringValue::~StringValue()
{
}


/**
 * @brief Returns a copy of the string payload.
 *
 * @param _string  Receives the embedded string.
 * @return Always true.
 */
bool
StringValue::ToString(BString& _string) const
{
	_string = fValue;
	return true;
}


/**
 * @brief Boxes the string payload as a C-string BVariant.
 *
 * @param _value  Receives the BVariant holding the C-string pointer.
 * @return Always true.
 */
bool
StringValue::ToVariant(BVariant& _value) const
{
	_value = fValue.String();
	return true;
}


/**
 * @brief Equality comparison against another Value.
 *
 * @param other  Other value to compare.
 * @return true when @a other is a StringValue with the same payload.
 */
bool
StringValue::operator==(const Value& other) const
{
	const StringValue* otherString = dynamic_cast<const StringValue*>(&other);
	return otherString != NULL ? fValue == otherString->fValue : false;
}
