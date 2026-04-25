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
 *   Copyright 2014, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ImageDebugInfoLoadingState.cpp
 * @brief Implementation of ImageDebugInfoLoadingState, the per-image
 *        bookkeeping object passed through the loading pipeline.
 *
 * The state carries an index identifying which SpecificTeamDebugInfo
 * backend is currently being tried plus an optional backend-specific
 * loading state object. The latter is what conveys "needs user input"
 * conditions back up to the UI layer.
 *
 * @see SpecificImageDebugInfoLoadingState
 */


#include "ImageDebugInfoLoadingState.h"

#include "SpecificImageDebugInfoLoadingState.h"


/**
 * @brief Constructs a fresh state with no backend selected and no
 *        backend-specific data.
 */
ImageDebugInfoLoadingState::ImageDebugInfoLoadingState()
	:
	fSpecificInfoLoadingState(),
	fSpecificInfoIndex(0)
{
}


/**
 * @brief Destroys the state; held BReference is automatically released.
 */
ImageDebugInfoLoadingState::~ImageDebugInfoLoadingState()
{
}


/**
 * @brief Reports whether a backend-specific loading state object is
 *        attached.
 *
 * @return @c true if a SpecificImageDebugInfoLoadingState is currently
 *         held.
 */
bool
ImageDebugInfoLoadingState::HasSpecificDebugInfoLoadingState() const
{
	return fSpecificInfoLoadingState.IsSet();
}


/**
 * @brief Takes over the supplied backend-specific loading state.
 *
 * @param state  Loading state object whose reference is now owned by this
 *               container.
 */
void
ImageDebugInfoLoadingState::SetSpecificDebugInfoLoadingState(
	SpecificImageDebugInfoLoadingState* state)
{
	fSpecificInfoLoadingState.SetTo(state, true);
}


/**
 * @brief Releases any attached backend-specific loading state.
 */
void
ImageDebugInfoLoadingState::ClearSpecificDebugInfoLoadingState()
{
	fSpecificInfoLoadingState = NULL;
}


/**
 * @brief Reports whether the loading flow currently needs user input.
 *
 * @return @c true if a backend state is attached and that state itself
 *         reports that user input is required; otherwise @c false.
 */
bool
ImageDebugInfoLoadingState::UserInputRequired() const
{
	if (HasSpecificDebugInfoLoadingState())
		return fSpecificInfoLoadingState->UserInputRequired();

	return false;
}


/**
 * @brief Records which SpecificTeamDebugInfo index is being processed.
 *
 * @param index  Backend index supplied by the orchestration layer.
 */
void
ImageDebugInfoLoadingState::SetSpecificInfoIndex(int32 index)
{
	fSpecificInfoIndex = index;
}
