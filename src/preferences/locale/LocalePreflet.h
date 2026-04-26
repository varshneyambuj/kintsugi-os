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
 * MIT License. Copyright 2005-2010, Axel Dörfler.
 */

/** @file LocalePreflet.h
    @brief Shared MIME signature and message codes for the Locale
           preference application. */

#ifndef LOCALE_PREFLET_H
#define LOCALE_PREFLET_H


#include <SupportDefs.h>


/** @brief MIME signature used to launch the Locale preference application. */
extern const char* kSignature;

/** @brief Application message: user accepted the prompt to restart Tracker
           and Deskbar after toggling filesystem translation. */
static const uint32 kMsgRestartTrackerAndDeskbar = 'Rstr';
/** @brief Window-level notification posted whenever any locale setting
           changes; enables the Revert button. */
static const uint32 kMsgSettingsChanged = 'SeCh';


#endif	/* LOCALE_PREFLET_H */

