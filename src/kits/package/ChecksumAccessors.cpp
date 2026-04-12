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
 *   Copyright 2011-2013, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 *       Ingo Weinhold <ingo_weinhold@gmx.de>
 */


/**
 * @file ChecksumAccessors.cpp
 * @brief Concrete implementations of the ChecksumAccessor interface.
 *
 * Provides three strategies for obtaining a hex-encoded SHA-256 checksum:
 * reading a pre-computed value from a dedicated checksum file
 * (ChecksumFileChecksumAccessor), computing it on-the-fly over an arbitrary
 * file (GeneralFileChecksumAccessor), or returning a caller-supplied string
 * directly (StringChecksumAccessor).
 *
 * @see ValidateChecksumJob
 */


#include <File.h>

#include <AutoDeleter.h>
#include <SHA256.h>

#include <package/ChecksumAccessors.h>


namespace BPackageKit {

namespace BPrivate {


#define NIBBLE_AS_HEX(nibble) \
	(nibble >= 10 ? 'a' + nibble - 10 : '0' + nibble)


// #pragma mark - ChecksumAccessor


/**
 * @brief Virtual destructor for the ChecksumAccessor interface.
 */
ChecksumAccessor::~ChecksumAccessor()
{
}


// #pragma mark - ChecksumFileChecksumAccessor


/**
 * @brief Construct an accessor that reads a checksum from a dedicated file.
 *
 * @param checksumFileEntry  BEntry pointing to the file containing the
 *                           64-character hex-encoded SHA-256 checksum string.
 */
ChecksumFileChecksumAccessor::ChecksumFileChecksumAccessor(
	const BEntry& checksumFileEntry)
	:
	fChecksumFileEntry(checksumFileEntry)
{
}


/**
 * @brief Read the hex-encoded SHA-256 checksum from the checksum file.
 *
 * Opens the checksum file and reads exactly 64 bytes (the expected length
 * of a hex-encoded SHA-256 digest) into \a checksum.
 *
 * @param checksum  Output string that receives the 64-character hex digest.
 * @return B_OK on success, B_NO_MEMORY if the string buffer could not be
 *         allocated, B_IO_ERROR if fewer than 64 bytes were available, or
 *         another error code on file-open failure.
 */
status_t
ChecksumFileChecksumAccessor::GetChecksum(BString& checksum) const
{
	BFile checksumFile(&fChecksumFileEntry, B_READ_ONLY);
	status_t result = checksumFile.InitCheck();
	if (result != B_OK)
		return result;

	const int kSHA256ChecksumHexDumpSize = 64;
	char* buffer = checksum.LockBuffer(kSHA256ChecksumHexDumpSize);
	if (buffer == NULL)
		return B_NO_MEMORY;

	ssize_t bytesRead = checksumFile.Read(buffer, kSHA256ChecksumHexDumpSize);
	buffer[kSHA256ChecksumHexDumpSize] = '\0';
	checksum.UnlockBuffer(kSHA256ChecksumHexDumpSize);
	if (bytesRead < 0)
		return bytesRead;
	if (bytesRead != kSHA256ChecksumHexDumpSize)
		return B_IO_ERROR;

	return B_OK;
}


// #pragma mark - GeneralFileChecksumAccessor


/**
 * @brief Construct an accessor that computes a SHA-256 checksum over a file.
 *
 * @param fileEntry        BEntry pointing to the file to be hashed.
 * @param skipMissingFile  If true, return B_OK with an empty checksum when the
 *                         file does not exist rather than returning an error.
 */
GeneralFileChecksumAccessor::GeneralFileChecksumAccessor(
	const BEntry& fileEntry, bool skipMissingFile)
	:
	fFileEntry(fileEntry),
	fSkipMissingFile(skipMissingFile)
{
}


/**
 * @brief Compute the SHA-256 checksum of the target file.
 *
 * Reads the file in 64 KiB blocks, feeds each block into a SHA256 context,
 * then encodes the resulting digest as a lowercase hex string into \a checksum.
 *
 * @param checksum  Output string that receives the hex-encoded SHA-256 digest.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, B_IO_ERROR on
 *         partial reads, or another error code on file access failure.
 */
status_t
GeneralFileChecksumAccessor::GetChecksum(BString& checksum) const
{
	SHA256 sha;

	checksum.Truncate(0);

	{
		BFile file(&fFileEntry, B_READ_ONLY);
		status_t result = file.InitCheck();
		if (result != B_OK) {
			if (result == B_ENTRY_NOT_FOUND && fSkipMissingFile)
				return B_OK;
			return result;
		}

		off_t fileSize;
		if ((result = file.GetSize(&fileSize)) != B_OK)
			return result;

		const int kBlockSize = 64 * 1024;
		void* buffer = malloc(kBlockSize);
		if (buffer == NULL)
			return B_NO_MEMORY;
		MemoryDeleter memoryDeleter(buffer);

		off_t handledSize = 0;
		while (handledSize < fileSize) {
			ssize_t bytesRead = file.Read(buffer, kBlockSize);
			if (bytesRead < 0)
				return bytesRead;

			sha.Update(buffer, bytesRead);

			handledSize += bytesRead;
		}
	}

	const int kSHA256ChecksumSize = sha.DigestLength();
	char* buffer = checksum.LockBuffer(2 * kSHA256ChecksumSize);
	if (buffer == NULL)
		return B_NO_MEMORY;
	const uint8* digest = sha.Digest();
	for (int i = 0; i < kSHA256ChecksumSize; ++i) {
		uint8 highNibble = (digest[i] & 0xF0) >> 4;
		buffer[i * 2] = NIBBLE_AS_HEX(highNibble);
		uint8 lowNibble = digest[i] & 0x0F;
		buffer[1 + i * 2] = NIBBLE_AS_HEX(lowNibble);
	}
	buffer[2 * kSHA256ChecksumSize] = '\0';
	checksum.UnlockBuffer(2 * kSHA256ChecksumSize);

	return B_OK;
}


// #pragma mark - StringChecksumAccessor


/**
 * @brief Construct an accessor that wraps a caller-supplied checksum string.
 *
 * @param checksum  The pre-computed hex-encoded checksum to return verbatim.
 */
StringChecksumAccessor::StringChecksumAccessor(const BString& checksum)
	:
	fChecksum(checksum)
{
}


/**
 * @brief Copy the stored checksum string into the output parameter.
 *
 * @param _checksum  Output string that receives the stored checksum.
 * @return Always returns B_OK.
 */
status_t
StringChecksumAccessor::GetChecksum(BString& _checksum) const
{
	_checksum = fChecksum;
	return B_OK;
}



}	// namespace BPrivate

}	// namespace BPackageKit
