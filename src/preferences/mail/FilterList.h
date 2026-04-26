/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2011-2015, Haiku, Inc. and Copyright 2011,
 * Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file FilterList.h
    @brief Catalogue of available mail-filter add-ons for one direction
           (inbound or outbound), used by FiltersConfigView to populate
           the Add menu and instantiate per-filter settings views. */

#ifndef FILTER_LIST_H
#define FILTER_LIST_H


#include <MailSettings.h>
#include <MailSettingsView.h>


/**
 * @brief Direction of a filter chain on a mail account.
 */
enum direction {
	kIncoming,
	kOutgoing
};


/**
 * @brief Loaded-add-on entry: image handle, file ref, and the two function
 *        pointers exported by every filter add-on.
 *
 * Held by FilterList in the order add-ons were discovered. The filter
 * add-on must export @c instantiate_filter_settings_view and
 * @c filter_name; the function pointers stored here are bound to those
 * symbols.
 */
struct FilterInfo {
	image_id		image;
	entry_ref		ref;
	BMailSettingsView* (*instantiateSettingsView)(
		const BMailAccountSettings& accountSettings,
		const BMailAddOnSettings& settings);
	BString			(*name)(
		const BMailAccountSettings& accountSettings,
		const BMailAddOnSettings* settings);
};


/**
 * @brief Discovered set of filter add-ons for one direction.
 *
 * Reload() rescans the standard add-on search paths; per-add-on accessors
 * resolve display names and create settings views by delegating to the
 * exported hooks recorded in FilterInfo.
 */
class FilterList {
public:
								FilterList(direction dir);
								~FilterList();

			void				Reload();

			int32				CountInfos() const;
			const FilterInfo&	InfoAt(int32 index) const;
			int32				InfoIndexFor(const entry_ref& ref) const;

			BString				SimpleName(int32 index,
									const BMailAccountSettings& settings) const;
			BString				SimpleName(const entry_ref& ref,
									const BMailAccountSettings& settings) const;
			BString				DescriptiveName(int32 index,
									const BMailAccountSettings& accountSettings,
									const BMailAddOnSettings* settings) const;
			BString				DescriptiveName(const entry_ref& ref,
									const BMailAccountSettings& accountSettings,
									const BMailAddOnSettings* settings) const;

			BMailSettingsView*	CreateSettingsView(
									const BMailAccountSettings& accountSettings,
									const BMailAddOnSettings& settings);

private:
			void				_MakeEmpty();
			status_t			_LoadAddOn(BEntry& entry);

private:
			direction			fDirection;
			std::vector<FilterInfo> fList;
};


#endif // FILTER_LIST_H
