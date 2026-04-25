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
 * @file StackTrace.cpp
 * @brief Implementation of StackTrace, an ordered collection of StackFrame
 *        references representing one thread's call chain.
 *
 * StackTrace owns a strong reference to each appended StackFrame so that
 * inner frames remain valid until the trace is destroyed.
 */

#include "StackTrace.h"


/**
 * @brief Constructs an empty StackTrace.
 */
StackTrace::StackTrace()
{
}


/**
 * @brief Releases the held reference on every contained StackFrame.
 */
StackTrace::~StackTrace()
{
	for (int32 i = 0; StackFrame* frame = FrameAt(i); i++)
		frame->ReleaseReference();
}


/**
 * @brief Appends @a frame to the trace, taking ownership of its reference.
 *
 * On success the StackTrace assumes the caller's reference. On allocation
 * failure the reference is released so the caller need not handle it.
 *
 * @param frame StackFrame to append; non-NULL, caller passes one reference.
 * @return     True if the frame was appended; false on allocation failure.
 */
bool
StackTrace::AddFrame(StackFrame* frame)
{
	if (fStackFrames.AddItem(frame))
		return true;

	frame->ReleaseReference();
	return false;
}


/**
 * @brief Returns the number of frames currently in the trace.
 *
 * @return Frame count.
 */
int32
StackTrace::CountFrames() const
{
	return fStackFrames.CountItems();
}


/**
 * @brief Returns the frame at @a index in innermost-first order.
 *
 * @param index Zero-based index into the trace.
 * @return     The frame, or NULL if @a index is out of range.
 */
StackFrame*
StackTrace::FrameAt(int32 index) const
{
	return fStackFrames.ItemAt(index);
}
