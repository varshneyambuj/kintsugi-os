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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file FunctionID.cpp
 * @brief Identity values that uniquely name a function across debugger sessions.
 *
 * Defines the abstract FunctionID, a SourceFunctionID variant scoped to a
 * source-file path, and an ImageFunctionID variant scoped to an image name.
 * Both are BArchivable so they can be persisted into BMessages.
 */


#include "FunctionID.h"

#include <new>

#include <Message.h>


// #pragma mark - FunctionID


/**
 * @brief Reconstruct a FunctionID from a BArchivable archive.
 *
 * Reads the "FunctionID::path" and "FunctionID::functionName" string fields.
 *
 * @param archive  Source archive.
 */
FunctionID::FunctionID(const BMessage& archive)
	:
	BArchivable(const_cast<BMessage*>(&archive))
{
	archive.FindString("FunctionID::path", &fPath);
	archive.FindString("FunctionID::functionName", &fFunctionName);
}


/**
 * @brief Construct from a path/name pair.
 *
 * @param path          Source file path or image name (subclass-specific).
 * @param functionName  Function name including any pretty-name decoration.
 */
FunctionID::FunctionID(const BString& path, const BString& functionName)
	:
	fPath(path),
	fFunctionName(functionName)
{
}


/** @brief Virtual destructor. */
FunctionID::~FunctionID()
{
}


/**
 * @brief Persist this identity into @a archive.
 *
 * Writes the "FunctionID::path" and "FunctionID::functionName" string fields
 * after letting BArchivable record the class signature.
 *
 * @param archive  Destination archive.
 * @param deep     Forwarded to BArchivable::Archive(); unused here.
 * @return The first non-OK status encountered, or B_OK on success.
 */
status_t
FunctionID::Archive(BMessage* archive, bool deep) const
{
	status_t error = BArchivable::Archive(archive, deep);
	if (error != B_OK)
		return error;

	error = archive->AddString("FunctionID::path", fPath);
	if (error == B_OK)
		error = archive->AddString("FunctionID::functionName", fFunctionName);
	return error;
}


/**
 * @brief Compute the cached hash for this identity.
 *
 * Combines the path and function-name hashes with a small prime multiplier
 * so different paths sharing a function name still hash distinctly.
 *
 * @return A hash usable in BOpenHashTable lookups.
 */
uint32
FunctionID::ComputeHashValue() const
{
	return fPath.HashValue() * 17
		+ fFunctionName.HashValue();
}


/**
 * @brief Test whether the identity is well-formed.
 *
 * @return true if both the path and the function name are non-empty.
 */
bool
FunctionID::IsValid() const
{
	return fPath.Length() != 0 && fFunctionName.Length() != 0;
}


// #pragma mark - SourceFunctionID


/**
 * @brief Reconstruct a SourceFunctionID from an archive.
 *
 * @param archive  Source archive.
 */
SourceFunctionID::SourceFunctionID(const BMessage& archive)
	:
	FunctionID(archive)
{
}


/**
 * @brief Construct from a source path and function name.
 *
 * @param sourceFilePath  Path to the source file in which the function is defined.
 * @param functionName    Function name.
 */
SourceFunctionID::SourceFunctionID(const BString& sourceFilePath,
	const BString& functionName)
	:
	FunctionID(sourceFilePath, functionName)
{
}


/** @brief Virtual destructor. */
SourceFunctionID::~SourceFunctionID()
{
}


/**
 * @brief BArchivable factory hook that returns a new SourceFunctionID.
 *
 * Rejects malformed archives by validating IsValid() before returning.
 *
 * @param archive  Archive to instantiate from.
 * @return A new SourceFunctionID on success, NULL on allocation failure or
 *         when the archive does not represent a valid identity.
 */
/*static*/ BArchivable*
SourceFunctionID::Instantiate(BMessage* archive)
{
	if (archive == NULL)
		return NULL;

	SourceFunctionID* object = new(std::nothrow) SourceFunctionID(*archive);
	if (object == NULL)
		return NULL;

	if (!object->IsValid()) {
		delete object;
		return NULL;
	}

	return object;
}


/**
 * @brief Equality with another ObjectID; matches only on identical SourceFunctionID values.
 *
 * @param _other  Polymorphic identity to compare with.
 * @return true if @a _other is a SourceFunctionID with matching path and name.
 */
bool
SourceFunctionID::operator==(const ObjectID& _other) const
{
	const SourceFunctionID* other = dynamic_cast<const SourceFunctionID*>(
		&_other);
	return other != NULL && fPath == other->fPath
		&& fFunctionName == other->fFunctionName;
}


// #pragma mark - ImageFunctionID


/**
 * @brief Reconstruct an ImageFunctionID from an archive.
 *
 * @param archive  Source archive.
 */
ImageFunctionID::ImageFunctionID(const BMessage& archive)
	:
	FunctionID(archive)
{
}


/**
 * @brief Construct from an image name and function name.
 *
 * @param imageName     Name of the image (executable or library).
 * @param functionName  Function name.
 */
ImageFunctionID::ImageFunctionID(const BString& imageName,
	const BString& functionName)
	:
	FunctionID(imageName, functionName)
{
}


/** @brief Virtual destructor. */
ImageFunctionID::~ImageFunctionID()
{
}


/**
 * @brief BArchivable factory hook that returns a new ImageFunctionID.
 *
 * @param archive  Archive to instantiate from.
 * @return A new ImageFunctionID on success, NULL on allocation failure or
 *         when the archive does not represent a valid identity.
 */
/*static*/ BArchivable*
ImageFunctionID::Instantiate(BMessage* archive)
{
	if (archive == NULL)
		return NULL;

	ImageFunctionID* object = new(std::nothrow) ImageFunctionID(*archive);
	if (object == NULL)
		return NULL;

	if (!object->IsValid()) {
		delete object;
		return NULL;
	}

	return object;
}


/**
 * @brief Equality with another ObjectID; matches only on identical ImageFunctionID values.
 *
 * @param _other  Polymorphic identity to compare with.
 * @return true if @a _other is an ImageFunctionID with matching image and name.
 */
bool
ImageFunctionID::operator==(const ObjectID& _other) const
{
	const ImageFunctionID* other = dynamic_cast<const ImageFunctionID*>(
		&_other);
	return other != NULL && fPath == other->fPath
		&& fFunctionName == other->fFunctionName;
}
