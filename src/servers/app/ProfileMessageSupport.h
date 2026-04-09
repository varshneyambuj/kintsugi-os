/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2007, Haiku, Inc. All rights reserved.
 * Authors:
 *		Stephan Aßmus <superstippi@gmx.de>
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file ProfileMessageSupport.h
 *  @brief Utility function for converting message codes to human-readable strings. */

#ifndef PROFILE_MESSAGE_SUPPORT_H
#define PROFILE_MESSAGE_SUPPORT_H


#include <String.h>


/** @brief Returns a human-readable name for the given 4-byte message code.
 *  @param code The 32-bit message 'what' code to look up.
 *  @return A constant string describing the message, or a generic fallback. */
const char* string_for_message_code(uint32 code);


#endif // PROFILE_MESSAGE_SUPPORT_H
