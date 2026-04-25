/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2013, Haiku.
 * Original author: Ingo Weinhold <ingo_weinhold@gmx.de>.
 */

/** @file PackageManagerUtils.h
    @brief DIE/DIE_DETAILS macros that throw BFatalErrorException from package manager code. */

#ifndef PACKAGE_MANAGER_UTILS_H
#define PACKAGE_MANAGER_UTILS_H


#include <package/manager/Exceptions.h>


/** @brief Throws a BFatalErrorException with a printf-style message. */
#define DIE(...)											\
do {														\
	throw BFatalErrorException(__VA_ARGS__);				\
} while(0)


/** @brief Throws a BFatalErrorException carrying a structured details object. */
#define DIE_DETAILS(details, ...)									\
do {																\
	throw BFatalErrorException(__VA_ARGS__).SetDetails(details);	\
} while(0)


#endif	// PACKAGE_MANAGER_UTILS_H
