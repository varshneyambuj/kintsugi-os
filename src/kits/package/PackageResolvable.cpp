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
 * @file PackageResolvable.cpp
 * @brief Implementation of BPackageResolvable, a named capability provided by a package.
 *
 * A BPackageResolvable describes something that a package provides — such as
 * a library, a command, or the package itself — optionally qualified with a
 * version and a minimum compatible version. The name is always stored in
 * lower-case. It is the counterpart to BPackageResolvableExpression.
 *
 * @see BPackageResolvableExpression, BPackageInfo
 */


#include <package/PackageResolvable.h>

#include <package/hpkg/PackageInfoAttributeValue.h>
#include <package/PackageInfo.h>


namespace BPackageKit {


/**
 * @brief Default-construct an empty, invalid BPackageResolvable.
 */
BPackageResolvable::BPackageResolvable()
{
}


/**
 * @brief Construct a BPackageResolvable from a low-level data struct.
 *
 * @param data  The raw attribute data containing name, version, and
 *              compatible version fields.
 */
BPackageResolvable::BPackageResolvable(const BPackageResolvableData& data)
	:
	fName(data.name),
	fVersion(data.version),
	fCompatibleVersion(data.compatibleVersion)
{
}


/**
 * @brief Construct a BPackageResolvable with explicit fields.
 *
 * The name is converted to lower-case on construction.
 *
 * @param name               The capability name.
 * @param version            The exact version provided.
 * @param compatibleVersion  The oldest version still compatible with this one.
 */
BPackageResolvable::BPackageResolvable(const BString& name,
	const BPackageVersion& version, const BPackageVersion& compatibleVersion)
	:
	fName(name),
	fVersion(version),
	fCompatibleVersion(compatibleVersion)
{
	fName.ToLower();
}


/**
 * @brief Check whether this resolvable has been properly initialised.
 *
 * @return B_OK if the name is non-empty, B_NO_INIT otherwise.
 */
status_t
BPackageResolvable::InitCheck() const
{
	return fName.Length() > 0 ? B_OK : B_NO_INIT;
}


/**
 * @brief Return the capability name.
 *
 * @return Reference to the lower-case name string.
 */
const BString&
BPackageResolvable::Name() const
{
	return fName;
}


/**
 * @brief Return the exact version of this resolvable.
 *
 * @return Reference to the version object.
 */
const BPackageVersion&
BPackageResolvable::Version() const
{
	return fVersion;
}


/**
 * @brief Return the minimum compatible version of this resolvable.
 *
 * @return Reference to the compatible-version object.
 */
const BPackageVersion&
BPackageResolvable::CompatibleVersion() const
{
	return fCompatibleVersion;
}


/**
 * @brief Serialise this resolvable to a human-readable string.
 *
 * The format is "name=version compat>=compatibleVersion", omitting optional
 * parts when they are not set.
 *
 * @return The formatted string representation.
 */
BString
BPackageResolvable::ToString() const
{
	// the type is part of the name
	BString string = fName;

	if (fVersion.InitCheck() == B_OK)
		string << '=' << fVersion.ToString();

	if (fCompatibleVersion.InitCheck() == B_OK)
		string << " compat>=" << fCompatibleVersion.ToString();

	return string;
}


/**
 * @brief Parse a resolvable expression string and replace the current state.
 *
 * Delegates to BPackageInfo::ParseResolvableString to perform the actual
 * parsing.
 *
 * @param expressionString  The string to parse.
 * @return B_OK on success, or an error code if parsing fails.
 */
status_t
BPackageResolvable::SetToString(const BString& expressionString)
{
	fName.Truncate(0);
	fVersion.Clear();
	fCompatibleVersion.Clear();

	return BPackageInfo::ParseResolvableString(expressionString,
		*this);
}


/**
 * @brief Set the fields of this resolvable directly.
 *
 * The name is converted to lower-case.
 *
 * @param name               The new capability name.
 * @param version            The new exact version.
 * @param compatibleVersion  The new minimum compatible version.
 */
void
BPackageResolvable::SetTo(const BString& name, const BPackageVersion& version,
	const BPackageVersion& compatibleVersion)
{
	fName = name;
	fVersion = version;
	fCompatibleVersion = compatibleVersion;

	fName.ToLower();
}


/**
 * @brief Reset all fields to their default (empty) state.
 */
void
BPackageResolvable::Clear()
{
	fName.Truncate(0);
	fVersion.Clear();
	fCompatibleVersion.Clear();
}


}	// namespace BPackageKit
