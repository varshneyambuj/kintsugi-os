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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file SettingsParser.cpp
 *  @brief Implements parsing of launch daemon configuration files into structured settings messages. */


#include "SettingsParser.h"

#include <string.h>

#include <DriverSettingsMessageAdapter.h>


/**
 * @brief Abstract base for converters that parse driver-settings parameters
 *        with optional argument lists into BMessages.
 */
class AbstractArgsConverter : public DriverSettingsConverter {
public:
	/**
	 * @brief Handles empty (no sub-parameters) driver-settings entries.
	 *
	 * If the parameter has no sub-parameters and its name differs from
	 * this converter's Name(), an empty BMessage is added under @a name.
	 *
	 * @param parameter The driver_parameter to convert.
	 * @param name      The key name to store the result under.
	 * @param type      The expected type (unused).
	 * @param target    The BMessage to populate.
	 * @return B_OK on success, or an error code on failure.
	 */
	status_t ConvertEmptyFromDriverSettings(
		const driver_parameter& parameter, const char* name, uint32 type,
		BMessage& target)
	{
		if (parameter.parameter_count != 0 || strcmp(name, Name()) == 0)
			return B_OK;

		BMessage message;
		return target.AddMessage(name, &message);
	}

protected:
	/**
	 * @brief Extracts a condition/event name and its trailing arguments into a sub-message.
	 *
	 * Uses the value at @a index as the sub-message key and all subsequent
	 * values as "args" strings within it.
	 *
	 * @param parameter The driver_parameter containing values.
	 * @param index     Index of the value to use as the sub-message name.
	 * @param target    The parent BMessage to add the sub-message to.
	 * @return B_OK on success, or an error code on failure.
	 */
	status_t AddSubMessage(const driver_parameter& parameter, int32 index,
		BMessage& target)
	{
		const char* condition = parameter.values[index];
		BMessage args;
		for (index++; index < parameter.value_count; index++) {
			status_t status = args.AddString("args",
				parameter.values[index]);
			if (status != B_OK)
				return status;
		}
		return target.AddMessage(condition, &args);
	}

	virtual const char* Name() = 0;
};


/**
 * @brief Converter that parses "if" / "not" condition blocks from driver-settings
 *        into nested BMessages.
 */
class ConditionConverter : public AbstractArgsConverter {
public:
	/**
	 * @brief Converts a condition driver-settings parameter into BMessage form.
	 *
	 * Handles "if" (with optional leading "not" operator) and plain condition
	 * names, building nested BMessages for compound conditions.
	 *
	 * @param parameter The driver_parameter to convert.
	 * @param name      The key name (unused for "if"/"not" parameters).
	 * @param index     The value index to process.
	 * @param type      The expected type (unused).
	 * @param target    The BMessage to populate.
	 * @return B_OK on success, or an error code on failure.
	 */
	status_t ConvertFromDriverSettings(const driver_parameter& parameter,
		const char* name, int32 index, uint32 type, BMessage& target)
	{
		BMessage message;
		if (strcmp(parameter.name, "if") == 0) {
			// Parse values directly following "if", with special
			// handling for the "not" operator.
			if (index != 0)
				return B_OK;

			BMessage* add = &target;
			bool notOperator = parameter.value_count > 1
				&& strcmp(parameter.values[0], "not") == 0;
			if (notOperator) {
				add = &message;
				index++;
			}

			status_t status = AddSubMessage(parameter, index, *add);
			if (status == B_OK && notOperator)
				status = target.AddMessage("not", &message);

			return status;
		}
		if (strcmp(parameter.name, "not") == 0) {
			if (index != 0)
				return B_OK;

			return AddSubMessage(parameter, index, target);
		}

		message.AddString("args", parameter.values[index]);
		return target.AddMessage(parameter.name, &message);
	}

	/** @brief Returns the keyword this converter handles ("if"). */
	const char* Name()
	{
		return "if";
	}
};


/**
 * @brief Converter that parses "on" event blocks from driver-settings into BMessages.
 */
