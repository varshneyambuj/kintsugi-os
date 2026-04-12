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
 *   Copyright 2013, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file PackageInfo.cpp
 * @brief Complete package metadata container for the Kintsugi package system.
 *
 * BPackageInfo holds all metadata fields defined in a .PackageInfo file or an
 * HPKG package: name, version, architecture, dependencies, writable-file
 * declarations, users/groups, and more.  It can be populated from a config
 * string, a config BFile, an HPKG package file (current and v1 formats), or a
 * BMessage archive, and serialised back to each of those forms.
 *
 * @see BPackageInfoContentHandler, BDaemonClient
 */


#include <package/PackageInfo.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <new>

#include <File.h>
#include <Entry.h>
#include <Message.h>
#include <package/hpkg/NoErrorOutput.h>
#include <package/hpkg/PackageReader.h>
#include <package/hpkg/v1/PackageInfoContentHandler.h>
#include <package/hpkg/v1/PackageReader.h>
#include <package/PackageInfoContentHandler.h>

#include "PackageInfoParser.h"
#include "PackageInfoStringBuilder.h"


namespace BPackageKit {


const char* const BPackageInfo::kElementNames[B_PACKAGE_INFO_ENUM_COUNT] = {
	"name",
	"summary",
	"description",
	"vendor",
	"packager",
	"architecture",
	"version",
	"copyrights",
	"licenses",
	"provides",
	"requires",
	"supplements",
	"conflicts",
	"freshens",
	"replaces",
	"flags",
	"urls",
	"source-urls",
	"checksum",		// not being parsed, computed externally
	NULL,			// install-path -- not settable via .PackageInfo
	"base-package",
	"global-writable-files",
	"user-settings-files",
	"users",
	"groups",
	"post-install-scripts",
	"pre-uninstall-scripts"
};


const char* const
BPackageInfo::kArchitectureNames[B_PACKAGE_ARCHITECTURE_ENUM_COUNT] = {
	"any",
	"x86",
	"x86_gcc2",
	"source",
	"x86_64",
	"ppc",
	"arm",
	"m68k",
	"sparc",
	"arm64",
	"riscv64"
};


const char* const BPackageInfo::kWritableFileUpdateTypes[
		B_WRITABLE_FILE_UPDATE_TYPE_ENUM_COUNT] = {
	"keep-old",
	"manual",
	"auto-merge",
};


// #pragma mark - FieldName


struct BPackageInfo::FieldName {
	FieldName(const char* prefix, const char* suffix)
	{
		size_t prefixLength = strlen(prefix);
		size_t suffixLength = strlen(suffix);
		if (prefixLength + suffixLength >= sizeof(fFieldName)) {
			fFieldName[0] = '\0';
			return;
		}

		memcpy(fFieldName, prefix, prefixLength);
		memcpy(fFieldName + prefixLength, suffix, suffixLength);
		fFieldName[prefixLength + suffixLength] = '\0';
	}

	bool ReplaceSuffix(size_t prefixLength, const char* suffix)
	{
		size_t suffixLength = strlen(suffix);
		if (prefixLength + suffixLength >= sizeof(fFieldName)) {
			fFieldName[0] = '\0';
			return false;
		}

		memcpy(fFieldName + prefixLength, suffix, suffixLength);
		fFieldName[prefixLength + suffixLength] = '\0';
		return true;
	}

	bool IsValid() const
	{
		return fFieldName[0] != '\0';
	}

	operator const char*()
	{
		return fFieldName;
	}

private:
	char	fFieldName[64];
};


// #pragma mark - PackageFileLocation


struct BPackageInfo::PackageFileLocation {
	PackageFileLocation(const char* path)
		:
		fPath(path),
		fFD(-1)
	{
	}

	PackageFileLocation(int fd)
		:
		fPath(NULL),
		fFD(fd)
	{
	}

	const char* Path() const
	{
		return fPath;
	}

