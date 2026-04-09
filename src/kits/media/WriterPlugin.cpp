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
 *   Copyright 2009, Stephan Aßmus <superstippi@gmx.de>. All rights reserved.
 *   Copyright 2004, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file WriterPlugin.cpp
 *  @brief Implements the Writer base class and WriterPlugin interface for media container muxing plug-ins. */

#include "WriterPlugin.h"

#include <stdio.h>


/** @brief Default constructor. Initialises the write target and media plugin pointers to NULL. */
Writer::Writer()
	:
	fTarget(NULL),
	fMediaPlugin(NULL)
{
}


/** @brief Destructor. */
Writer::~Writer()
{
}


/** @brief Returns the data target this writer is writing to.
 *  @return Pointer to the BDataIO target; may be NULL if Setup() has not been called. */
BDataIO*
Writer::Target() const
{
	return fTarget;
}


/** @brief Associates a write target with this writer.
 *  @param target  Pointer to the BDataIO object to write to; ownership is NOT transferred. */
void
Writer::Setup(BDataIO* target)
{
	fTarget = target;
}


/** @brief Hook for performing implementation-specific actions identified by a perform code.
 *  @param code  An opaque code identifying the action to perform.
 *  @param data  Pointer to action-specific data; may be NULL.
 *  @return B_OK always (base implementation is a no-op). */
status_t
Writer::Perform(perform_code code, void* data)
{
	return B_OK;
}

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Writer::_ReservedWriter1() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Writer::_ReservedWriter2() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Writer::_ReservedWriter3() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Writer::_ReservedWriter4() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Writer::_ReservedWriter5() {}
