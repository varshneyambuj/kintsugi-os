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
 * MIT License. Copyright 2009-2015, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file ValueLoader.h
    @brief Reads bytes from a debug target according to a piece-wise ValueLocation. */

#ifndef VALUE_LOADER_H
#define VALUE_LOADER_H


#include <String.h>

#include <Variant.h>


class Architecture;
class CpuState;
class TeamMemory;
class ValueLocation;


/**
 * @brief Bridges a typed ValueLocation to live target bytes via TeamMemory and CpuState.
 */
class ValueLoader {
public:
								ValueLoader(Architecture* architecture,
									TeamMemory* teamMemory,
									CpuState* cpuState);
									// cpuState can be NULL
								~ValueLoader();

			/** @brief Returns the architecture this loader is bound to. */
			Architecture*		GetArchitecture() const
									{ return fArchitecture; }

			status_t			LoadValue(ValueLocation* location,
									type_code valueType, bool shortValueIsFine,
									BVariant& _value);

			status_t			LoadRawValue(BVariant& location,
									size_t maxSize, void* _value);

			status_t			LoadStringValue(BVariant& location,
									size_t maxSize, BString& _value);

private:
			Architecture*		fArchitecture;
			TeamMemory*			fTeamMemory;
			CpuState*			fCpuState;
};


#endif	// VALUE_LOADER_H
