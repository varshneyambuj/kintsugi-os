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
 *   Copyright 2015-2016 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file btDebug.h
 *  @brief Lightweight kernel-land debug tracing macros for the Bluetooth stack. */

#ifndef _BTDEBUG_H
#define _BTDEBUG_H


/** @brief Emits a kernel debug message prefixed with "bt: " when DEBUG is defined;
 *         expands to nothing in release builds.
 *  @param x... printf-style format string and variadic arguments. */
#ifdef DEBUG
#   define TRACE(x...) dprintf("bt: " x)
#else
#   define TRACE(x...) ;
#endif

/** @brief Unconditionally emits a kernel debug error message prefixed with "bt: ".
 *  @param x... printf-style format string and variadic arguments. */
#define ERROR(x...) dprintf("bt: " x)

/** @brief Emits a TRACE message recording the name of the currently executing
 *         function; useful for entry-point tracing. */
#define CALLED(x...) TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


#endif /* _BTDEBUG_H */
