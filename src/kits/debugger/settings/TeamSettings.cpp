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
 *   Copyright 2013-2015, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TeamSettings.cpp
 * @brief Aggregate, persistable description of a debugged team's settings.
 *
 * TeamSettings bundles breakpoint settings, file-manager mappings, signal
 * dispositions, and a list of UI-specific subsettings. It can either snapshot
 * a live Team or be constructed from a BMessage archive (using a
 * TeamUiSettingsFactory to resurrect UI subsetting subclasses).
 */


#include "TeamSettings.h"

#include <new>

#include <Message.h>

#include <AutoLocker.h>

#include "ArchivingUtils.h"
#include "BreakpointSetting.h"
#include "Team.h"
#include "TeamFileManagerSettings.h"
#include "TeamSignalSettings.h"
#include "TeamUiSettings.h"
#include "TeamUiSettingsFactory.h"
#include "UserBreakpoint.h"


/**
 * @brief Construct an empty TeamSettings with default sub-settings.
 */
TeamSettings::TeamSettings()
{
	fFileManagerSettings = new TeamFileManagerSettings();
	fSignalSettings = new TeamSignalSettings();
}


/**
 * @brief Copy-construct via assignment, rolling back on failure.
 *
 * @param other  Source TeamSettings whose state is duplicated.
 */
TeamSettings::TeamSettings(const TeamSettings& other)
{
	try {
		*this = other;
	} catch (...) {
		_Unset();
		throw;
	}
}


/**
 * @brief Destructor; releases all owned sub-settings and entries.
 */
TeamSettings::~TeamSettings()
{
	_Unset();
	delete fFileManagerSettings;
	delete fSignalSettings;
}


/**
 * @brief Snapshot a live Team into these settings.
 *
 * Records the team's name, all user breakpoints, the default signal
 * disposition and any custom signal mappings. The team is locked while
 * iterating, so this method must not be called while the caller already
 * holds a stronger lock that orders before Team's lock.
 *
 * @param team  Team whose live state is captured.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When a setting/entry could not be allocated.
 */
