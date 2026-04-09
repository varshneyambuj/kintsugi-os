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
 *   Copyright 2006-2015, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Axel Dörfler, axeld@pinc-software.de
 *       Michael Lotz <mmlr@mlotz.ch>
 */

/** @file DriverSettingsMessageAdapter.cpp
 *  @brief Converts between driver_settings structures (as loaded by the kernel
 *         driver-settings API) and BMessage objects, and vice versa, using a
 *         settings_template to describe the mapping.
 */


#include "DriverSettingsMessageAdapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>

#include <File.h>
#include <String.h>


/** @brief Constructs a default DriverSettingsConverter. */
DriverSettingsConverter::DriverSettingsConverter()
{
}


/** @brief Destroys the converter. */
DriverSettingsConverter::~DriverSettingsConverter()
{
}


/** @brief Converts a single driver_parameter value to a BMessage field.
 *
 *  The default implementation returns B_NOT_SUPPORTED; subclasses should
 *  override this to provide type-specific conversion.
 *
 *  @param parameter  The driver parameter being converted.
 *  @param name       Destination field name in @p target.
 *  @param index      Index into parameter.values[] to convert.
 *  @param type       Expected BMessage field type.
 *  @param target     The BMessage to write the converted value into.
 *  @return B_OK on success, B_NOT_SUPPORTED if not handled, or an error code.
 */
status_t
DriverSettingsConverter::ConvertFromDriverSettings(
	const driver_parameter& parameter, const char* name, int32 index,
	uint32 type, BMessage& target)
{
	return B_NOT_SUPPORTED;
}


/** @brief Converts a driver_parameter with no values to a BMessage field.
 *
 *  Called when parameter.value_count == 0. The default returns B_NOT_SUPPORTED.
 *
 *  @param parameter  The driver parameter with no associated values.
 *  @param name       Destination field name in @p target.
 *  @param type       Expected BMessage field type.
 *  @param target     The BMessage to write the converted value into.
 *  @return B_OK on success, B_NOT_SUPPORTED if not handled, or an error code.
 */
status_t
DriverSettingsConverter::ConvertEmptyFromDriverSettings(
	const driver_parameter& parameter, const char* name, uint32 type,
	BMessage& target)
{
	return B_NOT_SUPPORTED;
}


/** @brief Converts a BMessage field value back to a driver-settings string.
 *
 *  The default returns B_NOT_SUPPORTED; subclasses should override this for
 *  custom serialisation.
 *
 *  @param source  The BMessage containing the field.
 *  @param name    Name of the field in @p source.
 *  @param index   Index of the value within the field.
 *  @param type    Type code of the field.
 *  @param value   Output BString that receives the serialised value text.
 *  @return B_OK on success, B_NOT_SUPPORTED if not handled, or an error code.
 */
status_t
DriverSettingsConverter::ConvertToDriverSettings(const BMessage& source,
	const char* name, int32 index, uint32 type, BString& value)
{
	return B_NOT_SUPPORTED;
}


// #pragma mark -


/** @brief Constructs a default DriverSettingsMessageAdapter. */
DriverSettingsMessageAdapter::DriverSettingsMessageAdapter()
{
}


/** @brief Destroys the adapter. */
DriverSettingsMessageAdapter::~DriverSettingsMessageAdapter()
{
}


/** @brief Converts all parameters in @p settings into fields of @p message.
 *
 *  Unknown parameters (not found in the template) are silently skipped.
 *
 *  @param settings          The driver_settings structure to convert.
 *  @param settingsTemplate  Describes the expected parameters and their types.
 *  @param message           Output BMessage; cleared before conversion begins.
 *  @return B_OK on success, or an error code if conversion fails.
 */
status_t
DriverSettingsMessageAdapter::ConvertFromDriverSettings(
	const driver_settings& settings, const settings_template* settingsTemplate,
	BMessage& message)
{
	message.MakeEmpty();

	for (int32 i = 0; i < settings.parameter_count; i++) {
		status_t status = _ConvertFromDriverParameter(settings.parameters[i],
			settingsTemplate, message);
		if (status == B_BAD_VALUE) {
			// ignore unknown entries
			continue;
		}
		if (status != B_OK)
			return status;
	}

	return B_OK;
}