class EventConverter : public AbstractArgsConverter {
public:
	/**
	 * @brief Converts an event driver-settings parameter into BMessage form.
	 *
	 * Handles "on" keywords by extracting the event name and arguments
	 * from the parameter values.
	 *
	 * @param parameter The driver_parameter to convert.
	 * @param name      The key name (unused for "on" parameters).
	 * @param index     The value index to process.
	 * @param type      The expected type (unused).
	 * @param target    The BMessage to populate.
	 * @return B_OK on success, or an error code on failure.
	 */
	status_t ConvertFromDriverSettings(const driver_parameter& parameter,
		const char* name, int32 index, uint32 type, BMessage& target)
	{
		BMessage message;
		if (strcmp(parameter.name, "on") == 0) {
			// Parse values directly following "on"
			if (index != 0)
				return B_OK;

			return AddSubMessage(parameter, index, target);
		}

		message.AddString("args", parameter.values[index]);
		return target.AddMessage(parameter.name, &message);
	}

	/** @brief Returns the keyword this converter handles ("on"). */
	const char* Name()
	{
		return "on";
	}
};


/**
 * @brief Converter that parses "run" targets from driver-settings into BMessages.
 */
class RunConverter : public DriverSettingsConverter {
public:
	/**
	 * @brief Converts a run parameter value into a "target" string in the message.
	 *
	 * Only handles leaf parameters (no sub-parameters).
	 *
	 * @param parameter The driver_parameter to convert.
	 * @param name      The key name (unused).
	 * @param index     The value index to use as the target name.
	 * @param type      The expected type (unused).
	 * @param target    The BMessage to populate with a "target" string.
	 * @return B_OK on success, or B_NOT_SUPPORTED if sub-parameters exist.
	 */
	status_t ConvertFromDriverSettings(const driver_parameter& parameter,
		const char* name, int32 index, uint32 type, BMessage& target)
	{
		if (parameter.parameter_count == 0)
			return target.AddString("target", parameter.values[index]);

		return B_NOT_SUPPORTED;
	}

	/**
	 * @brief Handles empty run parameters by using the parameter name as the target.
	 *
	 * @param parameter The driver_parameter to convert.
	 * @param name      The parameter name to use as the run target.
	 * @param type      The expected type (unused).
	 * @param target    The BMessage to populate.
	 * @return B_OK on success, or an error code on failure.
	 */
	status_t ConvertEmptyFromDriverSettings(
		const driver_parameter& parameter, const char* name, uint32 type,
		BMessage& target)
	{
		if (parameter.parameter_count != 0)
			return B_OK;

		return target.AddString("target", name);
	}
};


/** @brief Template defining the grammar for condition blocks (if/not/and/or). */
const static settings_template kConditionTemplate[] = {
	{B_STRING_TYPE, NULL, NULL, true, new ConditionConverter()},
	{B_MESSAGE_TYPE, "not", kConditionTemplate},
	{B_MESSAGE_TYPE, "and", kConditionTemplate},
	{B_MESSAGE_TYPE, "or", kConditionTemplate},
	{0, NULL, NULL}
};

/** @brief Template defining the grammar for event blocks (on/and/or). */
const static settings_template kEventTemplate[] = {
	{B_STRING_TYPE, NULL, NULL, true, new EventConverter()},
	{B_MESSAGE_TYPE, "and", kEventTemplate},
	{B_MESSAGE_TYPE, "or", kEventTemplate},
	{0, NULL, NULL}
};

/** @brief Template for port definitions (name and optional capacity). */
const static settings_template kPortTemplate[] = {
	{B_STRING_TYPE, "name", NULL, true},
	{B_INT32_TYPE, "capacity", NULL},
};

/** @brief Template for environment variable definitions (from_script or key=value). */
const static settings_template kEnvTemplate[] = {
	{B_STRING_TYPE, "from_script", NULL, true},
	{B_STRING_TYPE, NULL, NULL},
};

