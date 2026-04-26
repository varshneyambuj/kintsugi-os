/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2015, Haiku, Inc.
 * Original authors: Axel Dörfler.
 */

/** @file ServiceListItem.h
    @brief BListItem for one network service entry in the Network preflet's
           Services list. */

#ifndef SERVICE_LIST_ITEM_H
#define SERVICE_LIST_ITEM_H


#include <ListItem.h>
#include <NetworkSettings.h>
#include <NetworkSettingsAddOn.h>


using namespace BNetworkKit;


/**
 * @brief Single-line list row showing a network service's label and a
 *        right-aligned on/off indicator.
 *
 * Subscribes to BNetworkSettings notifications so the row's enabled/disabled
 * state stays in sync as services are toggled.
 */
class ServiceListItem : public BListItem,
	public BNetworkKit::BNetworkSettingsListener {
public:
								ServiceListItem(const char* name,
									const char* label,
									const BNetworkSettings& settings);
	virtual						~ServiceListItem();

	/** @brief Returns the human-readable label drawn in the row. */
			const char*			Label() const { return fLabel; }

	virtual	void				DrawItem(BView* owner,
									BRect bounds, bool complete);
	virtual	void				Update(BView* owner, const BFont* font);

	/** @brief Returns the service identifier this row represents. */
	inline	const char*			Name() const { return fName; }

	virtual	void				SettingsUpdated(uint32 type);

protected:
	virtual	bool				IsEnabled();

private:
			const char*			fName;
			const char*			fLabel;
			const BNetworkSettings&
								fSettings;

			BView*				fOwner;
			float				fLineOffset;
			bool				fEnabled;
};


#endif // SERVICE_LIST_ITEM_H
