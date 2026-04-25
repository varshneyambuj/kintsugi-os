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
 * @file FunctionInstance.cpp
 * @brief Implementation of FunctionInstance, a per-image realization of a
 *        Function paired with disassembled or sourceless code state.
 *
 * A single logical Function may have multiple FunctionInstance objects when
 * the same source-level entity exists in several loaded images. Each
 * instance keeps a reference to the FunctionDebugInfo that produced it and
 * tracks lazily loaded DisassembledCode.
 *
 * @see Function, FunctionDebugInfo, ImageDebugInfo
 */


#include "FunctionInstance.h"

#include <new>

#include "DisassembledCode.h"
#include "Function.h"
#include "FunctionID.h"
#include "ImageDebugInfo.h"
#include "LocatableFile.h"


/**
 * @brief Constructs a function instance bound to an image and its function
 *        debug info.
 *
 * @param imageDebugInfo      Image in which the function lives. The
 *                            reference is borrowed to avoid cyclic
 *                            ownership; see the inline TODO.
 * @param functionDebugInfo   Backend descriptor for the function;
 *                            reference acquired.
 */
FunctionInstance::FunctionInstance(ImageDebugInfo* imageDebugInfo,
	FunctionDebugInfo* functionDebugInfo)
	:
	fImageDebugInfo(imageDebugInfo),
	fFunction(NULL),
	fFunctionDebugInfo(functionDebugInfo),
	fSourceCode(NULL),
	fSourceCodeState(FUNCTION_SOURCE_NOT_LOADED)
{
	fFunctionDebugInfo->AcquireReference();
	// TODO: What about fImageDebugInfo? We must be careful regarding cyclic
	// references.
}


/**
 * @brief Destroys the instance and releases all owned references.
 */
FunctionInstance::~FunctionInstance()
{
	SetFunction(NULL);
	SetSourceCode(NULL, FUNCTION_SOURCE_NOT_LOADED);
	fFunctionDebugInfo->ReleaseReference();
}


/**
 * @brief Builds a stable identifier for this function instance.
 *
 * If a source file is known, a SourceFunctionID keyed on the file path and
 * function name is returned; otherwise an ImageFunctionID keyed on the
 * containing image name is used.
 *
 * @return A newly allocated FunctionID, or @c NULL on out-of-memory.
 * @note   Caller takes ownership of the returned object.
 */
FunctionID*
FunctionInstance::GetFunctionID() const
{
	if (LocatableFile* file = SourceFile()) {
		BString path;
		file->GetPath(path);
		return new(std::nothrow) SourceFunctionID(path, Name());
	}

	return new(std::nothrow) ImageFunctionID(
		GetImageDebugInfo()->GetImageInfo().Name(), Name());
}


/**
 * @brief Associates this instance with a logical Function.
 *
 * Releases any previous association and acquires a reference on @a function
 * when non-null.
 *
 * @param function  New owning Function, or @c NULL to detach.
 */
void
FunctionInstance::SetFunction(Function* function)
{
	if (fFunction != NULL)
		fFunction->ReleaseReference();

	fFunction = function;

	if (fFunction != NULL)
		fFunction->AcquireReference();
}


/**
 * @brief Caches a DisassembledCode object and updates loading state.
 *
 * No-op when the new source/state pair equals the current one. Notifies the
 * owning Function when the source actually changes so listeners can react.
 *
 * @param source  New disassembled code, or @c NULL.
 * @param state   Loading-state value associated with @a source.
 */
void
FunctionInstance::SetSourceCode(DisassembledCode* source,
	function_source_state state)
{
	if (source == fSourceCode && state == fSourceCodeState)
		return;

	if (fSourceCode != NULL)
		fSourceCode->ReleaseReference();

	fSourceCode = source;
	fSourceCodeState = state;

	if (fSourceCode != NULL)
		fSourceCode->AcquireReference();

	if (fFunction != NULL)
		fFunction->NotifySourceCodeChanged();
}
