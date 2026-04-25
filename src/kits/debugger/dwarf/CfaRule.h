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

/** @file CfaRule.h
    @brief Recovery rule types for the CFA and individual registers in DWARF CFI. */

#ifndef CFA_RULE_H
#define CFA_RULE_H


#include "Types.h"


/**
 * @brief Kind of register recovery rule produced by the CFI interpreter.
 *
 * Mirrors the rule types defined in the DWARF v5 specification, section 6.4.
 */
enum cfa_rule_type {
	CFA_RULE_UNDEFINED,
	CFA_RULE_SAME_VALUE,
	CFA_RULE_LOCATION_OFFSET,
	CFA_RULE_VALUE_OFFSET,
	CFA_RULE_REGISTER,
	CFA_RULE_LOCATION_EXPRESSION,
	CFA_RULE_VALUE_EXPRESSION
};


/**
 * @brief Kind of canonical-frame-address (CFA) rule.
 *
 * The CFA can be defined either as register+offset or as the result of a
 * DWARF expression.
 */
enum cfa_cfa_rule_type {
	CFA_CFA_RULE_UNDEFINED,
	CFA_CFA_RULE_REGISTER_OFFSET,
	CFA_CFA_RULE_EXPRESSION
};


/**
 * @brief Pointer/length pair describing a DWARF expression byte block.
 */
struct CfaExpression {
	const void*	block;
	size_t		size;
};


/**
 * @brief Per-register recovery rule (location, value, expression, ...).
 */
class CfaRule {
public:
	inline						CfaRule();

			cfa_rule_type		Type() const	{ return fType; }

			int64				Offset() const		{ return fOffset; }
			uint32				Register() const	{ return fRegister; }
			const CfaExpression& Expression() const	{ return fExpression; }

	inline	void				SetToUndefined();
	inline	void				SetToSameValue();
	inline	void				SetToLocationOffset(int64 offset);
	inline	void				SetToValueOffset(int64 offset);
	inline	void				SetToRegister(uint32 reg);
	inline	void				SetToLocationExpression(const void* block,
									size_t size);
	inline	void				SetToValueExpression(const void* block,
									size_t size);

private:
			cfa_rule_type		fType;
			union {
				int64			fOffset;
				uint32			fRegister;
				CfaExpression	fExpression;
			};
};


/**
 * @brief Recovery rule for the canonical frame address itself.
 */
class CfaCfaRule {
public:
	inline						CfaCfaRule();

			cfa_cfa_rule_type	Type() const	{ return fType; }

			uint64				Offset() const
									{ return fRegisterOffset.offset; }
			uint32				Register() const
									{ return fRegisterOffset.reg; }
			const CfaExpression& Expression() const	{ return fExpression; }

	inline	void				SetToUndefined();
	inline	void				SetToRegisterOffset(uint32 reg, uint64 offset);
	inline	void				SetToExpression(const void* block, size_t size);

	inline	void				SetRegister(uint32 reg);
	inline	void				SetOffset(uint64 offset);

private:
			cfa_cfa_rule_type	fType;
			union {
				struct {
					uint64		offset;
					uint32		reg;
				} 				fRegisterOffset;
				CfaExpression	fExpression;
			};
};


// #pragma mark - CfaRule


CfaRule::CfaRule()
	:
	fType(CFA_RULE_UNDEFINED)
{
}


void
CfaRule::SetToUndefined()
{
	fType = CFA_RULE_UNDEFINED;
}


void
CfaRule::SetToSameValue()
{
	fType = CFA_RULE_SAME_VALUE;
}


void
CfaRule::SetToLocationOffset(int64 offset)
{
	fType = CFA_RULE_LOCATION_OFFSET;
	fOffset = offset;
}


void
CfaRule::SetToValueOffset(int64 offset)
{
	fType = CFA_RULE_VALUE_OFFSET;
	fOffset = offset;
}


void
CfaRule::SetToRegister(uint32 reg)
{
	fType = CFA_RULE_REGISTER;
	fRegister = reg;
}


void
CfaRule::SetToLocationExpression(const void* block, size_t size)
{
	fType = CFA_RULE_LOCATION_EXPRESSION;
	fExpression.block = block;
	fExpression.size = size;
}


void
CfaRule::SetToValueExpression(const void* block, size_t size)
{
	fType = CFA_RULE_VALUE_EXPRESSION;
	fExpression.block = block;
	fExpression.size = size;
}


// #pragma mark - CfaCfaRule


CfaCfaRule::CfaCfaRule()
	:
	fType(CFA_CFA_RULE_UNDEFINED)
{
}


void
CfaCfaRule::SetToUndefined()
{
	fType = CFA_CFA_RULE_UNDEFINED;
}


void
CfaCfaRule::SetToRegisterOffset(uint32 reg, uint64 offset)
{
	fType = CFA_CFA_RULE_REGISTER_OFFSET;
	fRegisterOffset.reg = reg;
	fRegisterOffset.offset = offset;
}


void
CfaCfaRule::SetToExpression(const void* block, size_t size)
{
	fType = CFA_CFA_RULE_EXPRESSION;
	fExpression.block = block;
	fExpression.size = size;
}


void
CfaCfaRule::SetRegister(uint32 reg)
{
	fRegisterOffset.reg = reg;
}


void
CfaCfaRule::SetOffset(uint64 offset)
{
	fRegisterOffset.offset = offset;
}


#endif	// CFA_RULE_H
