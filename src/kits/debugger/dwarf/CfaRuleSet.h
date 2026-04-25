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
 * MIT License. Copyright 2009, Ingo Weinhold.
 */

/** @file CfaRuleSet.h
    @brief Snapshot bundle of CFA and per-register recovery rules used by DWARF CFI. */

#ifndef CFA_RULE_SET_H
#define CFA_RULE_SET_H


#include "CfaRule.h"


/**
 * @brief Recovery rules for the canonical frame address and every register.
 *
 * One CfaRuleSet captures the unwind state at a single program-counter
 * location.  The CFI interpreter clones rule sets to support
 * DW_CFA_remember_state / DW_CFA_restore_state.
 */
class CfaRuleSet {
public:
								CfaRuleSet();
								~CfaRuleSet();

			status_t			Init(uint32 registerCount);
			CfaRuleSet*			Clone() const;

			CfaCfaRule*			GetCfaCfaRule()			{ return &fCfaCfaRule; }
			const CfaCfaRule*	GetCfaCfaRule() const	{ return &fCfaCfaRule; }

			CfaRule*			RegisterRule(uint32 index) const;

private:
			CfaCfaRule			fCfaCfaRule;
			CfaRule*			fRegisterRules;
			uint32				fRegisterCount;
};


#endif	// CFA_RULE_SET_H
