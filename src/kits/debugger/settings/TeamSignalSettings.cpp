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
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TeamSignalSettings.cpp
 * @brief Per-team configuration of default and per-signal dispositions.
 *
 * TeamSignalSettings stores the default disposition (ignore, stop, etc.) for
 * unknown signals plus a list of per-signal overrides. Values are persisted
 * inside a BMessage so they round-trip cleanly through TeamSettings archives.
 */


#include "TeamSignalSettings.h"


/** @brief BMessage field name for the default signal disposition. */
static const char* skDefaultSignalFieldName = "signal:default_disposition";
/** @brief BMessage field name for a custom-disposition entry's signal number. */
static const char* skSignalNumberFieldName = "signal:number";
/** @brief BMessage field name for a custom-disposition entry's disposition. */
static const char* skSignalDispositionFieldName = "signal:disposition";
/** @brief BMessage repeated-field name carrying custom signal disposition entries. */
static const char* skSignalSettingName = "signal:setting";


/**
 * @brief Construct an empty signal-settings object.
 */
TeamSignalSettings::TeamSignalSettings()
	:
	fValues()
{
}


/**
 * @brief Destructor.
 */
TeamSignalSettings::~TeamSignalSettings()
{
}


/**
 * @brief Copy values from @a other.
 *
 * @param other  Source settings whose value message is copied.
 * @return Reference to @c *this.
 */
TeamSignalSettings&
TeamSignalSettings::operator=(const TeamSignalSettings& other)
{
	fValues = other.fValues;

	return *this;
}


/**
 * @brief Returns the stable identifier for these settings.
 *
 * @return The constant string @c "Signals" used in archives.
 */
const char*
TeamSignalSettings::ID() const
{
	return "Signals";
}


/**
 * @brief Loads the settings from a BMessage archive.
 *
 * @param archive  Source archive previously produced by WriteTo().
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When BMessage assignment throws.
 */
status_t
TeamSignalSettings::SetTo(const BMessage& archive)
{
	try {
		fValues = archive;
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Serialises the settings into @a archive.
 *
 * @param archive  Out: receives a copy of the underlying BMessage.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When BMessage assignment throws.
 */
status_t
TeamSignalSettings::WriteTo(BMessage& archive) const
{
	try {
		archive = fValues;
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Clears the settings, restoring an empty state.
 */
void
TeamSignalSettings::Unset()
{
	fValues.MakeEmpty();
}


/**
 * @brief Sets the default disposition used when no override matches.
 *
 * @param disposition  One of the @c SIGNAL_DISPOSITION_* constants.
 */
void
TeamSignalSettings::SetDefaultSignalDisposition(int32 disposition)
{
	fValues.SetInt32(skDefaultSignalFieldName, disposition);
}


/**
 * @brief Returns the default disposition for unconfigured signals.
 *
 * @return The stored disposition or @c SIGNAL_DISPOSITION_IGNORE if unset.
 */
int32
TeamSignalSettings::DefaultSignalDisposition() const
{
	return fValues.GetInt32(skDefaultSignalFieldName,
		SIGNAL_DISPOSITION_IGNORE);
}


/**
 * @brief Counts the configured per-signal overrides.
 *
 * @return Number of custom disposition entries.
 */
int32
TeamSignalSettings::CountCustomSignalDispositions() const
{
	type_code type;
	int32 count = 0;

	if (fValues.GetInfo(skSignalSettingName, &type, &count) == B_OK)
		return count;

	return 0;
}


/**
 * @brief Appends a per-signal disposition override.
 *
 * @param signal       Signal number to override.
 * @param disposition  Disposition to apply for that signal.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When the override could not be stored.
 */
status_t
TeamSignalSettings::AddCustomSignalDisposition(int32 signal, int32 disposition)
{
	BMessage setting;
	if (setting.AddInt32(skSignalNumberFieldName, signal) != B_OK
		|| setting.AddInt32(skSignalDispositionFieldName, disposition) != B_OK
		|| fValues.AddMessage(skSignalSettingName, &setting) != B_OK) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Removes the override at the given index.
 *
 * @param index  Zero-based index of the override to remove.
 * @return B_OK on success or BMessage::RemoveData() error.
 */
status_t
TeamSignalSettings::RemoveCustomSignalDispositionAt(int32 index)
{
	return fValues.RemoveData(skSignalSettingName, index);
}


/**
 * @brief Reads back the override at @a index.
 *
 * @param index        Zero-based index to query.
 * @param signal       Out: signal number stored at @a index.
 * @param disposition  Out: disposition stored at @a index.
 * @return B_OK on success or the underlying BMessage error.
 */
status_t
TeamSignalSettings::GetCustomSignalDispositionAt(int32 index, int32& signal,
	int32& disposition) const
{
	BMessage setting;
	status_t error = fValues.FindMessage(skSignalSettingName, index, &setting);
	if (error != B_OK)
		return error;

	error = setting.FindInt32(skSignalNumberFieldName, &signal);
	if (error != B_OK)
		return error;

	return setting.FindInt32(skSignalDispositionFieldName, &disposition);
}


/**
 * @brief Produces a deep copy of these settings on the heap.
 *
 * @return Newly allocated copy, or @c NULL on allocation/assignment failure.
 *         Caller takes ownership.
 */
TeamSignalSettings*
TeamSignalSettings::Clone() const
{
	TeamSignalSettings* settings = new(std::nothrow)
		TeamSignalSettings();

	if (settings == NULL)
		return NULL;

	if (settings->SetTo(fValues) != B_OK) {
		delete settings;
		return NULL;
	}

	return settings;
}
