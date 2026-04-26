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
 *   Copyright 2001-2010, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */


/**
 * @file SpoolFolder.cpp
 * @brief Glue between a printer's spool directory and the PrintersWindow.
 *
 * Subclasses Folder so the framework's job-watching logic translates spool
 * directory events into AddJob / RemoveJob / UpdateJob calls on the
 * Printers preflet window.
 */


#include "SpoolFolder.h"

#include "Jobs.h"
#include "PrintersWindow.h"
//#include "pr_server.h"


/**
 * @brief Constructs a spool watcher rooted at @a spoolDir.
 *
 * @param window   PrintersWindow whose UI should be updated on events.
 * @param item     PrinterItem the watcher belongs to.
 * @param spoolDir Spool directory for the printer.
 */
SpoolFolder::SpoolFolder(PrintersWindow* window, PrinterItem* item,
	const BDirectory& spoolDir)
	:
	Folder(NULL, window, spoolDir),
	fWindow(window),
	fItem(item)
{
}


/**
 * @brief Folder callback: translates spool events into UI updates.
 *
 * Forwards kJobAdded / kJobRemoved / kJobAttrChanged to the matching
 * PrintersWindow methods.
 *
 * @param job  Job whose state changed.
 * @param kind Event identifier (one of the Folder framework's
 *             kJob* constants).
 */
void
SpoolFolder::Notify(Job* job, int kind)
{
	switch (kind) {
		case kJobAdded:
			fWindow->AddJob(this, job);
			break;
		case kJobRemoved:
			fWindow->RemoveJob(this, job);
			break;
		case kJobAttrChanged:
			fWindow->UpdateJob(this, job);
			break;
		default:
			break;
	}
}
