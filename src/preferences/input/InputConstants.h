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
 * MIT License. Copyright 2019, Haiku, Inc.
 * Original author: Preetpal Kaur.
 */

/** @file InputConstants.h
    @brief Shared message codes and layout constants for the Input preflet. */

#ifndef INPUT_MOUSE_CONSTANTS_H
#define INPUT_MOUSE_CONSTANTS_H

#include <SupportDefs.h>


/** @brief Window message: Defaults button pressed. */
const uint32 kMsgDefaults		= 'BTde';
/** @brief Window message: Revert button pressed. */
const uint32 kMsgRevert			= 'BTre';
/** @brief Window message: refresh device pages. */
const uint32 kMsgUpdate			= 'BTup';
/** @brief Mouse settings: double-click speed slider changed. */
const uint32 kMsgDoubleClickSpeed	= 'SLdc';
/** @brief Mouse settings: cursor speed slider changed. */
const uint32 kMsgCursorSpeed		= 'SLcs';
/** @brief Mouse settings: focus-follows-mouse mode pop-up changed. */
const uint32 kMsgFollowsMouseMode	= 'PUff';
/** @brief Mouse settings: focus mode pop-up changed. */
const uint32 kMsgMouseFocusMode		= 'PUmf';
/** @brief Mouse settings: accept-first-click check box toggled. */
const uint32 kMsgAcceptFirstClick	= 'PUaf';
/** @brief Mouse settings: mouse type pop-up changed. */
const uint32 kMsgMouseType		= 'PUmt';
/** @brief Mouse settings: button mapping changed. */
const uint32 kMsgMouseMap		= 'PUmm';
/** @brief Mouse settings: mouse speed slider changed. */
const uint32 kMsgMouseSpeed		= 'SLms';
/** @brief Mouse settings: acceleration factor slider changed. */
const uint32 kMsgAccelerationFactor	= 'SLma';
/** @brief Keyboard settings: button selecting a keyboard variant. */
const uint32 kMsgKeyboardButton		= 'BKdr';
/** @brief Keyboard settings: key repeat rate slider changed. */
const uint32 kMsgSliderrepeatrate	= 'SLrr';
/** @brief Keyboard settings: key repeat delay slider changed. */
const uint32 kMsgSliderdelayrate	= 'SLdr';

/** @brief Diagnostic message: error while reading or writing a setting. */
const uint32 kMsgErrordetect		= 'ERor';

/** @brief Outer padding around grouped panels, in pixels. */
const uint32 kBorderSpace = 10;
/** @brief Default spacing between adjacent items, in pixels. */
const uint32 kItemSpace = 7;

#endif	/* INPUT_MOUSE_CONSTANTS_H */
