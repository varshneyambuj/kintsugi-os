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
 * MIT License. Copyright 2008, Haiku.
 * Original authors: Fredrik Modéen.
 */

/** @file PortItem.h
    @brief BStringItem variant carrying the bound joystick name for a port. */

#ifndef PORT_ITEM_H
#define PORT_ITEM_H

#include <ListItem.h>
#include <String.h>

/**
 * @brief List row representing a single game port.
 *
 * Beyond the BStringItem text label, this class remembers the currently
 * bound joystick name and the previously bound one so that the UI can
 * undo or rewrite settings symlinks when the binding changes.
 */
class PortItem : public BStringItem {
	public:
		PortItem(const char* label);
		~PortItem();
		virtual void DrawItem(BView *owner, BRect frame, bool complete = false);
		BString 	GetOldJoystickName();
		BString 	GetJoystickName();
		void		SetJoystickName(BString str);
	protected:
		BString 	fOldSelectedJoystick;
		BString 	fSelectedJoystick;
};


#endif	/* MESSAGED_ITEM_H */

