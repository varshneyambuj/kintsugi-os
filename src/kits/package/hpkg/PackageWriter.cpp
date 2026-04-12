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
 *   Copyright 2011, Oliver Tappe <zooey@hirschkaefer.de>
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageWriter.cpp
 * @brief Public facade for creating HPKG package archive files.
 *
 * BPackageWriter provides the entry point for assembling an HPKG file from
 * a set of file-system entries. All heavy lifting is delegated to the internal
 * PackageWriterImpl; this class owns the impl object and forwards every call,
 * guarding against a NULL impl (allocation failure at construction time).
 *
 * BPackageWriterParameters carries the knobs that control compression codec
 * and level, as well as optional behavioural flags such as license checking.
 *
 * @see BPackageReader, BRepositoryWriter
 */


#include <package/hpkg/PackageWriter.h>

#include <new>

#include <package/hpkg/PackageWriterImpl.h>


namespace BPackageKit {

namespace BHPKG {


// #pragma mark - BPackageWriterParameters


/**
 * @brief Constructs a BPackageWriterParameters object with default settings.
 *
 * Defaults to no flags, ZLIB compression, and the best compression level.
 */
BPackageWriterParameters::BPackageWriterParameters()
	:
	fFlags(0),
	fCompression(B_HPKG_COMPRESSION_ZLIB),
	fCompressionLevel(B_HPKG_COMPRESSION_LEVEL_BEST)
{
}


/**
 * @brief Destroys the BPackageWriterParameters object.
 */
BPackageWriterParameters::~BPackageWriterParameters()
{
}


/**
 * @brief Returns the current behavioural flags.
 *
 * @return Bitmask of flags controlling writer behaviour.
 */
uint32
BPackageWriterParameters::Flags() const
{
	return fFlags;
}


/**
 * @brief Sets the behavioural flags for this writer.
 *
 * @param flags Bitmask of flags to apply.
 */
void
BPackageWriterParameters::SetFlags(uint32 flags)
{
	fFlags = flags;
}


/**
 * @brief Returns the compression algorithm identifier in use.
 *
 * @return One of the B_HPKG_COMPRESSION_* constants.
 */
uint32
BPackageWriterParameters::Compression() const
{
	return fCompression;
}


/**
 * @brief Sets the compression algorithm to use when writing the package.
 *
 * @param compression One of the B_HPKG_COMPRESSION_* constants.
 */
void
BPackageWriterParameters::SetCompression(uint32 compression)
{
	fCompression = compression;
}


/**
 * @brief Returns the compression level currently configured.
 *
 * @return Integer compression level; higher values give smaller output.
 */
int32
BPackageWriterParameters::CompressionLevel() const
{
	return fCompressionLevel;
}


/**
 * @brief Sets the compression level for the selected algorithm.
 *
 * @param compressionLevel Compression level; meaning is algorithm-specific.
 */
void
BPackageWriterParameters::SetCompressionLevel(int32 compressionLevel)
{
	fCompressionLevel = compressionLevel;
}


// #pragma mark - BPackageWriter


/**
 * @brief Constructs a BPackageWriter and allocates its internal implementation.
 *
 * @param listener Callback object that receives progress and error notifications.
 */
BPackageWriter::BPackageWriter(BPackageWriterListener* listener)
	:
	fImpl(new (std::nothrow) PackageWriterImpl(listener))
{
}


/**
 * @brief Destroys the BPackageWriter and frees the implementation object.
 */
BPackageWriter::~BPackageWriter()
{
	delete fImpl;
}


/**
 * @brief Initialises the writer to write to a file identified by name.
 *
 * Opens or creates the file at @a fileName and prepares internal structures.
 * If @a parameters is NULL, default parameters (ZLIB, best compression) are used.
 *
 * @param fileName   Path to the output HPKG file.
 * @param parameters Optional writer parameters; may be NULL for defaults.
 * @return B_OK on success, B_NO_MEMORY if the impl was not allocated, or
 *         another error code on failure.
 */
status_t
BPackageWriter::Init(const char* fileName,
	const BPackageWriterParameters* parameters)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	BPackageWriterParameters defaultParameters;

	return fImpl->Init(fileName,
		parameters != NULL ? *parameters : defaultParameters);
}


/**
 * @brief Initialises the writer to write to an existing BPositionIO stream.
 *
 * Uses @a file as the output target. If @a keepFile is true the writer will
 * not close or delete @a file on destruction.
 *
 * @param file       Pre-opened output stream to write into.
 * @param keepFile   If true, ownership of @a file is NOT transferred.
 * @param parameters Optional writer parameters; may be NULL for defaults.
 * @return B_OK on success, B_NO_MEMORY if the impl was not allocated, or
 *         another error code on failure.
 */
status_t
BPackageWriter::Init(BPositionIO* file, bool keepFile,
	const BPackageWriterParameters* parameters)
{
	if (fImpl == NULL)
		return B_NO_MEMORY;

	BPackageWriterParameters defaultParameters;

	return fImpl->Init(file, keepFile,
		parameters != NULL ? *parameters : defaultParameters);
}


/**
 * @brief Sets the installation prefix path recorded in the package header.
 *
 * @param installPath Absolute path at which this package is to be installed.
 * @return B_OK on success, B_NO_INIT if the writer was not initialised.
 */
status_t
BPackageWriter::SetInstallPath(const char* installPath)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->SetInstallPath(installPath);
}


/**
 * @brief Enables or disables automatic licence file validation.
 *
 * When enabled, the writer verifies that every licence referenced in the
 * package info has a corresponding file present in the package.
 *
 * @param checkLicenses Pass true to enable licence checking.
 */
void
BPackageWriter::SetCheckLicenses(bool checkLicenses)
{
	if (fImpl != NULL)
		fImpl->SetCheckLicenses(checkLicenses);
}


/**
 * @brief Adds a file-system entry to the package being assembled.
 *
 * @param fileName Relative path of the entry inside the package.
 * @param fd       Optional open file descriptor for the entry; pass -1 to
 *                 let the writer open the file by name.
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BPackageWriter::AddEntry(const char* fileName, int fd)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->AddEntry(fileName, fd);
}


/**
 * @brief Finalises the package, writing all headers and flushing to disk.
 *
 * Must be called after all entries have been added. The file is not valid
 * until this method returns B_OK.
 *
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BPackageWriter::Finish()
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Finish();
}


/**
 * @brief Re-compresses an existing HPKG file using the current parameters.
 *
 * Reads the uncompressed data from @a inputFile and writes a new HPKG with
 * the compression codec and level configured on this writer.
 *
 * @param inputFile Source HPKG file to recompress.
 * @return B_OK on success, B_NO_INIT if not initialised, or an error code.
 */
status_t
BPackageWriter::Recompress(BPositionIO* inputFile)
{
	if (fImpl == NULL)
		return B_NO_INIT;

	return fImpl->Recompress(inputFile);
}


}	// namespace BHPKG

}	// namespace BPackageKit
