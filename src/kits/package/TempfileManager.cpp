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
 *   Copyright 2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file TempfileManager.cpp
 * @brief Implementation of TempfileManager, a scoped temporary-file factory.
 *
 * TempfileManager creates uniquely-named temporary files inside a designated
 * base directory and removes them — along with the directory itself — when
 * it is destroyed. It is used by the request/job infrastructure to manage
 * intermediate download files.
 *
 * @see BRequest, BContext
 */


#include "TempfileManager.h"


namespace BPackageKit {

namespace BPrivate {


/** @brief Default base name prefix applied to every temporary file created by this manager. */
const BString TempfileManager::kDefaultName = "tmp-pkgkit-file-";


/**
 * @brief Default-construct a TempfileManager with counter initialised to 1.
 */
TempfileManager::TempfileManager()
	:
	fNextNumber(1)
{
}


/**
 * @brief Destroy the manager, removing all created temp files and the base directory.
 *
 * Iterates all remaining entries in the base directory, removes each one,
 * then removes the directory itself.
 */
TempfileManager::~TempfileManager()
{
	if (fBaseDirectory.InitCheck() != B_OK)
		return;

	fBaseDirectory.Rewind();
	BEntry entry;
	while (fBaseDirectory.GetNextEntry(&entry) == B_OK)
		entry.Remove();

	fBaseDirectory.GetEntry(&entry);
	entry.Remove();
}


/**
 * @brief Set the directory in which temporary files will be created.
 *
 * @param baseDirectory  An already-open BDirectory to use as the scratch area.
 */
void
TempfileManager::SetBaseDirectory(const BDirectory& baseDirectory)
{
	fBaseDirectory = baseDirectory;
}


/**
 * @brief Construct and return a BEntry for a new unique temporary file.
 *
 * The file name is formed by appending an atomically-incremented counter to
 * @a baseName. The file is not actually created on disk; callers are
 * responsible for opening it.
 *
 * @param baseName  The prefix for the temporary file name.
 * @return A BEntry representing the new file path inside the base directory.
 */
BEntry
TempfileManager::Create(const BString& baseName)
{
	BString name = BString(baseName) << atomic_add(&fNextNumber, 1);

	return BEntry(&fBaseDirectory, name.String());
}


}	// namespace BPrivate

}	// namespace BPackageKit
