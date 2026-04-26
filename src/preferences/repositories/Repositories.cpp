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
 *   Copyright 2017 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Brian Hill
 */


/**
 * @file Repositories.cpp
 * @brief BApplication entry point for the Repositories preference panel.
 *
 * Owns a single RepositoriesWindow which manages the user's package
 * repository configuration on top of the package_daemon.
 *
 * @see RepositoriesWindow
 */


#include "Repositories.h"

#include <Catalog.h>

#include "constants.h"

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "RepositoriesApplication"

/** @brief MIME application signature used to register Repositories with
    the registrar. */
const char* kAppSignature = "application/x-vnd.Haiku-Repositories";


/**
 * @brief Constructs the BApplication and creates the main window.
 *
 * The window is shown by RepositoriesWindow's own constructor, so no
 * additional Show() call is needed here.
 */
RepositoriesApplication::RepositoriesApplication()
	:
	BApplication(kAppSignature)
{
	fWindow = new RepositoriesWindow();
}


/**
 * @brief Process entry point.
 *
 * Constructs the application and runs the BApplication message loop.
 *
 * @return Always 0 once the loop exits.
 */
int
main()
{
	RepositoriesApplication myApp;
	myApp.Run();
	return 0;
}