status_t
TeamSettings::SetTo(Team* team)
{
	_Unset();

	AutoLocker<Team> locker(team);

	fTeamName = team->Name();

	// add breakpoints
	for (UserBreakpointList::ConstIterator it
			= team->UserBreakpoints().GetIterator();
		UserBreakpoint* breakpoint = it.Next();) {
		BreakpointSetting* breakpointSetting
			= new(std::nothrow) BreakpointSetting;
		if (breakpointSetting == NULL)
			return B_NO_MEMORY;

		status_t error = breakpointSetting->SetTo(breakpoint->Location(),
			breakpoint->IsEnabled(), breakpoint->IsHidden(),
			breakpoint->Condition());
		if (error == B_OK && !fBreakpoints.AddItem(breakpointSetting))
			error = B_NO_MEMORY;
		if (error != B_OK) {
			delete breakpointSetting;
			return error;
		}
	}

	// add signal configuration

	fSignalSettings->SetDefaultSignalDisposition(
		team->DefaultSignalDisposition());

	const SignalDispositionMappings& mappings
		= team->GetSignalDispositionMappings();

	for (SignalDispositionMappings::const_iterator it = mappings.begin();
		it != mappings.end(); ++it) {
		status_t error = fSignalSettings->AddCustomSignalDisposition(
			it->first, it->second);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Initialise from a previously archived BMessage.
 *
 * Reads the team name, breakpoint entries, file-manager and signal
 * sub-settings, and uses @a factory to resurrect each UI sub-setting from
 * its archived form.
 *
 * @param archive  Source archive previously produced by WriteTo().
 * @param factory  Factory used to instantiate UI sub-settings subclasses.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When an entry could not be allocated.
 * @return Otherwise an underlying BMessage/factory error.
 */
status_t
TeamSettings::SetTo(const BMessage& archive,
	const TeamUiSettingsFactory& factory)
{
	_Unset();

	status_t error = archive.FindString("teamName", &fTeamName);
	if (error != B_OK)
		return error;

	// add breakpoints
	BMessage childArchive;
	for (int32 i = 0; archive.FindMessage("breakpoints", i, &childArchive)
			== B_OK; i++) {
		BreakpointSetting* breakpointSetting
			= new(std::nothrow) BreakpointSetting;
		if (breakpointSetting == NULL)
			return B_NO_MEMORY;

		error = breakpointSetting->SetTo(childArchive);
		if (error == B_OK && !fBreakpoints.AddItem(breakpointSetting))
			error = B_NO_MEMORY;
		if (error != B_OK) {
			delete breakpointSetting;
			return error;
		}
	}

	// add UI settings
	for (int32 i = 0; archive.FindMessage("uisettings", i, &childArchive)
		== B_OK; i++) {
		TeamUiSettings* setting = NULL;
		error = factory.Create(childArchive, setting);
		if (error == B_OK && !fUiSettings.AddItem(setting))
			error = B_NO_MEMORY;
		if (error != B_OK) {
			delete setting;
			return error;
		}
	}

	if (archive.FindMessage("filemanagersettings", &childArchive) == B_OK) {
		error = fFileManagerSettings->SetTo(childArchive);
		if (error != B_OK)
			return error;
	}

	if (archive.FindMessage("signalsettings", &childArchive) == B_OK) {
		error = fSignalSettings->SetTo(childArchive);
		if (error != B_OK)
			return error;
	}

	return B_OK;
}


/**
 * @brief Serialise the aggregate settings into @a archive.
 *
 * Writes the team name, every breakpoint, every UI sub-setting, the
 * file-manager settings, and the signal settings.
 *
 * @param archive  Out: receives the serialised representation.
 * @retval B_OK  On success.
 * @return       Otherwise the first BMessage::Add*() error encountered.
 */
status_t
TeamSettings::WriteTo(BMessage& archive) const
{
	status_t error = archive.AddString("teamName", fTeamName);
	if (error != B_OK)
		return error;

	BMessage childArchive;
	for (int32 i = 0; BreakpointSetting* breakpoint = fBreakpoints.ItemAt(i);
			i++) {
		error = breakpoint->WriteTo(childArchive);
		if (error != B_OK)
			return error;

		error = archive.AddMessage("breakpoints", &childArchive);
		if (error != B_OK)
			return error;
	}

	for (int32 i = 0; TeamUiSettings* uiSetting = fUiSettings.ItemAt(i);
			i++) {
		error = uiSetting->WriteTo(childArchive);
		if (error != B_OK)
			return error;

		error = archive.AddMessage("uisettings", &childArchive);
		if (error != B_OK)
			return error;
	}

	error = fFileManagerSettings->WriteTo(childArchive);
	if (error != B_OK)
		return error;

	error = archive.AddMessage("filemanagersettings", &childArchive);
	if (error != B_OK)
		return error;

	error = fSignalSettings->WriteTo(childArchive);
	if (error != B_OK)
		return error;

	error = archive.AddMessage("signalsettings", &childArchive);
	if (error != B_OK)
		return error;

	return B_OK;
}


/**
 * @brief Returns the number of stored breakpoints.
 *
 * @return Count of BreakpointSetting entries.
 */
int32
TeamSettings::CountBreakpoints() const
{
	return fBreakpoints.CountItems();
}


/**
 * @brief Returns the breakpoint at @a index.
 *
 * @param index  Zero-based index.
 * @return Pointer to the entry or @c NULL if @a index is out of range.
 */
const BreakpointSetting*
TeamSettings::BreakpointAt(int32 index) const
{
	return fBreakpoints.ItemAt(index);
}


/**
 * @brief Returns the number of stored UI sub-settings.
 *
 * @return Count of TeamUiSettings entries.
 */
int32
TeamSettings::CountUiSettings() const
{
	return fUiSettings.CountItems();
}


/**
 * @brief Returns the UI sub-setting at @a index.
 *
 * @param index  Zero-based index.
 * @return Pointer to the entry or @c NULL if @a index is out of range.
 */
const TeamUiSettings*
TeamSettings::UiSettingAt(int32 index) const
{
	return fUiSettings.ItemAt(index);
}


/**
 * @brief Looks up a UI sub-setting by its stable identifier.
 *
 * @param id  Identifier returned by TeamUiSettings::ID().
 * @return Matching entry, or @c NULL if no setting carries that id.
 */
const TeamUiSettings*
TeamSettings::UiSettingFor(const char* id) const
{
	for (int32 i = 0; i < fUiSettings.CountItems(); i++) {
		TeamUiSettings* settings = fUiSettings.ItemAt(i);
		if (strcmp(settings->ID(), id) == 0)
			return settings;
	}

	return NULL;
}


/**
 * @brief Takes ownership of @a settings and adds it to the UI list.
 *
 * @param settings  UI sub-setting subclass to add. Ownership transfers on
 *                  success.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When the entry could not be appended.
 */
status_t
TeamSettings::AddUiSettings(TeamUiSettings* settings)
{
	if (!fUiSettings.AddItem(settings))
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Deep copy-assignment.
 *
 * Resets prior state, copies the team name, deep-clones every breakpoint
 * and UI sub-setting, and copy-assigns the file-manager and signal
 * sub-settings. May throw @c std::bad_alloc on allocation failure.
 *
 * @param other  Source settings to copy.
 * @return Reference to @c *this.
 */
TeamSettings&
TeamSettings::operator=(const TeamSettings& other)
{
	if (this == &other)
		return *this;

	_Unset();

	fTeamName = other.fTeamName;

	for (int32 i = 0; BreakpointSetting* breakpoint
			= other.fBreakpoints.ItemAt(i); i++) {
		BreakpointSetting* clonedBreakpoint
			= new BreakpointSetting(*breakpoint);
		if (!fBreakpoints.AddItem(clonedBreakpoint)) {
			delete clonedBreakpoint;
			throw std::bad_alloc();
		}
	}

	for (int32 i = 0; TeamUiSettings* uiSetting
			= other.fUiSettings.ItemAt(i); i++) {
		TeamUiSettings* clonedSetting
			= uiSetting->Clone();
		if (!fUiSettings.AddItem(clonedSetting)) {
			delete clonedSetting;
			throw std::bad_alloc();
		}
	}

	*fFileManagerSettings = *other.fFileManagerSettings;

	*fSignalSettings = *other.fSignalSettings;

	return *this;
}


/**
 * @brief Returns the owned file-manager sub-settings.
 *
 * @return Pointer to the contained TeamFileManagerSettings; never @c NULL.
 */
TeamFileManagerSettings*
TeamSettings::FileManagerSettings() const
{
	return fFileManagerSettings;
}


/**
 * @brief Replaces the file-manager sub-settings by deep copy.
 *
 * @param settings  Source whose contents replace the current file-manager
 *                  sub-settings.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When the assignment throws.
 */
status_t
TeamSettings::SetFileManagerSettings(TeamFileManagerSettings* settings)
{
	try {
		*fFileManagerSettings = *settings;
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Returns the owned signal-settings sub-object.
 *
 * @return Pointer to the contained TeamSignalSettings; never @c NULL.
 */
TeamSignalSettings*
TeamSettings::SignalSettings() const
{
	return fSignalSettings;
}


/**
 * @brief Replaces the signal sub-settings by deep copy.
 *
 * @param settings  Source whose contents replace the current signal
 *                  sub-settings.
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  When the assignment throws.
 */
status_t
TeamSettings::SetSignalSettings(TeamSignalSettings* settings)
{
	try {
		*fSignalSettings = *settings;
	} catch (...) {
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Releases all per-breakpoint and per-UI entries and clears the name.
 *
 * Leaves @c fFileManagerSettings and @c fSignalSettings owned but emptied,
 * so the object can be re-initialised in place.
 */
void
TeamSettings::_Unset()
{
	for (int32 i = 0; BreakpointSetting* breakpoint = fBreakpoints.ItemAt(i);
			i++) {
		delete breakpoint;
	}

	for (int32 i = 0; TeamUiSettings* uiSetting = fUiSettings.ItemAt(i); i++)
		delete uiSetting;

	fBreakpoints.MakeEmpty();
	fUiSettings.MakeEmpty();
	fSignalSettings->Unset();

	fTeamName.Truncate(0);
}
