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
 *   Copyright 2010, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file Notifications.cpp
 *  @brief Definitions of the on-disk settings keys used by the notification daemon. */


#include <Notifications.h>


// Settings constants
/** @brief Path of the notifications settings file inside the user settings directory. */
const char* kSettingsFile = "system/notifications";

// General settings
/** @brief Settings key: whether the notification daemon should auto-start at login. */
const char* kAutoStartName = "auto-start";
/** @brief Settings key: per-notification timeout in microseconds. */
const char* kTimeoutName = "timeout";

// Display settings
/** @brief Settings key: notification window width in pixels. */
const char* kWidthName = "width";
/** @brief Settings key: icon size in pixels. */
const char* kIconSizeName = "icon size";
/** @brief Settings key: screen corner the notification window docks to. */
const char* kNotificationPositionName = "notification position";
