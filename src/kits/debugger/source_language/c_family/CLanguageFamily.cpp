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
 *   Copyright 2013-2014, Rene Gollent, rene@gollent.com.
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CLanguageFamily.cpp
 * @brief Shared SourceLanguage behaviour for C/C++.
 *
 * CLanguageFamily provides the C-family syntax highlighter and a thin
 * adapter around CLanguageExpressionEvaluator that turns thrown
 * ParseException / ValueNeededException objects into the EvaluateExpression
 * out-parameters expected by the SourceLanguage interface.
 */


#include "CLanguageFamily.h"

#include <new>

#include <stdlib.h>

#include "CLanguageExpressionEvaluator.h"
#include "CLanguageFamilySyntaxHighlighter.h"
#include "CLanguageTokenizer.h"
#include "ExpressionInfo.h"
#include "TeamTypeInformation.h"
#include "StringValue.h"
#include "Type.h"
#include "TypeLookupConstraints.h"


using CLanguage::ParseException;


/**
 * @brief Construct a CLanguageFamily base instance.
 */
CLanguageFamily::CLanguageFamily()
{
}


/**
 * @brief Destructor.
 */
CLanguageFamily::~CLanguageFamily()
{
}


/**
 * @brief Returns a freshly allocated C-family syntax highlighter.
 *
 * @return New CLanguageFamilySyntaxHighlighter, or @c NULL on
 *         allocation failure. Caller takes ownership.
 */
SyntaxHighlighter*
CLanguageFamily::GetSyntaxHighlighter() const
{
	return new(std::nothrow) CLanguageFamilySyntaxHighlighter();
}


/**
 * @brief Evaluates a C/C++ expression in the supplied frame context.
 *
 * Delegates to CLanguageExpressionEvaluator. Catches ParseException and
 * converts the diagnostic into a string-typed ExpressionResult, while
 * ValueNeededException is captured into @a _neededNode so the caller can
 * schedule a resolve job and retry.
 *
 * @param expression   Expression text to parse and evaluate.
 * @param manager      Value-node manager rooted at the active frame.
 * @param info         Team type-information service used for type lookups.
 * @param _output      Out: receives the evaluation result on success or a
 *                     parse-error message on @c B_BAD_DATA.
 * @param _neededNode  Out: receives the value node whose value must be
 *                     resolved before re-evaluation.
 * @retval B_OK         On successful evaluation or when @a _neededNode is
 *                      populated for a follow-up resolve.
 * @retval B_BAD_DATA   On a parse error.
 * @retval B_NO_MEMORY  When result allocation fails.
 */
status_t
CLanguageFamily::EvaluateExpression(const BString& expression,
	ValueNodeManager* manager, TeamTypeInformation* info,
	ExpressionResult*& _output, ValueNode*& _neededNode)
{
	_output = NULL;
	_neededNode = NULL;
	CLanguageExpressionEvaluator evaluator;
	try {
		_output = evaluator.Evaluate(expression, manager, info);
		return B_OK;
	} catch (ParseException& ex) {
		BString error;
		error.SetToFormat("Parse error at position %" B_PRId32 ": %s",
			ex.position, ex.message.String());
		StringValue* value = new(std::nothrow) StringValue(error.String());
		if (value == NULL)
			return B_NO_MEMORY;
		BReference<Value> valueReference(value, true);
		_output = new(std::nothrow) ExpressionResult();
		if (_output == NULL)
			return B_NO_MEMORY;
		_output->SetToPrimitive(value);
		return B_BAD_DATA;
	} catch (ValueNeededException& ex) {
		_neededNode = ex.value;
	}

	return B_OK;
}
