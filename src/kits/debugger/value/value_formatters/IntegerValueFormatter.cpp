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
 * @file IntegerValueFormatter.cpp
 * @brief Formatter that renders IntegerValue in signed/unsigned/hex/octal forms and parses edits back.
 *
 * Format selection is delegated to IntegerFormatter via a Config callback so a
 * single user-toggleable setting (decimal vs hex, signed vs unsigned) drives
 * every integer column in the variables view. Validation accepts text typed in
 * the same format and reconstructs an IntegerValue clamped to the target's
 * width.
 *
 * @see IntegerValue, IntegerFormatter
 */


#include "IntegerValueFormatter.h"

#include <new>

#include <ctype.h>

#include "IntegerFormatter.h"
#include "IntegerValue.h"


// #pragma mark - IntegerValueFormatter


/**
 * @brief Constructs the formatter and takes a reference on @a config if supplied.
 *
 * @param config  Optional configuration controlling integer presentation.
 */
IntegerValueFormatter::IntegerValueFormatter(Config* config)
	:
	ValueFormatter(),
	fConfig(config)
{
	if (fConfig != NULL)
		fConfig->AcquireReference();
}


/**
 * @brief Releases the configuration reference taken in the constructor.
 */
IntegerValueFormatter::~IntegerValueFormatter()
{
	if (fConfig != NULL)
		fConfig->ReleaseReference();
}


/**
 * @brief Returns the persistent settings backing the configuration, or NULL.
 *
 * @return Settings instance, or NULL if no config is attached.
 */
Settings*
IntegerValueFormatter::GetSettings() const
{
	return fConfig != NULL ? fConfig->GetSettings() : NULL;
}


/**
 * @brief Formats @a _value into @a _output using the configured integer format.
 *
 * @param _value   The IntegerValue to render.
 * @param _output  Receives the formatted text.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When @a _value is not an IntegerValue or formatting failed.
 */
