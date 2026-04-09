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
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file graphic_driver.h
 *  @brief C-level ioctl definitions for graphic device drivers. */

#if !defined(_GRAPHIC_DRIVER_H_)
#define _GRAPHIC_DRIVER_H_

#include <Drivers.h>

/* The API for driver access is C, not C++ */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief ioctl codes for the graphic driver interface. */
enum {
	B_GET_ACCELERANT_SIGNATURE = B_GRAPHIC_DRIVER_BASE
};

#ifdef __cplusplus
}
#endif

#endif
