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
 * MIT License. Copyright 2009, Haiku.
 * Original authors: Ingo Weinhold.
 */

/** @file BasicFunctionDebugInfo.h
    @brief Symbol-table backed FunctionDebugInfo used as a fallback when no
           richer source-level info is available. */

#ifndef BASIC_FUNCTION_DEBUG_INFO_H
#define BASIC_FUNCTION_DEBUG_INFO_H

#include <String.h>

#include "FunctionDebugInfo.h"


/** @brief Minimal FunctionDebugInfo describing a function from its symbol
           record alone (address, size, mangled and pretty names). */
class BasicFunctionDebugInfo : public FunctionDebugInfo {
public:
								BasicFunctionDebugInfo(
									SpecificImageDebugInfo* imageDebugInfo,
									target_addr_t address,
									target_size_t size,
									const BString& name,
									const BString& prettyName);
	virtual						~BasicFunctionDebugInfo();

	virtual	SpecificImageDebugInfo* GetSpecificImageDebugInfo() const;
	virtual	target_addr_t		Address() const;
	virtual	target_size_t		Size() const;
	virtual	const BString&		Name() const;
	virtual	const BString&		PrettyName() const;

	virtual	bool				IsMain() const;

	virtual	LocatableFile*		SourceFile() const;
	virtual	SourceLocation		SourceStartLocation() const;
	virtual	SourceLocation		SourceEndLocation() const;

private:
			SpecificImageDebugInfo* fImageDebugInfo;
			target_addr_t		fAddress;
			target_size_t		fSize;
			const BString		fName;
			const BString		fPrettyName;
};


#endif	// BASIC_FUNCTION_DEBUG_INFO_H
