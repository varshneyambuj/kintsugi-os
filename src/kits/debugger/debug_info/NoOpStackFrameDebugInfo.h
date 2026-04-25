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

/** @file NoOpStackFrameDebugInfo.h
    @brief Trivial StackFrameDebugInfo placeholder used when a frame has no
           debug data. */

#ifndef NO_OP_STACK_FRAME_DEBUG_INFO_H
#define NO_OP_STACK_FRAME_DEBUG_INFO_H


#include "StackFrameDebugInfo.h"


/** @brief Empty StackFrameDebugInfo subclass used as a non-null placeholder
           when no debug-info backend can describe a frame. */
class NoOpStackFrameDebugInfo : public StackFrameDebugInfo {
public:
								NoOpStackFrameDebugInfo();
	virtual						~NoOpStackFrameDebugInfo();

};


#endif	// NO_OP_STACK_FRAME_DEBUG_INFO_H
