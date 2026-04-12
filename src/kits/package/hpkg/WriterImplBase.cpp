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
 * @file WriterImplBase.cpp
 * @brief Shared base class for HPKG package and repository writer implementations.
 *
 * WriterImplBase manages the output file, the compressed heap writer, the
 * string cache, and the attribute value/encoding machinery shared by both
 * PackageWriterImpl and RepositoryWriterImpl. It provides helpers for
 * registering package metadata as typed attribute trees and for serialising
 * those trees to the heap using LEB128-encoded tags.
 *
 * @see PackageWriterImpl, RepositoryWriterImpl, ReaderImplBase
 */


#include <package/hpkg/WriterImplBase.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <algorithm>
#include <new>

#include <ByteOrder.h>
#include <File.h>

#include <AutoDeleter.h>
#include <ZlibCompressionAlgorithm.h>
#include <ZstdCompressionAlgorithm.h>

#include <package/hpkg/DataReader.h>
#include <package/hpkg/ErrorOutput.h>

#include <package/hpkg/HPKGDefsPrivate.h>


namespace BPackageKit {

namespace BHPKG {

namespace BPrivate {


// #pragma mark - AttributeValue


/**
 * @brief Constructs an AttributeValue with an invalid type and no encoding.
 */
WriterImplBase::AttributeValue::AttributeValue()
	:
	type(B_HPKG_ATTRIBUTE_TYPE_INVALID),
	encoding(-1)
{
}


/**
 * @brief Destroys the AttributeValue.
 */
WriterImplBase::AttributeValue::~AttributeValue()
{
}


/**
 * @brief Sets the value to a signed 8-bit integer.
 *
 * @param value Signed 8-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(int8 value)
{
	signedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_INT;
}


/**
 * @brief Sets the value to an unsigned 8-bit integer.
 *
 * @param value Unsigned 8-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(uint8 value)
{
	unsignedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_UINT;
}


/**
 * @brief Sets the value to a signed 16-bit integer.
 *
 * @param value Signed 16-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(int16 value)
{
	signedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_INT;
}


/**
 * @brief Sets the value to an unsigned 16-bit integer.
 *
 * @param value Unsigned 16-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(uint16 value)
{
	unsignedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_UINT;
}


/**
 * @brief Sets the value to a signed 32-bit integer.
 *
 * @param value Signed 32-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(int32 value)
{
	signedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_INT;
}


/**
 * @brief Sets the value to an unsigned 32-bit integer.
 *
 * @param value Unsigned 32-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(uint32 value)
{
	unsignedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_UINT;
}


/**
 * @brief Sets the value to a signed 64-bit integer.
 *
 * @param value Signed 64-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(int64 value)
{
	signedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_INT;
}


/**
 * @brief Sets the value to an unsigned 64-bit integer.
 *
 * @param value Unsigned 64-bit integer payload.
 */
void
WriterImplBase::AttributeValue::SetTo(uint64 value)
{
	unsignedInt = value;
	type = B_HPKG_ATTRIBUTE_TYPE_UINT;
}


/**
 * @brief Sets the value to a reference-counted cached string.
 *
 * @param value Pointer to a CachedString previously obtained from a StringCache.
 */
void
WriterImplBase::AttributeValue::SetTo(CachedString* value)
{
	string = value;
	type = B_HPKG_ATTRIBUTE_TYPE_STRING;
}


/**
 * @brief Sets the value to a heap-allocated raw data block.
 *
 * The block is not copied; @a offset references its location in the heap.
 *
 * @param size   Byte size of the raw data.
 * @param offset Heap offset at which the data begins.
 */
void
WriterImplBase::AttributeValue::SetToData(uint64 size, uint64 offset)
{
	data.size = size;
	data.offset = offset;
	type = B_HPKG_ATTRIBUTE_TYPE_RAW;
	encoding = B_HPKG_ATTRIBUTE_ENCODING_RAW_HEAP;
}


/**
 * @brief Sets the value to an inline raw data block (at most a few bytes).
 *
 * Copies up to @a size bytes from @a rawData into the internal inline buffer.
 *
 * @param size    Byte size of the data; must fit in the inline buffer.
 * @param rawData Pointer to the data to copy.
 */
void
WriterImplBase::AttributeValue::SetToData(uint64 size, const void* rawData)
{
	data.size = size;
	if (size > 0)
		memcpy(data.raw, rawData, size);
	type = B_HPKG_ATTRIBUTE_TYPE_RAW;
	encoding = B_HPKG_ATTRIBUTE_ENCODING_RAW_INLINE;
}


/**
 * @brief Returns the most compact encoding applicable to the current value.
 *
 * For integer types, selects 8-, 16-, 32-, or 64-bit encoding based on the
 * magnitude. For strings, chooses table vs. inline encoding based on whether
 * the string has been assigned a table index. For raw data, returns the
 * encoding stored in the fEncoding field.
 *
 * @return One of the B_HPKG_ATTRIBUTE_ENCODING_* constants.
 */
uint8
WriterImplBase::AttributeValue::ApplicableEncoding() const
{
	switch (type) {
		case B_HPKG_ATTRIBUTE_TYPE_INT:
			return _ApplicableIntEncoding(signedInt >= 0
				? (uint64)signedInt << 1
				: (uint64)(-(signedInt + 1) << 1));
		case B_HPKG_ATTRIBUTE_TYPE_UINT:
			return _ApplicableIntEncoding(unsignedInt);
		case B_HPKG_ATTRIBUTE_TYPE_STRING:
			return string->index >= 0
				? B_HPKG_ATTRIBUTE_ENCODING_STRING_TABLE
				: B_HPKG_ATTRIBUTE_ENCODING_STRING_INLINE;
		case B_HPKG_ATTRIBUTE_TYPE_RAW:
			return encoding;
		default:
			return 0;
	}
}


/**
 * @brief Selects the smallest integer encoding that can hold @a value.
 *
 * @param value Non-negative integer value to encode.
 * @return One of the B_HPKG_ATTRIBUTE_ENCODING_INT_*_BIT constants.
 */
/*static*/ uint8
WriterImplBase::AttributeValue::_ApplicableIntEncoding(uint64 value)
{
	if (value <= 0xff)
		return B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT;
	if (value <= 0xffff)
		return B_HPKG_ATTRIBUTE_ENCODING_INT_16_BIT;
	if (value <= 0xffffffff)
		return B_HPKG_ATTRIBUTE_ENCODING_INT_32_BIT;

	return B_HPKG_ATTRIBUTE_ENCODING_INT_64_BIT;
}


// #pragma mark - PackageAttribute


/**
 * @brief Constructs a PackageAttribute node for the attribute tree.
 *
 * @param id_       Attribute ID.
 * @param type_     HPKG type constant for the value.
 * @param encoding_ Encoding constant chosen for the value.
 */
WriterImplBase::PackageAttribute::PackageAttribute(BHPKGAttributeID id_,
	uint8 type_, uint8 encoding_)
	:
	id(id_)
{
	type = type_;
	encoding = encoding_;
}


/**
 * @brief Destroys the PackageAttribute and recursively deletes all children.
 */
WriterImplBase::PackageAttribute::~PackageAttribute()
{
	_DeleteChildren();
}


/**
 * @brief Appends @a child to this attribute's child list.
 *
 * @param child PackageAttribute node to add as a child.
 */
void
WriterImplBase::PackageAttribute::AddChild(PackageAttribute* child)
{
	children.Add(child);
}


/**
 * @brief Removes and deletes all children of this attribute node.
 */
void
WriterImplBase::PackageAttribute::_DeleteChildren()
{
	while (PackageAttribute* child = children.RemoveHead())
		delete child;
}


// #pragma mark - WriterImplBase


/**
 * @brief Constructs a WriterImplBase with no file open yet.
 *
 * @param fileType    Human-readable label used in error messages ("package" etc.).
 * @param errorOutput Destination for diagnostic messages.
 */
WriterImplBase::WriterImplBase(const char* fileType, BErrorOutput* errorOutput)
	:
	fHeapWriter(NULL),
	fCompressionAlgorithm(NULL),
	fCompressionParameters(NULL),
	fDecompressionAlgorithm(NULL),
	fDecompressionParameters(NULL),
	fFileType(fileType),
	fErrorOutput(errorOutput),
	fFileName(NULL),
	fParameters(),
	fFile(NULL),
	fOwnsFile(false),
	fFinished(false)
{
}


/**
 * @brief Destroys the WriterImplBase and cleans up all owned resources.
 *
 * If the write was not finished successfully and the file was created from a
 * path (not a pre-existing stream), the incomplete file is unlinked.
 */
WriterImplBase::~WriterImplBase()
{
	delete fHeapWriter;
	delete fCompressionAlgorithm;
	delete fCompressionParameters;
	delete fDecompressionAlgorithm;
	delete fDecompressionParameters;

	if (fOwnsFile)
		delete fFile;

	if (!fFinished && fFileName != NULL
		&& (Flags() & B_HPKG_WRITER_UPDATE_PACKAGE) == 0) {
		unlink(fFileName);
	}
}


/**
 * @brief Initialises the writer with an output stream and parameters.
 *
 * Opens or adopts @a file, stores the parameters, and initialises the string
 * cache. If @a file is NULL, opens @a fileName according to the parameters'
 * flags (creating or truncating as needed).
 *
 * @param file       Pre-opened output stream, or NULL to open @a fileName.
 * @param keepFile   If true and @a file is non-NULL, the writer takes ownership.
 * @param fileName   Path used when @a file is NULL; stored for error messages.
 * @param parameters Writer parameters controlling compression and flags.
 * @return B_OK on success, or an error code on failure.
 */
status_t
WriterImplBase::Init(BPositionIO* file, bool keepFile, const char* fileName,
	const BPackageWriterParameters& parameters)
{
	fParameters = parameters;

	if (fPackageStringCache.Init() != B_OK)
		throw std::bad_alloc();

	if (file == NULL) {
		if (fileName == NULL)
			return B_BAD_VALUE;

		// open file (don't truncate in update mode)
		int openMode = O_RDWR;
		if ((parameters.Flags() & B_HPKG_WRITER_UPDATE_PACKAGE) == 0)
			openMode |= O_CREAT | O_TRUNC;

		BFile* newFile = new BFile;
		status_t error = newFile->SetTo(fileName, openMode);
		if (error != B_OK) {
			fErrorOutput->PrintError("Failed to open %s file \"%s\": %s\n",
				fFileType, fileName, strerror(errno));
			delete newFile;
			return error;
		}

		fFile = newFile;
		fOwnsFile = true;
	} else {
		fFile = file;
		fOwnsFile = keepFile;
	}

	fFileName = fileName;

	return B_OK;
}


/**
 * @brief Creates the compressed heap writer for the configured compression algorithm.
 *
 * Instantiates a compression/decompression algorithm pair based on
 * fParameters.Compression() and creates a PackageFileHeapWriter.
 *
 * @param headerSize Byte offset (from the start of the file) at which the heap begins.
 * @return B_OK on success, or B_BAD_VALUE for an unknown compression type.
 * @throws std::bad_alloc if algorithm objects cannot be allocated.
 */
status_t
WriterImplBase::InitHeapReader(size_t headerSize)
{
	// allocate the compression/decompression algorithm
	CompressionAlgorithmOwner* compressionAlgorithm = NULL;
	BReference<CompressionAlgorithmOwner> compressionAlgorithmReference;

	DecompressionAlgorithmOwner* decompressionAlgorithm = NULL;
	BReference<DecompressionAlgorithmOwner> decompressionAlgorithmReference;

	switch (fParameters.Compression()) {
		case B_HPKG_COMPRESSION_NONE:
			break;
		case B_HPKG_COMPRESSION_ZLIB:
			compressionAlgorithm = CompressionAlgorithmOwner::Create(
				new(std::nothrow) BZlibCompressionAlgorithm,
				new(std::nothrow) BZlibCompressionParameters(
					(fParameters.CompressionLevel() / float(B_HPKG_COMPRESSION_LEVEL_BEST))
						* B_ZLIB_COMPRESSION_BEST));
			compressionAlgorithmReference.SetTo(compressionAlgorithm, true);

			decompressionAlgorithm = DecompressionAlgorithmOwner::Create(
				new(std::nothrow) BZlibCompressionAlgorithm,
				new(std::nothrow) BZlibDecompressionParameters);
			decompressionAlgorithmReference.SetTo(decompressionAlgorithm, true);

			if (compressionAlgorithm == NULL
				|| compressionAlgorithm->algorithm == NULL
				|| compressionAlgorithm->parameters == NULL
				|| decompressionAlgorithm == NULL
				|| decompressionAlgorithm->algorithm == NULL
				|| decompressionAlgorithm->parameters == NULL) {
				throw std::bad_alloc();
			}
			break;
		case B_HPKG_COMPRESSION_ZSTD:
			compressionAlgorithm = CompressionAlgorithmOwner::Create(
				new(std::nothrow) BZstdCompressionAlgorithm,
				new(std::nothrow) BZstdCompressionParameters(
					(fParameters.CompressionLevel() / float(B_HPKG_COMPRESSION_LEVEL_BEST))
						* B_ZSTD_COMPRESSION_BEST));
			compressionAlgorithmReference.SetTo(compressionAlgorithm, true);

			decompressionAlgorithm = DecompressionAlgorithmOwner::Create(
				new(std::nothrow) BZstdCompressionAlgorithm,
				new(std::nothrow) BZstdDecompressionParameters);
			decompressionAlgorithmReference.SetTo(decompressionAlgorithm, true);

			if (compressionAlgorithm == NULL
				|| compressionAlgorithm->algorithm == NULL
				|| compressionAlgorithm->parameters == NULL
				|| decompressionAlgorithm == NULL
				|| decompressionAlgorithm->algorithm == NULL
				|| decompressionAlgorithm->parameters == NULL) {
				throw std::bad_alloc();
			}
			break;
		default:
			fErrorOutput->PrintError("Error: Invalid heap compression\n");
			return B_BAD_VALUE;
	}

	// create heap writer
	fHeapWriter = new PackageFileHeapWriter(fErrorOutput, fFile, headerSize,
		compressionAlgorithm, decompressionAlgorithm);
	fHeapWriter->Init();

	return B_OK;
}


/**
 * @brief Overrides the compression algorithm in the stored parameters.
 *
 * @param compression One of the B_HPKG_COMPRESSION_* constants.
 */
void
WriterImplBase::SetCompression(uint32 compression)
{
	fParameters.SetCompression(compression);
}


/**
 * @brief Registers all fields of @a packageInfo as attribute nodes in @a attributeList.
 *
 * Translates every BPackageInfo field (name, summary, version, provides, requires,
 * global writable files, users, groups, scripts, etc.) into typed PackageAttribute
 * nodes attached to @a attributeList, ready for serialisation.
 *
 * @param attributeList Destination list to which attribute nodes are appended.
 * @param packageInfo   Package metadata to encode.
 */
void
WriterImplBase::RegisterPackageInfo(PackageAttributeList& attributeList,
	const BPackageInfo& packageInfo)
{
	// name
	AddStringAttribute(B_HPKG_ATTRIBUTE_ID_PACKAGE_NAME, packageInfo.Name(),
		attributeList);

	// summary
	AddStringAttribute(B_HPKG_ATTRIBUTE_ID_PACKAGE_SUMMARY,
		packageInfo.Summary(), attributeList);

	// description
	AddStringAttribute(B_HPKG_ATTRIBUTE_ID_PACKAGE_DESCRIPTION,
		packageInfo.Description(), attributeList);

	// vendor
	AddStringAttribute(B_HPKG_ATTRIBUTE_ID_PACKAGE_VENDOR,
		packageInfo.Vendor(), attributeList);

	// packager
	AddStringAttribute(B_HPKG_ATTRIBUTE_ID_PACKAGE_PACKAGER,
		packageInfo.Packager(), attributeList);

	// base package (optional)
	_AddStringAttributeIfNotEmpty(B_HPKG_ATTRIBUTE_ID_PACKAGE_BASE_PACKAGE,
		packageInfo.BasePackage(), attributeList);

	// flags
	PackageAttribute* flags = new PackageAttribute(
		B_HPKG_ATTRIBUTE_ID_PACKAGE_FLAGS, B_HPKG_ATTRIBUTE_TYPE_UINT,
		B_HPKG_ATTRIBUTE_ENCODING_INT_32_BIT);
	flags->unsignedInt = packageInfo.Flags();
	attributeList.Add(flags);

	// architecture
	PackageAttribute* architecture = new PackageAttribute(
		B_HPKG_ATTRIBUTE_ID_PACKAGE_ARCHITECTURE, B_HPKG_ATTRIBUTE_TYPE_UINT,
		B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT);
	architecture->unsignedInt = packageInfo.Architecture();
	attributeList.Add(architecture);

	// version
	RegisterPackageVersion(attributeList, packageInfo.Version());

	// copyright list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_COPYRIGHT,
			packageInfo.CopyrightList(), attributeList);

