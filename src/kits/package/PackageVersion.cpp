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
 * @file PackageVersion.cpp
 * @brief Implementation of BPackageVersion, the structured version number for packages.
 *
 * BPackageVersion represents a Haiku package version in the form
 * major.minor.micro~preRelease-revision and provides natural-order
 * comparison so the package manager can determine which version is newer.
 * All version components are stored in lower-case.
 *
 * @see BPackageInfo, BPackageResolvableExpression
 */


#include <package/PackageVersion.h>

#include <NaturalCompare.h>

#include <package/PackageInfo.h>
#include <package/hpkg/PackageInfoAttributeValue.h>


using BPrivate::NaturalCompare;


namespace BPackageKit {


/**
 * @brief Default-construct an uninitialised BPackageVersion with revision 0.
 */
BPackageVersion::BPackageVersion()
	:
	fRevision(0)
{
}


/**
 * @brief Construct a BPackageVersion from a low-level data struct.
 *
 * @param data  The raw attribute data containing major, minor, micro,
 *              preRelease, and revision fields.
 */
BPackageVersion::BPackageVersion(const BPackageVersionData& data)
{
	SetTo(data.major, data.minor, data.micro, data.preRelease, data.revision);
}


/**
 * @brief Construct a BPackageVersion by parsing a version string.
 *
 * @param versionString       The string to parse (e.g. "1.2.3~rc1-4").
 * @param revisionIsOptional  If true the revision component may be absent.
 */
BPackageVersion::BPackageVersion(const BString& versionString,
	bool revisionIsOptional)
{
	SetTo(versionString, revisionIsOptional);
}


/**
 * @brief Construct a BPackageVersion with explicit component strings.
 *
 * @param major       Major version component.
 * @param minor       Minor version component.
 * @param micro       Micro (patch) version component.
 * @param preRelease  Pre-release label (e.g. "rc1"); empty means released.
 * @param revision    Packaging revision number.
 */
BPackageVersion::BPackageVersion(const BString& major, const BString& minor,
	const BString& micro, const BString& preRelease, uint32 revision)
{
	SetTo(major, minor, micro, preRelease, revision);
}


/**
 * @brief Check whether this version has been properly initialised.
 *
 * @return B_OK if the major component is non-empty, B_NO_INIT otherwise.
 */
status_t
BPackageVersion::InitCheck() const
{
	return fMajor.Length() > 0 ? B_OK : B_NO_INIT;
}


/**
 * @brief Return the major version component.
 *
 * @return Reference to the major string.
 */
const BString&
BPackageVersion::Major() const
{
	return fMajor;
}


/**
 * @brief Return the minor version component.
 *
 * @return Reference to the minor string.
 */
const BString&
BPackageVersion::Minor() const
{
	return fMinor;
}


/**
 * @brief Return the micro (patch) version component.
 *
 * @return Reference to the micro string.
 */
const BString&
BPackageVersion::Micro() const
{
	return fMicro;
}


/**
 * @brief Return the pre-release label.
 *
 * @return Reference to the pre-release string; empty for released versions.
 */
const BString&
BPackageVersion::PreRelease() const
{
	return fPreRelease;
}


/**
 * @brief Return the packaging revision number.
 *
 * @return The revision, or 0 if unset.
 */
uint32
BPackageVersion::Revision() const
{
	return fRevision;
}


/**
 * @brief Compare this version to another using natural ordering.
 *
 * Component comparison order: major, minor, micro, preRelease (empty >
 * non-empty), revision.
 *
 * @param other  The version to compare against.
 * @return Negative if this < other, 0 if equal, positive if this > other.
 */
int
BPackageVersion::Compare(const BPackageVersion& other) const
{
	int diff = NaturalCompare(fMajor.String(), other.fMajor.String());
	if (diff != 0)
		return diff;

	diff = NaturalCompare(fMinor.String(), other.fMinor.String());
	if (diff != 0)
		return diff;

	diff = NaturalCompare(fMicro.String(), other.fMicro.String());
	if (diff != 0)
		return diff;

	// The pre-version works differently: The empty string is greater than any
	// non-empty string (e.g. "R1" is newer than "R1-rc2"). So we catch the
	// empty string cases first.
	if (fPreRelease.IsEmpty()) {
		if (!other.fPreRelease.IsEmpty())
			return 1;
	} else if (other.fPreRelease.IsEmpty()) {
		return -1;
	} else {
		// both are non-null -- compare normally
		diff = NaturalCompare(fPreRelease.String(), other.fPreRelease.String());
		if (diff != 0)
			return diff;
	}

	return (int)fRevision - (int)other.fRevision;
}


/**
 * @brief Serialise this version to a human-readable string.
 *
 * Returns an empty string when the version has not been initialised.
 * Format: "major[.minor[.micro]][~preRelease][-revision]".
 *
 * @return The formatted version string.
 */
BString
BPackageVersion::ToString() const
{
	if (InitCheck() != B_OK)
		return BString();

	BString string = fMajor;

	if (fMinor.Length() > 0) {
		string << '.' << fMinor;
		if (fMicro.Length() > 0)
			string << '.' << fMicro;
	}

	if (!fPreRelease.IsEmpty())
		string << '~' << fPreRelease;

	if (fRevision > 0)
		string << '-' << fRevision;

	return string;
}


/**
 * @brief Set all version components explicitly.
 *
 * All string components are converted to lower-case.
 *
 * @param major       Major version component.
 * @param minor       Minor version component.
 * @param micro       Micro (patch) version component.
 * @param preRelease  Pre-release label.
 * @param revision    Packaging revision number.
 */
void
BPackageVersion::SetTo(const BString& major, const BString& minor,
	const BString& micro, const BString& preRelease, uint32 revision)
{
	fMajor = major;
	fMinor = minor;
	fMicro = micro;
	fPreRelease = preRelease;
	fRevision = revision;

	fMajor.ToLower();
	fMinor.ToLower();
	fMicro.ToLower();
	fPreRelease.ToLower();
}


/**
 * @brief Parse a version string and replace the current state.
 *
 * @param versionString       The string to parse.
 * @param revisionIsOptional  If true the revision part may be absent.
 * @return B_OK on success, or an error code if parsing fails.
 */
status_t
BPackageVersion::SetTo(const BString& versionString, bool revisionIsOptional)
{
	Clear();
	return BPackageInfo::ParseVersionString(versionString, revisionIsOptional,
		*this);
}


/**
 * @brief Reset all components to their default (empty/zero) state.
 */
void
BPackageVersion::Clear()
{
	fMajor.Truncate(0);
	fMinor.Truncate(0);
	fMicro.Truncate(0);
	fPreRelease.Truncate(0);
	fRevision = 0;
}


}	// namespace BPackageKit
