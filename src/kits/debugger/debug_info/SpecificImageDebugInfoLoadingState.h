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

/** @file SpecificImageDebugInfoLoadingState.h
    @brief Abstract per-backend loading state held by
           ImageDebugInfoLoadingState. */

#ifndef SPECIFIC_IMAGE_DEBUG_INFO_LOADING_STATE_H
#define SPECIFIC_IMAGE_DEBUG_INFO_LOADING_STATE_H


#include <Referenceable.h>


/** @brief Reference-counted base class implemented by each debug-info
           backend that needs to carry per-image loading state. */
class SpecificImageDebugInfoLoadingState : public BReferenceable {
public:
								SpecificImageDebugInfoLoadingState();
	virtual						~SpecificImageDebugInfoLoadingState();

	virtual	bool				UserInputRequired() const = 0;
};


#endif // SPECIFIC_IMAGE_DEBUG_INFO_LOADING_STATE_H
