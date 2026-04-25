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

/** @file CfaContext.h
    @brief Mutable VM state for the DWARF call-frame-information interpreter. */

#ifndef CFA_CONTEXT_H
#define CFA_CONTEXT_H


#include <ObjectList.h>

#include "CfaRuleSet.h"
#include "Types.h"


/**
 * @brief Mutable interpreter state used while replaying a CFI program.
 *
 * Owns the active CfaRuleSet, the saved initial-rule snapshot from the
 * CIE prologue, and the remember-state stack used by the corresponding
 * DW_CFA opcodes.
 */
class CfaContext {
public:
								CfaContext();
								~CfaContext();

			void				SetLocation(target_addr_t targetLocation,
									target_addr_t initialLocation);

			status_t			Init(uint32 registerCount);
			status_t			SaveInitialRuleSet();

			status_t			PushRuleSet();
			status_t			PopRuleSet();

			target_addr_t		TargetLocation() const
									{ return fTargetLocation; }

			target_addr_t		Location() const
									{ return fLocation; }
			void				SetLocation(target_addr_t location);

			uint32				CodeAlignment() const
									{ return fCodeAlignment; }
			void				SetCodeAlignment(uint32 alignment);

			int32				DataAlignment() const
									{ return fDataAlignment; }
			void				SetDataAlignment(int32 alignment);

			uint32				ReturnAddressRegister() const
									{ return fReturnAddressRegister; }
			void				SetReturnAddressRegister(uint32 reg);

			CfaCfaRule*			GetCfaCfaRule() const
									{ return fRuleSet->GetCfaCfaRule(); }
			CfaRule*			RegisterRule(uint32 index) const
									{ return fRuleSet->RegisterRule(index); }

			void				RestoreRegisterRule(uint32 reg);

private:
			typedef BObjectList<CfaRuleSet, true> RuleSetList;

private:
			target_addr_t		fTargetLocation;
			target_addr_t		fLocation;
			uint32				fCodeAlignment;
			int32				fDataAlignment;
			uint32				fReturnAddressRegister;
			CfaRuleSet*			fRuleSet;
			CfaRuleSet*			fInitialRuleSet;
			RuleSetList			fRuleSetStack;
};



#endif	// CFA_CONTEXT_H
