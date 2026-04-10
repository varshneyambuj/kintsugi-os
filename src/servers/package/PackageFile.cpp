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

/** @file PackageFile.cpp
 *  @brief Implements PackageFile initialization, info reading, and reference-counted cleanup */



#include "PackageFile.h"

#include <fcntl.h>

#include <File.h>

#include <AutoDeleter.h>

#include "DebugSupport.h"
#include "PackageFileManager.h"


PackageFile::PackageFile()
	:
	fNodeRef(),
	fDirectoryRef(),
	fFileName(),
	fInfo(),
	fEntryRefHashTableNext(NULL),
// 	fNodeRefHashTableNext(NULL),
	fOwner(NULL),
	fIgnoreEntryCreated(0),
	fIgnoreEntryRemoved(0)
{
}


PackageFile::~PackageFile()
{
}


status_t
PackageFile::Init(const entry_ref& entryRef, PackageFileManager* owner)
{
	fDirectoryRef.device = entryRef.device;
	fDirectoryRef.node = entryRef.directory;

	// init the file name
	fFileName = entryRef.name;
	if (fFileName.IsEmpty())
		RETURN_ERROR(B_NO_MEMORY);

	// open the file and get the node_ref
	BFile file;
	status_t error = file.SetTo(&entryRef, B_READ_ONLY);
	if (error != B_OK)
		RETURN_ERROR(error);

	error = file.GetNodeRef(&fNodeRef);
	if (error != B_OK)
		RETURN_ERROR(error);

	// get the package info
	FileDescriptorCloser fd(file.Dup());
	if (!fd.IsSet())
		RETURN_ERROR(error);

	error = fInfo.ReadFromPackageFile(fd.Get());
	if (error != B_OK)
		RETURN_ERROR(error);

	if (fFileName != fInfo.CanonicalFileName())
		fInfo.SetFileName(fFileName);

	fOwner = owner;

	return B_OK;
}


BString
PackageFile::RevisionedName() const
{
	return BString().SetToFormat("%s-%s", fInfo.Name().String(),
		fInfo.Version().ToString().String());
}


BString
PackageFile::RevisionedNameThrows() const
{
	BString result(RevisionedName());
	if (result.IsEmpty())
		throw std::bad_alloc();
	return result;
}


void
PackageFile::LastReferenceReleased()
{
	if (fOwner != NULL)
		fOwner->RemovePackageFile(this);
	delete this;
}
