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
 * MIT License. Copyright 2010-2017, Haiku, Inc.
 */

/** @file PrefletView.h
    @brief Tab view that hosts the General and Applications panes of the
           Notifications preflet. */

#ifndef _PREFLET_VIEW_H
#define _PREFLET_VIEW_H

#include <Messenger.h>
#include <TabView.h>

#include "GeneralView.h"

class SettingsHost;

/** @brief Window-level message that toggles the Defaults/Revert button
           strip's visibility. */
const int32 kShowButtons = '_SHB';
/** @brief Boolean field name carrying the desired button-strip visibility
           inside a kShowButtons message. */
#define kShowButtonsKey "showButtons"


/**
 * @brief Two-tab BTabView used as the Notifications preflet's central
 *        view.
 *
 * Hosts a GeneralView and a NotificationsView and posts a kShowButtons
 * notification on tab change so the host window can hide the global
 * Defaults / Revert buttons for panes that opt out of them.
 */
class PrefletView : public BTabView {
public:
						PrefletView(SettingsHost* host);

			BView*		CurrentPage();
			BView*		PageAt(int32 index);
	virtual	void		Select(int32 index);

private:
			GeneralView*	fGeneralView;
			BMessenger		fMessenger;
};

#endif // PREFLETVIEW_H
