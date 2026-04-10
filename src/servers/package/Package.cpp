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
   /*
    * Copyright 2013-2014, Haiku, Inc. All Rights Reserved.
    * Distributed under the terms of the MIT License.
    *
    * Authors:
    *		Ingo Weinhold <ingo_weinhold@gmx.de>
    */
 */

/** @file Package.cpp
 *  @brief Implements Package lifecycle including reference-counted file ownership and cloning */



#include "Package.h"

#include <fcntl.h>

#include <File.h>

#include <AutoDeleter.h>

#include "DebugSupport.h"


Package::Package(PackageFile* file)
	:
	fFile(file),
	fActive(false),
	fFileNameHashTableNext(NULL),
	fNodeRefHashTableNext(NULL)
{
	fFile->AcquireReference();
}


Package::~Package()
{
	fFile->ReleaseReference();
}


Package*
Package::Clone() const
{
	Package* clone = new(std::nothrow) Package(fFile);
	if (clone != NULL)
		clone->fActive = fActive;
	return clone;
}