/** @brief Loads driver settings from @p path and converts them into @p message.
 *
 *  @param path              File path of the driver settings file.
 *  @param settingsTemplate  Describes the expected parameters and their types.
 *  @param message           Output BMessage populated with the settings.
 *  @return B_OK on success, B_ENTRY_NOT_FOUND if the file cannot be loaded,
 *          B_BAD_DATA if the settings handle yields no data, or another error.
 */
status_t
DriverSettingsMessageAdapter::ConvertFromDriverSettings(const char* path,
	const settings_template* settingsTemplate, BMessage& message)
{
	void* handle = load_driver_settings(path);
	if (handle == NULL)
		return B_ENTRY_NOT_FOUND;

	const driver_settings* settings = get_driver_settings(handle);
	status_t status;
	if (settings != NULL) {
		status = ConvertFromDriverSettings(*settings, settingsTemplate,
			message);
	} else
		status = B_BAD_DATA;

	unload_driver_settings(handle);
	return status;
}


/** @brief Converts all fields of @p message into driver-settings text in @p settings.
 *
 *  Iterates over every field in @p message and delegates to _AppendSettings()
 *  for each one.
 *
 *  @param settingsTemplate  Describes the valid fields and their types.
 *  @param settings          Output BString that receives the settings text.
 *  @param message           The BMessage whose fields are to be serialised.
 *  @return B_OK on success, or an error code if any field conversion fails.
 */
status_t
DriverSettingsMessageAdapter::ConvertToDriverSettings(
	const settings_template* settingsTemplate, BString& settings,
	const BMessage& message)
{
	int32 index = 0;
	char *name = NULL;
	type_code type;
	int32 count = 0;

	while (message.GetInfo(B_ANY_TYPE, index++, &name, &type, &count) == B_OK) {
		status_t result = _AppendSettings(settingsTemplate, settings, message,
			name, type, count);
		if (result != B_OK)
			return result;
	}

	return B_OK;
}


/** @brief Serialises @p message to driver-settings format and writes it to @p path.
 *
 *  Converts the message to text via ConvertToDriverSettings() then writes the
 *  result to the file at @p path, creating it if it does not exist and
 *  truncating it if it does.
 *
 *  @param path              Destination file path.
 *  @param settingsTemplate  Describes the valid fields and their types.
 *  @param message           The BMessage to serialise.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
DriverSettingsMessageAdapter::ConvertToDriverSettings(const char* path,
	const settings_template* settingsTemplate, const BMessage& message)
{
	BString settings;
	status_t status = ConvertToDriverSettings(settingsTemplate, settings,
		message);
	if (status != B_OK)
		return status;

	settings.RemoveFirst("\n");
	BFile settingsFile(path, B_WRITE_ONLY | B_ERASE_FILE | B_CREATE_FILE);

	ssize_t written = settingsFile.Write(settings.String(), settings.Length());
	if (written < 0)
		return written;

	return written == settings.Length() ? B_OK : B_ERROR;
}


// #pragma mark -


/** @brief Searches the template array for an entry matching @p name.
 *
 *  A NULL-named entry in the template is treated as a wildcard and is
 *  returned only when no exact match exists.
 *
 *  @param settingsTemplate  Array of settings_template entries (terminated by
 *                           an entry with type == 0).
 *  @param name              The parameter name to look up.
 *  @return Pointer to the matching template entry, or the wildcard entry,
 *          or NULL if neither is found.
 */
const settings_template*
DriverSettingsMessageAdapter::_FindSettingsTemplate(
	const settings_template* settingsTemplate, const char* name)
{
	const settings_template* wildcardTemplate = NULL;

	while (settingsTemplate->type != 0) {
		if (settingsTemplate->name != NULL
			&& !strcmp(name, settingsTemplate->name))
			return settingsTemplate;

		if (settingsTemplate->name == NULL)
			wildcardTemplate = settingsTemplate;
		settingsTemplate++;
	}

	return wildcardTemplate;
}


/** @brief Finds the sub-template entry marked as a parent_value.
 *
 *  Scans the sub_template of @p settingsTemplate for the first entry
 *  with parent_value set to true.
 *
 *  @param settingsTemplate  The parent template whose sub_template is searched.
 *  @return Pointer to the parent_value template entry, or NULL if none found.
 */