/** @brief Template defining the grammar for job and service definitions. */
const static settings_template kJobTemplate[] = {
	{B_STRING_TYPE, "name", NULL, true},
	{B_BOOL_TYPE, "disabled", NULL},
	{B_STRING_TYPE, "launch", NULL},
	{B_STRING_TYPE, "requires", NULL},
	{B_BOOL_TYPE, "legacy", NULL},
	{B_MESSAGE_TYPE, "port", kPortTemplate},
	{B_MESSAGE_TYPE, "on", kEventTemplate},
	{B_MESSAGE_TYPE, "if", kConditionTemplate},
	{B_BOOL_TYPE, "no_safemode", NULL},
	{B_BOOL_TYPE, "on_demand", NULL},
	{B_MESSAGE_TYPE, "env", kEnvTemplate},
	{0, NULL, NULL}
};

/** @brief Template defining the grammar for target definitions (containing jobs/services). */
const static settings_template kTargetTemplate[] = {
	{B_STRING_TYPE, "name", NULL, true},
	{B_BOOL_TYPE, "reset", NULL},
	{B_MESSAGE_TYPE, "on", kEventTemplate},
	{B_MESSAGE_TYPE, "if", kConditionTemplate},
	{B_BOOL_TYPE, "no_safemode", NULL},
	{B_MESSAGE_TYPE, "env", kEnvTemplate},
	{B_MESSAGE_TYPE, "job", kJobTemplate},
	{B_MESSAGE_TYPE, "service", kJobTemplate},
	{0, NULL, NULL}
};

/** @brief Template for conditional run blocks (then/else branches). */
const static settings_template kRunConditionalTemplate[] = {
	{B_STRING_TYPE, NULL, NULL, true, new RunConverter()},
	{0, NULL, NULL}
};

/** @brief Template defining the grammar for "run" directives with optional conditions. */
const static settings_template kRunTemplate[] = {
	{B_STRING_TYPE, NULL, NULL, true, new RunConverter()},
	{B_MESSAGE_TYPE, "if", kConditionTemplate},
	{B_MESSAGE_TYPE, "then", kRunConditionalTemplate},
	{B_MESSAGE_TYPE, "else", kRunConditionalTemplate},
	{0, NULL, NULL}
};

/** @brief Top-level template defining the overall launch daemon settings grammar. */
const static settings_template kSettingsTemplate[] = {
	{B_MESSAGE_TYPE, "target", kTargetTemplate},
	{B_MESSAGE_TYPE, "job", kJobTemplate},
	{B_MESSAGE_TYPE, "service", kJobTemplate},
	{B_MESSAGE_TYPE, "run", kRunTemplate},
	{0, NULL, NULL}
};


/** @brief Constructs the settings parser. */
SettingsParser::SettingsParser()
{
}


/**
 * @brief Parses a launch daemon configuration file into a BMessage.
 *
 * Uses DriverSettingsMessageAdapter to convert the driver-settings format
 * file at @a path into a structured BMessage according to kSettingsTemplate.
 *
 * @param path     Absolute filesystem path to the configuration file.
 * @param settings Output BMessage that will be populated with the parsed settings.
 * @return B_OK on success, or an error code on failure.
 */
status_t
SettingsParser::ParseFile(const char* path, BMessage& settings)
{
	DriverSettingsMessageAdapter adapter;
	return adapter.ConvertFromDriverSettings(path, kSettingsTemplate, settings);
}


#ifdef TEST_HAIKU


/**
 * @brief Parses a driver-settings string directly into a BMessage.
 *
 * Only available in test builds (TEST_HAIKU). Parses @a text as if it
 * were the contents of a driver-settings file.
 *
 * @param text     The settings text to parse.
 * @param settings Output BMessage that will be populated with the parsed settings.
 * @return B_OK on success, B_BAD_VALUE if parsing fails, or another error code.
 */
status_t
SettingsParser::Parse(const char* text, BMessage& settings)
{
	void* driverSettings = parse_driver_settings_string(text);
	if (driverSettings == NULL)
		return B_BAD_VALUE;

	DriverSettingsMessageAdapter adapter;
	status_t status = adapter.ConvertFromDriverSettings(
		*get_driver_settings(driverSettings), kSettingsTemplate, settings);

	unload_driver_settings(driverSettings);
	return status;
}


#endif	// TEST_HAIKU
