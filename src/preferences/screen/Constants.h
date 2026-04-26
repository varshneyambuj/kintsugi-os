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
 * MIT License. Copyright 2001-2006, Haiku.
 * Original authors: Rafael Romo, Stefano Ceccherini (burton666@libero.it).
 */

/** @file Constants.h
    @brief Shared message codes and tunable constants for the Screen preferences app. */

#ifndef CONSTANTS_H
#define CONSTANTS_H


#include <ScreenDefs.h>
#include <SupportDefs.h>


// Messages
/** @brief Notify that the active workspace context (single vs. all) changed. */
static const uint32 WORKSPACE_CHECK_MSG = 'wchk';
/** @brief Launch the Backgrounds preferences app from the monitor preview. */
static const uint32 BUTTON_LAUNCH_BACKGROUNDS_MSG = 'blbk';
/** @brief Reset all controls to factory defaults. */
static const uint32 BUTTON_DEFAULTS_MSG = 'bdef';
/** @brief Revert pending changes back to the original screen mode. */
static const uint32 BUTTON_REVERT_MSG = 'brev';
/** @brief Apply the pending screen mode selection to the display. */
static const uint32 BUTTON_APPLY_MSG = 'bapl';
/** @brief Close the active modal dialog. */
static const uint32 BUTTON_DONE_MSG = 'bdon';
/** @brief Keep the newly applied mode (confirmation alert). */
static const uint32 BUTTON_KEEP_MSG = 'bkep';
/** @brief Undo the just-applied screen mode change. */
static const uint32 BUTTON_UNDO_MSG = 'bund';
/** @brief Resolution menu item selection. */
static const uint32 POP_RESOLUTION_MSG = 'pres';
/** @brief Color depth menu item selection. */
static const uint32 POP_COLORS_MSG = 'pclr';
/** @brief Refresh-rate menu item selection. */
static const uint32 POP_REFRESH_MSG = 'prfr';
/** @brief Custom refresh-rate dialog requested ("Other..." menu item). */
static const uint32 POP_OTHER_REFRESH_MSG = 'porf';
/** @brief Combine-displays mode menu selection. */
static const uint32 POP_COMBINE_DISPLAYS_MSG = 'pcdi';
/** @brief Swap-displays toggle menu selection. */
static const uint32 POP_SWAP_DISPLAYS_MSG = 'psdi';
/** @brief Use-laptop-panel toggle menu selection. */
static const uint32 POP_USE_LAPTOP_PANEL_MSG = 'pulp';
/** @brief TV standard menu selection. */
static const uint32 POP_TV_STANDARD_MSG = 'ptvs';
//static const uint32 UPDATE_DESKTOP_COLOR_MSG = 'udsc';
	// This is now defined in headers/private/preferences/ScreenDefs.h
/** @brief Notify the monitor preview view that desktop dimensions changed. */
static const uint32 UPDATE_DESKTOP_MSG = 'udsk';
/** @brief Slider value changed during a drag (continuous update). */
static const uint32 SLIDER_MODIFICATION_MSG = 'sldm';
/** @brief Slider invoked at end of interaction (commit). */
static const uint32 SLIDER_INVOKE_MSG = 'sldi';
/** @brief Brightness slider value changed; drives backlight update. */
static const uint32 SLIDER_BRIGHTNESS_MSG = 'brig';
/** @brief Custom refresh rate accepted from the "Other..." dialog. */
static const uint32 SET_CUSTOM_REFRESH_MSG = 'scrf';
/** @brief Dim countdown tick (legacy). */
static const uint32 DIM_COUNT_MSG = 'scrf';
/** @brief User chose "Keep" in the apply confirmation dialog. */
static const uint32 MAKE_INITIAL_MSG = 'mkin';
/** @brief Workspace layout (rows x columns) changed. */
static const uint32 kMsgWorkspaceLayoutChanged = 'wslc';
/** @brief Workspace columns spinner changed. */
static const uint32 kMsgWorkspaceColumnsChanged = 'wscc';
/** @brief Workspace rows spinner changed. */
static const uint32 kMsgWorkspaceRowsChanged = 'wsrc';

// Constants
/** @brief MIME signature of the Backgrounds preferences application. */
extern const char* kBackgroundsSignature;

/** @brief Lower bound (Hz) of the user-selectable refresh rate range. */
static const int32 gMinRefresh = 45;	// This is the minimum selectable refresh
/** @brief Upper bound (Hz) of the user-selectable refresh rate range. */
static const int32 gMaxRefresh = 140;	// This is the maximum selectable refresh

#endif	/* CONSTANTS_H */