const settings_template*
DriverSettingsMessageAdapter::_FindParentValueTemplate(
	const settings_template* settingsTemplate)
{
	settingsTemplate = settingsTemplate->sub_template;
	if (settingsTemplate == NULL)
		return NULL;

	while (settingsTemplate->type != 0) {
		if (settingsTemplate->parent_value)
			return settingsTemplate;

		settingsTemplate++;
	}

	return NULL;
}


/** @brief Adds all values of @p parameter to @p message according to the template.
 *
 *  Iterates over parameter.values[], using the template's type and optional
 *  converter to store each value. Empty parameters with a BOOL type are
 *  treated as @c true.
 *
 *  @param parameter       The driver parameter whose values are to be added.
 *  @param settingsTemplate  Describes how to interpret the parameter.
 *  @param message           The BMessage to add fields to.
 *  @return B_OK on success, B_BAD_VALUE for unsupported types, or another error.
 */
status_t
DriverSettingsMessageAdapter::_AddParameter(const driver_parameter& parameter,
	const settings_template& settingsTemplate, BMessage& message)
{
	const char* name = settingsTemplate.name;
	if (name == NULL)
		name = parameter.name;

	for (int32 i = 0; i < parameter.value_count; i++) {
		if (settingsTemplate.converter != NULL) {
			status_t status
				= settingsTemplate.converter->ConvertFromDriverSettings(
					parameter, name, i, settingsTemplate.type, message);
			if (status == B_OK)
				continue;
			if (status != B_NOT_SUPPORTED)
				return status;
		}

		status_t status = B_OK;

		switch (settingsTemplate.type) {
			case B_STRING_TYPE:
				status = message.AddString(name, parameter.values[i]);
				break;
			case B_INT32_TYPE:
				status = message.AddInt32(name, atoi(parameter.values[i]));
				break;
			case B_BOOL_TYPE:
			{
				bool value=!strcasecmp(parameter.values[i], "true")
					|| !strcasecmp(parameter.values[i], "on")
					|| !strcasecmp(parameter.values[i], "yes")
					|| !strcasecmp(parameter.values[i], "enabled")
					|| !strcasecmp(parameter.values[i], "1");
				status = message.AddBool(name, value);
				break;
			}
			case B_MESSAGE_TYPE:
				// Is handled outside of this method
				break;

			default:
				return B_BAD_VALUE;
		}
		if (status != B_OK)
			return status;
	}

	if (parameter.value_count == 0) {
		if (settingsTemplate.converter != NULL) {
			status_t status
				= settingsTemplate.converter->ConvertEmptyFromDriverSettings(
					parameter, name, settingsTemplate.type, message);
			if (status == B_NOT_SUPPORTED)
				return B_OK;
		} else if (settingsTemplate.type == B_BOOL_TYPE) {
			// Empty boolean parameters are always true
			return message.AddBool(name, true);
		}
	}

	return B_OK;
}


/** @brief Recursively converts a single driver_parameter (and its children).
 *
 *  Locates the template entry for @p parameter, adds its values, and for
 *  B_MESSAGE_TYPE parameters recurses into sub-parameters.
 *
 *  @param parameter         The driver parameter to convert.
 *  @param settingsTemplate  Top-level template array to search.
 *  @param message           The BMessage to write converted values into.
 *  @return B_OK on success, or an error code on failure.
 */
status_t
DriverSettingsMessageAdapter::_ConvertFromDriverParameter(
	const driver_parameter& parameter,
	const settings_template* settingsTemplate, BMessage& message)
{
	settingsTemplate = _FindSettingsTemplate(settingsTemplate, parameter.name);
	if (settingsTemplate == NULL) {
		// We almost silently ignore this kind of issues
		fprintf(stderr, "unknown parameter %s\n", parameter.name);
		return B_OK;
	}

	status_t status = _AddParameter(parameter, *settingsTemplate, message);
	if (status != B_OK)
		return status;

	if (settingsTemplate->type == B_MESSAGE_TYPE) {
		BMessage subMessage;
		for (int32 j = 0; j < parameter.parameter_count; j++) {
			status = _ConvertFromDriverParameter(parameter.parameters[j],
				settingsTemplate->sub_template, subMessage);
			if (status != B_OK)
				return status;
		}

		const settings_template* parentValueTemplate
			= _FindParentValueTemplate(settingsTemplate);
		if (parentValueTemplate != NULL)
			status = _AddParameter(parameter, *parentValueTemplate, subMessage);
		if (status == B_OK)
			status = message.AddMessage(parameter.name, &subMessage);
	}

	return status;
}