	// license list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_LICENSE,
		packageInfo.LicenseList(), attributeList);

	// URL list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_URL,
		packageInfo.URLList(), attributeList);

	// source URL list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_SOURCE_URL,
		packageInfo.SourceURLList(), attributeList);

	// provides list
	const BObjectList<BPackageResolvable, true>& providesList
		= packageInfo.ProvidesList();
	for (int i = 0; i < providesList.CountItems(); ++i) {
		BPackageResolvable* resolvable = providesList.ItemAt(i);
		bool hasVersion = resolvable->Version().InitCheck() == B_OK;
		bool hasCompatibleVersion
			= resolvable->CompatibleVersion().InitCheck() == B_OK;

		PackageAttribute* provides = AddStringAttribute(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_PROVIDES, resolvable->Name(),
			attributeList);

		if (hasVersion)
			RegisterPackageVersion(provides->children, resolvable->Version());

		if (hasCompatibleVersion) {
			RegisterPackageVersion(provides->children,
				resolvable->CompatibleVersion(),
				B_HPKG_ATTRIBUTE_ID_PACKAGE_PROVIDES_COMPATIBLE);
		}
	}

	// requires list
	RegisterPackageResolvableExpressionList(attributeList,
		packageInfo.RequiresList(), B_HPKG_ATTRIBUTE_ID_PACKAGE_REQUIRES);

	// supplements list
	RegisterPackageResolvableExpressionList(attributeList,
		packageInfo.SupplementsList(), B_HPKG_ATTRIBUTE_ID_PACKAGE_SUPPLEMENTS);

	// conflicts list
	RegisterPackageResolvableExpressionList(attributeList,
		packageInfo.ConflictsList(), B_HPKG_ATTRIBUTE_ID_PACKAGE_CONFLICTS);

	// freshens list
	RegisterPackageResolvableExpressionList(attributeList,
		packageInfo.FreshensList(), B_HPKG_ATTRIBUTE_ID_PACKAGE_FRESHENS);

	// replaces list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_REPLACES,
		packageInfo.ReplacesList(), attributeList);

	// global writable file info list
	const BObjectList<BGlobalWritableFileInfo, true>& globalWritableFileInfos
		= packageInfo.GlobalWritableFileInfos();
	for (int32 i = 0; i < globalWritableFileInfos.CountItems(); ++i) {
		BGlobalWritableFileInfo* info = globalWritableFileInfos.ItemAt(i);
		PackageAttribute* attribute = AddStringAttribute(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_GLOBAL_WRITABLE_FILE, info->Path(),
			attributeList);

		if (info->IsDirectory()) {
			PackageAttribute* isDirectoryAttribute = new PackageAttribute(
				B_HPKG_ATTRIBUTE_ID_PACKAGE_IS_WRITABLE_DIRECTORY,
				B_HPKG_ATTRIBUTE_TYPE_UINT,
				B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT);
			isDirectoryAttribute->unsignedInt = 1;
			attribute->children.Add(isDirectoryAttribute);
		}

		if (info->IsIncluded()) {
			PackageAttribute* updateTypeAttribute = new PackageAttribute(
				B_HPKG_ATTRIBUTE_ID_PACKAGE_WRITABLE_FILE_UPDATE_TYPE,
				B_HPKG_ATTRIBUTE_TYPE_UINT,
				B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT);
			updateTypeAttribute->unsignedInt = info->UpdateType();
			attribute->children.Add(updateTypeAttribute);
		}
	}

	// user settings file info list
	const BObjectList<BUserSettingsFileInfo, true>& userSettingsFileInfos
		= packageInfo.UserSettingsFileInfos();
	for (int32 i = 0; i < userSettingsFileInfos.CountItems(); ++i) {
		BUserSettingsFileInfo* info = userSettingsFileInfos.ItemAt(i);
		PackageAttribute* attribute = AddStringAttribute(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_SETTINGS_FILE, info->Path(),
			attributeList);

		if (info->IsDirectory()) {
			PackageAttribute* isDirectoryAttribute = new PackageAttribute(
				B_HPKG_ATTRIBUTE_ID_PACKAGE_IS_WRITABLE_DIRECTORY,
				B_HPKG_ATTRIBUTE_TYPE_UINT,
				B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT);
			isDirectoryAttribute->unsignedInt = 1;
			attribute->children.Add(isDirectoryAttribute);
		} else {
			_AddStringAttributeIfNotEmpty(
				B_HPKG_ATTRIBUTE_ID_PACKAGE_SETTINGS_FILE_TEMPLATE,
				info->TemplatePath(), attribute->children);
		}
	}

	// user list
	const BObjectList<BUser, true>& users = packageInfo.Users();
	for (int32 i = 0; i < users.CountItems(); ++i) {
		const BUser* user = users.ItemAt(i);
		PackageAttribute* attribute = AddStringAttribute(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_USER, user->Name(), attributeList);

		_AddStringAttributeIfNotEmpty(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_REAL_NAME, user->RealName(),
			attribute->children);
		_AddStringAttributeIfNotEmpty(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_HOME, user->Home(),
			attribute->children);
		_AddStringAttributeIfNotEmpty(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_SHELL, user->Shell(),
			attribute->children);

		for (int32 k = 0; k < user->Groups().CountStrings(); k++) {
			AddStringAttribute(B_HPKG_ATTRIBUTE_ID_PACKAGE_USER_GROUP,
				user->Groups().StringAt(k), attribute->children);
		}
	}

	// group list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_GROUP,
		packageInfo.Groups(), attributeList);

	// post install script list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_POST_INSTALL_SCRIPT,
		packageInfo.PostInstallScripts(), attributeList);

	// pre uninstall script list
	_AddStringAttributeList(B_HPKG_ATTRIBUTE_ID_PACKAGE_PRE_UNINSTALL_SCRIPT,
		packageInfo.PreUninstallScripts(), attributeList);

	// checksum (optional, only exists in repositories)
	_AddStringAttributeIfNotEmpty(B_HPKG_ATTRIBUTE_ID_PACKAGE_CHECKSUM,
		packageInfo.Checksum(), attributeList);

	// install path (optional)
	_AddStringAttributeIfNotEmpty(B_HPKG_ATTRIBUTE_ID_PACKAGE_INSTALL_PATH,
		packageInfo.InstallPath(), attributeList);
}


