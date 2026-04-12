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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file Settings.cpp
 * @brief Generic setting handler classes for the Tracker settings subsystem.
 *
 * Provides StringValueSetting, EnumeratedStringValueSetting, and
 * ScalarValueSetting, which are SettingsArgvDispatcher subclasses.  Each
 * class handles a typed persistent setting: loading from an argv-style token
 * stream, saving back, and carrying a default value.
 *
 * @see TrackerSettings, SettingsArgvDispatcher
 */

// generic setting handler classes


#include <Debug.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "TrackerSettings.h"


Settings* settings = NULL;


//	#pragma mark - StringValueSetting


/**
 * @brief Construct a StringValueSetting with a name, default, and error strings.
 *
 * @param name                     Setting token name.
 * @param defaultValue             The value used when the setting is absent.
 * @param valueExpectedErrorString Error message when no value token follows the name.
 * @param wrongValueErrorString    Error message when an unrecognised value is read.
 */
StringValueSetting::StringValueSetting(const char* name,
	const char* defaultValue, const char* valueExpectedErrorString,
	const char* wrongValueErrorString)
	:
	SettingsArgvDispatcher(name),
	fDefaultValue(defaultValue),
	fValueExpectedErrorString(valueExpectedErrorString),
	fWrongValueErrorString(wrongValueErrorString),
	fValue(defaultValue)
{
}


/**
 * @brief Destroy the StringValueSetting.
 */
StringValueSetting::~StringValueSetting()
{
}


/**
 * @brief Update the stored value.
 *
 * @param newValue  The new setting value string.
 */
void
StringValueSetting::ValueChanged(const char* newValue)
{
	fValue = newValue;
}


/**
 * @brief Return the current value string.
 *
 * @return Pointer to the current setting value.
 */
const char*
StringValueSetting::Value() const
{
	return fValue.String();
}


/**
 * @brief Write the current value as a quoted token to the settings stream.
 *
 * @param settings  The Settings writer to write to.
 */
void
StringValueSetting::SaveSettingValue(Settings* settings)
{
	settings->Write("\"%s\"", fValue.String());
}


/**
 * @brief Return true if the current value differs from the default and must be saved.
 *
 * @return True when the setting needs to be persisted.
 */
bool
StringValueSetting::NeedsSaving() const
{
	// needs saving if different than default
	return fValue != fDefaultValue;
}


/**
 * @brief Parse the next token from the settings argv as the new value.
 *
 * @param argv  Null-terminated token array; *argv is the setting name token.
 * @return NULL on success, or the error string if parsing fails.
 */
const char*
StringValueSetting::Handle(const char* const* argv)
{
	if (!*++argv)
		return fValueExpectedErrorString;

	ValueChanged(*argv);
	return 0;
}


//	#pragma mark - EnumeratedStringValueSetting


/**
 * @brief Construct an EnumeratedStringValueSetting.
 *
 * @param name                     Setting token name.
 * @param defaultValue             Default value; must be one of @p values.
 * @param values                   NULL-terminated array of valid value strings.
 * @param valueExpectedErrorString Error when no value token follows the name.
 * @param wrongValueErrorString    Error when the token is not in @p values.
 */
EnumeratedStringValueSetting::EnumeratedStringValueSetting(const char* name,
	const char* defaultValue, const char* const* values,
	const char* valueExpectedErrorString, const char* wrongValueErrorString)
	:
	StringValueSetting(name, defaultValue, valueExpectedErrorString,
		wrongValueErrorString),
	fValues(values)
{
}


/**
 * @brief Update the value, asserting in debug builds that it is in the enumeration.
 *
 * @param newValue  The new setting value; must be one of the valid strings.
 */
void
EnumeratedStringValueSetting::ValueChanged(const char* newValue)
{
#if DEBUG
	// must be one of the enumerated values
	bool found = false;
	for (int32 index = 0; ; index++) {
		if (fValues[index] == NULL)
			break;

		if (strcmp(fValues[index], newValue) != 0)
			continue;

		found = true;
		break;
	}
	ASSERT(found);
#endif
	StringValueSetting::ValueChanged(newValue);
}


/**
 * @brief Parse and validate the next token against the allowed-values list.
 *
 * @param argv  Null-terminated token array; *argv is the setting name token.
 * @return NULL on success, or the appropriate error string.
 */
const char*
EnumeratedStringValueSetting::Handle(const char* const* argv)
{
	if (!*++argv)
		return fValueExpectedErrorString;

	bool found = false;
	for (int32 index = 0; ; index++) {
		if (fValues[index] == NULL)
			break;

		if (strcmp(fValues[index], *argv) != 0)
			continue;

		found = true;
		break;
	}

	if (!found)
		return fWrongValueErrorString;

	ValueChanged(*argv);
	return 0;
}


