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
 * MIT License. Copyright 2003-2006, Haiku.
 * Original authors: Sikosis, Jérôme Duval.
 */

/** @file Media.h
    @brief BApplication subclass that owns the MediaIcons resource cache and the MediaWindow. */

#ifndef MEDIA_H
#define MEDIA_H


#include "MediaWindow.h"

#include <Application.h>
#include <Catalog.h>
#include <Locale.h>


/**
 * @brief BApplication that drives the Media preflet.
 *
 * Loads the bitmap resources at construction time, restores the saved
 * window frame, and creates the main MediaWindow. InitCheck() forwards
 * the window's media-server connection status so the @c main() entry
 * point can decide whether to enter the run loop.
 */
class Media : public BApplication {
public:
								Media();

			status_t			InitCheck();

private:
			MediaIcons			fIcons;
			MediaWindow*		fWindow;
};

#endif	// MEDIA_H
