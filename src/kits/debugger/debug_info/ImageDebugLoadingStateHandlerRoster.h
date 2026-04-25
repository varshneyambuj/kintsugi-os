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

/** @file ImageDebugLoadingStateHandlerRoster.h
    @brief Process-wide registry that maps backend loading states to the
           handlers that can drive their interactive resolution. */

#ifndef IMAGE_DEBUG_LOADING_STATE_HANDLER_ROSTER_H
#define IMAGE_DEBUG_LOADING_STATE_HANDLER_ROSTER_H


#include <Locker.h>
#include <ObjectList.h>


class ImageDebugLoadingStateHandler;
class SpecificImageDebugInfoLoadingState;


typedef BObjectList<ImageDebugLoadingStateHandler> LoadingStateHandlerList;


/** @brief Holds reference-counted ImageDebugLoadingStateHandler instances
           and dispatches incoming SpecificImageDebugInfoLoadingState objects
           to the appropriate handler. */
class ImageDebugLoadingStateHandlerRoster {
public:
								ImageDebugLoadingStateHandlerRoster();
								~ImageDebugLoadingStateHandlerRoster();

	static	ImageDebugLoadingStateHandlerRoster*
								Default();
	static	status_t			CreateDefault();
	static	void				DeleteDefault();

			status_t			Init();
			status_t			RegisterDefaultHandlers();

			status_t			FindStateHandler(
									SpecificImageDebugInfoLoadingState* state,
									ImageDebugLoadingStateHandler*&
										_handler);
									// returns a reference

			bool				RegisterHandler(
									ImageDebugLoadingStateHandler*
										handler);
			void				UnregisterHandler(
									ImageDebugLoadingStateHandler*
										handler);

private:
			BLocker				fLock;
			LoadingStateHandlerList
								fStateHandlers;
	static	ImageDebugLoadingStateHandlerRoster*
								sDefaultInstance;
};


#endif	// IMAGE_DEBUG_LOADING_STATE_HANDLER_ROSTER_H

