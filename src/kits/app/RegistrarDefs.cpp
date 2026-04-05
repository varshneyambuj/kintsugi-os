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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold (bonefish@users.sf.net)
 */

/** @file RegistrarDefs.cpp
 *  @brief Registrar service definitions and constants.
 *
 *  Defines string constants used for communicating with the registrar
 *  service, including the port name used by the registrar's application
 *  looper.
 */

//! API classes - registrar interface.


#include <AppMisc.h>
#include <RegistrarDefs.h>


namespace BPrivate {


/** @brief Port name for the registrar's application looper.
 *
 *  On the native platform this is "rAppLooperPort"; on the test platform
 *  it is prefixed with "haiku-test:" to avoid conflicts.
 */
#ifdef HAIKU_TARGET_PLATFORM_HAIKU
const char* kRAppLooperPortName = "rAppLooperPort";
#else
const char* kRAppLooperPortName = "haiku-test:rAppLooperPort";
#endif


}	// namespace BPrivate
