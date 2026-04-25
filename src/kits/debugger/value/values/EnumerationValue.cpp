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
 * @file EnumerationValue.cpp
 * @brief Implementation of EnumerationValue, an IntegerValue tied to its EnumerationType.
 *
 * Stores both the raw integer payload and a reference to the EnumerationType
 * declaration so ToString() can substitute the matching enumerator's name
 * when one exists; otherwise it falls back to the inherited integer
 * representation.
 *
 * @see IntegerValue, EnumerationType, EnumerationValueFormatter
 */


#include "EnumerationValue.h"

#include "Type.h"


/**
 * @brief Constructs an EnumerationValue and references its declaring type.
 *
 * @param type   EnumerationType describing the legal enumerator names.
 * @param value  BVariant holding the underlying integer payload.
 */
EnumerationValue::EnumerationValue(EnumerationType* type, const BVariant& value)
	:
	IntegerValue(value),
	fType(type)
{
	fType->AcquireReference();
}


/**
 * @brief Releases the reference taken on the declaring EnumerationType.
 */
EnumerationValue::~EnumerationValue()
{
	fType->ReleaseReference();
}


/**
 * @brief Renders the payload as an enumerator name when one matches.
 *
 * Falls back to the base-class decimal integer representation when no
 * enumerator declares the current value (e.g. for bit-mask combinations).
 *
 * @param _string  Receives the formatted text.
 * @return true on success, false when the payload is not an integer or the
 *         resulting string is empty.
 */
bool
EnumerationValue::ToString(BString& _string) const
{
	if (!fValue.IsInteger())
		return false;

	EnumeratorValue* enumValue = fType->ValueFor(fValue);
	if (enumValue == NULL)
		return IntegerValue::ToString(_string);

	BString string(enumValue->Name());
	if (string.Length() == 0)
		return false;

	_string = string;
	return true;
}
