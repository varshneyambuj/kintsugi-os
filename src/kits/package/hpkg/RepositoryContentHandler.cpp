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
 *   Copyright 2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file RepositoryContentHandler.cpp
 * @brief Abstract base class for receiving parsed HPKG repository content.
 *
 * BRepositoryContentHandler defines the callback interface that clients
 * implement to process package entries discovered while reading an HPKG
 * repository index file. BRepositoryReader invokes these callbacks as it
 * walks the attribute tree in the repository section of the archive.
 *
 * @see BRepositoryReader, BPackageContentHandler
 */


#include <package/hpkg/RepositoryContentHandler.h>


namespace BPackageKit {

namespace BHPKG {


/**
 * @brief Destroys the BRepositoryContentHandler.
 *
 * Defined here so that the vtable is emitted in this translation unit and
 * derived classes can override individual callbacks without pulling in
 * unrelated translation units.
 */
BRepositoryContentHandler::~BRepositoryContentHandler()
{
}


}	// namespace BHPKG

}	// namespace BPackageKit
