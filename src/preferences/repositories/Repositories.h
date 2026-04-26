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
 * MIT License. Copyright 2017, Haiku Inc.
 * Original authors: Brian Hill.
 */

/** @file Repositories.h
    @brief BApplication subclass for the Repositories preference panel. */

#ifndef REPOSITORIES_H
#define REPOSITORIES_H


#include <Application.h>

#include "RepositoriesWindow.h"


/**
 * @brief Top-level BApplication for the Repositories preference panel.
 *
 * Owns a single RepositoriesWindow created at startup; no message
 * forwarding is required at the application level.
 */
class RepositoriesApplication : public BApplication {
public:
							RepositoriesApplication();

private:
	RepositoriesWindow*		fWindow;
};


#endif
