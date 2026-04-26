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
 *   Copyright 2001-2016, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */


/**
 * @file Globals.cpp
 * @brief Helper free functions used across the Printers preflet.
 *
 * Centralises the small bits of print_server scripting that several
 * sources need: looking up the active printer name and opening a
 * BMessenger to print_server.
 */


#include "Globals.h"

#include <stdio.h>

#include <Roster.h>

#include "pr_server.h"


/**
 * @brief Asks print_server for the name of the currently active printer.
 *
 * @return BString containing the printer name, or an empty BString if
 *         print_server is unavailable or did not return a result.
 */
BString
ActivePrinterName()
{
	BMessenger msgr;
	if (GetPrinterServerMessenger(msgr) != B_OK)
		return BString();

	BMessage getNameOfActivePrinter(B_GET_PROPERTY);
	getNameOfActivePrinter.AddSpecifier("ActivePrinter");

	BMessage reply;
	msgr.SendMessage(&getNameOfActivePrinter, &reply);

	BString activePrinterName;
	reply.FindString("result", &activePrinterName);

	return activePrinterName;
}


/**
 * @brief Returns a BMessenger pointing at print_server.
 *
 * @param msgr Output messenger; valid only when the call returns B_OK.
 *
 * @retval B_OK    The messenger is valid and addresses print_server.
 * @retval B_ERROR print_server is not running or the messenger could not be
 *                 created.
 */
status_t
GetPrinterServerMessenger(BMessenger& msgr)
{
	msgr = BMessenger(PSRV_SIGNATURE_TYPE);
	return msgr.IsValid() ? B_OK : B_ERROR;
}
