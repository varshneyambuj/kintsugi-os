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

/** @file DwarfLoadingStateHandler.h
    @brief Loading-state handler that resolves missing external DWARF
           debug-info files by prompting the user or installing the
           corresponding debug-info package. */

#ifndef DWARF_LOADING_STATE_HANDLER
#define DWARF_LOADING_STATE_HANDLER


#include "ImageDebugLoadingStateHandler.h"


namespace BPackageKit {
	class BPackageVersion;
}


class BString;


/** @brief Handler that interprets DwarfImageDebugInfoLoadingState and
           drives the user dialog (install / locate / skip) used to
           supply a missing external debug-info file. */
class DwarfLoadingStateHandler : public ImageDebugLoadingStateHandler {
public:
								DwarfLoadingStateHandler();
	virtual						~DwarfLoadingStateHandler();

	virtual	bool				SupportsState(
									SpecificImageDebugInfoLoadingState* state);

	virtual	void				HandleState(
									SpecificImageDebugInfoLoadingState* state,
									UserInterface* interface);

private:
			status_t			_GetMatchingDebugInfoPackage(
									const BString& debugFileName,
									BString& _packageName);

			status_t			_GetResolvableName(const BString& debugFileName,
									BString& _resolvableName,
									BPackageKit::BPackageVersion&
										_resolvableVersion);
};


#endif	// DWARF_LOADING_STATE_HANDLER_H