status_t
IntegerValueFormatter::FormatValue(Value* _value, BString& _output)
{
	IntegerValue* value = dynamic_cast<IntegerValue*>(_value);
	if (value == NULL)
		return B_BAD_VALUE;

	// format the value
	integer_format format = fConfig != NULL
		? fConfig->IntegerFormat() : INTEGER_FORMAT_DEFAULT;
	char buffer[32];
	if (!IntegerFormatter::FormatValue(value->GetValue(), format,  buffer,
			sizeof(buffer))) {
		return B_BAD_VALUE;
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
IntegerValueFormatter::SupportsValidation() const
{
	return true;
}


/**
 * @brief Validates user-typed text against an integer @a type.
 *
 * @param input  Formatted text supplied by the user.
 * @param type   Target integer type code.
 * @return true when @a input parses cleanly and fits @a type.
 */
bool
IntegerValueFormatter::ValidateFormattedValue(const BString& input,
	type_code type) const
{
	::Value* value = NULL;
	return _PerformValidation(input, type, value, false) == B_OK;
}


/**
 * @brief Builds an IntegerValue from user-typed text.
 *
 * @param input    Formatted text supplied by the user.
 * @param type     Target integer type code.
 * @param _output  Set to a freshly allocated IntegerValue on success.
 * @return Status code from the validation routine.
 */
status_t
IntegerValueFormatter::GetValueFromFormattedInput(const BString& input,
	type_code type, Value*& _output) const
{
	return _PerformValidation(input, type, _output, true);
}


/**
 * @brief Common entry point for validation and value construction.
 *
 * Dispatches to _ValidateSigned() or _ValidateUnsigned() based on the
 * configured integer format (or, when no config is present, the signedness
 * implied by @a type).
 *
 * @param input       Formatted text supplied by the user.
 * @param type        Target integer type code.
 * @param _output     Set to a freshly allocated IntegerValue when @a wantsValue is true.
 * @param wantsValue  When true, allocate a Value; otherwise just validate.
 * @retval B_OK         On a successful parse.
 * @retval B_BAD_VALUE  When @a type is not an integer or input is out of range.
 */
status_t
IntegerValueFormatter::_PerformValidation(const BString& input, type_code type,
	::Value*& _output, bool wantsValue) const
{
	integer_format format;
	if (fConfig != NULL)
		format = fConfig->IntegerFormat();
	else {
		bool isSigned;
		if (BVariant::TypeIsInteger(type, &isSigned)) {
			format = isSigned ? INTEGER_FORMAT_SIGNED
				: INTEGER_FORMAT_UNSIGNED;
		} else
			return B_BAD_VALUE;
	}

	status_t error = B_OK;
	if (format == INTEGER_FORMAT_UNSIGNED
		|| format >= INTEGER_FORMAT_HEX_DEFAULT) {
		error = _ValidateUnsigned(input, type, _output, format, wantsValue);
	} else
		error = _ValidateSigned(input, type, _output, wantsValue);

	return error;
}


/**
 * @brief Parses signed decimal text and clamps it to the requested width.
 *
 * @param input       Formatted text.
 * @param type        Target signed integer type (B_INT8_TYPE..B_INT64_TYPE).
 * @param _output     Set to a freshly allocated IntegerValue when @a wantsValue is true.
 * @param wantsValue  When true, allocate a Value; otherwise just validate.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  When @a type is unrecognised or the parsed value overflows.
 * @retval B_NO_MEMORY  When trailing garbage is present or allocation failed.
 */
status_t
IntegerValueFormatter::_ValidateSigned(const BString& input, type_code type,
	::Value*& _output, bool wantsValue) const
{
	const char* text = input.String();
	char *parseEnd = NULL;
	intmax_t parsedValue = strtoimax(text, &parseEnd, 10);
	if (parseEnd - text < input.Length() && !isspace(*parseEnd))
		return B_NO_MEMORY;

	BVariant newValue;
	switch (type) {
		case B_INT8_TYPE:
		{
			if (parsedValue < INT8_MIN || parsedValue > INT8_MAX)
				return B_BAD_VALUE;

			newValue.SetTo((int8)parsedValue);
			break;
		}
		case B_INT16_TYPE:
		{
			if (parsedValue < INT16_MIN || parsedValue > INT16_MAX)
				return B_BAD_VALUE;

			newValue.SetTo((int16)parsedValue);
			break;
		}
		case B_INT32_TYPE:
		{
			if (parsedValue < INT32_MIN || parsedValue > INT32_MAX)
				return B_BAD_VALUE;

			newValue.SetTo((int32)parsedValue);
			break;
		}
		case B_INT64_TYPE:
		{
			newValue.SetTo((int64)parsedValue);
			break;
		}
		default:
			return B_BAD_VALUE;
	}

	if (wantsValue) {
		_output = new(std::nothrow) IntegerValue(newValue);
		if (_output == NULL)
			return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Parses unsigned decimal or hex text and clamps it to the requested width.
 *
 * @param input       Formatted text.
 * @param type        Target unsigned integer type (B_UINT8_TYPE..B_UINT64_TYPE).
 * @param _output     Set to a freshly allocated IntegerValue when @a wantsValue is true.
 * @param format      Currently selected integer format; selects base (10 vs 16).
 * @param wantsValue  When true, allocate a Value; otherwise just validate.
 * @retval B_OK         On success.
 * @retval B_BAD_VALUE  On overflow, trailing garbage, or unrecognised @a type.
 * @retval B_NO_MEMORY  When allocation failed.
 */
status_t
IntegerValueFormatter::_ValidateUnsigned(const BString& input, type_code type,
	::Value*& _output, integer_format format, bool wantsValue) const
{
	const char* text = input.String();
	int32 base = format == INTEGER_FORMAT_UNSIGNED ? 10 : 16;

	char *parseEnd = NULL;
	uintmax_t parsedValue = strtoumax(text, &parseEnd, base);
	if (parseEnd - text < input.Length() && !isspace(*parseEnd))
		return B_BAD_VALUE;

	BVariant newValue;
	switch (type) {
		case B_UINT8_TYPE:
		{
			if (parsedValue > UINT8_MAX)
				return B_BAD_VALUE;

			newValue.SetTo((uint8)parsedValue);
			break;
		}
		case B_UINT16_TYPE:
		{
			if (parsedValue > UINT16_MAX)
				return B_BAD_VALUE;

			newValue.SetTo((uint16)parsedValue);
			break;
		}
		case B_UINT32_TYPE:
		{
			if (parsedValue > UINT32_MAX)
				return B_BAD_VALUE;

			newValue.SetTo((uint32)parsedValue);
			break;
		}
		case B_UINT64_TYPE:
		{
			newValue.SetTo((uint64)parsedValue);
			break;
		}
		default:
			return B_BAD_VALUE;
	}

	if (wantsValue) {
		_output = new(std::nothrow) IntegerValue(newValue);
		if (_output == NULL)
			return B_NO_MEMORY;
	}

	return B_OK;
}



// #pragma mark - Config


/**
 * @brief Destructor for the Config abstract base.
 *
 * Defined out-of-line so the vtable has a single home translation unit.
 */
IntegerValueFormatter::Config::~Config()
{
}