/**
 * @brief Registers a BPackageVersion as a version attribute sub-tree.
 *
 * Creates a major-version string attribute (using @a attributeID as the id)
 * and attaches minor, micro, pre-release, and revision child nodes as present.
 *
 * @param attributeList Destination list.
 * @param version       Version data to encode.
 * @param attributeID   Attribute ID for the major component (default is the
 *                      standard package version major ID).
 */
void
WriterImplBase::RegisterPackageVersion(PackageAttributeList& attributeList,
	const BPackageVersion& version, BHPKGAttributeID attributeID)
{
	PackageAttribute* versionMajor = AddStringAttribute(attributeID,
		version.Major(), attributeList);

	if (!version.Minor().IsEmpty()) {
		AddStringAttribute(B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MINOR,
			version.Minor(), versionMajor->children);
		_AddStringAttributeIfNotEmpty(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_MICRO, version.Micro(),
			versionMajor->children);
	}

	_AddStringAttributeIfNotEmpty(
		B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_PRE_RELEASE,
		version.PreRelease(), versionMajor->children);

	if (version.Revision() != 0) {
		PackageAttribute* versionRevision = new PackageAttribute(
			B_HPKG_ATTRIBUTE_ID_PACKAGE_VERSION_REVISION,
			B_HPKG_ATTRIBUTE_TYPE_UINT, B_HPKG_ATTRIBUTE_ENCODING_INT_32_BIT);
		versionRevision->unsignedInt = version.Revision();
		versionMajor->children.Add(versionRevision);
	}
}


