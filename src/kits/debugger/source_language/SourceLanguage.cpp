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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file SourceLanguage.cpp
 * @brief Default implementations for the SourceLanguage interface.
 *
 * SourceLanguage is the abstract per-language hook providing syntax
 * highlighting and expression evaluation for source code shown in the
 * debugger. The defaults declared here return @c NULL and
 * @c B_NOT_SUPPORTED, letting concrete languages override only the hooks
 * they actually implement.
 */


#include "SourceLanguage.h"


/**
 * @brief Virtual destructor.
 */
SourceLanguage::~SourceLanguage()
{
}


/**
 * @brief Returns the default (no) syntax highlighter.
 *
 * @return Always @c NULL. Subclasses override to supply a highlighter.
 */
SyntaxHighlighter*
SourceLanguage::GetSyntaxHighlighter() const
{
	return NULL;
}


/**
 * @brief Default implementation that refuses to evaluate expressions.
 *
 * @param expression    Expression text the user typed.
 * @param manager       Value-node manager rooted at the active frame.
 * @param info          Team type-information service.
 * @param _resultValue  Out: receives the evaluation result; left untouched.
 * @param _neededNode   Out: receives a node needing resolution; left untouched.
 * @retval B_NOT_SUPPORTED  The base class never evaluates expressions.
 */
status_t
SourceLanguage::EvaluateExpression(const BString& expression,
	ValueNodeManager* manager, TeamTypeInformation* info,
	ExpressionResult*& _resultValue, ValueNode*& _neededNode)
{
	return B_NOT_SUPPORTED;
}
