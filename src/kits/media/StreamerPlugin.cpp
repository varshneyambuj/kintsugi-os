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
 *   Copyright 2016, Dario Casalinuovo. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file StreamerPlugin.cpp
 *  @brief Implements the Streamer base class for network/URL-based media streaming plug-ins. */

#include "StreamerPlugin.h"


/** @brief Default constructor. Initialises the media plugin pointer to NULL. */
Streamer::Streamer()
	:
	fMediaPlugin(NULL)
{
}


/** @brief Destructor. */
Streamer::~Streamer()
{
}


/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Streamer::_ReservedStreamer1() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Streamer::_ReservedStreamer2() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Streamer::_ReservedStreamer3() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Streamer::_ReservedStreamer4() {}
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
void Streamer::_ReservedStreamer5() {}