	int FD() const
	{
		return fFD;
	}

private:
	const char*	fPath;
	int			fFD;
};


// #pragma mark - BPackageInfo


/**
 * @brief Default constructor; creates an empty, uninitialised package-info object.
 */
BPackageInfo::BPackageInfo()
	:
	BArchivable(),
	fFlags(0),
	fArchitecture(B_PACKAGE_ARCHITECTURE_ENUM_COUNT),
	fCopyrightList(4),
	fLicenseList(4),
	fURLList(4),
	fSourceURLList(4),
	fGlobalWritableFileInfos(4),
	fUserSettingsFileInfos(4),
	fUsers(4),
	fGroups(4),
	fPostInstallScripts(4),
	fPreUninstallScripts(4),
	fProvidesList(20),
	fRequiresList(20),
	fSupplementsList(20),
	fConflictsList(4),
	fFreshensList(4),
	fReplacesList(4)
{
}


/**
 * @brief Reconstruct a BPackageInfo from a BMessage archive.
 *
 * Extracts all metadata fields from \a archive.  If any mandatory field is
 * missing or invalid, the error is reported through \a _error.
 *
 * @param archive  BMessage produced by Archive().
 * @param _error   Optional out-pointer receiving B_OK on success or an error
 *                 code on deserialisation failure.
 */
BPackageInfo::BPackageInfo(BMessage* archive, status_t* _error)
	:
	BArchivable(archive),
	fFlags(0),
	fArchitecture(B_PACKAGE_ARCHITECTURE_ENUM_COUNT),
	fCopyrightList(4),
	fLicenseList(4),
	fURLList(4),
	fSourceURLList(4),
	fGlobalWritableFileInfos(4),
	fUserSettingsFileInfos(4),
	fUsers(4),
	fGroups(4),
	fPostInstallScripts(4),
	fPreUninstallScripts(4),
	fProvidesList(20),
	fRequiresList(20),
	fSupplementsList(20),
	fConflictsList(4),
	fFreshensList(4),
	fReplacesList(4)
{
	status_t error;
	int32 architecture;
	if ((error = archive->FindString("name", &fName)) == B_OK
		&& (error = archive->FindString("summary", &fSummary)) == B_OK
		&& (error = archive->FindString("description", &fDescription)) == B_OK
		&& (error = archive->FindString("vendor", &fVendor)) == B_OK
		&& (error = archive->FindString("packager", &fPackager)) == B_OK
		&& (error = archive->FindString("basePackage", &fBasePackage)) == B_OK
		&& (error = archive->FindUInt32("flags", &fFlags)) == B_OK
		&& (error = archive->FindInt32("architecture", &architecture)) == B_OK
		&& (error = _ExtractVersion(archive, "version", 0, fVersion)) == B_OK
		&& (error = _ExtractStringList(archive, "copyrights", fCopyrightList))
			== B_OK
		&& (error = _ExtractStringList(archive, "licenses", fLicenseList))
			== B_OK
		&& (error = _ExtractStringList(archive, "urls", fURLList)) == B_OK
		&& (error = _ExtractStringList(archive, "source-urls", fSourceURLList))
			== B_OK
		&& (error = _ExtractGlobalWritableFileInfos(archive,
			"global-writable-files", fGlobalWritableFileInfos)) == B_OK
		&& (error = _ExtractUserSettingsFileInfos(archive, "user-settings-files",
			fUserSettingsFileInfos)) == B_OK
		&& (error = _ExtractUsers(archive, "users", fUsers)) == B_OK
		&& (error = _ExtractStringList(archive, "groups", fGroups)) == B_OK
		&& (error = _ExtractStringList(archive, "post-install-scripts",
			fPostInstallScripts)) == B_OK
		&& (error = _ExtractStringList(archive, "pre-uninstall-scripts",
			fPreUninstallScripts)) == B_OK
		&& (error = _ExtractResolvables(archive, "provides", fProvidesList))
			== B_OK
		&& (error = _ExtractResolvableExpressions(archive, "requires",
			fRequiresList)) == B_OK
		&& (error = _ExtractResolvableExpressions(archive, "supplements",
			fSupplementsList)) == B_OK
		&& (error = _ExtractResolvableExpressions(archive, "conflicts",
			fConflictsList)) == B_OK
		&& (error = _ExtractResolvableExpressions(archive, "freshens",
			fFreshensList)) == B_OK
		&& (error = _ExtractStringList(archive, "replaces", fReplacesList))
			== B_OK
		&& (error = archive->FindString("checksum", &fChecksum)) == B_OK
		&& (error = archive->FindString("install-path", &fInstallPath)) == B_OK
		&& (error = archive->FindString("file-name", &fFileName)) == B_OK) {
		if (architecture >= 0
			&& architecture <= B_PACKAGE_ARCHITECTURE_ENUM_COUNT) {
			fArchitecture = (BPackageArchitecture)architecture;
		} else
			error = B_BAD_DATA;
	}

	if (_error != NULL)
		*_error = error;
}


/**
 * @brief Destructor.
 */
BPackageInfo::~BPackageInfo()
{
}


/**
 * @brief Read and parse package info from a .PackageInfo BEntry.
 *
 * Opens the file at \a packageInfoEntry and delegates to ReadFromConfigFile(BFile&).
 *
 * @param packageInfoEntry  BEntry pointing to the .PackageInfo text file.
 * @param listener          Optional error listener for parse diagnostics.
 * @return B_OK on success, or an error code if the entry or file is invalid.
 */
status_t
BPackageInfo::ReadFromConfigFile(const BEntry& packageInfoEntry,
	ParseErrorListener* listener)
{
	status_t result = packageInfoEntry.InitCheck();
	if (result != B_OK)
		return result;

	BFile file(&packageInfoEntry, B_READ_ONLY);
	if ((result = file.InitCheck()) != B_OK)
		return result;

	return ReadFromConfigFile(file, listener);
}


/**
 * @brief Read and parse package info from an open .PackageInfo BFile.
 *
 * Reads the entire file into a BString and delegates to ReadFromConfigString().
 *
 * @param packageInfoFile  Open, readable BFile containing .PackageInfo text.
 * @param listener         Optional error listener for parse diagnostics.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, B_IO_ERROR on
 *         short read, or another error code on file access failure.
 */
status_t
BPackageInfo::ReadFromConfigFile(BFile& packageInfoFile,
	ParseErrorListener* listener)
{
	off_t size;
	status_t result = packageInfoFile.GetSize(&size);
	if (result != B_OK)
		return result;

	BString packageInfoString;
	char* buffer = packageInfoString.LockBuffer(size);
	if (buffer == NULL)
		return B_NO_MEMORY;

	if ((result = packageInfoFile.Read(buffer, size)) < size) {
		packageInfoString.UnlockBuffer(0);
		return result >= 0 ? B_IO_ERROR : result;
	}

	buffer[size] = '\0';
	packageInfoString.UnlockBuffer(size);

	return ReadFromConfigString(packageInfoString, listener);
}


/**
 * @brief Parse package info from a .PackageInfo format string.
 *
 * Resets the object via Clear() then passes the string through the internal
 * Parser, populating all fields it recognises.
 *
 * @param packageInfoString  String containing .PackageInfo-format text.
 * @param listener           Optional error listener for parse diagnostics.
 * @return B_OK on success, B_BAD_DATA on parse error, or B_NO_MEMORY.
 */
status_t
BPackageInfo::ReadFromConfigString(const BString& packageInfoString,
	ParseErrorListener* listener)
{
	Clear();

	Parser parser(listener);
	return parser.Parse(packageInfoString, this);
}


/**
 * @brief Read package info from an HPKG package file by path.
 *
 * @param path  Filesystem path to the .hpkg file.
 * @return B_OK on success, or an error code if the file could not be read.
 */
status_t
BPackageInfo::ReadFromPackageFile(const char* path)
{
	return _ReadFromPackageFile(PackageFileLocation(path));
}


/**
 * @brief Read package info from an HPKG package file by open file descriptor.
 *
 * @param fd  Open file descriptor of the .hpkg file.
 * @return B_OK on success, or an error code if the file could not be read.
 */
status_t
BPackageInfo::ReadFromPackageFile(int fd)
{
	return _ReadFromPackageFile(PackageFileLocation(fd));
}


/**
 * @brief Check that all mandatory fields are set and internally consistent.
 *
 * Verifies that name, summary, description, vendor, packager, architecture,
 * version, copyrights, licenses, and provides are all present and valid.
 * Also validates each global writable file, user settings file, and user entry.
 *
 * @return B_OK if valid, B_NO_INIT if mandatory fields are missing, or
 *         B_BAD_VALUE if a user/group constraint is violated.
 */
status_t
BPackageInfo::InitCheck() const
{
	if (fName.Length() == 0 || fSummary.Length() == 0
		|| fDescription.Length() == 0 || fVendor.Length() == 0
		|| fPackager.Length() == 0
		|| fArchitecture == B_PACKAGE_ARCHITECTURE_ENUM_COUNT
		|| fVersion.InitCheck() != B_OK
		|| fCopyrightList.IsEmpty() || fLicenseList.IsEmpty()
		|| fProvidesList.IsEmpty())
		return B_NO_INIT;

	// check global writable files
	int32 globalWritableFileCount = fGlobalWritableFileInfos.CountItems();
	for (int32 i = 0; i < globalWritableFileCount; i++) {
		const BGlobalWritableFileInfo* info
			= fGlobalWritableFileInfos.ItemAt(i);
		status_t error = info->InitCheck();
		if (error != B_OK)
			return error;
	}

	// check user settings files
	int32 userSettingsFileCount = fUserSettingsFileInfos.CountItems();
	for (int32 i = 0; i < userSettingsFileCount; i++) {
		const BUserSettingsFileInfo* info = fUserSettingsFileInfos.ItemAt(i);
		status_t error = info->InitCheck();
		if (error != B_OK)
			return error;
	}

	// check users
	int32 userCount = fUsers.CountItems();
	for (int32 i = 0; i < userCount; i++) {
		const BUser* user = fUsers.ItemAt(i);
		status_t error = user->InitCheck();
		if (error != B_OK)
			return B_NO_INIT;

		// make sure the user's groups are specified as groups
		const BStringList& userGroups = user->Groups();
		int32 groupCount = userGroups.CountStrings();
		for (int32 k = 0; k < groupCount; k++) {
			const BString& group = userGroups.StringAt(k);
			if (!fGroups.HasString(group))
				return B_BAD_VALUE;
		}
	}

	// check groups
	int32 groupCount = fGroups.CountStrings();
	for (int32 i = 0; i< groupCount; i++) {
		if (!BUser::IsValidUserName(fGroups.StringAt(i)))
			return B_BAD_VALUE;
	}

	return B_OK;
}


/**
 * @brief Return the package name.
 *
 * @return Const reference to the name string (always lower-case).
 */
const BString&
BPackageInfo::Name() const
{
	return fName;
}


/**
 * @brief Return the one-line package summary.
 *
 * @return Const reference to the summary string.
 */
const BString&
BPackageInfo::Summary() const
{
	return fSummary;
}


/**
 * @brief Return the long package description.
 *
 * @return Const reference to the description string.
 */
const BString&
BPackageInfo::Description() const
{
	return fDescription;
}


/**
 * @brief Return the vendor name.
 *
 * @return Const reference to the vendor string.
 */
const BString&
BPackageInfo::Vendor() const
{
	return fVendor;
}


/**
 * @brief Return the packager identity string.
 *
 * @return Const reference to the packager string.
 */
const BString&
BPackageInfo::Packager() const
{
	return fPackager;
}


/**
 * @brief Return the base-package name, if any.
 *
 * @return Const reference to the base package string.
 */
const BString&
BPackageInfo::BasePackage() const
{
	return fBasePackage;
}


/**
 * @brief Return the hex-encoded SHA-256 checksum of the package file.
 *
 * @return Const reference to the checksum string.
 */
const BString&
BPackageInfo::Checksum() const
{
	return fChecksum;
}


/**
 * @brief Return the install path override, if set.
 *
 * @return Const reference to the install-path string.
 */
const BString&
BPackageInfo::InstallPath() const
{
	return fInstallPath;
}


/**
 * @brief Return the file name, or the canonical file name if none was set.
 *
 * @return A BString with the package file name.
 */
BString
BPackageInfo::FileName() const
{
	return fFileName.IsEmpty() ? CanonicalFileName() : fFileName;
}


/**
 * @brief Return the package flags bitmask.
 *
 * @return The uint32 package flags value.
 */
uint32
BPackageInfo::Flags() const
{
	return fFlags;
}


/**
 * @brief Return the target architecture.
 *
 * @return The BPackageArchitecture enum value.
 */
BPackageArchitecture
BPackageInfo::Architecture() const
{
	return fArchitecture;
}


/**
 * @brief Return the architecture name string for the stored architecture.
 *
 * @return A C-string from kArchitectureNames, or NULL if the architecture
 *         value is out of range.
 */
const char*
BPackageInfo::ArchitectureName() const
{
	if ((int)fArchitecture < 0
		|| fArchitecture >= B_PACKAGE_ARCHITECTURE_ENUM_COUNT) {
		return NULL;
	}
	return kArchitectureNames[fArchitecture];
}


/**
 * @brief Return the package version.
 *
 * @return Const reference to the BPackageVersion.
 */
const BPackageVersion&
BPackageInfo::Version() const
{
	return fVersion;
}


/**
 * @brief Return the list of copyright strings.
 *
 * @return Const reference to the copyright BStringList.
 */
const BStringList&
BPackageInfo::CopyrightList() const
{
	return fCopyrightList;
}


/**
 * @brief Return the list of license identifiers.
 *
 * @return Const reference to the license BStringList.
 */
const BStringList&
BPackageInfo::LicenseList() const
{
	return fLicenseList;
}


/**
 * @brief Return the list of homepage/info URLs.
 *
 * @return Const reference to the URL BStringList.
 */
const BStringList&
BPackageInfo::URLList() const
{
	return fURLList;
}


/**
 * @brief Return the list of source-code URLs.
 *
 * @return Const reference to the source-URL BStringList.
 */
const BStringList&
BPackageInfo::SourceURLList() const
{
	return fSourceURLList;
}


/**
 * @brief Return the list of globally writable file/directory declarations.
 *
 * @return Const reference to the BGlobalWritableFileInfo object list.
 */
const BObjectList<BGlobalWritableFileInfo, true>&
BPackageInfo::GlobalWritableFileInfos() const
{
	return fGlobalWritableFileInfos;
}


/**
 * @brief Return the list of user settings file/directory declarations.
 *
 * @return Const reference to the BUserSettingsFileInfo object list.
 */
const BObjectList<BUserSettingsFileInfo, true>&
BPackageInfo::UserSettingsFileInfos() const
{
	return fUserSettingsFileInfos;
}


/**
 * @brief Return the list of system-user declarations.
 *
 * @return Const reference to the BUser object list.
 */
const BObjectList<BUser, true>&
BPackageInfo::Users() const
{
	return fUsers;
}


/**
 * @brief Return the list of system-group names.
 *
 * @return Const reference to the groups BStringList.
 */
const BStringList&
BPackageInfo::Groups() const
{
	return fGroups;
}


/**
 * @brief Return the list of post-install script paths.
 *
 * @return Const reference to the post-install-scripts BStringList.
 */
const BStringList&
BPackageInfo::PostInstallScripts() const
{
	return fPostInstallScripts;
}


/**
 * @brief Return the list of pre-uninstall script paths.
 *
 * @return Const reference to the pre-uninstall-scripts BStringList.
 */
const BStringList&
BPackageInfo::PreUninstallScripts() const
{
	return fPreUninstallScripts;
}


/**
 * @brief Return the list of resolvables this package provides.
 *
 * @return Const reference to the BPackageResolvable object list.
 */
const BObjectList<BPackageResolvable, true>&
BPackageInfo::ProvidesList() const
{
	return fProvidesList;
}


/**
 * @brief Return the list of resolvable expressions this package requires.
 *
 * @return Const reference to the BPackageResolvableExpression object list.
 */
const BObjectList<BPackageResolvableExpression, true>&
BPackageInfo::RequiresList() const
{
	return fRequiresList;
}


/**
 * @brief Return the list of resolvable expressions this package supplements.
 *
 * @return Const reference to the supplements BPackageResolvableExpression list.
 */
const BObjectList<BPackageResolvableExpression, true>&
BPackageInfo::SupplementsList() const
{
	return fSupplementsList;
}


/**
 * @brief Return the list of resolvable expressions this package conflicts with.
 *
 * @return Const reference to the conflicts BPackageResolvableExpression list.
 */
const BObjectList<BPackageResolvableExpression, true>&
BPackageInfo::ConflictsList() const
{
	return fConflictsList;
}


/**
 * @brief Return the list of resolvable expressions this package freshens.
 *
 * @return Const reference to the freshens BPackageResolvableExpression list.
 */
const BObjectList<BPackageResolvableExpression, true>&
BPackageInfo::FreshensList() const
{
	return fFreshensList;
}


/**
 * @brief Return the list of package names this package replaces.
 *
 * @return Const reference to the replaces BStringList.
 */
const BStringList&
BPackageInfo::ReplacesList() const
{
	return fReplacesList;
}


/**
 * @brief Build the canonical HPKG file name for this package.
 *
 * The canonical name has the form "name-version-architecture.hpkg".
 * Returns an empty BString if InitCheck() fails.
 *
 * @return A BString containing the canonical file name.
 */
BString
BPackageInfo::CanonicalFileName() const
{
	if (InitCheck() != B_OK)
		return BString();

	return BString().SetToFormat("%s-%s-%s.hpkg", fName.String(),
		fVersion.ToString().String(), kArchitectureNames[fArchitecture]);
}


/**
 * @brief Check whether this package satisfies the given resolvable expression.
 *
 * First checks for an explicit "pkg:name" match, then searches the provides
 * list for a match against \a expression.
 *
 * @param expression  The BPackageResolvableExpression to test against.
 * @return true if this package satisfies the expression, false otherwise.
 */
bool
BPackageInfo::Matches(const BPackageResolvableExpression& expression) const
{
	// check for an explicit match on the package
	if (expression.Name().StartsWith("pkg:")) {
		return fName == expression.Name().String() + 4
			&& expression.Matches(fVersion, fVersion);
	}

	// search for a matching provides
	int32 count = fProvidesList.CountItems();
	for (int32 i = 0; i < count; i++) {
		const BPackageResolvable* provides = fProvidesList.ItemAt(i);
		if (expression.Matches(*provides))
			return true;
	}

	return false;
}


/**
 * @brief Set the package name, converting it to lower-case.
 *
 * @param name  New package name.
 */
void
BPackageInfo::SetName(const BString& name)
{
	fName = name;
	fName.ToLower();
}


/**
 * @brief Set the one-line package summary.
 *
 * @param summary  New summary string.
 */
void
BPackageInfo::SetSummary(const BString& summary)
{
	fSummary = summary;
}


/**
 * @brief Set the long package description.
 *
 * @param description  New description string.
 */
void
BPackageInfo::SetDescription(const BString& description)
{
	fDescription = description;
}


/**
 * @brief Set the vendor name.
 *
 * @param vendor  New vendor string.
 */
void
BPackageInfo::SetVendor(const BString& vendor)
{
	fVendor = vendor;
}


/**
 * @brief Set the packager identity string.
 *
 * @param packager  New packager string.
 */
void
BPackageInfo::SetPackager(const BString& packager)
{
	fPackager = packager;
}


/**
 * @brief Set the base-package name.
 *
 * @param basePackage  New base package name.
 */
void
BPackageInfo::SetBasePackage(const BString& basePackage)
{
	fBasePackage = basePackage;
}


/**
 * @brief Set the hex-encoded SHA-256 checksum.
 *
 * @param checksum  New checksum string.
 */
void
BPackageInfo::SetChecksum(const BString& checksum)
{
	fChecksum = checksum;
}


/**
 * @brief Set the install-path override.
 *
 * @param installPath  New install path string.
 */
void
BPackageInfo::SetInstallPath(const BString& installPath)
{
	fInstallPath = installPath;
}


/**
 * @brief Set the package file name override.
 *
 * @param fileName  New file name string.
 */
void
BPackageInfo::SetFileName(const BString& fileName)
{
	fFileName = fileName;
}


/**
 * @brief Set the package version.
 *
 * @param version  New BPackageVersion.
 */
void
BPackageInfo::SetVersion(const BPackageVersion& version)
{
	fVersion = version;
}


/**
 * @brief Set the package flags bitmask.
 *
 * @param flags  New flags value.
 */
void
BPackageInfo::SetFlags(uint32 flags)
{
	fFlags = flags;
}


/**
 * @brief Set the target architecture.
 *
 * @param architecture  New BPackageArchitecture value.
 */
void
BPackageInfo::SetArchitecture(BPackageArchitecture architecture)
{
	fArchitecture = architecture;
}


/**
 * @brief Remove all entries from the copyright list.
 */
void
BPackageInfo::ClearCopyrightList()
{
	fCopyrightList.MakeEmpty();
}


/**
 * @brief Append a copyright string to the copyright list.
 *
 * @param copyright  Copyright string to add.
 * @return B_OK on success, B_ERROR on allocation failure.
 */
status_t
BPackageInfo::AddCopyright(const BString& copyright)
{
	return fCopyrightList.Add(copyright) ? B_OK : B_ERROR;
}


/**
 * @brief Remove all entries from the license list.
 */
void
BPackageInfo::ClearLicenseList()
{
	fLicenseList.MakeEmpty();
}


/**
 * @brief Append a license identifier to the license list.
 *
 * @param license  License identifier to add.
 * @return B_OK on success, B_ERROR on allocation failure.
 */
status_t
BPackageInfo::AddLicense(const BString& license)
{
	return fLicenseList.Add(license) ? B_OK : B_ERROR;
}


/**
 * @brief Remove all entries from the URL list.
 */
void
BPackageInfo::ClearURLList()
{
	fURLList.MakeEmpty();
}


/**
 * @brief Append a homepage/info URL to the URL list.
 *
 * @param url  URL to add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddURL(const BString& url)
{
	return fURLList.Add(url) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Remove all entries from the source-URL list.
 */
void
BPackageInfo::ClearSourceURLList()
{
	fSourceURLList.MakeEmpty();
}


/**
 * @brief Append a source-code URL to the source-URL list.
 *
 * @param url  Source URL to add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddSourceURL(const BString& url)
{
	return fSourceURLList.Add(url) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Remove all globally writable file info entries.
 */
void
BPackageInfo::ClearGlobalWritableFileInfos()
{
	fGlobalWritableFileInfos.MakeEmpty();
}


/**
 * @brief Append a globally writable file/directory declaration.
 *
 * @param info  The BGlobalWritableFileInfo to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddGlobalWritableFileInfo(const BGlobalWritableFileInfo& info)
{
	BGlobalWritableFileInfo* newInfo
		= new (std::nothrow) BGlobalWritableFileInfo(info);
	if (newInfo == NULL || !fGlobalWritableFileInfos.AddItem(newInfo)) {
		delete newInfo;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Remove all user settings file info entries.
 */
void
BPackageInfo::ClearUserSettingsFileInfos()
{
	fUserSettingsFileInfos.MakeEmpty();
}


/**
 * @brief Append a user settings file/directory declaration.
 *
 * @param info  The BUserSettingsFileInfo to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddUserSettingsFileInfo(const BUserSettingsFileInfo& info)
{
	BUserSettingsFileInfo* newInfo
		= new (std::nothrow) BUserSettingsFileInfo(info);
	if (newInfo == NULL || !fUserSettingsFileInfos.AddItem(newInfo)) {
		delete newInfo;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Remove all user declarations.
 */
void
BPackageInfo::ClearUsers()
{
	fUsers.MakeEmpty();
}


/**
 * @brief Append a system-user declaration.
 *
 * @param user  The BUser to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddUser(const BUser& user)
{
	BUser* newUser = new (std::nothrow) BUser(user);
	if (newUser == NULL || !fUsers.AddItem(newUser)) {
		delete newUser;
		return B_NO_MEMORY;
	}

	return B_OK;
}


/**
 * @brief Remove all group declarations.
 */
void
BPackageInfo::ClearGroups()
{
	fGroups.MakeEmpty();
}


/**
 * @brief Append a system-group name.
 *
 * @param group  Group name to add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddGroup(const BString& group)
{
	return fGroups.Add(group) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Remove all post-install script entries.
 */
void
BPackageInfo::ClearPostInstallScripts()
{
	fPostInstallScripts.MakeEmpty();
}


/**
 * @brief Remove all pre-uninstall script entries.
 */
void
BPackageInfo::ClearPreUninstallScripts()
{
	fPreUninstallScripts.MakeEmpty();
}


/**
 * @brief Append a post-install script path.
 *
 * @param path  Path of the post-install script relative to the package root.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddPostInstallScript(const BString& path)
{
	return fPostInstallScripts.Add(path) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Append a pre-uninstall script path.
 *
 * @param path  Path of the pre-uninstall script relative to the package root.
 * @return B_OK on success, B_NO_MEMORY on allocation failure.
 */
status_t
BPackageInfo::AddPreUninstallScript(const BString& path)
{
	return fPreUninstallScripts.Add(path) ? B_OK : B_NO_MEMORY;
}


/**
 * @brief Remove all resolvable entries from the provides list.
 */
void
BPackageInfo::ClearProvidesList()
{
	fProvidesList.MakeEmpty();
}


/**
 * @brief Append a resolvable to the provides list.
 *
 * @param provides  The BPackageResolvable to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_ERROR on
 *         list-insertion failure.
 */
status_t
BPackageInfo::AddProvides(const BPackageResolvable& provides)
{
	BPackageResolvable* newProvides
		= new (std::nothrow) BPackageResolvable(provides);
	if (newProvides == NULL)
		return B_NO_MEMORY;

	return fProvidesList.AddItem(newProvides) ? B_OK : B_ERROR;
}


/**
 * @brief Remove all entries from the requires list.
 */
void
BPackageInfo::ClearRequiresList()
{
	fRequiresList.MakeEmpty();
}


/**
 * @brief Append a resolvable expression to the requires list.
 *
 * @param packageRequires  The BPackageResolvableExpression to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_ERROR.
 */
status_t
BPackageInfo::AddRequires(const BPackageResolvableExpression& packageRequires)
{
	BPackageResolvableExpression* newRequires
		= new (std::nothrow) BPackageResolvableExpression(packageRequires);
	if (newRequires == NULL)
		return B_NO_MEMORY;

	return fRequiresList.AddItem(newRequires) ? B_OK : B_ERROR;
}


/**
 * @brief Remove all entries from the supplements list.
 */
void
BPackageInfo::ClearSupplementsList()
{
	fSupplementsList.MakeEmpty();
}


/**
 * @brief Append a resolvable expression to the supplements list.
 *
 * @param supplements  The BPackageResolvableExpression to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_ERROR.
 */
status_t
BPackageInfo::AddSupplements(const BPackageResolvableExpression& supplements)
{
	BPackageResolvableExpression* newSupplements
		= new (std::nothrow) BPackageResolvableExpression(supplements);
	if (newSupplements == NULL)
		return B_NO_MEMORY;

	return fSupplementsList.AddItem(newSupplements) ? B_OK : B_ERROR;
}


/**
 * @brief Remove all entries from the conflicts list.
 */
void
BPackageInfo::ClearConflictsList()
{
	fConflictsList.MakeEmpty();
}


/**
 * @brief Append a resolvable expression to the conflicts list.
 *
 * @param conflicts  The BPackageResolvableExpression to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_ERROR.
 */
status_t
BPackageInfo::AddConflicts(const BPackageResolvableExpression& conflicts)
{
	BPackageResolvableExpression* newConflicts
		= new (std::nothrow) BPackageResolvableExpression(conflicts);
	if (newConflicts == NULL)
		return B_NO_MEMORY;

	return fConflictsList.AddItem(newConflicts) ? B_OK : B_ERROR;
}


/**
 * @brief Remove all entries from the freshens list.
 */
void
BPackageInfo::ClearFreshensList()
{
	fFreshensList.MakeEmpty();
}


/**
 * @brief Append a resolvable expression to the freshens list.
 *
 * @param freshens  The BPackageResolvableExpression to copy and add.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or B_ERROR.
 */
status_t
BPackageInfo::AddFreshens(const BPackageResolvableExpression& freshens)
{
	BPackageResolvableExpression* newFreshens
		= new (std::nothrow) BPackageResolvableExpression(freshens);
	if (newFreshens == NULL)
		return B_NO_MEMORY;

	return fFreshensList.AddItem(newFreshens) ? B_OK : B_ERROR;
}


/**
 * @brief Remove all entries from the replaces list.
 */
void
BPackageInfo::ClearReplacesList()
{
	fReplacesList.MakeEmpty();
}


/**
 * @brief Append a package name to the replaces list (stored lower-case).
 *
 * @param replaces  Package name to add.
 * @return B_OK on success, B_ERROR on allocation failure.
 */
status_t
BPackageInfo::AddReplaces(const BString& replaces)
{
	return fReplacesList.Add(BString(replaces).ToLower()) ? B_OK : B_ERROR;
}


/**
 * @brief Reset all fields to their empty/default state.
 */
void
BPackageInfo::Clear()
{
	fName.Truncate(0);
	fSummary.Truncate(0);
	fDescription.Truncate(0);
	fVendor.Truncate(0);
	fPackager.Truncate(0);
	fBasePackage.Truncate(0);
	fChecksum.Truncate(0);
	fInstallPath.Truncate(0);
	fFileName.Truncate(0);
	fFlags = 0;
	fArchitecture = B_PACKAGE_ARCHITECTURE_ENUM_COUNT;
	fVersion.Clear();
	fCopyrightList.MakeEmpty();
	fLicenseList.MakeEmpty();
	fURLList.MakeEmpty();
	fSourceURLList.MakeEmpty();
	fGlobalWritableFileInfos.MakeEmpty();
	fUserSettingsFileInfos.MakeEmpty();
	fUsers.MakeEmpty();
	fGroups.MakeEmpty();
	fPostInstallScripts.MakeEmpty();
	fPreUninstallScripts.MakeEmpty();
	fRequiresList.MakeEmpty();
	fProvidesList.MakeEmpty();
	fSupplementsList.MakeEmpty();
	fConflictsList.MakeEmpty();
	fFreshensList.MakeEmpty();
	fReplacesList.MakeEmpty();
}


/**
 * @brief Serialise all package metadata into a BMessage for IPC or persistence.
 *
 * @param archive  Target BMessage to populate.
 * @param deep     Passed through to BArchivable::Archive().
 * @return B_OK on success, or an error code if any field could not be stored.
 */
status_t
BPackageInfo::Archive(BMessage* archive, bool deep) const
{
	status_t error = BArchivable::Archive(archive, deep);
	if (error != B_OK)
		return error;

	if ((error = archive->AddString("name", fName)) != B_OK
		|| (error = archive->AddString("summary", fSummary)) != B_OK
		|| (error = archive->AddString("description", fDescription)) != B_OK
		|| (error = archive->AddString("vendor", fVendor)) != B_OK
		|| (error = archive->AddString("packager", fPackager)) != B_OK
		|| (error = archive->AddString("basePackage", fBasePackage)) != B_OK
		|| (error = archive->AddUInt32("flags", fFlags)) != B_OK
		|| (error = archive->AddInt32("architecture", fArchitecture)) != B_OK
		|| (error = _AddVersion(archive, "version", fVersion)) != B_OK
		|| (error = archive->AddStrings("copyrights", fCopyrightList))
			!= B_OK
		|| (error = archive->AddStrings("licenses", fLicenseList)) != B_OK
		|| (error = archive->AddStrings("urls", fURLList)) != B_OK
		|| (error = archive->AddStrings("source-urls", fSourceURLList))
			!= B_OK
		|| (error = _AddGlobalWritableFileInfos(archive,
			"global-writable-files", fGlobalWritableFileInfos)) != B_OK
		|| (error = _AddUserSettingsFileInfos(archive,
			"user-settings-files", fUserSettingsFileInfos)) != B_OK
		|| (error = _AddUsers(archive, "users", fUsers)) != B_OK
		|| (error = archive->AddStrings("groups", fGroups)) != B_OK
		|| (error = archive->AddStrings("post-install-scripts",
			fPostInstallScripts)) != B_OK
		|| (error = archive->AddStrings("pre-uninstall-scripts",
			fPreUninstallScripts)) != B_OK
		|| (error = _AddResolvables(archive, "provides", fProvidesList)) != B_OK
		|| (error = _AddResolvableExpressions(archive, "requires",
			fRequiresList)) != B_OK
		|| (error = _AddResolvableExpressions(archive, "supplements",
			fSupplementsList)) != B_OK
		|| (error = _AddResolvableExpressions(archive, "conflicts",
			fConflictsList)) != B_OK
		|| (error = _AddResolvableExpressions(archive, "freshens",
			fFreshensList)) != B_OK
		|| (error = archive->AddStrings("replaces", fReplacesList)) != B_OK
		|| (error = archive->AddString("checksum", fChecksum)) != B_OK
		|| (error = archive->AddString("install-path", fInstallPath)) != B_OK
		|| (error = archive->AddString("file-name", fFileName)) != B_OK) {
		return error;
	}

	return B_OK;
}


/**
 * @brief BArchivable instantiation hook.
 *
 * @param archive  Source BMessage.
 * @return A new BPackageInfo, or NULL if validation fails.
 */
/*static*/ BArchivable*
BPackageInfo::Instantiate(BMessage* archive)
{
	if (validate_instantiation(archive, "BPackageInfo"))
		return new(std::nothrow) BPackageInfo(archive);
	return NULL;
}


/**
 * @brief Serialise the package info to a .PackageInfo format string.
 *
 * Uses the internal StringBuilder to produce the canonical text representation
 * that can be stored in a .PackageInfo file or embedded in an HPKG archive.
 *
 * @param _string  Output BString that receives the formatted text.
 * @return B_OK on success, or an error code from the StringBuilder.
 */
status_t
BPackageInfo::GetConfigString(BString& _string) const
{
	return StringBuilder()
		.Write("name", fName)
		.Write("version", fVersion)
		.Write("summary", fSummary)
		.Write("description", fDescription)
		.Write("vendor", fVendor)
		.Write("packager", fPackager)
		.Write("architecture", kArchitectureNames[fArchitecture])
		.Write("copyrights", fCopyrightList)
		.Write("licenses", fLicenseList)
		.Write("urls", fURLList)
		.Write("source-urls", fSourceURLList)
		.Write("global-writable-files", fGlobalWritableFileInfos)
		.Write("user-settings-files", fUserSettingsFileInfos)
		.Write("users", fUsers)
		.Write("groups", fGroups)
		.Write("post-install-scripts", fPostInstallScripts)
		.Write("pre-uninstall-scripts", fPreUninstallScripts)
		.Write("provides", fProvidesList)
		.BeginRequires(fBasePackage)
			.Write("requires", fRequiresList)
		.EndRequires()
		.Write("supplements", fSupplementsList)
		.Write("conflicts", fConflictsList)
		.Write("freshens", fFreshensList)
		.Write("replaces", fReplacesList)
		.WriteFlags("flags", fFlags)
		.Write("checksum", fChecksum)
		.GetString(_string);
	// Note: fInstallPath and fFileName can not be specified via .PackageInfo.
}


/**
 * @brief Return the package info as a .PackageInfo format string.
 *
 * @return A BString containing the formatted package info, or empty on error.
 */
BString
BPackageInfo::ToString() const
{
	BString string;
	GetConfigString(string);
	return string;
}


/**
 * @brief Look up a BPackageArchitecture by its name string.
 *
 * Performs a case-insensitive search through kArchitectureNames.
 *
 * @param name           Architecture name to look up.
 * @param _architecture  Output parameter set to the matching enum value.
 * @return B_OK on success, B_NAME_NOT_FOUND if the name is not recognised.
 */
/*static*/ status_t
BPackageInfo::GetArchitectureByName(const BString& name,
	BPackageArchitecture& _architecture)
{
	for (int i = 0; i < B_PACKAGE_ARCHITECTURE_ENUM_COUNT; ++i) {
		if (name.ICompare(kArchitectureNames[i]) == 0) {
			_architecture = (BPackageArchitecture)i;
			return B_OK;
		}
	}
	return B_NAME_NOT_FOUND;
}


/**
 * @brief Parse a version string into a BPackageVersion.
 *
 * @param string              Version string in major.minor.micro~revision format.
 * @param revisionIsOptional  If true, a missing revision field is allowed.
 * @param _version            Output BPackageVersion populated on success.
 * @param listener            Optional error listener for parse diagnostics.
 * @return B_OK on success, B_BAD_DATA on parse error, or B_NO_MEMORY.
 */
/*static*/ status_t
BPackageInfo::ParseVersionString(const BString& string, bool revisionIsOptional,
	BPackageVersion& _version, ParseErrorListener* listener)
{
	return Parser(listener).ParseVersion(string, revisionIsOptional, _version);
}


/**
 * @brief Parse a resolvable string into a BPackageResolvable.
 *
 * @param string       Resolvable string (e.g. "libfoo >= 1.0").
 * @param _expression  Output BPackageResolvable populated on success.
 * @param listener     Optional error listener for parse diagnostics.
 * @return B_OK on success, B_BAD_DATA on parse error, or B_NO_MEMORY.
 */
/*static*/ status_t
BPackageInfo::ParseResolvableString(const BString& string,
	BPackageResolvable& _expression, ParseErrorListener* listener)
{
	return Parser(listener).ParseResolvable(string, _expression);
}


/**
 * @brief Parse a resolvable-expression string into a BPackageResolvableExpression.
 *
 * @param string       Expression string (e.g. "libfoo >= 1.0").
 * @param _expression  Output BPackageResolvableExpression populated on success.
 * @param listener     Optional error listener for parse diagnostics.
 * @return B_OK on success, B_BAD_DATA on parse error, or B_NO_MEMORY.
 */
/*static*/ status_t
BPackageInfo::ParseResolvableExpressionString(const BString& string,
	BPackageResolvableExpression& _expression, ParseErrorListener* listener)
{
	return Parser(listener).ParseResolvableExpression(string, _expression);
}


/**
 * @brief Internal helper that opens and parses an HPKG file using a PackageFileLocation.
 *
 * Tries the current HPKG format first; falls back to v1 format if the reader
 * returns B_MISMATCHED_VALUES (format-version mismatch).
 *
 * @param fileLocation  A PackageFileLocation wrapping either a path or a fd.
 * @return B_OK on success, or an error code from the package reader.
 */
status_t
BPackageInfo::_ReadFromPackageFile(const PackageFileLocation& fileLocation)
{
	BHPKG::BNoErrorOutput errorOutput;

	// try current package file format version
	{
		BHPKG::BPackageReader packageReader(&errorOutput);
		status_t error = fileLocation.Path() != NULL
			? packageReader.Init(fileLocation.Path())
			: packageReader.Init(fileLocation.FD(), false);
		if (error == B_OK) {
			BPackageInfoContentHandler handler(*this);
			return packageReader.ParseContent(&handler);
		}

		if (error != B_MISMATCHED_VALUES)
			return error;
	}

	// try package file format version 1
	BHPKG::V1::BPackageReader packageReader(&errorOutput);
	status_t error = fileLocation.Path() != NULL
		? packageReader.Init(fileLocation.Path())
		: packageReader.Init(fileLocation.FD(), false);
	if (error != B_OK)
		return error;

	BHPKG::V1::BPackageInfoContentHandler handler(*this);
	return packageReader.ParseContent(&handler);
}


/**
 * @brief Encode a BPackageVersion into a BMessage under prefixed field names.
 *
 * Stores major, minor, micro, pre-release, and revision as separate fields
 * named "\<field\>:major", "\<field\>:minor", etc.
 *
 * @param archive  Target BMessage.
 * @param field    Base field name prefix.
 * @param version  Version to encode.
 * @return B_OK on success, B_BAD_VALUE if the composed field name would overflow,
 *         or an error code from BMessage::AddString/AddUInt32.
 */
/*static*/ status_t
BPackageInfo::_AddVersion(BMessage* archive, const char* field,
	const BPackageVersion& version)
{
	// Storing BPackageVersion::ToString() would be nice, but the corresponding
	// constructor only works for valid versions and we might want to store
	// invalid versions as well.

	// major
	size_t fieldLength = strlen(field);
	FieldName fieldName(field, ":major");
	if (!fieldName.IsValid())
		return B_BAD_VALUE;

	status_t error = archive->AddString(fieldName, version.Major());
	if (error != B_OK)
		return error;

	// minor
	if (!fieldName.ReplaceSuffix(fieldLength, ":minor"))
		return B_BAD_VALUE;

	error = archive->AddString(fieldName, version.Minor());
	if (error != B_OK)
		return error;

	// micro
	if (!fieldName.ReplaceSuffix(fieldLength, ":micro"))
		return B_BAD_VALUE;

	error = archive->AddString(fieldName, version.Micro());
	if (error != B_OK)
		return error;

	// pre-release
	if (!fieldName.ReplaceSuffix(fieldLength, ":pre"))
		return B_BAD_VALUE;

	error = archive->AddString(fieldName, version.PreRelease());
	if (error != B_OK)
		return error;

	// revision
	if (!fieldName.ReplaceSuffix(fieldLength, ":revision"))
		return B_BAD_VALUE;

	return archive->AddUInt32(fieldName, version.Revision());
}


/**
 * @brief Encode a list of BPackageResolvable objects into a BMessage.
 *
 * @param archive     Target BMessage.
 * @param field       Base field name prefix for the resolvable arrays.
 * @param resolvables Source list of resolvables.
 * @return B_OK on success, B_BAD_VALUE on field-name overflow, or an error
 *         code from BMessage add methods.
 */
/*static*/ status_t
BPackageInfo::_AddResolvables(BMessage* archive, const char* field,
	const ResolvableList& resolvables)
{
	// construct the field names we need
	FieldName nameField(field, ":name");
	FieldName typeField(field, ":type");
	FieldName versionField(field, ":version");
	FieldName compatibleVersionField(field, ":compat");

	if (!nameField.IsValid() || !typeField.IsValid() || !versionField.IsValid()
		|| !compatibleVersionField.IsValid()) {
		return B_BAD_VALUE;
	}

	// add fields
	int32 count = resolvables.CountItems();
	for (int32 i = 0; i < count; i++) {
		const BPackageResolvable* resolvable = resolvables.ItemAt(i);
		status_t error;
		if ((error = archive->AddString(nameField, resolvable->Name())) != B_OK
			|| (error = _AddVersion(archive, versionField,
				resolvable->Version())) != B_OK
			|| (error = _AddVersion(archive, compatibleVersionField,
				resolvable->CompatibleVersion())) != B_OK) {
			return error;
		}
	}

	return B_OK;
}


/**
 * @brief Encode a list of BPackageResolvableExpression objects into a BMessage.
 *
 * @param archive     Target BMessage.
 * @param field       Base field name prefix.
 * @param expressions Source list of resolvable expressions.
 * @return B_OK on success, B_BAD_VALUE on overflow, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_AddResolvableExpressions(BMessage* archive, const char* field,
	const ResolvableExpressionList& expressions)
{
	// construct the field names we need
	FieldName nameField(field, ":name");
	FieldName operatorField(field, ":operator");
	FieldName versionField(field, ":version");

	if (!nameField.IsValid() || !operatorField.IsValid()
		|| !versionField.IsValid()) {
		return B_BAD_VALUE;
	}

	// add fields
	int32 count = expressions.CountItems();
	for (int32 i = 0; i < count; i++) {
		const BPackageResolvableExpression* expression = expressions.ItemAt(i);
		status_t error;
		if ((error = archive->AddString(nameField, expression->Name())) != B_OK
			|| (error = archive->AddInt32(operatorField,
				expression->Operator())) != B_OK
			|| (error = _AddVersion(archive, versionField,
				expression->Version())) != B_OK) {
			return error;
		}
	}

	return B_OK;
}


/**
 * @brief Encode a list of BGlobalWritableFileInfo objects into a BMessage.
 *
 * @param archive  Target BMessage.
 * @param field    Base field name prefix.
 * @param infos    Source list of globally writable file infos.
 * @return B_OK on success, B_BAD_VALUE on overflow, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_AddGlobalWritableFileInfos(BMessage* archive, const char* field,
	const GlobalWritableFileInfoList& infos)
{
	// construct the field names we need
	FieldName pathField(field, ":path");
	FieldName updateTypeField(field, ":updateType");
	FieldName isDirectoryField(field, ":isDirectory");

	if (!pathField.IsValid() || !updateTypeField.IsValid()
		|| !isDirectoryField.IsValid()) {
		return B_BAD_VALUE;
	}

	// add fields
	int32 count = infos.CountItems();
	for (int32 i = 0; i < count; i++) {
		const BGlobalWritableFileInfo* info = infos.ItemAt(i);
		status_t error;
		if ((error = archive->AddString(pathField, info->Path())) != B_OK
			|| (error = archive->AddInt32(updateTypeField, info->UpdateType()))
				!= B_OK
			|| (error = archive->AddBool(isDirectoryField,
				info->IsDirectory())) != B_OK) {
			return error;
		}
	}

	return B_OK;
}


/**
 * @brief Encode a list of BUserSettingsFileInfo objects into a BMessage.
 *
 * @param archive  Target BMessage.
 * @param field    Base field name prefix.
 * @param infos    Source list of user settings file infos.
 * @return B_OK on success, B_BAD_VALUE on overflow, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_AddUserSettingsFileInfos(BMessage* archive, const char* field,
	const UserSettingsFileInfoList& infos)
{
	// construct the field names we need
	FieldName pathField(field, ":path");
	FieldName templatePathField(field, ":templatePath");
	FieldName isDirectoryField(field, ":isDirectory");

	if (!pathField.IsValid() || !templatePathField.IsValid()
		|| !isDirectoryField.IsValid()) {
		return B_BAD_VALUE;
	}

	// add fields
	int32 count = infos.CountItems();
	for (int32 i = 0; i < count; i++) {
		const BUserSettingsFileInfo* info = infos.ItemAt(i);
		status_t error;
		if ((error = archive->AddString(pathField, info->Path())) != B_OK
			|| (error = archive->AddString(templatePathField,
				info->TemplatePath())) != B_OK
			|| (error = archive->AddBool(isDirectoryField,
				info->IsDirectory())) != B_OK) {
			return error;
		}
	}

	return B_OK;
}


/**
 * @brief Encode a list of BUser objects into a BMessage.
 *
 * @param archive  Target BMessage.
 * @param field    Base field name prefix.
 * @param users    Source list of BUser objects.
 * @return B_OK on success, B_BAD_VALUE on overflow, B_NO_MEMORY on group-join
 *         failure, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_AddUsers(BMessage* archive, const char* field,
	const UserList& users)
{
	// construct the field names we need
	FieldName nameField(field, ":name");
	FieldName realNameField(field, ":realName");
	FieldName homeField(field, ":home");
	FieldName shellField(field, ":shell");
	FieldName groupsField(field, ":groups");

	if (!nameField.IsValid() || !realNameField.IsValid() || !homeField.IsValid()
		|| !shellField.IsValid() || !groupsField.IsValid())
		return B_BAD_VALUE;

	// add fields
	int32 count = users.CountItems();
	for (int32 i = 0; i < count; i++) {
		const BUser* user = users.ItemAt(i);
		BString groups = user->Groups().Join(" ");
		if (groups.IsEmpty() && !user->Groups().IsEmpty())
			return B_NO_MEMORY;

		status_t error;
		if ((error = archive->AddString(nameField, user->Name())) != B_OK
			|| (error = archive->AddString(realNameField, user->RealName()))
				!= B_OK
			|| (error = archive->AddString(homeField, user->Home())) != B_OK
			|| (error = archive->AddString(shellField, user->Shell())) != B_OK
			|| (error = archive->AddString(groupsField, groups)) != B_OK) {
			return error;
		}
	}

	return B_OK;
}


/**
 * @brief Decode a BPackageVersion from prefixed fields in a BMessage.
 *
 * @param archive   Source BMessage.
 * @param field     Base field name prefix (e.g. "version").
 * @param index     Array index for repeated fields.
 * @param _version  Output version populated on success.
 * @return B_OK on success, B_BAD_VALUE on overflow, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_ExtractVersion(BMessage* archive, const char* field, int32 index,
	BPackageVersion& _version)
{
	// major
	size_t fieldLength = strlen(field);
	FieldName fieldName(field, ":major");
	if (!fieldName.IsValid())
		return B_BAD_VALUE;

	BString major;
	status_t error = archive->FindString(fieldName, index, &major);
	if (error != B_OK)
		return error;

	// minor
	if (!fieldName.ReplaceSuffix(fieldLength, ":minor"))
		return B_BAD_VALUE;

	BString minor;
	error = archive->FindString(fieldName, index, &minor);
	if (error != B_OK)
		return error;

	// micro
	if (!fieldName.ReplaceSuffix(fieldLength, ":micro"))
		return B_BAD_VALUE;

	BString micro;
	error = archive->FindString(fieldName, index, &micro);
	if (error != B_OK)
		return error;

	// pre-release
	if (!fieldName.ReplaceSuffix(fieldLength, ":pre"))
		return B_BAD_VALUE;

	BString preRelease;
	error = archive->FindString(fieldName, index, &preRelease);
	if (error != B_OK)
		return error;

	// revision
	if (!fieldName.ReplaceSuffix(fieldLength, ":revision"))
		return B_BAD_VALUE;

	uint32 revision;
	error = archive->FindUInt32(fieldName, index, &revision);
	if (error != B_OK)
		return error;

	_version.SetTo(major, minor, micro, preRelease, revision);
	return B_OK;
}


/**
 * @brief Extract a BStringList from a named field; treat missing field as empty.
 *
 * @param archive  Source BMessage.
 * @param field    Field name to read.
 * @param _list    Output list populated from the archive field.
 * @return B_OK on success or if the field is absent, otherwise a BMessage error.
 */
/*static*/ status_t
BPackageInfo::_ExtractStringList(BMessage* archive, const char* field,
	BStringList& _list)
{
	status_t error = archive->FindStrings(field, &_list);
	return error == B_NAME_NOT_FOUND ? B_OK : error;
		// If the field doesn't exist, that's OK.
}


/**
 * @brief Decode a list of BPackageResolvable objects from prefixed BMessage fields.
 *
 * @param archive       Source BMessage.
 * @param field         Base field name prefix.
 * @param _resolvables  Output list populated on success.
 * @return B_OK on success, B_BAD_VALUE on overflow, B_NO_MEMORY on allocation
 *         failure, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_ExtractResolvables(BMessage* archive, const char* field,
	ResolvableList& _resolvables)
{
	// construct the field names we need
	FieldName nameField(field, ":name");
	FieldName typeField(field, ":type");
	FieldName versionField(field, ":version");
	FieldName compatibleVersionField(field, ":compat");

	if (!nameField.IsValid() || !typeField.IsValid() || !versionField.IsValid()
		|| !compatibleVersionField.IsValid()) {
		return B_BAD_VALUE;
	}

	// get the number of items
	type_code type;
	int32 count;
	if (archive->GetInfo(nameField, &type, &count) != B_OK) {
		// the field is missing
		return B_OK;
	}

	// extract fields
	for (int32 i = 0; i < count; i++) {
		BString name;
		status_t error = archive->FindString(nameField, i, &name);
		if (error != B_OK)
			return error;

		BPackageVersion version;
		error = _ExtractVersion(archive, versionField, i, version);
		if (error != B_OK)
			return error;

		BPackageVersion compatibleVersion;
		error = _ExtractVersion(archive, compatibleVersionField, i,
			compatibleVersion);
		if (error != B_OK)
			return error;

		BPackageResolvable* resolvable = new(std::nothrow) BPackageResolvable(
			name, version, compatibleVersion);
		if (resolvable == NULL || !_resolvables.AddItem(resolvable)) {
			delete resolvable;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Decode a list of BPackageResolvableExpression objects from a BMessage.
 *
 * @param archive      Source BMessage.
 * @param field        Base field name prefix.
 * @param _expressions Output list populated on success.
 * @return B_OK on success, B_BAD_VALUE on overflow, B_BAD_DATA on invalid
 *         operator value, B_NO_MEMORY on allocation failure, or a BMessage error.
 */
/*static*/ status_t
BPackageInfo::_ExtractResolvableExpressions(BMessage* archive,
	const char* field, ResolvableExpressionList& _expressions)
{
	// construct the field names we need
	FieldName nameField(field, ":name");
	FieldName operatorField(field, ":operator");
	FieldName versionField(field, ":version");

	if (!nameField.IsValid() || !operatorField.IsValid()
		|| !versionField.IsValid()) {
		return B_BAD_VALUE;
	}

	// get the number of items
	type_code type;
	int32 count;
	if (archive->GetInfo(nameField, &type, &count) != B_OK) {
		// the field is missing
		return B_OK;
	}

	// extract fields
	for (int32 i = 0; i < count; i++) {
		BString name;
		status_t error = archive->FindString(nameField, i, &name);
		if (error != B_OK)
			return error;

		int32 operatorType;
		error = archive->FindInt32(operatorField, i, &operatorType);
		if (error != B_OK)
			return error;
		if (operatorType < 0
			|| operatorType > B_PACKAGE_RESOLVABLE_OP_ENUM_COUNT) {
			return B_BAD_DATA;
		}

		BPackageVersion version;
		error = _ExtractVersion(archive, versionField, i, version);
		if (error != B_OK)
			return error;

		BPackageResolvableExpression* expression
			= new(std::nothrow) BPackageResolvableExpression(name,
				(BPackageResolvableOperator)operatorType, version);
		if (expression == NULL || !_expressions.AddItem(expression)) {
			delete expression;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Decode a list of BGlobalWritableFileInfo objects from a BMessage.
 *
 * @param archive  Source BMessage.
 * @param field    Base field name prefix.
 * @param _infos   Output list populated on success.
 * @return B_OK on success, B_BAD_VALUE on overflow, B_BAD_DATA on invalid
 *         update type, B_NO_MEMORY on allocation failure, or a BMessage error.
 */
/*static*/ status_t
BPackageInfo::_ExtractGlobalWritableFileInfos(BMessage* archive,
	const char* field, GlobalWritableFileInfoList& _infos)
{
	// construct the field names we need
	FieldName pathField(field, ":path");
	FieldName updateTypeField(field, ":updateType");
	FieldName isDirectoryField(field, ":isDirectory");

	if (!pathField.IsValid() || !updateTypeField.IsValid()
		|| !isDirectoryField.IsValid()) {
		return B_BAD_VALUE;
	}

	// get the number of items
	type_code type;
	int32 count;
	if (archive->GetInfo(pathField, &type, &count) != B_OK) {
		// the field is missing
		return B_OK;
	}

	// extract fields
	for (int32 i = 0; i < count; i++) {
		BString path;
		status_t error = archive->FindString(pathField, i, &path);
		if (error != B_OK)
			return error;

		int32 updateType;
		error = archive->FindInt32(updateTypeField, i, &updateType);
		if (error != B_OK)
			return error;
		if (updateType < 0
			|| updateType > B_WRITABLE_FILE_UPDATE_TYPE_ENUM_COUNT) {
			return B_BAD_DATA;
		}

		bool isDirectory;
		error = archive->FindBool(isDirectoryField, i, &isDirectory);
		if (error != B_OK)
			return error;

		BGlobalWritableFileInfo* info
			= new(std::nothrow) BGlobalWritableFileInfo(path,
				(BWritableFileUpdateType)updateType, isDirectory);
		if (info == NULL || !_infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Decode a list of BUserSettingsFileInfo objects from a BMessage.
 *
 * @param archive  Source BMessage.
 * @param field    Base field name prefix.
 * @param _infos   Output list populated on success.
 * @return B_OK on success, B_BAD_VALUE on overflow, B_NO_MEMORY on allocation
 *         failure, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_ExtractUserSettingsFileInfos(BMessage* archive,
	const char* field, UserSettingsFileInfoList& _infos)
{
	// construct the field names we need
	FieldName pathField(field, ":path");
	FieldName templatePathField(field, ":templatePath");
	FieldName isDirectoryField(field, ":isDirectory");

	if (!pathField.IsValid() || !templatePathField.IsValid()
		|| !isDirectoryField.IsValid()) {
		return B_BAD_VALUE;
	}

	// get the number of items
	type_code type;
	int32 count;
	if (archive->GetInfo(pathField, &type, &count) != B_OK) {
		// the field is missing
		return B_OK;
	}

	// extract fields
	for (int32 i = 0; i < count; i++) {
		BString path;
		status_t error = archive->FindString(pathField, i, &path);
		if (error != B_OK)
			return error;

		BString templatePath;
		error = archive->FindString(templatePathField, i, &templatePath);
		if (error != B_OK)
			return error;

		bool isDirectory;
		error = archive->FindBool(isDirectoryField, i, &isDirectory);
		if (error != B_OK)
			return error;

		BUserSettingsFileInfo* info = isDirectory
			? new(std::nothrow) BUserSettingsFileInfo(path, true)
			: new(std::nothrow) BUserSettingsFileInfo(path, templatePath);
		if (info == NULL || !_infos.AddItem(info)) {
			delete info;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


/**
 * @brief Decode a list of BUser objects from prefixed BMessage fields.
 *
 * @param archive  Source BMessage.
 * @param field    Base field name prefix.
 * @param _users   Output list populated on success.
 * @return B_OK on success, B_BAD_VALUE on overflow, B_NO_MEMORY on allocation
 *         or group-split failure, or a BMessage error code.
 */
/*static*/ status_t
BPackageInfo::_ExtractUsers(BMessage* archive, const char* field,
	UserList& _users)
{
	// construct the field names we need
	FieldName nameField(field, ":name");
	FieldName realNameField(field, ":realName");
	FieldName homeField(field, ":home");
	FieldName shellField(field, ":shell");
	FieldName groupsField(field, ":groups");

	if (!nameField.IsValid() || !realNameField.IsValid() || !homeField.IsValid()
		|| !shellField.IsValid() || !groupsField.IsValid())
		return B_BAD_VALUE;

	// get the number of items
	type_code type;
	int32 count;
	if (archive->GetInfo(nameField, &type, &count) != B_OK) {
		// the field is missing
		return B_OK;
	}

	// extract fields
	for (int32 i = 0; i < count; i++) {
		BString name;
		status_t error = archive->FindString(nameField, i, &name);
		if (error != B_OK)
			return error;

		BString realName;
		error = archive->FindString(realNameField, i, &realName);
		if (error != B_OK)
			return error;

		BString home;
		error = archive->FindString(homeField, i, &home);
		if (error != B_OK)
			return error;

		BString shell;
		error = archive->FindString(shellField, i, &shell);
		if (error != B_OK)
			return error;

		BString groupsString;
		error = archive->FindString(groupsField, i, &groupsString);
		if (error != B_OK)
			return error;

		BStringList groups;
		if (!groupsString.IsEmpty() && !groupsString.Split(" ", false, groups))
			return B_NO_MEMORY;

		BUser* user = new(std::nothrow) BUser(name, realName, home, shell,
			groups);
		if (user == NULL || !_users.AddItem(user)) {
			delete user;
			return B_NO_MEMORY;
		}
	}

	return B_OK;
}


}	// namespace BPackageKit
