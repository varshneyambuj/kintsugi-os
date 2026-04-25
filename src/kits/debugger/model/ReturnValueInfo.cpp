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
 *   Copyright 2013, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ReturnValueInfo.cpp
 * @brief Implementation of ReturnValueInfo, a captured snapshot of the CPU
 *        state at the moment a tracked function returned.
 *
 * The debugger uses this object to defer computation of a function's return
 * value until the user inspects it: it pairs the return-instruction address
 * with the CpuState observed when the call site stepped out.
 */


#include "ReturnValueInfo.h"

#include "CpuState.h"


/**
 * @brief Constructs an empty ReturnValueInfo with no associated CPU state.
 */
ReturnValueInfo::ReturnValueInfo()
	:
	BReferenceable(),
	fAddress(0),
	fState(NULL)
{
}


/**
 * @brief Constructs a ReturnValueInfo that owns a reference to @a state.
 *
 * @param address Address of the call/return site whose value was captured.
 * @param state   CpuState snapshot at the return instant; reference is acquired.
 */
ReturnValueInfo::ReturnValueInfo(target_addr_t address, CpuState* state)
	:
	BReferenceable(),
	fAddress(address),
	fState(state)
{
	state->AcquireReference();
}


/**
 * @brief Releases the held CpuState reference, if any.
 */
ReturnValueInfo::~ReturnValueInfo()
{
	if (fState != NULL)
		fState->ReleaseReference();
}


/**
 * @brief Replaces the captured address and CPU state.
 *
 * The previously held CpuState reference is released and a new one acquired.
 *
 * @param address New call/return-site address.
 * @param state   Replacement CpuState snapshot; reference is acquired.
 */
void
ReturnValueInfo::SetTo(target_addr_t address, CpuState* state)
{
	fAddress = address;

	if (fState != NULL)
		fState->ReleaseReference();

	fState = state;
	fState->AcquireReference();
}
