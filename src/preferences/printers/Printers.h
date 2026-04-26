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
 * MIT License. Copyright 2001-2010, Haiku.
 * Original authors: Michael Pfeiffer.
 */

/** @file Printers.h
    @brief BApplication subclass for the Printers preference panel. */

#ifndef _PRINTERS_H
#define _PRINTERS_H


#include <Application.h>
#include <Catalog.h>

#include "ScreenSettings.h"


/** @brief MIME application signature used to register Printers with the
    registrar. */
#define PRINTERS_SIGNATURE	"application/x-vnd.Be-PRNT"


/**
 * @brief Top-level BApplication for the Printers preference panel.
 *
 * Owns no state of its own beyond the BApplication base class and a single
 * PrintersWindow created at startup. Acts as a relay for print_server
 * broadcasts to every open window.
 */
class PrintersApp : public BApplication {
			typedef BApplication Inherited;
public:
								PrintersApp();
			void				ReadyToRun();
			void				MessageReceived(BMessage* msg);
			void				ArgvReceived(int32 argc, char** argv);
};

#endif // _PRINTERS_H
