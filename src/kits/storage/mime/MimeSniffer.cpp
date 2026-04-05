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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file MimeSniffer.cpp
 * @brief Abstract base class for MIME type sniffing implementations.
 *
 * MimeSniffer defines the interface that concrete sniffers must implement in
 * order to be used by the MIME subsystem.  It provides virtual GuessMimeType()
 * overloads that operate on filenames and on raw file buffers respectively.
 * This translation unit supplies the out-of-line virtual destructor required
 * to anchor the vtable.
 *
 * @see MimeSniffer
 */


#include <mime/MimeSniffer.h>


namespace BPrivate {
namespace Storage {
namespace Mime {


/**
 * @brief Destroys the MimeSniffer base object.
 */
MimeSniffer::~MimeSniffer()
{
}


} // namespace Mime
} // namespace Storage
} // namespace BPrivate
