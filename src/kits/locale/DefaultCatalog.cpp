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
 *   Copyright 2003-2009, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe, zooey@hirschkaefer.de
 *       Adrien Destugues, pulkomandy@gmail.com
 */


/**
 * @file DefaultCatalog.cpp
 * @brief Implementation of the built-in hash-map catalog type for the Locale Kit.
 *
 * DefaultCatalog is the primary catalog backend shipped as part of liblocale.so.
 * It serializes to a flat BMessage stream and can be loaded from disk files,
 * application resources (type 'CADA'), or file attributes. Fingerprint-based
 * version checking detects stale catalogs that no longer match the source code.
 *
 * @see HashMapCatalog, BCatalogData, MutableLocaleRoster
 */


#include <algorithm>
#include <new>

#include <AppFileInfo.h>
#include <Application.h>
#include <DataIO.h>
#include <Directory.h>
#include <File.h>
#include <FindDirectory.h>
#include <fs_attr.h>
#include <Message.h>
#include <Mime.h>
#include <Path.h>
#include <Resources.h>
#include <Roster.h>
#include <StackOrHeapArray.h>

#include <DefaultCatalog.h>
#include <MutableLocaleRoster.h>


#include <cstdio>


using std::min;
using std::max;
using std::pair;


/*!	This file implements the default catalog-type for the opentracker locale
	kit. Alternatively, this could be used as a full add-on, but currently this
	is provided as part of liblocale.so.
*/


/** @brief Subdirectory name inside the app folder that holds catalog files. */
static const char *kCatFolder = "catalogs";

/** @brief File extension for catalog files on disk. */
static const char *kCatExtension = ".catalog";