//	#pragma mark - ScalarValueSetting


/**
 * @brief Construct a ScalarValueSetting with range clamping.
 *
 * @param name                     Setting token name.
 * @param defaultValue             Default integer value.
 * @param valueExpectedErrorString Error when no value token follows the name.
 * @param wrongValueErrorString    Error when the value is out of range.
 * @param min                      Minimum acceptable value (inclusive).
 * @param max                      Maximum acceptable value (inclusive).
 */
ScalarValueSetting::ScalarValueSetting(const char* name, int32 defaultValue,
	const char* valueExpectedErrorString, const char* wrongValueErrorString,
	int32 min, int32 max)
	:
	SettingsArgvDispatcher(name),
	fDefaultValue(defaultValue),
	fValue(defaultValue),
	fMax(max),
	fMin(min),
	fValueExpectedErrorString(valueExpectedErrorString),
	fWrongValueErrorString(wrongValueErrorString)
{
}


/**
 * @brief Update the stored integer value after range checking.
 *
 * @param newValue  The new value; must be in [fMin, fMax].
 */
void
ScalarValueSetting::ValueChanged(int32 newValue)
{
	ASSERT(newValue > fMin);
	ASSERT(newValue < fMax);
	fValue = newValue;
}


/**
 * @brief Return the current integer value.
 *
 * @return Current setting value as int32.
 */
int32
ScalarValueSetting::Value() const
{
	return fValue;
}


/**
 * @brief Format the current value as a decimal string into @p buffer.
 *
 * @param buffer  Output character buffer; must be large enough for an int32 decimal string.
 */
void
ScalarValueSetting::GetValueAsString(char* buffer) const
{
	sprintf(buffer, "%" B_PRId32, fValue);
}


/**
 * @brief Parse the next token as a decimal or hex integer and store it if in range.
 *
 * @param argv  Null-terminated token array; *argv is the setting name token.
 * @return NULL on success, or the appropriate error string.
 */
const char*
ScalarValueSetting::Handle(const char* const* argv)
{
	if (!*++argv)
		return fValueExpectedErrorString;

	int32 newValue;
	if ((*argv)[0] == '0' && (*argv)[1] == 'x')
		sscanf(*argv, "%" B_PRIx32, &newValue);
	else
		newValue = atoi(*argv);

	if (newValue < fMin || newValue > fMax)
		return fWrongValueErrorString;

	fValue = newValue;
	return NULL;
}


/**
 * @brief Write the current value as a decimal string to the settings stream.
 *
 * @param settings  The Settings writer to write to.
 */
void
ScalarValueSetting::SaveSettingValue(Settings* settings)
{
	settings->Write("%" B_PRId32, fValue);
}


/**
 * @brief Return true if the current value differs from the default.
 *
 * @return True when saving is required.
 */
bool
ScalarValueSetting::NeedsSaving() const
{
	return fValue != fDefaultValue;
}


//	#pragma mark - HexScalarValueSetting


HexScalarValueSetting::HexScalarValueSetting(const char* name,
	int32 defaultValue, const char* valueExpectedErrorString,
	const char* wrongValueErrorString, int32 min, int32 max)
	:
	ScalarValueSetting(name, defaultValue, valueExpectedErrorString,
		wrongValueErrorString, min, max)
{
}


void
HexScalarValueSetting::GetValueAsString(char* buffer) const
{
	sprintf(buffer, "0x%08" B_PRIx32, fValue);
}


void
HexScalarValueSetting::SaveSettingValue(Settings* settings)
{
	settings->Write("0x%08" B_PRIx32, fValue);
}


//	#pragma mark - BooleanValueSetting


BooleanValueSetting::BooleanValueSetting(const char* name, bool defaultValue)
	:	ScalarValueSetting(name, defaultValue, 0, 0)
{
}


bool
BooleanValueSetting::Value() const
{
	return fValue != 0;
}


void
BooleanValueSetting::SetValue(bool value)
{
	fValue = value;
}


const char*
BooleanValueSetting::Handle(const char* const* argv)
{
	if (!*++argv)
		return "on or off expected";

	if (strcmp(*argv, "on") == 0)
		fValue = true;
	else if (strcmp(*argv, "off") == 0)
		fValue = false;
	else
		return "on or off expected";

	return 0;
}


void
BooleanValueSetting::SaveSettingValue(Settings* settings)
{
	settings->Write(fValue ? "on" : "off");
}