/**
 * @brief Registers a list of resolvable expressions as attribute nodes.
 *
 * For each entry in @a expressionList, creates a string attribute with id @a id
 * and, if the expression has a version, adds operator and version child nodes.
 *
 * @param attributeList  Destination list.
 * @param expressionList List of BPackageResolvableExpression entries to encode.
 * @param id             Attribute ID for the resolvable expression (requires, etc.).
 */
void
WriterImplBase::RegisterPackageResolvableExpressionList(
	PackageAttributeList& attributeList,
	const BObjectList<BPackageResolvableExpression, true>& expressionList, uint8 id)
{
	for (int i = 0; i < expressionList.CountItems(); ++i) {
		BPackageResolvableExpression* resolvableExpr = expressionList.ItemAt(i);
		PackageAttribute* name = AddStringAttribute((BHPKGAttributeID)id,
			resolvableExpr->Name(), attributeList);

		if (resolvableExpr->Version().InitCheck() == B_OK) {
			PackageAttribute* op = new PackageAttribute(
				B_HPKG_ATTRIBUTE_ID_PACKAGE_RESOLVABLE_OPERATOR,
				B_HPKG_ATTRIBUTE_TYPE_UINT,
				B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT);
			op->unsignedInt = resolvableExpr->Operator();
			name->children.Add(op);
			RegisterPackageVersion(name->children, resolvableExpr->Version());
		}
	}
}


