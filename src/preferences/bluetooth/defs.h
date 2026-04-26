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
 */

/** @file defs.h
    @brief Shared message codes, layout constants, and globals for the Bluetooth preflet. */

#ifndef DEFS_H_
#define DEFS_H_

#include <bluetooth/LocalDevice.h>

#include <bluetoothserver_p.h>


#define APPLY_SETTINGS 'aply'
#define REVERT_SETTINGS 'rvrt'
#define DEFAULT_SETTINGS 'dflt'
#define TRY_SETTINGS 'trys'

#define ATTRIBUTE_CHOSEN 'atch'
#define UPDATE_COLOR 'upcl'
#define DECORATOR_CHOSEN 'dcch'
#define UPDATE_DECORATOR 'updc'
#define UPDATE_COLOR_SET 'upcs'

#define SET_VISIBLE 		'sVis'
#define SET_DISCOVERABLE 	'sDis'
#define SET_AUTHENTICATION 	'sAth'

#define SET_UI_COLORS 'suic'
#define PREFS_CHOSEN 'prch'

// user interface
/** @brief Outer padding around grouped panels, in pixels. */
const uint32 kBorderSpace = 10;
/** @brief Default spacing between adjacent items, in pixels. */
const uint32 kItemSpace = 7;

/** @brief Inter-window message: append a discovered device to the remote list. */
static const uint32 kMsgAddToRemoteList = 'aDdL';
/** @brief Inter-window message: rebuild the local-device pop-up. */
static const uint32 kMsgRefresh = 'rFLd';

/** @brief Settings-view message: connection-policy pop-up changed. */
static const int32 kMsgSetConnectionPolicy = 'sCpo';
/** @brief Settings-view message: device-class identity pop-up changed. */
static const int32 kMsgSetDeviceClass = 'sDC0';
/** @brief Settings-view message: inquiry-time slider value changed. */
static const int32 kMsgSetInquiryTime = 'afEa';
/** @brief Settings-view message: a different LocalDevice was picked. */
static const int32 kMsgLocalSwitched = 'lDsW';

/** @brief Currently active LocalDevice shared across the preflet's views. */
extern LocalDevice* ActiveLocalDevice;

#endif
