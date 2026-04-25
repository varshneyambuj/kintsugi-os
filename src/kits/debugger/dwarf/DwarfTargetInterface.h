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
 * MIT License. Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 */

/** @file DwarfTargetInterface.h
    @brief Abstract interface used by DWARF evaluation to read inferior registers and memory. */

#ifndef DWARF_TARGET_INTERFACE_H
#define DWARF_TARGET_INTERFACE_H


#include <Referenceable.h>
#include <Variant.h>

#include "Types.h"


class CfaContext;
class Register;


/**
 * @brief Abstraction over the inferior process consulted by DWARF evaluators.
 *
 * Concrete debugger backends implement this interface to expose register
 * banks, memory reads, and CFA register-rule initialisation to the DWARF
 * expression evaluator and call-frame information unwinder.
 */
class DwarfTargetInterface : public BReferenceable {
public:
	virtual						~DwarfTargetInterface();

	virtual	uint32				CountRegisters() const = 0;
	virtual	uint32				RegisterValueType(uint32 index) const = 0;

	virtual	bool				GetRegisterValue(uint32 index,
									BVariant& _value) const = 0;
	virtual	bool				SetRegisterValue(uint32 index,
									const BVariant& value) = 0;
	virtual	bool				IsCalleePreservedRegister(uint32 index) const
									= 0;
	virtual status_t			InitRegisterRules(CfaContext& context) const
									= 0;

	virtual	bool				ReadMemory(target_addr_t address, void* buffer,
									size_t size) const = 0;
	virtual	bool				ReadValueFromMemory(target_addr_t address,
									uint32 valueType, BVariant& _value) const
										= 0;
	virtual	bool				ReadValueFromMemory(target_addr_t addressSpace,
									target_addr_t address,
									uint32 valueType, BVariant& _value) const
										= 0;
};


#endif	// DWARF_TARGET_INTERFACE_H
