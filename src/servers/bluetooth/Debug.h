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
 *   Copyright 2016, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Augustin Cavalier, <waddlesplash>
 */

/** @file Debug.h
 *  @brief Tracing macro used by the Bluetooth server. */

#ifndef _BLUETOOTH_SERVER_DEBUG_H
#define _BLUETOOTH_SERVER_DEBUG_H

//#ifdef TRACE_BLUETOOTH_SERVER
#if 1
/** @brief Bluetooth-server trace macro; expands to printf when tracing is enabled. */
#	define TRACE_BT(x...) printf(x)
#else
#	define TRACE_BT(x)
#endif

#endif
