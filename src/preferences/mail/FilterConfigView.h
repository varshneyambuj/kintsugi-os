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
 * MIT License. Copyright 2007-2016, Haiku, Inc.;
 * Copyright 2001-2002 Dr. Zoidberg Enterprises;
 * Copyright 2011, Clemens Zeidler <haiku@clemens-zeidler.de>.
 */

/** @file FilterConfigView.h
    @brief Pane that lists the inbound or outbound filters of a mail
           account, lets the user reorder them via drag-and-drop, and
           hosts each filter add-on's settings view inline. */

#ifndef FILTER_CONFIG_VIEW_H
#define FILTER_CONFIG_VIEW_H


#include <vector>

#include <Button.h>
#include <GroupView.h>
#include <ListView.h>
#include <MailSettings.h>
#include <Message.h>
#include <MenuField.h>

#include "FilterList.h"


class FilterSettingsView;


/**
 * @brief Filters tab for one mail account: switches between inbound and
 *        outbound chains, lists the configured filters, and embeds each
 *        filter add-on's settings view.
 */
class FiltersConfigView : public BGroupView {
public:
								FiltersConfigView(
									BMailAccountSettings& account);
								~FiltersConfigView();

			void				AttachedToWindow();
			void				DetachedFromWindow();
			void				MessageReceived(BMessage *msg);

private:
			BMailProtocolSettings* _MailSettings();
			::FilterList*		_FilterList();

			void				_SelectFilter(int32 index);
			void				_SetDirection(direction direction);
			void				_SaveConfig(int32 index);

private:
			BMailAccountSettings& fAccount;
			direction			fDirection;

			::FilterList		fInboundFilters;
			::FilterList		fOutboundFilters;

			BMenuField*			fChainsField;
			BListView*			fListView;
			BMenuField*			fAddField;
			BButton*			fRemoveButton;
			FilterSettingsView*	fFilterView;

			int32				fCurrentIndex;
};


#endif // FILTER_CONFIG_VIEW_H
