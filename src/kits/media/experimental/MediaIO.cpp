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
 *   Copyright 2016 Dario Casalinuovo. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file MediaIO.cpp
 * @brief Implementation of BMediaIO, the abstract streaming I/O base class.
 *
 * BMediaIO serves as the abstract interface that media decoders and encoders
 * use to access their data source or sink in a position-independent manner.
 * Concrete subclasses such as BAdapterIO implement the actual read/write
 * behaviour for specific streaming scenarios.
 *
 * @see BAdapterIO, BPositionIO
 */


#include <MediaIO.h>


/**
 * @brief Constructs a default BMediaIO instance.
 */
BMediaIO::BMediaIO()
{
}


/**
 * @brief Copy constructor — copying is not allowed and does nothing.
 */
BMediaIO::BMediaIO(const BMediaIO &)
{
	// copying not allowed...
}


/**
 * @brief Destroys the BMediaIO instance.
 */
BMediaIO::~BMediaIO()
{
}


// FBC
void BMediaIO::_ReservedMediaIO1() {}
void BMediaIO::_ReservedMediaIO2() {}
void BMediaIO::_ReservedMediaIO3() {}
void BMediaIO::_ReservedMediaIO4() {}
void BMediaIO::_ReservedMediaIO5() {}
