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
 * MIT License. Copyright 2017, Haiku, Inc.
 */

/** @file NotificationsConstants.h
    @brief Shared message codes and layout constants for the Notifications
           preference application. */

#ifndef NOTIFICATIONS_CONSTANTS_H
#define NOTIFICATIONS_CONSTANTS_H


// interface messages
/** @brief Restore default settings on every pane. */
const uint32 kDefaults = '_DFT';
/** @brief Revert each pane to the values loaded from disk. */
const uint32 kRevert = '_RVT';
/** @brief Persist the current settings to disk. */
const uint32 kApply = '_APY';
/** @brief Persist the current settings and post a sample notification. */
const uint32 kApplyWithExample = '_APE';
/** @brief Sent when the apps-list selection changes. */
const uint32 kApplicationSelected = '_ASL';
/** @brief "Add..." button pressed in the apps pane. */
const uint32 kAddApplication = '_AAP';
/** @brief File panel returned a new application reference. */
const uint32 kAddApplicationRef = '_AAR';
/** @brief "Remove" button pressed in the apps pane. */
const uint32 kRemoveApplication = '_RAP';
/** @brief Mute checkbox state changed for the selected app. */
const uint32 kMuteChanged = '_MCH';

// user interface
/** @brief Padding between the window edge and child views. */
const float kEdgePadding = 5.0;
/** @brief Padding around column titles in the applications list. */
const float kCLVTitlePadding = 8.0;


#endif	/* NOTIFICATIONS_CONSTANTS_H */