/**
 * @brief Creates a string attribute node and appends it to @a list.
 *
 * Looks up or inserts @a value into the package string cache, then constructs
 * a PackageAttribute set to the string table encoding.
 *
 * @param id    Attribute ID.
 * @param value String value; must outlive the attribute or be in the cache.
 * @param list  List to which the new attribute is appended.
 * @return Pointer to the newly created attribute.
 */
WriterImplBase::PackageAttribute*
WriterImplBase::AddStringAttribute(BHPKGAttributeID id, const BString& value,
	DoublyLinkedList<PackageAttribute>& list)
{
	PackageAttribute* attribute = new PackageAttribute(id,
		B_HPKG_ATTRIBUTE_TYPE_STRING, B_HPKG_ATTRIBUTE_ENCODING_STRING_TABLE);
	attribute->string = fPackageStringCache.Get(value);
	list.Add(attribute);
	return attribute;
}


/**
 * @brief Writes the cached string table to the heap and assigns table indices.
 *
 * Sorts strings by descending usage count, assigns ascending indices to those
 * with usageCount >= @a minUsageCount, and writes each to the heap followed by
 * a terminating zero byte.
 *
 * @param cache          String cache to serialise.
 * @param minUsageCount  Minimum usage count for a string to be table-encoded.
 * @return Number of strings written to the table.
 */
