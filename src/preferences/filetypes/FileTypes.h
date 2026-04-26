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
 * MIT License. Copyright 2006, Axel Dörfler, axeld@pinc-software.de.
 */

/**
 * @file FileTypes.h
 * @brief Cross-cutting message constants, signature, and small helpers
 *        shared by every window and view in the FileTypes preference app.
 */

#ifndef FILE_TYPES_H
#define FILE_TYPES_H


#include <Alert.h>

class BFile;


/** @brief Application MIME signature for the FileTypes preference app. */
extern const char* kSignature;

/** @brief Request to show the system Open file panel for a target. */
static const uint32 kMsgOpenFilePanel = 'opFp';

/** @brief Request to open the main MIME-types browser window. */
static const uint32 kMsgOpenTypesWindow = 'opTw';
/** @brief Notification that the main MIME-types window closed. */
static const uint32 kMsgTypesWindowClosed = 'clTw';

/** @brief Request to open the application-types browser window. */
static const uint32 kMsgOpenApplicationTypesWindow = 'opAw';
/** @brief Notification that the application-types window closed. */
static const uint32 kMsgApplicationTypesWindowClosed = 'clAw';

/** @brief Notification that a per-application type window closed. */
static const uint32 kMsgTypeWindowClosed = 'cltw';
/** @brief Generic window-closed notification used for refcounting. */
static const uint32 kMsgWindowClosed = 'WiCl';

/** @brief Settings change broadcast carrying frame and view-state fields. */
static const uint32 kMsgSettingsChanged = 'SeCh';


// exported functions

extern bool is_application(BFile& file);
extern bool is_resource(BFile& file);
extern void error_alert(const char* message, status_t status = B_OK,
	alert_type type = B_WARNING_ALERT);

#endif	// FILE_TYPES_H
