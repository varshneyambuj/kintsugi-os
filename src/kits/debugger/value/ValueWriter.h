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
 * MIT License. Copyright 2009-2015, Haiku.
 * Original authors: Ingo Weinhold, Rene Gollent.
 */

/** @file ValueWriter.h
    @brief Writes edited values back into a debug target through a piece-wise ValueLocation. */

#ifndef VALUE_WRITER_H
#define VALUE_WRITER_H


#include <OS.h>
#include <String.h>

#include <Variant.h>


class Architecture;
class CpuState;
class DebuggerInterface;
class ValueLocation;


/**
 * @brief Inverse of ValueLoader: pushes a BVariant back into target memory and registers.
 */
class ValueWriter {
public:
								ValueWriter(Architecture* architecture,
									DebuggerInterface* interface,
									CpuState* cpuState,
									thread_id targetThread);
									// cpuState can be NULL
								~ValueWriter();

			/** @brief Returns the architecture this writer is bound to. */
			Architecture*		GetArchitecture() const
									{ return fArchitecture; }

			status_t			WriteValue(ValueLocation* location,
									BVariant& value);

private:
			Architecture*		fArchitecture;
			DebuggerInterface*	fDebuggerInterface;
			CpuState*			fCpuState;
			thread_id			fTargetThread;
};


#endif	// VALUE_WRITER_H
