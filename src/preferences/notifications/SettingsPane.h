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

/** @file SettingsPane.h
    @brief Base class for tabs hosted by the Notifications preflet, defining
           the Load/Save/Revert/Defaults contract. */

#ifndef _SETTINGS_PANE_H
#define _SETTINGS_PANE_H

#include <View.h>

class BNode;

class SettingsHost;


/**
 * @brief Common base for every tab in the Notifications preflet.
 *
 * Each pane knows how to (de)serialize itself into a BMessage, revert to
 * the last loaded values, and reset to factory defaults. Panes whose
 * Save() pushes changes immediately (e.g. the applications list) opt out
 * of the global Defaults/Revert buttons via UseDefaultRevertButtons().
 */
class SettingsPane : public BView {
public:
							SettingsPane(const char* name, SettingsHost* host);

	void					SettingsChanged(bool showExample);

	virtual status_t		Load(BMessage&) = 0;
	virtual	status_t		Save(BMessage&) = 0;
	virtual	status_t		Revert() = 0;
	virtual bool			RevertPossible() = 0;
	virtual status_t		Defaults() = 0;
	virtual bool			DefaultsPossible() = 0;
	virtual bool			UseDefaultRevertButtons() = 0;

protected:
			SettingsHost*	fHost;
};

#endif // _SETTINGS_PANE_H