int32
WriterImplBase::WriteCachedStrings(const StringCache& cache,
	uint32 minUsageCount)
{
	// create an array of the cached strings
	int32 count = cache.CountElements();
	CachedString** cachedStrings = new CachedString*[count];
	ArrayDeleter<CachedString*> cachedStringsDeleter(cachedStrings);

	int32 index = 0;
	for (CachedStringTable::Iterator it = cache.GetIterator();
			CachedString* string = it.Next();) {
		cachedStrings[index++] = string;
	}

	// sort it by descending usage count
	std::sort(cachedStrings, cachedStrings + count, CachedStringUsageGreater());

	// assign the indices and write entries to disk
	int32 stringsWritten = 0;
	for (int32 i = 0; i < count; i++) {
		CachedString* cachedString = cachedStrings[i];

		// empty strings must be stored inline, as they can't be distinguished
		// from the end-marker!
		if (strlen(cachedString->string) == 0)
			continue;

		// strings that are used only once are better stored inline
		if (cachedString->usageCount < minUsageCount)
			break;

		WriteString(cachedString->string);

		cachedString->index = stringsWritten++;
	}

	// write a terminating 0 byte
	Write<uint8>(0);

	return stringsWritten;
}


/**
 * @brief Writes the package attributes section to the heap.
 *
 * First writes the string table (using strings with usageCount >= 2), then
 * serialises the attribute tree. Returns the string count and stores the
 * uncompressed strings length.
 *
 * @param packageAttributes          Root list of package attribute nodes.
 * @param _stringsLengthUncompressed Output for the uncompressed bytes used by strings.
 * @return Number of strings written to the string table.
 */
