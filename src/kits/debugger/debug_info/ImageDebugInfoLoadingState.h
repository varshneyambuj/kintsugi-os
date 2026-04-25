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
 * MIT License. Copyright 2014, Haiku.
 * Original authors: Rene Gollent.
 */

/** @file ImageDebugInfoLoadingState.h
    @brief Per-image bookkeeping carried through the debug-info loading
           pipeline, including any backend-specific substate. */

#ifndef IMAGE_DEBUG_INFO_LOADING_STATE_H
#define IMAGE_DEBUG_INFO_LOADING_STATE_H

#include <Referenceable.h>


class SpecificImageDebugInfoLoadingState;


/** @brief Carries the backend index currently being attempted plus any
           backend-specific loading state used to surface user prompts. */
class ImageDebugInfoLoadingState {
public:
								ImageDebugInfoLoadingState();
	virtual						~ImageDebugInfoLoadingState();

			bool				HasSpecificDebugInfoLoadingState() const;
			/** @brief Returns the attached backend-specific state, if any. */
			SpecificImageDebugInfoLoadingState*
								GetSpecificDebugInfoLoadingState() const
									{ return fSpecificInfoLoadingState; }
			void				SetSpecificDebugInfoLoadingState(
									SpecificImageDebugInfoLoadingState* state);
									// note: takes over reference of passed
									// in state object.
			void				ClearSpecificDebugInfoLoadingState();

			bool				UserInputRequired() const;


			/** @brief Returns the SpecificTeamDebugInfo index currently being
			           processed. */
			int32				GetSpecificInfoIndex() const
									{ return fSpecificInfoIndex; }
			void				SetSpecificInfoIndex(int32 index);

private:
			BReference<SpecificImageDebugInfoLoadingState>
								fSpecificInfoLoadingState;
			int32				fSpecificInfoIndex;
};


#endif // IMAGE_DEBUG_INFO_LOADING_STATE_H
