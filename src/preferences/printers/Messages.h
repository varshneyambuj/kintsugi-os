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
 * MIT License. Copyright 2001-2007, Haiku.
 * Original authors: Michael Pfeiffer, Philippe Houdoin.
 */

/** @file Messages.h
    @brief Internal BMessage 'what' codes used by the Printers preflet. */

#ifndef _MESSAGES_H
#define _MESSAGES_H


#include <SupportDefs.h>


/** @brief Sent by the Add button to open the AddPrinterDialog. */
const uint32 kMsgAddPrinter         = 'AddP';
/** @brief Posted by the dialog when it has finished, so the window can
    re-enable Add. */
const uint32 kMsgAddPrinterClosed   = 'APCl';
/** @brief Sent by the Remove button to delete the selected printer. */
const uint32 kMsgRemovePrinter      = 'RemP';
/** @brief Sent to mark the selected printer as the system default. */
const uint32 kMsgMakeDefaultPrinter = 'MDfP';
/** @brief Posted when the printer list selection changes. */
const uint32 kMsgPrinterSelected    = 'PSel';
/** @brief Sent by the Cancel job button to abort the selected job. */
const uint32 kMsgCancelJob          = 'CncJ';
/** @brief Sent by the Restart job button to requeue a failed job. */
const uint32 kMsgRestartJob         = 'RstJ';
/** @brief Posted when the job list selection changes. */
const uint32 kMsgJobSelected        = 'JSel';
/** @brief Sent to render and spool a test page on the selected printer. */
const uint32 kMsgPrintTestPage		= 'PtPg';

#endif // _MESSAGES_H
