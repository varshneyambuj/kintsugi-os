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
 *   Copyright 2011-2016, Haiku, Inc. All rights reserved.
 *   Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file FilterList.cpp
 * @brief Implements FilterList, the discovery-and-binding layer between
 *        on-disk filter add-ons and the FiltersConfigView UI.
 */


#include "FilterList.h"

#include <set>
#include <stdio.h>

#include <Directory.h>
#include <PathFinder.h>
#include <Path.h>


/**
 * @brief Constructs an empty list bound to the given direction.
 *
 * @param dir  @c kIncoming or @c kOutgoing; selects the add-on subdirectory
 *             scanned by Reload().
 */
FilterList::FilterList(direction dir)
	:
	fDirection(dir)
{
}


/** @brief Unloads every add-on image owned by the list. */
FilterList::~FilterList()
{
	_MakeEmpty();
}


/**
 * @brief Rescans the inbound or outbound add-on directories and rebuilds
 *        the FilterInfo list.
 *
 * User-installed add-ons override system add-ons of the same filename;
 * once a name has been seen, later occurrences in lower-priority paths
 * are skipped.
 */
void
FilterList::Reload()
{
	_MakeEmpty();

	std::set<BString> knownNames;

	BString subPath("mail_daemon/");
	subPath << (fDirection == kIncoming
		? "inbound_filters" : "outbound_filters");

	BStringList paths;
	BPathFinder().FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, subPath, paths);
	for (int32 i = 0; i < paths.CountStrings(); i++) {
		BPath path(paths.StringAt(i));

		BDirectory dir(path.Path());
		if (dir.InitCheck() != B_OK)
			continue;

		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			// Ignore entries we already had before (ie., user add-ons are
			// overriding system add-ons)
			if (knownNames.find(entry.Name()) != knownNames.end())
				continue;

			if (_LoadAddOn(entry) == B_OK)
				knownNames.insert(entry.Name());
		}
	}
}


/**
 * @brief Returns the number of discovered filter add-ons.
 *
 * @return Count of FilterInfo entries.
 */
int32
FilterList::CountInfos() const
{
	return fList.size();
}


/**
 * @brief Returns the FilterInfo at @a index.
 *
 * @param index  Zero-based position; no bounds check is performed.
 * @return Reference into the underlying vector; valid until Reload().
 */
const FilterInfo&
FilterList::InfoAt(int32 index) const
{
	return fList[index];
}


/**
 * @brief Linearly searches for the FilterInfo whose entry_ref matches
 *        @a ref.
 *
 * @param ref  Add-on file ref typically taken from a stored
 *             BMailAddOnSettings.
 * @return Index of the matching entry, or @c -1 if no add-on with that
 *         ref is currently loaded.
 */
int32
FilterList::InfoIndexFor(const entry_ref& ref) const
{
	for (size_t i = 0; i < fList.size(); i++) {
		const FilterInfo& info = fList[i];
		if (info.ref == ref)
			return i;
	}
	return -1;
}


/**
 * @brief Returns the add-on's generic display name (no per-instance
 *        configuration mixed in).
 *
 * @param index            Filter index.
 * @param accountSettings  Read-only account context passed to the add-on.
 * @return Localised filter name; @c "-" when @a index is out of range.
 */
BString
FilterList::SimpleName(int32 index,
	const BMailAccountSettings& accountSettings) const
{
	return DescriptiveName(index, accountSettings, NULL);
}


/**
 * @brief Convenience overload that resolves @a ref before delegating.
 *
 * @param ref              Add-on file ref.
 * @param accountSettings  Read-only account context passed to the add-on.
 * @return Localised filter name; @c "-" when no add-on matches @a ref.
 */
BString
FilterList::SimpleName(const entry_ref& ref,
	const BMailAccountSettings& accountSettings) const
{
	return DescriptiveName(InfoIndexFor(ref), accountSettings, NULL);
}


/**
 * @brief Returns the add-on's per-instance display name as it knows how
 *        to render it for the current settings.
 *
 * @param index            Filter index.
 * @param accountSettings  Read-only account context.
 * @param settings         Optional per-instance settings; @c NULL for the
 *                         generic name.
 * @return Localised name; @c "-" when @a index is out of range.
 */
BString
FilterList::DescriptiveName(int32 index,
	const BMailAccountSettings& accountSettings,
	const BMailAddOnSettings* settings) const
{
	if (index < 0 || index >= CountInfos())
		return "-";

	const FilterInfo& info = InfoAt(index);
	return info.name(accountSettings, settings);
}


/**
 * @brief Convenience overload that resolves @a ref before delegating.
 *
 * @param ref              Add-on file ref.
 * @param accountSettings  Read-only account context.
 * @param settings         Optional per-instance settings.
 * @return Localised name; @c "-" if no add-on matches @a ref.
 */
BString
FilterList::DescriptiveName(const entry_ref& ref,
	const BMailAccountSettings& accountSettings,
	const BMailAddOnSettings* settings) const
{
	return DescriptiveName(InfoIndexFor(ref), accountSettings, settings);
}


/**
 * @brief Asks the matching add-on to instantiate a settings view for the
 *        given filter instance.
 *
 * @param accountSettings  Read-only account context.
 * @param settings         Per-instance settings whose AddOnRef() picks the
 *                         add-on.
 * @return Newly allocated BMailSettingsView owned by the caller, or
 *         @c NULL when the referenced add-on is no longer loaded.
 */
BMailSettingsView*
FilterList::CreateSettingsView(const BMailAccountSettings& accountSettings,
	const BMailAddOnSettings& settings)
{
	const entry_ref& ref = settings.AddOnRef();
	int32 index = InfoIndexFor(ref);
	if (index < 0)
		return NULL;

	const FilterInfo& info = InfoAt(index);
	return info.instantiateSettingsView(accountSettings, settings);
}


/**
 * @brief Unloads every loaded add-on image and clears the FilterInfo
 *        vector.
 */
void
FilterList::_MakeEmpty()
{
	for (size_t i = 0; i < fList.size(); i++) {
		FilterInfo& info = fList[i];
		unload_add_on(info.image);
	}
	fList.clear();
}


/**
 * @brief Loads the add-on at @a entry and resolves the two required
 *        exports.
 *
 * Successful adds are appended to @c fList. Add-ons that load but lack
 * one of the required symbols are unloaded and reported on stderr so
 * users can spot broken installs.
 *
 * @param entry  File entry pointing at the candidate add-on.
 * @retval B_OK              Add-on accepted and added.
 * @retval B_NAME_NOT_FOUND  Required symbol missing.
 * @retval (image error)     Negative image_id from @c load_add_on().
 */
status_t
FilterList::_LoadAddOn(BEntry& entry)
{
	FilterInfo info;

	BPath path(&entry);
	info.image = load_add_on(path.Path());
	if (info.image < 0)
		return info.image;

	status_t status = get_image_symbol(info.image,
		"instantiate_filter_settings_view", B_SYMBOL_TYPE_TEXT,
		(void**)&info.instantiateSettingsView);
	if (status == B_OK) {
		status = get_image_symbol(info.image, "filter_name", B_SYMBOL_TYPE_TEXT,
			(void**)&info.name);
	}
	if (status != B_OK) {
		fprintf(stderr, "Filter \"%s\" misses required hooks!\n", path.Path());
		unload_add_on(info.image);
		return B_NAME_NOT_FOUND;
	}

	entry.GetRef(&info.ref);
	fList.push_back(info);
	return B_OK;
}
