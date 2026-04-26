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
 * @file SettingsPane.cpp
 * @brief Implementation of SettingsPane, the BView base class shared by
 *        every tab in the Notifications preflet.
 */


#include <Message.h>

#include "SettingsPane.h"
#include "SettingsHost.h"


/**
 * @brief Constructs the pane and remembers its host for change callbacks.
 *
 * @param name  BHandler name passed through to BView.
 * @param host  Host receiving SettingsChanged callbacks; not owned.
 */
SettingsPane::SettingsPane(const char* name, SettingsHost* host)
	:
	BView(name, B_WILL_DRAW),
	fHost(host)
{
}


/**
 * @brief Forwards a "settings changed" notification to the host.
 *
 * @param showExample  When true the host should also fire a sample
 *                     notification.
 */
void
SettingsPane::SettingsChanged(bool showExample)
{
	fHost->SettingChanged(showExample);
}