int32
WriterImplBase::WritePackageAttributes(
	const PackageAttributeList& packageAttributes,
	uint32& _stringsLengthUncompressed)
{
	// write the cached strings
	uint64 startOffset = fHeapWriter->UncompressedHeapSize();
	uint32 stringsCount = WriteCachedStrings(fPackageStringCache, 2);
	_stringsLengthUncompressed
		= fHeapWriter->UncompressedHeapSize() - startOffset;

	_WritePackageAttributes(packageAttributes);

	return stringsCount;
}


/**
 * @brief Serialises a single attribute value to the heap using the given encoding.
 *
 * Handles all attribute types: integers (written big-endian at 8/16/32/64 bits),
 * strings (LEB128 index or inline null-terminated), and raw data (LEB128 size
 * followed by heap offset or inline bytes).
 *
 * @param value    Attribute value to serialise.
 * @param encoding Encoding constant selecting the wire format.
 * @throws status_t(B_BAD_VALUE) for an unsupported encoding or type.
 */
void
WriterImplBase::WriteAttributeValue(const AttributeValue& value, uint8 encoding)
{
	switch (value.type) {
		case B_HPKG_ATTRIBUTE_TYPE_INT:
		case B_HPKG_ATTRIBUTE_TYPE_UINT:
		{
			uint64 intValue = value.type == B_HPKG_ATTRIBUTE_TYPE_INT
				? (uint64)value.signedInt : value.unsignedInt;

			switch (encoding) {
				case B_HPKG_ATTRIBUTE_ENCODING_INT_8_BIT:
					Write<uint8>((uint8)intValue);
					break;
				case B_HPKG_ATTRIBUTE_ENCODING_INT_16_BIT:
					Write<uint16>(
						B_HOST_TO_BENDIAN_INT16((uint16)intValue));
					break;
				case B_HPKG_ATTRIBUTE_ENCODING_INT_32_BIT:
					Write<uint32>(
						B_HOST_TO_BENDIAN_INT32((uint32)intValue));
					break;
				case B_HPKG_ATTRIBUTE_ENCODING_INT_64_BIT:
					Write<uint64>(
						B_HOST_TO_BENDIAN_INT64((uint64)intValue));
					break;
				default:
				{
					fErrorOutput->PrintError("WriteAttributeValue(): invalid "
						"encoding %d for int value type %d\n", encoding,
						value.type);
					throw status_t(B_BAD_VALUE);
				}
			}

			break;
		}

		case B_HPKG_ATTRIBUTE_TYPE_STRING:
		{
			if (encoding == B_HPKG_ATTRIBUTE_ENCODING_STRING_TABLE)
				WriteUnsignedLEB128(value.string->index);
			else
				WriteString(value.string->string);
			break;
		}

		case B_HPKG_ATTRIBUTE_TYPE_RAW:
		{
			WriteUnsignedLEB128(value.data.size);
			if (encoding == B_HPKG_ATTRIBUTE_ENCODING_RAW_HEAP)
				WriteUnsignedLEB128(value.data.offset);
			else
				fHeapWriter->AddDataThrows(value.data.raw, value.data.size);
			break;
		}

		default:
			fErrorOutput->PrintError(
				"WriteAttributeValue(): invalid value type: %d\n", value.type);
			throw status_t(B_BAD_VALUE);
	}
}


