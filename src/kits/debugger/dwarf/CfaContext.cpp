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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CfaContext.cpp
 * @brief Mutable state used by the DWARF call-frame-information interpreter.
 *
 * CfaContext threads the runtime state of the CFI virtual machine: the
 * target program-counter that triggered unwinding, the current PC inside
 * the CFI program, code/data alignment factors lifted from the CIE, and
 * the active CfaRuleSet plus a stack of saved rule sets manipulated by
 * DW_CFA_remember_state / DW_CFA_restore_state.
 */


#include <new>

#include "CfaContext.h"


/**
 * @brief Constructs an empty CFA context with all fields zeroed.
 *
 * @ref Init must be called once the register count is known.
 */
CfaContext::CfaContext()
	:
	fTargetLocation(0),
	fLocation(0),
	fCodeAlignment(0),
	fDataAlignment(0),
	fReturnAddressRegister(0),
	fRuleSet(NULL),
	fInitialRuleSet(NULL),
	fRuleSetStack(10)
{
}


/**
 * @brief Destroys the context and frees the active and initial rule sets.
 */
CfaContext::~CfaContext()
{
	delete fRuleSet;
	delete fInitialRuleSet;
}


/**
 * @brief Records the unwind target PC and the initial CFI program location.
 *
 * @param targetLocation  Program counter for which unwind rules are sought.
 * @param initialLocation Starting PC of the CFI program (FDE @c initial_location).
 */
void
CfaContext::SetLocation(target_addr_t targetLocation,
	target_addr_t initialLocation)
{
	fTargetLocation = targetLocation;
	fLocation = initialLocation;
}


/**
 * @brief Allocates the active CfaRuleSet sized for the target's register file.
 *
 * @param registerCount Number of registers exposed by the target ABI.
 * @retval B_OK         Storage allocated and ready for use.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
CfaContext::Init(uint32 registerCount)
{
	fRuleSet = new(std::nothrow) CfaRuleSet;
	if (fRuleSet == NULL)
		return B_NO_MEMORY;

	return fRuleSet->Init(registerCount);
}


/**
 * @brief Snapshots the rules established by the CIE's initial instructions.
 *
 * The DW_CFA_restore opcode restores a single register's rule from this
 * saved snapshot.
 *
 * @retval B_OK         Snapshot saved successfully.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
CfaContext::SaveInitialRuleSet()
{
	fInitialRuleSet = fRuleSet->Clone();
	if (fInitialRuleSet == NULL)
		return B_NO_MEMORY;
	return B_OK;
}


/**
 * @brief Pushes a clone of the current rule set onto the remember-state stack.
 *
 * Implements DW_CFA_remember_state.
 *
 * @retval B_OK         Pushed successfully.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
CfaContext::PushRuleSet()
{
	CfaRuleSet* ruleSet = fRuleSet->Clone();
	if (ruleSet == NULL || !fRuleSetStack.AddItem(ruleSet)) {
		delete ruleSet;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Pops the most recently pushed rule set, replacing the active one.
 *
 * Implements DW_CFA_restore_state.
 *
 * @retval B_OK        Popped successfully.
 * @retval B_BAD_DATA  Stack was empty (malformed CFI program).
 */
status_t
CfaContext::PopRuleSet()
{
	if (fRuleSetStack.IsEmpty())
		return B_BAD_DATA;

	delete fRuleSet;
	fRuleSet = fRuleSetStack.RemoveItemAt(
		fRuleSetStack.CountItems() - 1);

	return B_OK;
}


/**
 * @brief Updates the current program counter inside the CFI program.
 *
 * @param location New PC after consuming a DW_CFA_advance_loc / set_loc opcode.
 */
void
CfaContext::SetLocation(target_addr_t location)
{
	fLocation = location;
}


/**
 * @brief Records the CIE's @c code_alignment_factor.
 *
 * @param alignment Code-alignment factor used to scale advance_loc operands.
 */
void
CfaContext::SetCodeAlignment(uint32 alignment)
{
	fCodeAlignment = alignment;
}


/**
 * @brief Records the CIE's @c data_alignment_factor.
 *
 * @param alignment Signed data-alignment factor used to scale offset operands.
 */
void
CfaContext::SetDataAlignment(int32 alignment)
{
	fDataAlignment = alignment;
}


/**
 * @brief Records the CIE's return-address register number.
 *
 * @param reg Architectural register number that holds the return address.
 */
void
CfaContext::SetReturnAddressRegister(uint32 reg)
{
	fReturnAddressRegister = reg;
}


/**
 * @brief Restores a single register's rule from the initial CIE snapshot.
 *
 * Implements DW_CFA_restore.  Silently does nothing if the register index
 * is out of range or if no initial snapshot has been saved.
 *
 * @param reg Architectural register whose rule should be reverted.
 */
void
CfaContext::RestoreRegisterRule(uint32 reg)
{
	if (CfaRule* rule = RegisterRule(reg)) {
		if (fInitialRuleSet != NULL)
			*rule = *fInitialRuleSet->RegisterRule(reg);
	}
}
