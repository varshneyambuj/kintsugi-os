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
 */


/**
 * @file PrintAddOnServerProtocol.cpp
 * @brief String constant definitions for the print add-on server IPC protocol.
 *
 * Defines all string keys used as BMessage field names and the application
 * signature of the print add-on server. These constants are shared between the
 * server application and PrintAddOnServer, the client-side proxy.
 *
 * @see PrintAddOnServer, PrintAddOnServerProtocol.h
 */


#include "PrintAddOnServerProtocol.h"

/** @brief Application signature of the print add-on server. */
const char* kPrintAddOnServerApplicationSignature =
	"application/x-vnd.haiku-print-addon-server";

/** @brief BMessage field name that carries the numeric result status code. */
const char* kPrintAddOnServerStatusAttribute = "status";

/** @brief BMessage field name that carries the printer driver add-on name. */
const char* kPrinterDriverAttribute = "driver";

/** @brief BMessage field name that carries the printer's display name. */
const char* kPrinterNameAttribute = "name";

/** @brief BMessage field name that carries the path to the printer spool folder. */
const char* kPrinterFolderAttribute = "folder";

/** @brief BMessage field name that carries the path to a print job spool file. */
const char* kPrintJobFileAttribute = "job";

/** @brief BMessage field name that carries the serialised print settings message. */
const char* kPrintSettingsAttribute = "settings";