/**
 * @brief Encodes @a value as an unsigned LEB128 integer and appends it to the heap.
 *
 * @param value 64-bit unsigned value to encode.
 */
void
WriterImplBase::WriteUnsignedLEB128(uint64 value)
{
	uint8 bytes[10];
	int32 count = 0;
	do {
		uint8 byte = value & 0x7f;
		value >>= 7;
		bytes[count++] = byte | (value != 0 ? 0x80 : 0);
	} while (value != 0);

	fHeapWriter->AddDataThrows(bytes, count);
}


/**
 * @brief Writes @a size bytes from @a buffer to the file at @a offset.
 *
 * Used to update fixed-size header fields after the heap has been written.
 * Throws the error code on failure.
 *
 * @param buffer Pointer to the data to write.
 * @param size   Number of bytes to write.
 * @param offset Absolute byte offset in the output file.
 * @throws status_t error code if the write fails.
 */
void
WriterImplBase::RawWriteBuffer(const void* buffer, size_t size, off_t offset)
{
	status_t error = fFile->WriteAtExactly(offset, buffer, size);
	if (error != B_OK) {
		fErrorOutput->PrintError(
			"RawWriteBuffer(%p, %lu) failed to write data: %s\n", buffer, size,
			strerror(error));
		throw error;
	}
}


/**
 * @brief Appends each string in @a value as a separate attribute node to @a list.
 *
 * @param id    Attribute ID to use for each node.
 * @param value BStringList whose entries are encoded as string attributes.
 * @param list  Destination attribute list.
 */
void
WriterImplBase::_AddStringAttributeList(BHPKGAttributeID id,
	const BStringList& value, DoublyLinkedList<PackageAttribute>& list)
{
	for (int32 i = 0; i < value.CountStrings(); i++)
		AddStringAttribute(id, value.StringAt(i), list);
}


/**
 * @brief Recursively serialises a package attribute list to the heap.
 *
 * For each attribute, encodes the LEB128 tag (id | type | encoding | has-children),
 * then the value, then recurses into children. Writes a terminal zero LEB128 after
 * the last sibling.
 *
 * @param packageAttributes List of attribute nodes to serialise.
 */
void
WriterImplBase::_WritePackageAttributes(
	const PackageAttributeList& packageAttributes)
{
	DoublyLinkedList<PackageAttribute>::ConstIterator it
		= packageAttributes.GetIterator();
	while (PackageAttribute* attribute = it.Next()) {
		uint8 encoding = attribute->ApplicableEncoding();

		// write tag
		WriteUnsignedLEB128(compose_attribute_tag(
			attribute->id, attribute->type, encoding,
			!attribute->children.IsEmpty()));

		// write value
		WriteAttributeValue(*attribute, encoding);

		if (!attribute->children.IsEmpty())
			_WritePackageAttributes(attribute->children);
	}

	WriteUnsignedLEB128(0);
}


}	// namespace BPrivate

}	// namespace BHPKG

}	// namespace BPackageKit
