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
 * @file ValidateChecksumJob.cpp
 * @brief Implementation of ValidateChecksumJob, a job that verifies file integrity.
 *
 * ValidateChecksumJob compares two checksums — an expected value from a
 * reference source and an actual value computed from the downloaded file — and
 * optionally fails with B_BAD_DATA when they do not match. The result is
 * available via ChecksumsMatch() after the job has run.
 *
 * @see BRequest, ChecksumAccessor, RefreshRepositoryRequest
 */


#include "ValidateChecksumJob.h"

#include <File.h>

#include <package/Context.h>


namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Construct a ValidateChecksumJob.
 *
 * Takes ownership of both accessor objects.
 *
 * @param context                     Shared BContext for progress reporting.
 * @param title                       Human-readable job title.
 * @param expectedChecksumAccessor    Accessor that returns the expected checksum.
 * @param realChecksumAccessor        Accessor that returns the actual checksum.
 * @param failIfChecksumsDontMatch    If true, Execute() returns B_BAD_DATA on mismatch.
 */
ValidateChecksumJob::ValidateChecksumJob(const BContext& context,
	const BString& title, ChecksumAccessor* expectedChecksumAccessor,
	ChecksumAccessor* realChecksumAccessor, bool failIfChecksumsDontMatch)
	:
	inherited(context, title),
	fExpectedChecksumAccessor(expectedChecksumAccessor),
	fRealChecksumAccessor(realChecksumAccessor),
	fFailIfChecksumsDontMatch(failIfChecksumsDontMatch),
	fChecksumsMatch(false)
{
}


/**
 * @brief Destroy the job, deleting both checksum accessor objects.
 */
ValidateChecksumJob::~ValidateChecksumJob()
{
	delete fRealChecksumAccessor;
	delete fExpectedChecksumAccessor;
}


/**
 * @brief Retrieve both checksums and compare them.
 *
 * Stores the comparison result in fChecksumsMatch. When
 * fFailIfChecksumsDontMatch is true and the checksums differ, sets an
 * error string and returns B_BAD_DATA.
 *
 * @return B_OK on success (including an intentional mismatch when
 *         fFailIfChecksumsDontMatch is false), B_BAD_VALUE if either
 *         accessor is NULL, or B_BAD_DATA on a failing mismatch.
 */
status_t
ValidateChecksumJob::Execute()
{
	if (fExpectedChecksumAccessor == NULL || fRealChecksumAccessor == NULL)
		return B_BAD_VALUE;

	BString expectedChecksum;
	BString realChecksum;

	status_t result = fExpectedChecksumAccessor->GetChecksum(expectedChecksum);
	if (result != B_OK)
		return result;

	result = fRealChecksumAccessor->GetChecksum(realChecksum);
	if (result != B_OK)
		return result;

	fChecksumsMatch = expectedChecksum.ICompare(realChecksum) == 0;

	if (fFailIfChecksumsDontMatch && !fChecksumsMatch) {
		BString error = BString("Checksum error:\n")
			<< "expected '"	<< expectedChecksum << "'\n"
			<< "got      '" << realChecksum << "'";
		SetErrorString(error);
		return B_BAD_DATA;
	}

	return B_OK;
}


/**
 * @brief Return whether the two checksums matched after the last Execute() call.
 *
 * @return True if the checksums were equal, false otherwise.
 */
bool
ValidateChecksumJob::ChecksumsMatch() const
{
	return fChecksumsMatch;
}


}	// namespace BPrivate

}	// namespace BPackageKit
