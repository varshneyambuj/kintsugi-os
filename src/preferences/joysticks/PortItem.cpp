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
 *   Copyright 2008 Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Fredrik Modéen, fredrik@modeen.se
 */


/**
 * @file PortItem.cpp
 * @brief BStringItem variant that remembers the joystick currently and
 *        previously bound to a game port row.
 */

#include "PortItem.h"

/**
 * @brief Creates a port list item with the given visible label.
 *
 * @param label Text shown in the list view.
 */
PortItem::PortItem(const char* label)
:BStringItem(label)
{}


/** @brief Destructor; nothing owned beyond the base class state. */
PortItem::~PortItem()
{}

/**
 * @brief Draws the row by delegating to BStringItem.
 *
 * @param owner    View doing the drawing.
 * @param frame    Rectangle to draw into.
 * @param complete When true, redraw the entire row.
 */
void
PortItem::DrawItem(BView *owner, BRect frame, bool complete)
{
	BStringItem::DrawItem(owner, frame, complete);
}

/**
 * @brief Returns the previously selected joystick name (before the most
 *        recent SetJoystickName() call).
 *
 * @return Old joystick name; an empty BString when no prior value exists.
 */
BString
PortItem::GetOldJoystickName()
{
	return fOldSelectedJoystick;
}


/**
 * @brief Returns the joystick currently bound to this port row.
 *
 * @return Current joystick name.
 */
BString
PortItem::GetJoystickName()
{
	return fSelectedJoystick;
}


/**
 * @brief Updates the joystick bound to this port and remembers the prior
 *        value.
 *
 * @param str New joystick name to associate with this port row.
 */
void
PortItem::SetJoystickName(BString str)
{
	fOldSelectedJoystick = fSelectedJoystick;
	fSelectedJoystick = str;
}
