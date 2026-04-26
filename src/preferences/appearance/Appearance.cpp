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
 *   Copyright 2002-2006, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm (darkwyrm@earthlink.net)
 */


/**
 * @file Appearance.cpp
 * @brief Entry point for the Appearance preference application.
 *
 * Defines the AppearanceApplication class, a BApplication subclass that
 * constructs and shows the top-level AppearanceWindow at startup.
 *
 * @see AppearanceWindow
 */


#include "Appearance.h"
#include "AppearanceWindow.h"
#include <stdio.h>

#include <Catalog.h>
#include <Locale.h>


/**
 * @brief Constructs the Appearance application and shows its main window.
 *
 * Registers the application's MIME signature, instantiates the
 * AppearanceWindow at a default frame, and makes it visible.
 */
AppearanceApplication::AppearanceApplication(void)
 :	BApplication("application/x-vnd.Haiku-Appearance")
{
	fWindow = new AppearanceWindow(BRect(100, 100, 550, 420));
	fWindow->Show();
}


/**
 * @brief Process entry point for the Appearance preference application.
 *
 * Instantiates the AppearanceApplication object and runs its message loop
 * until the user requests it to quit.
 *
 * @return Always zero on normal termination.
 */
int
main(int, char**)
{
	AppearanceApplication myApplication;
	myApplication.Run();

	return(0);
}
