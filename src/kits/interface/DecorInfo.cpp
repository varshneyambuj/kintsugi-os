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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Public domain source code.
 *   Author:
 *       Joseph "looncraz" Groover <looncraz@satx.rr.com>
 */


/**
 * @file DecorInfo.cpp
 * @brief Implementation of BDecorInfo, a class for querying decorator information
 *
 * BDecorInfo provides access to metadata about installed window decorators,
 * including their names, paths, and settings. It communicates with the decorator
 * manager to enumerate available decorators.
 *
 * @see BWindow
 */


#include <DecorInfo.h>

#include <new>
#include <stdio.h>

#include <Autolock.h>
#include <FindDirectory.h>
#include <Path.h>
#include <Resources.h>
#include <SystemCatalog.h>

#include <DecoratorPrivate.h>


#define B_TRANSLATION_CONTEXT "Default decorator about box"


namespace BPrivate {


/**
 * @brief Default constructor; creates an uninitialised DecorInfo.
 *
 * The object is not usable until SetTo() is called.  InitCheck() returns
 * B_NO_INIT after construction.
 *
 * @see SetTo(), InitCheck()
 */
DecorInfo::DecorInfo()
	:
	fVersion(0),
	fModificationTime(0),
	fInitStatus(B_NO_INIT)
{
}


/**
 * @brief Constructs a DecorInfo from a file-system path string.
 *
 * Resolves the path to an entry_ref, then calls _Init() to load
 * decorator metadata.  Pass the string "Default" to represent the
 * built-in default decorator.
 *
 * @param path  Absolute path to the decorator add-on, or "Default".
 * @see InitCheck(), SetTo()
 */
DecorInfo::DecorInfo(const BString& path)
	:
	fPath(path),
	fVersion(0),
	fModificationTime(0),
	fInitStatus(B_NO_INIT)
{
	BEntry entry(path.String(), true);
	entry.GetRef(&fRef);

	_Init();
}


/**
 * @brief Constructs a DecorInfo from an entry_ref.
 *
 * Converts @a ref to a path string and calls _Init() to load metadata.
 *
 * @param ref  A valid entry_ref pointing to the decorator add-on file.
 * @see InitCheck(), SetTo()
 */
DecorInfo::DecorInfo(const entry_ref& ref)
	:
	fRef(ref),
	fVersion(0),
	fModificationTime(0),
	fInitStatus(B_NO_INIT)
{
	BPath path(&ref);
	fPath = path.Path();

	_Init();
}


/**
 * @brief Destroys the DecorInfo object.
 */
DecorInfo::~DecorInfo()
{
}


/**
 * @brief Reinitialises the DecorInfo to refer to a different decorator by ref.
 *
 * Calls Unset() to clear all current data, then loads the new decorator
 * identified by @a ref.
 *
 * @param ref  An entry_ref pointing to the new decorator add-on.
 * @return The new InitCheck() status after loading.
 * @retval B_OK On success.
 * @see SetTo(BString), Unset(), InitCheck()
 */
status_t
DecorInfo::SetTo(const entry_ref& ref)
{
	Unset();

	BPath path(&ref);
	fPath = path.Path();
	fRef = ref;
	_Init();

	return InitCheck();
}


/**
 * @brief Reinitialises the DecorInfo to refer to a different decorator by path.
 *
 * Resolves @a path to an entry_ref and delegates to SetTo(const entry_ref&).
 *
 * @param path  Absolute path to the decorator add-on.
 * @return The new InitCheck() status after loading.
 * @retval B_OK On success.
 * @see SetTo(const entry_ref&), Unset()
 */
status_t
DecorInfo::SetTo(BString path)
{
	BEntry entry(path.String(), true);
	entry_ref ref;
	entry.GetRef(&ref);
	return SetTo(ref);
}


/**
 * @brief Returns the initialisation status of this DecorInfo.
 *
 * @return B_OK if the object was successfully initialised, B_NO_INIT if
 *         no decorator has been loaded yet, or another error code on failure.
 * @see SetTo()
 */
status_t
DecorInfo::InitCheck()	const
{
	return fInitStatus;
}


/**
 * @brief Resets all fields to their uninitialised defaults.
 *
 * After this call InitCheck() returns B_NO_INIT.  This method is called
 * internally by SetTo() before loading new data.
 *
 * @see SetTo(), InitCheck()
 */
void
DecorInfo::Unset()
{
	fRef = entry_ref();
	fPath = "";
	fName = "";
	fAuthors = "";
	fShortDescription = "";
	fLicenseURL = "";
	fLicenseName = "";
	fSupportURL = "";
	fVersion = 0;
	fModificationTime = 0;
	fInitStatus = B_NO_INIT;
}


/**
 * @brief Returns whether this DecorInfo represents the built-in default decorator.
 *
 * @return @c true if the path is exactly the sentinel string "Default".
 */
bool
DecorInfo::IsDefault() const
{
	return fPath == "Default";
}


/**
 * @brief Returns the file-system path of the decorator add-on.
 *
 * For the default decorator this returns the sentinel string "Default".
 *
 * @return The path as a BString.
 * @see Name(), Ref()
 */
BString
DecorInfo::Path() const
{
	return fPath;
}


/**
 * @brief Returns a pointer to the entry_ref of the decorator file.
 *
 * @return Pointer to the internal entry_ref, or @c NULL if the DecorInfo
 *         is uninitialised or represents the default decorator.
 * @see Path()
 */
const entry_ref*
DecorInfo::Ref() const
{
	if (InitCheck() != B_OK || IsDefault())
		return NULL;
	return &fRef;
}


/**
 * @brief Returns the human-readable name of the decorator.
 *
 * The name is read from the "name" field of the embedded BMessage resource
 * in the decorator add-on.  Falls back to the file-system leaf name when
 * the resource is absent.
 *
 * @return The decorator name as a BString.
 * @see ShortcutName(), ShortDescription()
 */
BString
DecorInfo::Name() const
{
	return fName;
}


/**
 * @brief Returns the short identifier used to select this decorator by name.
 *
 * For the default decorator returns "Default"; for file-system decorators
 * returns the entry_ref leaf name (the add-on file name).
 *
 * @return A short identifier BString, or the full name if the ref is unavailable.
 * @see Name()
 */
BString
DecorInfo::ShortcutName() const
{
	if (IsDefault())
		return "Default";
	else if (Ref() != NULL)
		return fRef.name;

	return fName;
}


/**
 * @brief Returns the authors of the decorator.
 *
 * Read from the "authors" field of the embedded info resource.
 *
 * @return Authors string, or an empty BString if not set.
 */
BString
DecorInfo::Authors() const
{
	return fAuthors;
}


/**
 * @brief Returns the short description of the decorator.
 *
 * Read from the "short_descr" field of the embedded info resource.
 *
 * @return A brief one-line description, or an empty BString if not set.
 * @see LongDescription()
 */
BString
DecorInfo::ShortDescription() const
{
	return fShortDescription;
}


/**
 * @brief Returns the long description of the decorator.
 *
 * Read from the "long_descr" field of the embedded info resource.
 *
 * @return A multi-line description, or an empty BString if not set.
 * @see ShortDescription()
 */
BString
DecorInfo::LongDescription() const
{
	return fLongDescription;
}


/**
 * @brief Returns the URL of the decorator's license.
 *
 * Read from the "lic_url" field of the embedded info resource.
 *
 * @return A URL string, or an empty BString if not set.
 * @see LicenseName()
 */
BString
DecorInfo::LicenseURL() const
{
	return fLicenseURL;
}


/**
 * @brief Returns the name of the license under which the decorator is distributed.
 *
 * Read from the "lic_name" field of the embedded info resource.
 *
 * @return The license name (e.g. "MIT"), or an empty BString if not set.
 * @see LicenseURL()
 */
BString
DecorInfo::LicenseName() const
{
	return fLicenseName;
}


/**
 * @brief Returns the support URL for the decorator.
 *
 * Read from the "support_url" field of the embedded info resource.
 *
 * @return A URL string, or an empty BString if not set.
 */
BString
DecorInfo::SupportURL() const
{
	return fSupportURL;
}


/**
 * @brief Returns the version number of the decorator.
 *
 * Read from the "version" field of the embedded info resource.
 *
 * @return The version as a float, or 0.0 if not set.
 */
float
DecorInfo::Version() const
{
	return fVersion;
}


/**
 * @brief Returns the last modification time of the decorator file.
 *
 * For the default decorator this reflects the modification time of the
 * app_server binary.
 *
 * @return A time_t value, or 0 if it could not be determined.
 */
time_t
DecorInfo::ModificationTime() const
{
	return fModificationTime;
}


/**
 * @brief Checks whether the on-disk decorator file has changed since it was loaded.
 *
 * Compares the stored modification time against the current file-system value.
 * If a change is detected, calls _Init(true) to reload the metadata.  Sets
 * @a deleted to @c true if the file no longer exists.
 *
 * @param deleted  Output: set to @c true if the decorator file was deleted.
 * @return @c true if a change was detected (including deletion), @c false if
 *         the file is unchanged or the object is uninitialised.
 * @see _Init()
 */
bool
DecorInfo::CheckForChanges(bool& deleted)
{
	if (InitCheck() != B_OK)
		return false;

	BEntry entry(&fRef);

	if (entry.InitCheck() != B_OK)
		return false;

	if (!entry.Exists()) {
		deleted = true;
		return true;
	}

	time_t modtime = 0;
	if (entry.GetModificationTime(&modtime) != B_OK) {
		fprintf(stderr, "DecorInfo::CheckForChanges()\tERROR: "
			"BEntry:GetModificationTime() failed\n");
		return false;
	}

	if (fModificationTime != modtime) {
		_Init(true);
		return true;
	}

	return false;
}


/**
 * @brief Internal method that loads decorator metadata from disk.
 *
 * For the built-in default decorator, hard-coded metadata is applied and
 * the modification time of the app_server binary is used.  For file-system
 * decorators the method reads a flattened BMessage stored as a B_MESSAGE_TYPE
 * resource named "be:decor:info" from the add-on file and extracts each
 * metadata field.
 *
 * @param isUpdate  Pass @c true when refreshing an already-initialised object
 *                  (called from CheckForChanges()); @c false for first-time init.
 * @see CheckForChanges(), SetTo()
 */
void
DecorInfo::_Init(bool isUpdate)
{
	if (!isUpdate && InitCheck() != B_NO_INIT) {
		// TODO: remove after validation
		fprintf(stderr, "DecorInfo::_Init()\tImproper init state\n");
		return;
	}

	BEntry entry;

	if (fPath == "Default") {
		if (isUpdate) {
			// should never happen
			fprintf(stderr, "DecorInfo::_Init(true)\tBUG BUG updating default"
				"decorator!?!?!\n");
			return;
		}

		fAuthors = "DarkWyrm, Stephan Aßmus, Clemens Zeidler, Ingo Weinhold";
		fLongDescription = "";
		fLicenseURL = "http://";
		fLicenseName = "MIT";
		fSupportURL = "http://www.haiku-os.org/";
		fVersion = 0.5;
		fInitStatus = B_OK;

		fName = gSystemCatalog.GetString(B_TRANSLATE_MARK("Default"),
			B_TRANSLATION_CONTEXT);
		fShortDescription = gSystemCatalog.GetString(B_TRANSLATE_MARK(
				"Default Haiku window decorator."),
			B_TRANSLATION_CONTEXT);

		// The following is to get the modification time of the app_server
		// and, thusly, the Default decorator...
		// If you can make it more simple, please do!
		BPath path;
		find_directory(B_SYSTEM_SERVERS_DIRECTORY, &path);
		path.Append("app_server");
		entry.SetTo(path.Path(), true);
		if (!entry.Exists()) {
			fprintf(stderr, "Server MIA the world has become its slave! "
				"Call the CIA!\n");
			return;
		}

		entry.GetModificationTime(&fModificationTime);
		return;
	}

	// Is a file system object...

	entry.SetTo(&fRef, true);	// follow link
	if (entry.InitCheck() != B_OK) {
		fInitStatus = entry.InitCheck();
		return;
	}

	if (!entry.Exists()) {
		if (isUpdate) {
			fprintf(stderr, "DecorInfo::_Init()\tERROR: decorator deleted"
					" after CheckForChanges() found it!\n");
			fprintf(stderr, "DecorInfo::_Init()\tERROR: DecorInfo will "
					"Unset\n");
			Unset();
		}
		return;
	}

	// update fRef to match file system object
	entry.GetRef(&fRef);
	entry.GetModificationTime(&fModificationTime);

	BResources resources(&fRef);
	if (resources.InitCheck() != B_OK) {
		fprintf(stderr, "DecorInfo::_Init()\t BResource InitCheck() failure\n");
		return;
	}

	size_t infoSize = 0;
	const void* infoData = resources.LoadResource(B_MESSAGE_TYPE,
		"be:decor:info", &infoSize);
	BMessage infoMessage;

	if (infoData == NULL || infoSize == 0
		|| infoMessage.Unflatten((const char*)infoData) != B_OK) {
		fprintf(stderr, "DecorInfo::_init()\tNo extended information found for"
			" \"%s\"\n", fRef.name);
	} else {
		infoMessage.FindString("name", &fName);
		infoMessage.FindString("authors", &fAuthors);
		infoMessage.FindString("short_descr", &fShortDescription);
		infoMessage.FindString("long_descr", &fLongDescription);
		infoMessage.FindString("lic_url", &fLicenseURL);
		infoMessage.FindString("lic_name", &fLicenseName);
		infoMessage.FindString("support_url", &fSupportURL);
		infoMessage.FindFloat ("version", &fVersion);
	}

	fInitStatus = B_OK;
	fName = fRef.name;
}


// #pragma mark - DecorInfoUtility


/**
 * @brief Constructs a DecorInfoUtility and optionally scans for installed decorators.
 *
 * Always creates a DecorInfo entry for the built-in default decorator.
 * If @a scanNow is @c true, ScanDecorators() is called immediately so that
 * the list is fully populated before the constructor returns.
 *
 * @param scanNow  Pass @c true to scan all decorator directories immediately;
 *                 @c false to defer scanning until the first query.
 * @see ScanDecorators(), CountDecorators()
 */
DecorInfoUtility::DecorInfoUtility(bool scanNow)
	:
	fHasScanned(false)
{
	// get default decorator from app_server
	DecorInfo* info = new(std::nothrow) DecorInfo("Default");
	if (info == NULL || info->InitCheck() != B_OK)	{
		delete info;
		fprintf(stderr, "DecorInfoUtility::constructor\tdefault decorator's "
			"DecorInfo failed InitCheck()\n");
		return;
	}

	fList.AddItem(info);

	if (scanNow)
		ScanDecorators();
}


/**
 * @brief Destroys the DecorInfoUtility and frees all cached DecorInfo objects.
 */
DecorInfoUtility::~DecorInfoUtility()
{
	BAutolock _(fLock);
	for	(int i = fList.CountItems() - 1; i >= 0; --i)
		delete fList.ItemAt(i);
}


/**
 * @brief Scans all known decorator directories and updates the internal list.
 *
 * Searches for decorator add-ons in the following directories (in order):
 * - B_SYSTEM_ADDONS_DIRECTORY/decorators
 * - B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY/decorators
 * - B_USER_ADDONS_DIRECTORY/decorators
 * - B_USER_NONPACKAGED_ADDONS_DIRECTORY/decorators
 *
 * For each directory that exists, delegates to _ScanDecorators().
 * Already-known decorators are updated via CheckForChanges(); new ones
 * are added.  Sets fHasScanned to @c true on completion.
 *
 * @return B_OK on success, or the first directory-scan error encountered.
 * @retval B_OK On success.
 * @see CountDecorators(), DecoratorAt()
 */
status_t
DecorInfoUtility::ScanDecorators()
{
	status_t result;

	BPath systemPath;
	result = find_directory(B_SYSTEM_ADDONS_DIRECTORY, &systemPath);
	if (result == B_OK)
		result = systemPath.Append("decorators");

	if (result == B_OK) {
		BDirectory systemDirectory(systemPath.Path());
		result = systemDirectory.InitCheck();
		if (result == B_OK) {
			result = _ScanDecorators(systemDirectory);
			if (result != B_OK) {
				fprintf(stderr, "DecorInfoUtility::ScanDecorators()\tERROR: %s\n",
					strerror(result));
				return result;
			}
		}
	}

	BPath systemNonPackagedPath;
	result = find_directory(B_SYSTEM_NONPACKAGED_ADDONS_DIRECTORY,
		&systemNonPackagedPath);
	if (result == B_OK)
		result = systemNonPackagedPath.Append("decorators");

	if (result == B_OK) {
		BDirectory systemNonPackagedDirectory(systemNonPackagedPath.Path());
		result = systemNonPackagedDirectory.InitCheck();
		if (result == B_OK) {
			result = _ScanDecorators(systemNonPackagedDirectory);
			if (result != B_OK) {
				fprintf(stderr, "DecorInfoUtility::ScanDecorators()\tERROR: %s\n",
					strerror(result));
				return result;
			}
		}
	}

	BPath userPath;
	result = find_directory(B_USER_ADDONS_DIRECTORY, &userPath);
	if (result == B_OK)
		result = userPath.Append("decorators");

	if (result == B_OK) {
		BDirectory userDirectory(userPath.Path());
		result = userDirectory.InitCheck();
		if (result == B_OK) {
			result = _ScanDecorators(userDirectory);
			if (result != B_OK) {
				fprintf(stderr, "DecorInfoUtility::ScanDecorators()\tERROR: %s\n",
					strerror(result));
				return result;
			}
		}
	}

	BPath userNonPackagedPath;
	result = find_directory(B_USER_NONPACKAGED_ADDONS_DIRECTORY,
		&userNonPackagedPath);
	if (result == B_OK)
		result = userNonPackagedPath.Append("decorators");

	if (result == B_OK) {
		BDirectory userNonPackagedDirectory(userNonPackagedPath.Path());
		result = userNonPackagedDirectory.InitCheck();
		if (result == B_OK) {
			result = _ScanDecorators(userNonPackagedDirectory);
			if (result != B_OK) {
				fprintf(stderr, "DecorInfoUtility::ScanDecorators()\tERROR: %s\n",
					strerror(result));
				return result;
			}
		}
	}

	fHasScanned = true;

	return B_OK;
}


/**
 * @brief Returns the total number of available decorators, including the default.
 *
 * Triggers ScanDecorators() on the first call if it has not been done yet.
 *
 * @return The count of decorator entries in the internal list.
 * @see DecoratorAt(), ScanDecorators()
 */
int32
DecorInfoUtility::CountDecorators()
{
	BAutolock _(fLock);
	if (!fHasScanned)
		ScanDecorators();

	return fList.CountItems();
}


/**
 * @brief Returns the DecorInfo at the given index in the internal list.
 *
 * Index 0 is always the built-in default decorator.
 *
 * @param index  Zero-based index into the decorator list.
 * @return Pointer to the DecorInfo, or @c NULL if @a index is out of range.
 * @see CountDecorators(), FindDecorator()
 */
DecorInfo*
DecorInfoUtility::DecoratorAt(int32 index)
{
	BAutolock _(fLock);
	return fList.ItemAt(index);
}


/**
 * @brief Searches the decorator list by path or display name.
 *
 * An empty @a string returns the current decorator.  The string "Default"
 * returns the built-in default.  Otherwise the list is searched first by
 * exact path, then by case-insensitive display name.
 *
 * @param string  The path or name to search for.
 * @return Pointer to the matching DecorInfo, or @c NULL if not found.
 * @see CurrentDecorator(), DefaultDecorator(), DecoratorAt()
 */
DecorInfo*
DecorInfoUtility::FindDecorator(const BString& string)
{
	if (string.Length() == 0)
		return CurrentDecorator();

	if (string == "Default")
		return DefaultDecorator();

	BAutolock _(fLock);
	if (!fHasScanned)
		ScanDecorators();

	// search by path
	DecorInfo* decor = _FindDecor(string);
	if (decor != NULL)
		return decor;

	// search by name
	for (int i = 1; i < fList.CountItems(); ++i) {
		decor = fList.ItemAt(i);
		if (string.ICompare(decor->Name()) == 0)
			return decor;
	}

	return NULL;
}


/**
 * @brief Returns the DecorInfo for the decorator that is currently active.
 *
 * Queries the app server for the active decorator's path via get_decorator()
 * and then looks it up in the internal list with FindDecorator().
 *
 * @return Pointer to the currently-active DecorInfo, or @c NULL on error.
 * @see SetDecorator(), DefaultDecorator()
 */
DecorInfo*
DecorInfoUtility::CurrentDecorator()
{
	BAutolock _(fLock);
	if (!fHasScanned)
		ScanDecorators();

	BString name;
	get_decorator(name);
	return FindDecorator(name);
}


/**
 * @brief Returns the DecorInfo for the built-in default decorator.
 *
 * The default decorator is always at index 0 in the internal list.
 *
 * @return Pointer to the default DecorInfo; never @c NULL after successful
 *         construction.
 * @see CurrentDecorator()
 */
DecorInfo*
DecorInfoUtility::DefaultDecorator()
{
	BAutolock _(fLock);
	return fList.ItemAt(0);
}


/**
 * @brief Tests whether @a decor is the currently active decorator.
 *
 * Compares the path of @a decor against the path of CurrentDecorator().
 *
 * @param decor  The DecorInfo to test.  May be @c NULL (returns @c false).
 * @return @c true if @a decor's path matches the active decorator's path.
 * @see CurrentDecorator()
 */
bool
DecorInfoUtility::IsCurrentDecorator(DecorInfo* decor)
{
	BAutolock _(fLock);
	if (decor == NULL)
		 return false;
	return decor->Path() == CurrentDecorator()->Path();
}


/**
 * @brief Activates the decorator represented by @a decor.
 *
 * Calls set_decorator() with the decorator's path, or with the sentinel
 * string "Default" if @a decor is the built-in default.
 *
 * @param decor  The DecorInfo to activate.  Must not be @c NULL.
 * @return B_OK on success.
 * @retval B_BAD_VALUE  If @a decor is @c NULL.
 * @see SetDecorator(int32), CurrentDecorator()
 */
status_t
DecorInfoUtility::SetDecorator(DecorInfo* decor)
{
	if (decor == NULL)
		return B_BAD_VALUE;

	BAutolock _(fLock);
	if (decor->IsDefault())
		return set_decorator("Default");

	return set_decorator(decor->Path());
}


/**
 * @brief Activates the decorator at the given list index.
 *
 * Requires that ScanDecorators() has already been run; returns B_ERROR
 * otherwise.
 *
 * @param index  Zero-based index into the decorator list.
 * @return B_OK on success.
 * @retval B_ERROR      If the list has not been scanned yet.
 * @retval B_BAD_INDEX  If @a index is out of range.
 * @see SetDecorator(DecorInfo*), CountDecorators()
 */
status_t
DecorInfoUtility::SetDecorator(int32 index)
{
	BAutolock _(fLock);
	if (!fHasScanned)
		return B_ERROR;

	DecorInfo* decor = DecoratorAt(index);
	if (decor == NULL)
		return B_BAD_INDEX;

	return SetDecorator(decor);
}


/**
 * @brief Previews a decorator on a specific window without making it permanent.
 *
 * Calls preview_decorator() with the decorator's path and the target window.
 *
 * @param decor   The DecorInfo describing the decorator to preview.
 *                Must not be @c NULL.
 * @param window  The window on which the preview is applied.
 * @return B_OK on success.
 * @retval B_BAD_VALUE  If @a decor is @c NULL.
 * @see SetDecorator()
 */
status_t
DecorInfoUtility::Preview(DecorInfo* decor, BWindow* window)
{
	if (decor == NULL)
		return B_BAD_VALUE;

	return preview_decorator(decor->Path(), window);
}


// #pargma mark - private


/**
 * @brief Searches the internal list for a decorator matching @a pathString exactly.
 *
 * Matches against both the cached path and the current file-system path
 * (to handle cases where the containing folder was moved).  The caller
 * must hold the internal lock before invoking this method.
 *
 * @param pathString  The path to search for, or "Default" for the built-in.
 * @return Pointer to the matching DecorInfo, or @c NULL if not found.
 * @see FindDecorator()
 */
DecorInfo*
DecorInfoUtility::_FindDecor(const BString& pathString)
{
	// find decor by path and path alone!
	if (!fLock.IsLocked()) {
		fprintf(stderr, "DecorInfoUtility::_find_decor()\tfailure to lock! - "
			"BUG BUG BUG\n");
		return NULL;
	}

	if (pathString == "Default")
		return fList.ItemAt(0);

	for (int i = 1; i < fList.CountItems(); ++i) {
		DecorInfo* decor = fList.ItemAt(i);
		// Find the DecoratorInfo either by its true current location or by
		// what we still think the location is (before we had a chance to
		// update). NOTE: This will only catch the case when the user moved the
		// folder in which the add-on file lives. It will not work when the user
		// moves the add-on file itself or renames it.
		BPath path(decor->Ref());
		if (path.Path() == pathString || decor->Path() == pathString)
			return decor;
	}

	return NULL;
}


/**
 * @brief Scans a single decorator directory and updates the internal list.
 *
 * On a rescan (fHasScanned is @c true) existing entries are checked for
 * changes and deleted ones are removed.  New entries found in
 * @a decoratorDirectory that are not already in the list are added.
 * Entries whose InitCheck() fails are discarded with a diagnostic message.
 *
 * @param decoratorDirectory  The directory to scan for decorator add-ons.
 * @return Always returns B_OK.
 * @see ScanDecorators()
 */
status_t
DecorInfoUtility::_ScanDecorators(BDirectory decoratorDirectory)
{
	BAutolock _(fLock);

	// First, run through our list and DecorInfos CheckForChanges()
	if (fHasScanned) {
		for (int i = fList.CountItems() - 1; i > 0; --i) {
			DecorInfo* decorInfo = fList.ItemAt(i);

			bool deleted = false;
			decorInfo->CheckForChanges(deleted);

			if (deleted) {
				fList.RemoveItem(decorInfo);
				delete decorInfo;
			}
		}
	}

	entry_ref ref;
	// Now, look at file system, skip the entries for which we already have
	// a DecorInfo in the list.
	while (decoratorDirectory.GetNextRef(&ref) == B_OK) {
		BPath path(&decoratorDirectory);
		status_t result = path.Append(ref.name);
		if (result != B_OK) {
			fprintf(stderr, "DecorInfoUtility::_ScanDecorators()\tFailed to"
				"append decorator file to path, skipping: %s.\n", strerror(result));
			continue;
		}
		if (_FindDecor(path.Path()) != NULL)
			continue;

		DecorInfo* decorInfo = new(std::nothrow) DecorInfo(ref);
		if (decorInfo == NULL || decorInfo->InitCheck() != B_OK) {
			fprintf(stderr, "DecorInfoUtility::_ScanDecorators()\tInitCheck() "
				"failure on decorator, skipping.\n");
			delete decorInfo;
			continue;
		}

		fList.AddItem(decorInfo);
	}

	return B_OK;
}


}	// namespace BPrivate