namespace BPrivate {


/** @brief MIME type string registered for the default catalog file format. */
const char *DefaultCatalog::kCatMimeType
	= "locale/x-vnd.Be.locale-catalog.default";

/** @brief Version of the BMessage-based catalog archive structure. */
static int16 kCatArchiveVersion = 1;
	// version of the catalog archive structure, bump this if you change it!

/** @brief Add-on priority; higher value = checked earlier during catalog lookup. */
const uint8 DefaultCatalog::kDefaultCatalogAddOnPriority = 1;
	// give highest priority to our embedded catalog-add-on


/*!	Constructs a DefaultCatalog with given signature and language and reads
	the catalog from disk.
	InitCheck() will be B_OK if catalog could be loaded successfully, it will
	give an appropriate error-code otherwise.
*/
/**
 * @brief Construct and load a DefaultCatalog for the given application entry_ref.
 *
 * Searches for the catalog file in the application's locale subdirectory, then
 * in the standard system-wide locale directories, and finally falls back to an
 * embedded resource inside the application binary.
 *
 * @param catalogOwner  entry_ref of the owning application or add-on.
 * @param language      BCP-47 language tag to load.
 * @param fingerprint   Version fingerprint; 0 accepts any version.
 */
DefaultCatalog::DefaultCatalog(const entry_ref &catalogOwner,
	const char *language, uint32 fingerprint)
	:
	HashMapCatalog("", language, fingerprint)
{
	// We created the catalog with an invalid signature, but we fix that now.
	SetSignature(catalogOwner);
	status_t status;

	// search for catalog living in sub-folder of app's folder:
	node_ref nref;
	nref.device = catalogOwner.device;
	nref.node = catalogOwner.directory;
	BDirectory appDir(&nref);
	BString catalogName("locale/");
	catalogName << kCatFolder
		<< "/" << fSignature
		<< "/" << fLanguageName
		<< kCatExtension;
	BPath catalogPath(&appDir, catalogName.String());
	status = ReadFromFile(catalogPath.Path());

	// search for catalogs in the standard ../data/locale/ directories
	// (packaged/non-packaged and system/home)
	if (status != B_OK)
		status = ReadFromStandardLocations();

	if (status != B_OK) {
		// give lowest priority to catalog embedded as resource in application
		// executable, so they can be overridden easily.
		status = ReadFromResource(catalogOwner);
	}

	fInitCheck = status;
}


/*!	Constructs a DefaultCatalog and reads it from the resources of the
	given entry-ref (which usually is an app- or add-on-file).
	InitCheck() will be B_OK if catalog could be loaded successfully, it will
	give an appropriate error-code otherwise.
*/
/**
 * @brief Construct a DefaultCatalog by reading from the resources of an entry.
 *
 * @param appOrAddOnRef  entry_ref of the file whose resources are searched.
 */
DefaultCatalog::DefaultCatalog(entry_ref *appOrAddOnRef)
	:
	HashMapCatalog("", "", 0)
{
	fInitCheck = ReadFromResource(*appOrAddOnRef);
}


/*!	Constructs an empty DefaultCatalog with given sig and language.
	This is used for editing/testing purposes.
	InitCheck() will always be B_OK.
*/
/**
 * @brief Construct an empty writable DefaultCatalog at the given path.
 *
 * Used by catalog editing tools. InitCheck() always returns B_OK.
 *
 * @param path       File system path where the catalog will be written.
 * @param signature  MIME signature for the new catalog.
 * @param language   BCP-47 language tag for the new catalog.
 */
DefaultCatalog::DefaultCatalog(const char *path, const char *signature,
	const char *language)
	:
	HashMapCatalog(signature, language, 0),
	fPath(path)
{
	fInitCheck = B_OK;
}


/**
 * @brief Destroy the DefaultCatalog.
 */
DefaultCatalog::~DefaultCatalog()
{
}


/**
 * @brief Read the MIME signature from the owning application file.
 *
 * Strips the "application/" supertype prefix so that only the subtype is
 * stored in fSignature (e.g. "x-vnd.MyApp").
 *
 * @param catalogOwner  entry_ref of the application whose signature is read.
 */
void
DefaultCatalog::SetSignature(const entry_ref &catalogOwner)
{
	// figure out mimetype from image
	BFile objectFile(&catalogOwner, B_READ_ONLY);
	BAppFileInfo objectInfo(&objectFile);
	char objectSignature[B_MIME_TYPE_LENGTH];
	if (objectInfo.GetSignature(objectSignature) != B_OK) {
		fSignature = "";
		return;
	}

	// drop supertype from mimetype (should be "application/"):
	char* stripSignature = objectSignature;
	while (*stripSignature != '/' && *stripSignature != '\0')
		stripSignature ++;

	if (*stripSignature == '\0')
		stripSignature = objectSignature;
	else
		stripSignature ++;

	fSignature = stripSignature;
}


/**
 * @brief Insert or replace a pre-constructed CatKey/value pair directly.
 *
 * Bypasses the normal string-parsing path used by SetString() and stores
 * the translated value verbatim.
 *
 * @param key         The CatKey identifying the source string.
 * @param translated  The translated string to store.
 * @return B_OK on success, or an error code from the underlying hash map.
 */
status_t
DefaultCatalog::SetRawString(const CatKey& key, const char *translated)
{
	return fCatMap.Put(key, translated);
}


/**
 * @brief Search the standard system locale directories for a matching catalog.
 *
 * Checks B_USER_NONPACKAGED_DATA_DIRECTORY, B_USER_DATA_DIRECTORY,
 * B_SYSTEM_NONPACKAGED_DATA_DIRECTORY, and B_SYSTEM_DATA_DIRECTORY in that
 * priority order.
 *
 * @return B_OK if a catalog was found and loaded, B_ENTRY_NOT_FOUND otherwise.
 */
status_t
DefaultCatalog::ReadFromStandardLocations()
{
	// search in data folders

	directory_which which[] = {
		B_USER_NONPACKAGED_DATA_DIRECTORY,
		B_USER_DATA_DIRECTORY,
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY
	};

	status_t status = B_ENTRY_NOT_FOUND;

	for (size_t i = 0; i < sizeof(which) / sizeof(which[0]); i++) {
		BPath path;
		if (find_directory(which[i], &path) == B_OK) {
			BString catalogName(path.Path());
			catalogName << "/locale/" << kCatFolder
				<< "/" << fSignature
				<< "/" << fLanguageName
				<< kCatExtension;
			status = ReadFromFile(catalogName.String());
			if (status == B_OK)
				break;
		}
	}

	return status;
}


/**
 * @brief Load catalog contents from a flat-message file on disk.
 *
 * Reads the entire file into memory, then calls Unflatten(). If loading
 * succeeds, UpdateAttributes() is called to keep the file's metadata current.
 *
 * @param path  Path to the catalog file, or NULL to use fPath.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the file does not exist,
 *         B_NO_MEMORY on allocation failure, or another error code.
 */
status_t
DefaultCatalog::ReadFromFile(const char *path)
{
	if (!path)
		path = fPath.String();

	BFile catalogFile;
	status_t res = catalogFile.SetTo(path, B_READ_ONLY);
	if (res != B_OK)
		return B_ENTRY_NOT_FOUND;

	fPath = path;

	off_t sz = 0;
	res = catalogFile.GetSize(&sz);
	if (res != B_OK) {
		return res;
	}

	BStackOrHeapArray<char, 0> buf(sz);
	if (!buf.IsValid())
		return B_NO_MEMORY;
	res = catalogFile.Read(buf, sz);
	if (res < B_OK)
		return res;
	if (res < sz)
		return res;
	BMemoryIO memIO(buf, sz);
	res = Unflatten(&memIO);

	if (res == B_OK) {
		// some information living in member variables needs to be copied
		// to attributes. Although these attributes should have been written
		// when creating the catalog, we make sure that they exist there:
		UpdateAttributes(catalogFile);
	}

	return res;
}


/**
 * @brief Load catalog contents from an embedded resource in the given file.
 *
 * Looks for a resource of type 'CADA' whose name matches fLanguageName.
 *
 * @param appOrAddOnRef  entry_ref of the application or add-on to read from.
 * @return B_OK on success, B_ENTRY_NOT_FOUND if the file cannot be opened,
 *         B_NAME_NOT_FOUND if the resource is absent, or another error.
 */
status_t
DefaultCatalog::ReadFromResource(const entry_ref &appOrAddOnRef)
{
	BFile file;
	status_t res = file.SetTo(&appOrAddOnRef, B_READ_ONLY);
	if (res != B_OK)
		return B_ENTRY_NOT_FOUND;

	BResources rsrc;
	res = rsrc.SetTo(&file);
	if (res != B_OK)
		return res;

	size_t sz;
	const void *buf = rsrc.LoadResource('CADA', fLanguageName, &sz);
	if (!buf)
		return B_NAME_NOT_FOUND;

	BMemoryIO memIO(buf, sz);
	res = Unflatten(&memIO);

	return res;
}


/**
 * @brief Write catalog contents to a flat-message file on disk.
 *
 * Flattens the catalog into a BMallocIO buffer, then writes it atomically to
 * the target file and updates the file's metadata attributes.
 *
 * @param path  Destination path, or NULL to use fPath.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DefaultCatalog::WriteToFile(const char *path)
{
	BFile catalogFile;
	if (path)
		fPath = path;
	status_t res = catalogFile.SetTo(fPath.String(),
		B_WRITE_ONLY | B_CREATE_FILE | B_ERASE_FILE);
	if (res != B_OK)
		return res;

	BMallocIO mallocIO;
	mallocIO.SetBlockSize(max(fCatMap.Size() * 20, (int32)256));
		// set a largish block-size in order to avoid reallocs
	res = Flatten(&mallocIO);
	if (res == B_OK) {
		ssize_t wsz;
		wsz = catalogFile.Write(mallocIO.Buffer(), mallocIO.BufferLength());
		if (wsz != (ssize_t)mallocIO.BufferLength())
			return B_FILE_ERROR;

		// set mimetype-, language- and signature-attributes:
		UpdateAttributes(catalogFile);
	}
	if (res == B_OK)
		UpdateAttributes(catalogFile);
	return res;
}


/**
 * @brief Write catalog contents as an embedded resource in the given file.
 *
 * The resource type is 'CADA' and the resource name is the mangled hash of
 * fLanguageName so that multiple language catalogs can coexist in one file.
 *
 * @param appOrAddOnRef  entry_ref of the target application or add-on file.
 * @return B_OK on success, or an error code on failure.
 */
status_t
DefaultCatalog::WriteToResource(const entry_ref &appOrAddOnRef)
{
	BFile file;
	status_t res = file.SetTo(&appOrAddOnRef, B_READ_WRITE);
	if (res != B_OK)
		return res;

	BResources rsrc;
	res = rsrc.SetTo(&file);
	if (res != B_OK)
		return res;

	BMallocIO mallocIO;
	mallocIO.SetBlockSize(max(fCatMap.Size() * 20, (int32)256));
		// set a largish block-size in order to avoid reallocs
	res = Flatten(&mallocIO);

	int mangledLanguage = CatKey::HashFun(fLanguageName.String(), 0);

	if (res == B_OK) {
		res = rsrc.AddResource('CADA', mangledLanguage,
			mallocIO.Buffer(), mallocIO.BufferLength(),
			BString(fLanguageName));
	}

	return res;
}


/*!	Writes mimetype, language-name and signature of catalog into the
	catalog-file.
*/
/**
 * @brief Update the MIME type, language, signature, and fingerprint attributes.
 *
 * Only writes each attribute if it is missing or has the wrong value, to avoid
 * unnecessary disk writes on read-only media.
 *
 * @param catalogFile  An open BFile for the catalog; must be writable.
 */
void
DefaultCatalog::UpdateAttributes(BFile& catalogFile)
{
	static const int bufSize = 256;
	char buf[bufSize];
	uint32 temp;
	if (catalogFile.ReadAttr("BEOS:TYPE", B_MIME_STRING_TYPE, 0, &buf,
			bufSize) <= 0
		|| strcmp(kCatMimeType, buf) != 0) {
		catalogFile.WriteAttr("BEOS:TYPE", B_MIME_STRING_TYPE, 0,
			kCatMimeType, strlen(kCatMimeType)+1);
	}
	if (catalogFile.ReadAttr(BLocaleRoster::kCatLangAttr, B_STRING_TYPE, 0,
			&buf, bufSize) <= 0
		|| fLanguageName != buf) {
		catalogFile.WriteAttr(BLocaleRoster::kCatLangAttr, B_STRING_TYPE, 0,
			fLanguageName.String(), fLanguageName.Length()+1);
	}
	if (catalogFile.ReadAttr(BLocaleRoster::kCatSigAttr, B_STRING_TYPE, 0,
			&buf, bufSize) <= 0
		|| fSignature != buf) {
		catalogFile.WriteAttr(BLocaleRoster::kCatSigAttr, B_STRING_TYPE, 0,
			fSignature.String(), fSignature.Length()+1);
	}
	if (catalogFile.ReadAttr(BLocaleRoster::kCatFingerprintAttr, B_UINT32_TYPE,
		0, &temp, sizeof(uint32)) <= 0) {
		catalogFile.WriteAttr(BLocaleRoster::kCatFingerprintAttr, B_UINT32_TYPE,
			0, &fFingerprint, sizeof(uint32));
	}
}


/**
 * @brief Serialize the catalog to the given BDataIO stream.
 *
 * Writes a header BMessage followed by one BMessage per catalog entry.
 * The fingerprint is recomputed before writing so it reflects the current
 * state of the map.
 *
 * @param dataIO  Output stream to write the serialized catalog to.
 * @return B_OK on success, or an error code if any operation fails.
 */
status_t
DefaultCatalog::Flatten(BDataIO *dataIO)
{
	UpdateFingerprint();
		// make sure we have the correct fingerprint before we flatten it

	status_t res;
	BMessage archive;
	int32 count = fCatMap.Size();
	res = archive.AddString("class", "DefaultCatalog");
	if (res == B_OK)
		res = archive.AddInt32("c:sz", count);
	if (res == B_OK)
		res = archive.AddInt16("c:ver", kCatArchiveVersion);
	if (res == B_OK)
		res = archive.AddString("c:lang", fLanguageName.String());
	if (res == B_OK)
		res = archive.AddString("c:sig", fSignature.String());
	if (res == B_OK)
		res = archive.AddInt32("c:fpr", fFingerprint);
	if (res == B_OK)
		res = archive.Flatten(dataIO);

	CatMap::Iterator iter = fCatMap.GetIterator();
	CatMap::Entry entry;
	while (res == B_OK && iter.HasNext()) {
		entry = iter.Next();
		archive.MakeEmpty();
		res = archive.AddString("c:ostr", entry.key.fString.String());
		if (res == B_OK)
			res = archive.AddString("c:ctxt", entry.key.fContext.String());
		if (res == B_OK)
			res = archive.AddString("c:comt", entry.key.fComment.String());
		if (res == B_OK)
			res = archive.AddInt32("c:hash", entry.key.fHashVal);
		if (res == B_OK)
			res = archive.AddString("c:tstr", entry.value.String());
		if (res == B_OK)
			res = archive.Flatten(dataIO);
	}

	return res;
}


/**
 * @brief Deserialize a catalog from the given BDataIO stream.
 *
 * Reads and verifies the header BMessage, then reads one BMessage per entry.
 * If a non-zero fingerprint was requested and does not match the stored
 * fingerprint, B_MISMATCHED_VALUES is returned.
 *
 * @param dataIO  Input stream containing the serialized catalog data.
 * @return B_OK on success, B_MISMATCHED_VALUES on fingerprint mismatch,
 *         B_BAD_DATA if the post-load computed fingerprint differs, or
 *         another error code on failure.
 */
status_t
DefaultCatalog::Unflatten(BDataIO *dataIO)
{
	fCatMap.Clear();
	int32 count = 0;
	int16 version;
	BMessage archiveMsg;
	status_t res = archiveMsg.Unflatten(dataIO);

	if (res == B_OK) {
		res = archiveMsg.FindInt16("c:ver", &version)
			|| archiveMsg.FindInt32("c:sz", &count);
	}
	if (res == B_OK) {
		fLanguageName = archiveMsg.FindString("c:lang");
		fSignature = archiveMsg.FindString("c:sig");
		uint32 foundFingerprint = archiveMsg.FindInt32("c:fpr");

		// if a specific fingerprint has been requested and the catalog does in
		// fact have a fingerprint, both are compared. If they mismatch, we do
		// not accept this catalog:
		if (foundFingerprint != 0 && fFingerprint != 0
			&& foundFingerprint != fFingerprint) {
			res = B_MISMATCHED_VALUES;
		} else
			fFingerprint = foundFingerprint;
	}

	if (res == B_OK && count > 0) {
		CatKey key;
		const char *keyStr;
		const char *keyCtx;
		const char *keyCmt;
		const char *translated;

		// fCatMap.resize(count);
			// There is no resize method in Haiku's HashMap to preallocate
			// memory.
		for (int i=0; res == B_OK && i < count; ++i) {
			res = archiveMsg.Unflatten(dataIO);
			if (res == B_OK)
				res = archiveMsg.FindString("c:ostr", &keyStr);
			if (res == B_OK)
				res = archiveMsg.FindString("c:ctxt", &keyCtx);
			if (res == B_OK)
				res = archiveMsg.FindString("c:comt", &keyCmt);
			if (res == B_OK)
				res = archiveMsg.FindInt32("c:hash", (int32*)&key.fHashVal);
			if (res == B_OK)
				res = archiveMsg.FindString("c:tstr", &translated);
			if (res == B_OK) {
				key.fString = keyStr;
				key.fContext = keyCtx;
				key.fComment = keyCmt;
				fCatMap.Put(key, translated);
			}
		}
		uint32 checkFP = ComputeFingerprint();
		if (fFingerprint != checkFP)
			return B_BAD_DATA;
	}
	return res;
}


/**
 * @brief Factory method: load an existing DefaultCatalog from disk.
 *
 * Called by MutableLocaleRoster when the "Default" catalog add-on is selected.
 *
 * @param catalogOwner  entry_ref of the owning application.
 * @param language      BCP-47 language tag to load.
 * @param fingerprint   Version fingerprint; 0 accepts any.
 * @return A newly allocated DefaultCatalog on success, or NULL on failure.
 */
BCatalogData *
DefaultCatalog::Instantiate(const entry_ref &catalogOwner, const char *language,
	uint32 fingerprint)
{
	DefaultCatalog *catalog
		= new(std::nothrow) DefaultCatalog(catalogOwner, language, fingerprint);
	if (catalog && catalog->InitCheck() != B_OK) {
		delete catalog;
		return NULL;
	}
	return catalog;
}


/**
 * @brief Factory method: create a new empty DefaultCatalog for editing.
 *
 * @param signature  MIME application signature for the new catalog.
 * @param language   BCP-47 language tag for the new catalog.
 * @return A newly allocated DefaultCatalog on success, or NULL on failure.
 */
BCatalogData *
DefaultCatalog::Create(const char *signature, const char *language)
{
	DefaultCatalog *catalog
		= new(std::nothrow) DefaultCatalog("", signature, language);
	if (catalog && catalog->InitCheck() != B_OK) {
		delete catalog;
		return NULL;
	}
	return catalog;
}


} // namespace BPrivate


