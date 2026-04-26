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
 *   Copyright 2010-2017, Haiku, Inc. All Rights Reserved.
 *   Copyright 2009, Pier Luigi Fiorini.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Pier Luigi Fiorini, pierluigi.fiorini@gmail.com
 *       Brian Hill, supernova@tycho.email
 */


/**
 * @file PrefletView.cpp
 * @brief Implementation of PrefletView, the tab container for the
 *        Notifications preflet's General and Applications panes.
 */


#include <Catalog.h>
#include <CardLayout.h>
#include <GroupLayout.h>
#include <GroupLayoutBuilder.h>
#include <LayoutItem.h>
#include <Message.h>
#include <Window.h>

#include "NotificationsView.h"
#include "PrefletView.h"
#include "SettingsHost.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "PrefletView"


/**
 * @brief Constructs the tab view and adds the General and Applications
 *        pages.
 *
 * @param host  Settings host forwarded to each pane.
 */
PrefletView::PrefletView(SettingsHost* host)
	:
	BTabView("pages", B_WIDTH_FROM_WIDEST)
{
	// Pages
	fGeneralView = new GeneralView(host);
	NotificationsView* apps = new NotificationsView(host);

	// Page selector
	BTab* tab = new BTab();
	AddTab(fGeneralView, tab);
	tab->SetLabel(B_TRANSLATE("General"));

	tab = new BTab();
	AddTab(apps, tab);
	tab->SetLabel(B_TRANSLATE("Applications"));
}


/**
 * @brief Returns the view associated with the focused tab.
 *
 * @return Pointer to the focused BView; never NULL while a tab is focused.
 */
BView*
PrefletView::CurrentPage()
{
	return PageAt(FocusTab());
}


/**
 * @brief Returns the BView attached to the tab at @a index.
 *
 * @param index  Zero-based tab index.
 * @return Pointer to that tab's view.
 */
BView*
PrefletView::PageAt(int32 index)
{
	return TabAt(index)->View();
}


/**
 * @brief Selects the tab at @a index, and notifies the parent window
 *        whether to show the global Defaults/Revert buttons.
 *
 * The decision is delegated to SettingsPane::UseDefaultRevertButtons() so
 * panes that manage their own apply semantics can hide the strip.
 *
 * @param index  Zero-based tab index to focus.
 */
void
PrefletView::Select(int32 index)
{
	if (index == Selection())
		return;

	BTabView::Select(index);

	SettingsPane* pane = dynamic_cast<SettingsPane*>(PageAt(index));
	bool showButtons = (pane != NULL) && pane->UseDefaultRevertButtons();
	BMessage showMessage(kShowButtons);
	showMessage.AddBool(kShowButtonsKey, showButtons);
	Window()->PostMessage(&showMessage);
}
