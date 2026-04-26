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
 * MIT License. Copyright 2006, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file PreferredAppMenu.h
 * @brief Helpers that build a "Preferred application" pop-up menu for a
 *        MIME type and resolve a user selection back to a signature.
 */

#ifndef PREFERRED_APP_MENU_H
#define PREFERRED_APP_MENU_H


#include <SupportDefs.h>

class BMenu;
class BMessage;
class BMimeType;
class BString;

void update_preferred_app_menu(BMenu* menu, BMimeType* type, uint32 what,
	const char* preferredFrom = NULL);

status_t retrieve_preferred_app(BMessage* message, bool sameAs,
	const char* forType, BString& preferredApp);

#endif	// PREFERRED_APP_MENU_H
