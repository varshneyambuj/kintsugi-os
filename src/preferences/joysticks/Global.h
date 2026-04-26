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
 * MIT License. Copyright 2007, Haiku.
 * Original authors: Oliver Ruiz Dorantes, Ryan Leavengood.
 */

/** @file Global.h
    @brief Internal BMessage 'what' constants shared across the Joysticks UI. */

#ifndef _GLOBAL_H
#define _GLOBAL_H


/* Application Messages */
/** @brief Sent when the user selects a game port row in the list. */
#define PORT_SELECTED  'pSeL'
/** @brief Sent when the user selects a controller row in the list. */
#define JOY_SELECTED   'jYSl'

/** @brief Sent when a game port row is double-clicked or invoked. */
#define PORT_INVOKE    'PInV'
/** @brief Sent when a controller row is double-clicked or invoked. */
#define JOY_INVOKE     'jInV'

/** @brief Toggles the disabled state of the currently selected port. */
#define DISABLEPORT		'pdis'
/** @brief Triggers an autodetect probe of the currently selected port. */
#define PROBE			'prob'
/** @brief Opens the calibration window for the connected joystick. */
#define CALIBRATE		'cali'

/** @brief Generic selection-changed notification reused by helpers. */
#define SELECTED  'sele'

#endif	/* _GLOBAL_H */

