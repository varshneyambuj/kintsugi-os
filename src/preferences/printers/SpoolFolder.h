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

/** @file SpoolFolder.h
    @brief Folder subclass coupling a printer's spool dir to the UI. */

#ifndef _SPOOL_FOLDER_H
#define _SPOOL_FOLDER_H


#include "Jobs.h"


class PrintersWindow;
class PrinterItem;


/**
 * @brief Folder subclass that updates the Printers preflet UI in
 *        response to spool events.
 */
class SpoolFolder : public Folder {
public:
								SpoolFolder(PrintersWindow* window,
									PrinterItem* item,
									const BDirectory& spoolDir);
			/** @brief Returns the PrinterItem this watcher is bound to. */
			PrinterItem* 		Item() const { return fItem; }

protected:
			void				Notify(Job* job, int kind);

			PrintersWindow* 	fWindow;
			PrinterItem* 		fItem;
};


#endif // _SPOOL_FOLDER_H
