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

/** @file ImageDebugLoadingStateHandler.h
    @brief Abstract handler that drives interactive resolution of a
           backend-specific debug-info loading state. */

#ifndef IMAGE_DEBUG_LOADING_STATE_HANDLER_H
#define IMAGE_DEBUG_LOADING_STATE_HANDLER_H


#include <Referenceable.h>


class SpecificImageDebugInfoLoadingState;
class UserInterface;


/** @brief Reference-counted handler that detects whether it can act on a
           given backend loading state and prompts the user accordingly. */
class ImageDebugLoadingStateHandler : public BReferenceable {
public:
	virtual						~ImageDebugLoadingStateHandler();

	virtual	bool				SupportsState(
									SpecificImageDebugInfoLoadingState* state)
									= 0;

	virtual	void				HandleState(
									SpecificImageDebugInfoLoadingState* state,
									UserInterface* interface) = 0;
};


#endif	// IMAGE_DEBUG_LOADING_STATE_HANDLER_H
