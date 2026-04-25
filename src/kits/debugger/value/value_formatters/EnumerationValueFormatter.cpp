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
 *   Copyright 2015, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file EnumerationValueFormatter.cpp
 * @brief Formatter that prints enum values as their declared names when possible.
 *
 * In INTEGER_FORMAT_DEFAULT mode this looks up the enumerator that matches the
 * stored integer and prints its name (e.g. "B_OK"); for any other integer
 * format the formatter falls back to IntegerValueFormatter so the user can
 * still see the raw decimal/hex/signed/unsigned representation.
 *
 * @see EnumerationValue, IntegerValueFormatter
 */


#include "EnumerationValueFormatter.h"

#include "EnumerationValue.h"
#include "Type.h"


/**
 * @brief Constructs the formatter, sharing IntegerValueFormatter's config.
 *
 * @param config  Shared integer-format configuration; may be NULL.
 */
EnumerationValueFormatter::EnumerationValueFormatter(Config* config)
	:
	IntegerValueFormatter(config)
{
}


/**
 * @brief Trivial destructor.
 */
EnumerationValueFormatter::~EnumerationValueFormatter()
{
}


/**
 * @brief Renders @a _value as an enumerator name in default mode, integer otherwise.
 *
 * @param _value   The EnumerationValue to format.
 * @param _output  Receives the formatted string.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When @a _value is not an EnumerationValue.
 * @return Other status_t propagated from IntegerValueFormatter::FormatValue().
 */
status_t
EnumerationValueFormatter::FormatValue(Value* _value, BString& _output)
{
	Config* config = GetConfig();
	if (config != NULL && config->IntegerFormat() == INTEGER_FORMAT_DEFAULT) {
		EnumerationValue* value = dynamic_cast<EnumerationValue*>(_value);
		if (value == NULL)
			return B_BAD_VALUE;

		if (EnumeratorValue* enumValue
				= value->GetType()->ValueFor(value->GetValue())) {
			_output.SetTo(enumValue->Name());
			return B_OK;
		}
	}

	return IntegerValueFormatter::FormatValue(_value, _output);
}
