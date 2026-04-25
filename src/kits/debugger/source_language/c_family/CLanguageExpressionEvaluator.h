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
 * MIT License. Copyright 2006-2014 Haiku, Inc.
 * Original authors: Stephan Aßmus, Rene Gollent, John Scipione,
 *                   Ingo Weinhold.
 */

/** @file CLanguageExpressionEvaluator.h
    @brief Recursive-descent C/C++ expression evaluator used by the
           debugger's expression panel and conditional breakpoints. */

#ifndef C_LANGUAGE_EXPRESSION_EVALUATOR_H
#define C_LANGUAGE_EXPRESSION_EVALUATOR_H


#include <String.h>


namespace CLanguage {
	struct Token;
	class Tokenizer;
}

class BVariant;
class TeamTypeInformation;
class Type;
class ValueNode;
class ValueNodeChild;
class ValueNodeManager;
class Variable;


/**
 * @brief Thrown by the evaluator when a referenced ValueNode has not been
 *        resolved yet; the caller is expected to schedule a resolve job
 *        and re-run evaluation once the value is available.
 */
class ValueNeededException {
public:
	ValueNeededException(ValueNode* node)
		:
		value(node)
	{
	}

	ValueNode* value;
};


class ExpressionResult;
class Number;


/**
 * @brief Hand-written recursive-descent evaluator for C/C++ expressions.
 *
 * Produces an ExpressionResult from a textual expression in the context of
 * a value-node manager (variables) and a team type-information service
 * (type lookups). Used by the debugger's expression panel and to evaluate
 * conditional breakpoint expressions.
 */
class CLanguageExpressionEvaluator {

public:
								CLanguageExpressionEvaluator();
								~CLanguageExpressionEvaluator();

			ExpressionResult*	Evaluate(const char* expressionString,
									ValueNodeManager* manager,
									TeamTypeInformation* info);

private:
 			class InternalVariableID;
			class Operand;

private:
			Operand				_ParseSum();
			Operand				_ParseProduct();
			Operand				_ParseUnary();
			Operand				_ParseIdentifier(ValueNode* parentNode = NULL);
			Operand				_ParseAtom();

			void				_EatToken(int32 type);

			Operand				_ParseType(Type* baseType);
									// the passed in Type object
									// is expected to be the initial
									// base type that was recognized by
									// e.g. ParseIdentifier. This function then
									// takes care of handling any modifiers
									// that go with it, and returns a
									// corresponding final type.

			void				_RequestValueIfNeeded(
									const CLanguage::Token& token,
									ValueNodeChild* child);

			void				_GetNodeChildForPrimitive(
									const CLanguage::Token& token,
									const BVariant& value,
									ValueNodeChild*& _output) const;

private:
			CLanguage::Tokenizer* fTokenizer;
			TeamTypeInformation* fTypeInfo;
			ValueNodeManager*	fNodeManager;
};

#endif // C_LANGUAGE_EXPRESSION_EVALUATOR_H