/** @brief Serialises a single BMessage field as driver-settings text.
 *
 *  Looks up the field in the template, validates the type, and appends the
 *  formatted text representation to @p settings. Handles bool, string, int32,
 *  and nested message (B_MESSAGE_TYPE) fields.
 *
 *  @param settingsTemplate  Template used to validate the field.
 *  @param settings          Output BString to append the formatted text to.
 *  @param message           The BMessage containing the field.
 *  @param name              Field name to serialise.
 *  @param type              Type code of the field.
 *  @param count             Number of values in the field.
 *  @param settingName       Override name used in the output (NULL = use @p name).
 *  @return B_OK on success, B_BAD_VALUE for unknown/mismatched fields, or another error.
 */
status_t
DriverSettingsMessageAdapter::_AppendSettings(
	const settings_template* settingsTemplate, BString& settings,
	const BMessage& message, const char* name, type_code type, int32 count,
	const char* settingName)
{
	const settings_template* valueTemplate
		= _FindSettingsTemplate(settingsTemplate, name);
	if (valueTemplate == NULL) {
		fprintf(stderr, "unknown field %s\n", name);
		return B_BAD_VALUE;
	}

	if (valueTemplate->type != type) {
		fprintf(stderr, "field type mismatch %s\n", name);
		return B_BAD_VALUE;
	}

	if (settingName == NULL)
		settingName = name;

	if (type != B_MESSAGE_TYPE) {
		settings.Append("\n");
		settings.Append(settingName);
		settings.Append("\t");
	}

	for (int32 valueIndex = 0; valueIndex < count; valueIndex++) {
		if (valueIndex > 0 && type != B_MESSAGE_TYPE)
			settings.Append(" ");

		if (valueTemplate->converter != NULL) {
			status_t status = valueTemplate->converter->ConvertToDriverSettings(
				message, name, type, valueIndex, settings);
			if (status == B_OK)
				continue;
			if (status != B_NOT_SUPPORTED)
				return status;
		}

		switch (type) {
			case B_BOOL_TYPE:
			{
				bool value;
				status_t result = message.FindBool(name, valueIndex, &value);
				if (result != B_OK)
					return result;

				settings.Append(value ? "true" : "false");
				break;
			}

			case B_STRING_TYPE:
			{
				const char* value = NULL;
				status_t result = message.FindString(name, valueIndex, &value);
				if (result != B_OK)
					return result;

				settings.Append(value);
				break;
			}

			case B_INT32_TYPE:
			{
				int32 value;
				status_t result = message.FindInt32(name, valueIndex, &value);
				if (result != B_OK)
					return result;

				char buffer[100];
				snprintf(buffer, sizeof(buffer), "%" B_PRId32, value);
				settings.Append(buffer, sizeof(buffer));
				break;
			}

			case B_MESSAGE_TYPE:
			{
				BMessage subMessage;
				status_t result = message.FindMessage(name, valueIndex,
					&subMessage);
				if (result != B_OK)
					return result;

				const settings_template* parentValueTemplate
					= _FindParentValueTemplate(valueTemplate);
				if (parentValueTemplate != NULL) {
					_AppendSettings(valueTemplate->sub_template, settings,
						subMessage, parentValueTemplate->name,
						parentValueTemplate->type, 1, name);
					subMessage.RemoveName(parentValueTemplate->name);
				}

				BString subSettings;
				ConvertToDriverSettings(valueTemplate->sub_template,
					subSettings, subMessage);
				subSettings.ReplaceAll("\n", "\n\t");
				subSettings.RemoveFirst("\n");

				if (!subSettings.IsEmpty()) {
					settings.Append(" {\n");
					settings.Append(subSettings);
					settings.Append("\n}");
				}
			}
		}
	}

	return B_OK;
}
