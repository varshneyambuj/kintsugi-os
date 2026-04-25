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
 * @file CfaRuleSet.cpp
 * @brief Implementation of CfaRuleSet, a snapshot of DWARF call-frame rules.
 *
 * A CfaRuleSet bundles the canonical-frame-address (CFA) rule together with
 * one rule per architectural register, capturing how each value can be
 * recovered at a given program-counter location during stack unwinding.
 * Rule sets are constructed and mutated by the call-frame-information (CFI)
 * interpreter as it walks DW_CFA_* opcodes inside a Common Information
 * Entry / Frame Description Entry (CIE/FDE).
 */


#include <string.h>

#include <new>

#include "CfaRuleSet.h"


/**
 * @brief Constructs an empty rule set with no register storage allocated.
 *
 * @ref Init must be called before the rule set is usable.
 */
CfaRuleSet::CfaRuleSet()
	:
	fRegisterRules(NULL),
	fRegisterCount(0)
{
}


/**
 * @brief Destroys the rule set and releases its register-rule array.
 */
CfaRuleSet::~CfaRuleSet()
{
	delete[] fRegisterRules;
}


/**
 * @brief Allocates the per-register rule array.
 *
 * @param registerCount Number of architectural registers that the target
 *                      ABI exposes; the rule set carries one CfaRule per
 *                      register.
 * @retval B_OK         Storage allocated successfully.
 * @retval B_NO_MEMORY  Allocation failed.
 */
status_t
CfaRuleSet::Init(uint32 registerCount)
{
	fRegisterRules = new(std::nothrow) CfaRule[registerCount];
	if (fRegisterRules == NULL)
		return B_NO_MEMORY;

	fRegisterCount = registerCount;

	return B_OK;
}


/**
 * @brief Returns a deep copy of this rule set.
 *
 * Used by the CFI interpreter to implement DW_CFA_remember_state, which
 * pushes the current rule set onto a stack so that DW_CFA_restore_state
 * can later pop it.
 *
 * @return Newly-allocated CfaRuleSet owned by the caller, or NULL on
 *         allocation failure.
 */
CfaRuleSet*
CfaRuleSet::Clone() const
{
	CfaRuleSet* other = new(std::nothrow) CfaRuleSet;
	if (other == NULL)
		return NULL;

	if (other->Init(fRegisterCount) != B_OK) {
		delete other;
		return NULL;
	}

	other->fCfaCfaRule = fCfaCfaRule;
	memcpy(other->fRegisterRules, fRegisterRules,
		sizeof(CfaRule) * fRegisterCount);

	return other;
}


/**
 * @brief Returns the rule describing how to recover register @a index.
 *
 * @param index Architectural register number.
 * @return Pointer to the per-register rule, or NULL if @a index is out of
 *         range for the configured register count.
 */
CfaRule*
CfaRuleSet::RegisterRule(uint32 index) const
{
	return index < fRegisterCount ? fRegisterRules + index : NULL;
}
