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
 * @file FloatValueFormatter.cpp
 * @brief Formatter that renders FloatValue as %f / %g and parses edits via strtod.
 *
 * B_FLOAT_TYPE is rendered with "%f" (fixed point) and B_DOUBLE_TYPE with
 * "%g" (shortest representation). Validation uses strtod() and rejects input
 * with non-whitespace trailing characters.
 *
 * @see FloatValue
 */


#include "FloatValueFormatter.h"

#include <new>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "FloatValue.h"


/**
 * @brief Trivial constructor; FloatValueFormatter is stateless.
 */
FloatValueFormatter::FloatValueFormatter()
	:
	ValueFormatter()
{
}


/**
 * @brief Trivial destructor.
 */
FloatValueFormatter::~FloatValueFormatter()
{
}


/**
 * @brief Formats @a _value as either %f (float) or %g (double).
 *
 * @param _value   The FloatValue to render.
 * @param _output  Receives the formatted text.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When @a _value is not a FloatValue.
 */
status_t
FloatValueFormatter::FormatValue(Value* _value, BString& _output)
{
	FloatValue* value = dynamic_cast<FloatValue*>(_value);
	if (value == NULL)
		return B_BAD_VALUE;

	char buffer[64];
	BVariant variantValue = value->GetValue();
	switch (variantValue.Type()) {
		case B_FLOAT_TYPE:
		{
			snprintf(buffer, sizeof(buffer), "%f", variantValue.ToFloat());
			break;
		}
		case B_DOUBLE_TYPE:
		{
			snprintf(buffer, sizeof(buffer), "%g", variantValue.ToDouble());
			break;
		}
	}

	_output.SetTo(buffer);

	return B_OK;
}


/**
 * @brief Reports that this formatter participates in inline edit validation.
 *
 * @return true.
 */
bool
FloatValueFormatter::SupportsValidation() const
{
	return true;
}


/**
 * @brief Validates that @a input parses as a float of @a type.
 *
 * @param input  Formatted text supplied by the user.
 * @param type   Target type (B_FLOAT_TYPE or B_DOUBLE_TYPE).
 * @return true when @a input is a clean numeric literal for @a type.
 */
bool
FloatValueFormatter::ValidateFormattedValue(const BString& input,
	type_code type) const
{
	::Value* value = NULL;
	return _PerformValidation(input, type, value, false) == B_OK;
}


/**
 * @brief Builds a FloatValue from user-typed text.
 *
 * @param input    Formatted text.
 * @param type     Target type (B_FLOAT_TYPE or B_DOUBLE_TYPE).
 * @param _output  Set to a freshly allocated FloatValue on success.
 * @return Status code from the validation routine.
 */
status_t
FloatValueFormatter::GetValueFromFormattedInput(const BString& input,
	type_code type, Value*& _output) const
{
	return _PerformValidation(input, type, _output, true);
}


/**
 * @brief Common entry point for validation and value construction.
 *
 * @param input       Formatted text.
 * @param type        Target type (B_FLOAT_TYPE or B_DOUBLE_TYPE).
 * @param _output     Set to a freshly allocated FloatValue when @a wantsValue is true.
 * @param wantsValue  When true, allocate a Value; otherwise just validate.
 * @retval B_OK         On a successful parse.
 * @retval B_BAD_VALUE  When @a type is neither float nor double.
 * @retval B_NO_MEMORY  When trailing garbage is present or allocation failed.
 */
status_t
FloatValueFormatter::_PerformValidation(const BString& input, type_code type,
	::Value*& _output, bool wantsValue) const
{
	const char* text = input.String();
	char *parseEnd = NULL;
	double parsedValue = strtod(text, &parseEnd);
	if (parseEnd - text < input.Length() && !isspace(*parseEnd))
		return B_NO_MEMORY;

	BVariant newValue;
	switch (type) {
		case B_FLOAT_TYPE:
		{
			newValue.SetTo((float)parsedValue);
			break;
		}
		case B_DOUBLE_TYPE:
		{
			newValue.SetTo(parsedValue);
			break;
		}
		default:
			return B_BAD_VALUE;
	}
	if (wantsValue) {
		_output = new(std::nothrow) FloatValue(newValue);
		if (_output == NULL)
			return B_NO_MEMORY;
	}

	return B_OK;
}