/**
 * @brief C-linkage entry point that enumerates available catalog language files.
 *
 * Scans the application's locale subdirectory and all standard system locale
 * directories for .catalog files matching the given signature, adding each
 * discovered language name to \a availableLanguages.
 *
 * @param availableLanguages  Output BMessage; "language" strings are added.
 * @param sigPattern          Signature pattern to filter by (required).
 * @param langPattern         Language pattern (currently unused).
 * @param fingerprint         Fingerprint filter (currently unused).
 * @return B_OK on success, B_BAD_DATA if required parameters are NULL.
 */
extern "C" status_t
default_catalog_get_available_languages(BMessage* availableLanguages,
	const char* sigPattern, const char* langPattern, int32 fingerprint)
{
	if (availableLanguages == NULL || sigPattern == NULL)
		return B_BAD_DATA;

	app_info appInfo;
	be_app->GetAppInfo(&appInfo);
	node_ref nref;
	nref.device = appInfo.ref.device;
	nref.node = appInfo.ref.directory;
	BDirectory appDir(&nref);
	BString catalogName("locale/");
	catalogName << kCatFolder
		<< "/" << sigPattern ;
	BPath catalogPath(&appDir, catalogName.String());
	BEntry file(catalogPath.Path());
	BDirectory dir(&file);

	char fileName[B_FILE_NAME_LENGTH];
	while(dir.GetNextEntry(&file) == B_OK) {
		file.GetName(fileName);
		BString langName(fileName);
		langName.Replace(kCatExtension, "", 1);
		availableLanguages->AddString("language", langName);
	}

	// search in data folders

	directory_which which[] = {
		B_USER_NONPACKAGED_DATA_DIRECTORY,
		B_USER_DATA_DIRECTORY,
		B_SYSTEM_NONPACKAGED_DATA_DIRECTORY,
		B_SYSTEM_DATA_DIRECTORY
	};

	for (size_t i = 0; i < sizeof(which) / sizeof(which[0]); i++) {
		BPath path;
		if (find_directory(which[i], &path) == B_OK) {
			catalogName = BString("locale/")
				<< kCatFolder
				<< "/" << sigPattern;

			BPath catalogPath(path.Path(), catalogName.String());
			BEntry file(catalogPath.Path());
			BDirectory dir(&file);

			char fileName[B_FILE_NAME_LENGTH];
			while(dir.GetNextEntry(&file) == B_OK) {
				file.GetName(fileName);
				BString langName(fileName);
				langName.Replace(kCatExtension, "", 1);
				availableLanguages->AddString("language", langName);
			}
		}
	}

	return B_OK;
}
