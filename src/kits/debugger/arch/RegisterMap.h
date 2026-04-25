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
 * MIT License. Copyright 2009, Haiku.
 * Original authors: Ingo Weinhold.
 */

/** @file RegisterMap.h
    @brief Reference-counted base class for register-index translation tables. */

#ifndef REGISTER_MAP_H
#define REGISTER_MAP_H


#include <Referenceable.h>


/** @brief Abstract bidirectional map between native register indices and a foreign numbering. */
class RegisterMap : public BReferenceable {
public:
	virtual						~RegisterMap();

	virtual	int32				CountRegisters() const = 0;
	virtual	int32				MapRegisterIndex(int32 index) const = 0;
};


#endif	// REGISTER_MAP_H
