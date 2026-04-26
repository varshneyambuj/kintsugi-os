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

/** @file Globals.h
    @brief Free function declarations shared across the Printers preflet. */

#ifndef GLOBALS_H
#define GLOBALS_H


#include <Messenger.h>
#include <String.h>


BString ActivePrinterName();
status_t GetPrinterServerMessenger(BMessenger& msgr);

#endif // GLOBALS_H

