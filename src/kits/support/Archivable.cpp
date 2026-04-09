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
 *   Copyright 2001-2012 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Rene Gollent (rene@gollent.com)
 *       Erik Jaesler (erik@cgsoftware.com)
 *       Alex Wilson (yourpalal2@gmail.com)
 */

/**
 * @file Archivable.cpp
 * @brief Implementation of BArchivable, BArchiver, BUnarchiver, and the global
 *        archiving helper functions.
 *
 * BArchivable is the mix-in class that defines the archiving protocol for all
 * kit objects that support flattening to/from a BMessage.  BArchiver and
 * BUnarchiver provide managed archiving sessions that handle object graphs with
 * shared or circular references.  The free functions instantiate_object(),
 * validate_instantiation(), and find_instantiation_func() provide the public
 * C-linkage entry points used by application code and the roster.
 *
 * @see BArchivable, BArchiver, BUnarchiver
 */


#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string>
#include <syslog.h>
#include <typeinfo>
#include <vector>

#include <AppFileInfo.h>
#include <Archivable.h>
#include <Entry.h>
#include <List.h>
#include <OS.h>
#include <Path.h>
#include <Roster.h>
#include <String.h>

#include <binary_compatibility/Support.h>

#include "ArchivingManagers.h"


using std::string;
using std::vector;

using namespace BPrivate::Archiving;

const char* B_CLASS_FIELD = "class";
const char* B_ADD_ON_FIELD = "add_on";
const int32 FUNC_NAME_LEN = 1024;

// TODO: consider moving these to a separate module, and making them more
//	full-featured (e.g., taking NS::ClassName::Function(Param p) instead
//	of just NS::ClassName)


/**
 * @brief Decode a compiler-mangled type name into a human-readable
 *        "Namespace::ClassName" string.
 *
 * Supports both GCC 2 (Q-encoded) and GCC 4+ (length-prefixed) ABI name
 * encodings.  Template classes are not yet handled.
 *
 * @param name  The raw mangled name as returned by typeid().name().
 * @param out   Receives the decoded "NS::Class" string on success.
 * @return B_OK on success, B_BAD_VALUE if the name cannot be decoded.
 */
static status_t
demangle_class_name(const char* name, BString& out)
{
// TODO: add support for template classes
//	_find__t12basic_string3ZcZt18string_char_traits1ZcZt24__default_alloc_template2b0i0PCccUlUl

	out = "";

#if __GNUC__ >= 4
	if (name[0] == 'N')
		name++;
	int nameLen;
	bool first = true;
	while ((nameLen = strtoul(name, (char**)&name, 10))) {
		if (!first)
			out += "::";
		else
			first = false;
		out.Append(name, nameLen);
		name += nameLen;
	}
	if (first)
		return B_BAD_VALUE;

#else
	if (name[0] == 'Q') {
		// The name is in a namespace
		int namespaceCount = 0;
		name++;
		if (name[0] == '_') {
			// more than 10 namespaces deep
			if (!isdigit(*++name))
				return B_BAD_VALUE;

			namespaceCount = strtoul(name, (char**)&name, 10);
			if (name[0] != '_')
				return B_BAD_VALUE;
		} else
			namespaceCount = name[0] - '0';

		name++;

		for (int i = 0; i < namespaceCount - 1; i++) {
			if (!isdigit(name[0]))
				return B_BAD_VALUE;

			int nameLength = strtoul(name, (char**)&name, 10);
			out.Append(name, nameLength);
			out += "::";
			name += nameLength;
		}
	}

	int nameLength = strtoul(name, (char**)&name, 10);
	out.Append(name, nameLength);
#endif

	return B_OK;
}


/**
 * @brief Encode a human-readable "NS::ClassName" string into the
 *        compiler's ABI-mangled form.
 *
 * Produces the mangled class name used as an infix inside an Instantiate
 * symbol.  GCC 2 uses Q-encoding; GCC 4+ uses length-prefixed components
 * without the 'N'/'E' wrappers (which are added by build_function_name()).
 * Template classes are not yet handled.
 *
 * @param name  The demangled class name (e.g. "BPrivate::MyClass").
 * @param out   Receives the mangled form.
 */
static void
mangle_class_name(const char* name, BString& out)
{
// TODO: add support for template classes
//	_find__t12basic_string3ZcZt18string_char_traits1ZcZt24__default_alloc_template2b0i0PCccUlUl

	//	Chop this:
	//		testthree::testfour::Testthree::Testfour
	//	up into little bite-sized pieces
	int count = 0;
	string origName(name);
	vector<string> spacenames;

	string::size_type pos = 0;
	string::size_type oldpos = 0;
	while (pos != string::npos) {
		pos = origName.find_first_of("::", oldpos);
		spacenames.push_back(string(origName, oldpos, pos - oldpos));
		pos = origName.find_first_not_of("::", pos);
		oldpos = pos;
		++count;
	}

	//	Now mangle it into this:
	//		9testthree8testfour9Testthree8Testfour
	//			(for __GNUC__ > 2)
	//			this isn't always the proper mangled class name, it should
	//			actually have an 'N' prefix and 'E' suffix if the name is
	//			in > 0 namespaces, but these would have to be removed in
	//			build_function_name() (the only place this function is called)
	//			so we don't add them.
	//	or this:
	//		Q49testthree8testfour9Testthree8Testfour
	//			(for __GNUC__ == 2)

	out = "";
#if __GNUC__ == 2
	if (count > 1) {
		out += 'Q';
		if (count > 10)
			out += '_';
		out << count;
		if (count > 10)
			out += '_';
	}
#endif

	for (unsigned int i = 0; i < spacenames.size(); ++i) {
		out << (int)spacenames[i].length();
		out += spacenames[i].c_str();
	}
}


/**
 * @brief Build the full ABI-mangled symbol name for a class's static
 *        Instantiate(BMessage*) function.
 *
 * Combines the mangled class name with the ABI-appropriate prefix/suffix
 * so the result can be passed directly to get_image_symbol().
 *
 * @param className  The demangled class name.
 * @param funcName   Receives the complete mangled symbol string.
 */
static void
build_function_name(const BString& className, BString& funcName)
{
	funcName = "";

	//	This is what we're after:
	//		Instantiate__Q28OpenBeOS11BArchivableP8BMessage
	mangle_class_name(className.String(), funcName);
#if __GNUC__ >= 4
	funcName.Prepend("_ZN");
	funcName.Append("11InstantiateE");
#else
	funcName.Prepend("Instantiate__");
#endif
	funcName.Append("P8BMessage");
}


/**
 * @brief Prepend "BPrivate::" to \a name if it begins with an underscore.
 *
 * Used for backwards compatibility: classes whose names begin with '_' were
 * historically in the BPrivate namespace and may be found there on a second
 * search pass.
 *
 * @param name  The class name to examine and possibly modify in place.
 * @return true if the prefix was added, false otherwise.
 */
static bool
add_private_namespace(BString& name)
{
	if (name.Compare("_", 1) != 0)
		return false;

	name.Prepend("BPrivate::");
	return true;
}


/**
 * @brief Look up a symbol by name in a single loaded image.
 *
 * @param funcName  The mangled symbol name to search for.
 * @param id        The image_id of the image to search.
 * @param err       Receives the status code from get_image_symbol().
 * @return The function pointer on success, NULL if the symbol is not found.
 */
static instantiation_func
find_function_in_image(BString& funcName, image_id id, status_t& err)
{
	instantiation_func instantiationFunc = NULL;
	err = get_image_symbol(id, funcName.String(), B_SYMBOL_TYPE_TEXT,
		(void**)&instantiationFunc);
	if (err != B_OK)
		return NULL;

	return instantiationFunc;
}


/**
 * @brief Verify that the MIME application signature of an image matches a
 *        requested signature string.
 *
 * If \a signature is NULL the check is skipped and B_OK is returned
 * unconditionally, allowing any image to match.
 *
 * @param signature  The expected MIME signature, or NULL to skip the check.
 * @param info       The image_info of the candidate image.
 * @return B_OK if the signatures match or \a signature is NULL,
 *         B_MISMATCHED_VALUES if they differ, or another error code if the
 *         image file cannot be opened or its signature cannot be read.
 */
static status_t
check_signature(const char* signature, image_info& info)
{
	if (signature == NULL) {
		// If it wasn't specified, anything "matches"
		return B_OK;
	}

	// Get image signature
	BFile file(info.name, B_READ_ONLY);
	status_t err = file.InitCheck();
	if (err != B_OK)
		return err;

	char imageSignature[B_MIME_TYPE_LENGTH];
	BAppFileInfo appFileInfo(&file);
	err = appFileInfo.GetSignature(imageSignature);
	if (err != B_OK) {
		syslog(LOG_ERR, "instantiate_object - couldn't get mime sig for %s",
			info.name);
		return err;
	}

	if (strcmp(signature, imageSignature) != 0)
		return B_MISMATCHED_VALUES;

	return B_OK;
}


namespace BPrivate {

/**
 * @brief Locate the Instantiate(BMessage*) function for \a className in the
 *        current team's loaded images.
 *
 * Iterates over every image loaded into the calling thread's team, builds the
 * ABI-mangled Instantiate symbol name for \a className, and calls
 * get_image_symbol() on each image.  If \a className begins with '_', a second
 * pass with "BPrivate::" prepended is performed for backwards compatibility.
 * If \a signature is non-NULL the image in which the symbol is found must also
 * have a matching MIME application signature.
 *
 * @param className  The demangled C++ class name (e.g. "MyKit::MyWidget").
 * @param signature  Optional MIME application signature to restrict the search
 *                   to a specific add-on/application.  May be NULL.
 * @param id         If non-NULL, receives the image_id of the image in which
 *                   the symbol was found.
 * @return A pointer to the Instantiate function on success, NULL on failure.
 *         errno is set to B_BAD_VALUE if \a className is NULL or
 *         get_thread_info() fails.
 * @see find_instantiation_func() (public overloads), instantiate_object()
 */
instantiation_func
find_instantiation_func(const char* className, const char* signature,
	image_id* id)
{
	if (className == NULL) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	thread_info threadInfo;
	status_t err = get_thread_info(find_thread(NULL), &threadInfo);
	if (err != B_OK) {
		errno = err;
		return NULL;
	}

	instantiation_func instantiationFunc = NULL;
	image_info imageInfo;

	BString name = className;
	for (int32 pass = 0; pass < 2; pass++) {
		BString funcName;
		build_function_name(name, funcName);

		// for each image_id in team_id
		int32 cookie = 0;
		while (instantiationFunc == NULL
			&& get_next_image_info(threadInfo.team, &cookie, &imageInfo)
				== B_OK) {
			instantiationFunc = find_function_in_image(funcName, imageInfo.id,
				err);
		}
		if (instantiationFunc != NULL) {
			// if requested, save the image id in
			// which the function was found
			if (id != NULL)
				*id = imageInfo.id;
			break;
		}

		// Check if we have a private class, and add the BPrivate namespace
		// (for backwards compatibility)
		if (!add_private_namespace(name))
			break;
	}

	if (instantiationFunc != NULL
		&& check_signature(signature, imageInfo) != B_OK)
		return NULL;

	return instantiationFunc;
}

}	// namespace BPrivate


//	#pragma mark - BArchivable


/**
 * @brief Default constructor — initialises the archiving token to NULL_TOKEN.
 *
 * Use this constructor when creating a BArchivable-derived object that is not
 * being reconstructed from an archive.
 */
BArchivable::BArchivable()
	:
	fArchivingToken(NULL_TOKEN)
{
}


/**
 * @brief Archive-restoring constructor — registers this object with the
 *        active BUnarchiver session if the archive is managed.
 *
 * If \a from is part of a managed archiving session (i.e. created by
 * BUnarchiver::PrepareArchive()), this constructor calls PrepareArchive() on
 * the message and registers the new object so the session can track it for
 * AllUnarchived() notification.
 *
 * @param from  The BMessage archive to restore state from.
 * @see BArchivable::Archive(), BUnarchiver
 */
BArchivable::BArchivable(BMessage* from)
	:
	fArchivingToken(NULL_TOKEN)
{
	if (BUnarchiver::IsArchiveManaged(from)) {
		BUnarchiver::PrepareArchive(from);
		BUnarchiver(from).RegisterArchivable(this);
	}
}


/**
 * @brief Destructor — no-op base implementation.
 */
BArchivable::~BArchivable()
{
}


/**
 * @brief Flatten this object's class name into \a into so it can be
 *        reconstructed by instantiate_object().
 *
 * Writes the demangled class name to the B_CLASS_FIELD key of \a into and,
 * if a managed archiving session is active, registers this object with the
 * session's BArchiver.  Derived classes must call BArchivable::Archive()
 * before adding their own fields.
 *
 * @param into  The destination BMessage.  Must not be NULL.
 * @param deep  If true, child objects should also be archived (convention for
 *              derived classes; unused at this level).
 * @return B_OK on success, B_BAD_VALUE if \a into is NULL, or another error
 *         code if the class name cannot be demangled.
 * @see Instantiate(), BArchiver
 */
status_t
BArchivable::Archive(BMessage* into, bool deep) const
{
	if (!into) {
		// TODO: logging/other error reporting?
		return B_BAD_VALUE;
	}

	if (BManagerBase::ArchiveManager(into))
		BArchiver(into).RegisterArchivable(this);

	BString name;
	status_t status = demangle_class_name(typeid(*this).name(), name);
	if (status != B_OK)
		return status;

	return into->AddString(B_CLASS_FIELD, name);
}


/**
 * @brief Factory method — always calls debugger() because BArchivable itself
 *        cannot be directly instantiated.
 *
 * Every concrete subclass must override this method and return a new instance
 * constructed from \a from.
 *
 * @param from  The archive message (unused in this base implementation).
 * @return NULL (never returns normally; triggers a debugger call).
 */
BArchivable*
BArchivable::Instantiate(BMessage* from)
{
	debugger("Can't create a plain BArchivable object");
	return NULL;
}


/**
 * @brief Dispatch hook for binary-compatibility virtual calls.
 *
 * Routes PERFORM_CODE_ALL_UNARCHIVED and PERFORM_CODE_ALL_ARCHIVED perform
 * codes to AllUnarchived() and AllArchived() respectively via the
 * perform_data_* structs.  Subclasses may extend this to add their own
 * perform codes.
 *
 * @param d    The perform code identifying the virtual to call.
 * @param arg  A pointer to the appropriate perform_data_* struct.
 * @return B_OK if the code was handled, B_NAME_NOT_FOUND otherwise.
 */
status_t
BArchivable::Perform(perform_code d, void* arg)
{
	switch (d) {
		case PERFORM_CODE_ALL_UNARCHIVED:
		{
			perform_data_all_unarchived* data =
				(perform_data_all_unarchived*)arg;

			data->return_value = BArchivable::AllUnarchived(data->archive);
			return B_OK;
		}

		case PERFORM_CODE_ALL_ARCHIVED:
		{
			perform_data_all_archived* data =
				(perform_data_all_archived*)arg;

			data->return_value = BArchivable::AllArchived(data->archive);
			return B_OK;
		}
	}

	return B_NAME_NOT_FOUND;
}


/**
 * @brief Called after all objects in a managed unarchiving session have been
 *        instantiated, allowing cross-object linkage to be completed.
 *
 * The default implementation does nothing and returns B_OK.  Override in
 * derived classes to perform post-instantiation wiring (e.g. re-attach child
 * views by token).
 *
 * @param archive  The original archive message for this object.
 * @return B_OK on success, or an error code to abort the session.
 * @see BUnarchiver, Archive()
 */
status_t
BArchivable::AllUnarchived(const BMessage* archive)
{
	return B_OK;
}


/**
 * @brief Called after all objects in a managed archiving session have been
 *        archived, allowing cross-object token references to be finalised.
 *
 * The default implementation does nothing and returns B_OK.  Override in
 * derived classes to add references to other archived objects by token.
 *
 * @param archive  The archive message for this object.
 * @return B_OK on success, or an error code to abort the session.
 * @see BArchiver, Archive()
 */
status_t
BArchivable::AllArchived(BMessage* archive) const
{
	return B_OK;
}


// #pragma mark - BArchiver


/**
 * @brief Construct a BArchiver bound to \a archive.
 *
 * If a BArchiveManager session is not already associated with \a archive a new
 * one is created.  The BArchiver forwards all token-assignment operations to
 * this manager.
 *
 * @param archive  The top-level BMessage that will receive the archived data.
 * @see BArchiver::Finish(), BArchiveManager
 */
BArchiver::BArchiver(BMessage* archive)
	:
	fManager(BManagerBase::ArchiveManager(archive)),
	fArchive(archive),
	fFinished(false)
{
	if (fManager == NULL)
		fManager = new BArchiveManager(this);
}


/**
 * @brief Destructor — calls ArchiverLeaving() with B_OK if Finish() was not
 *        called explicitly.
 *
 * This ensures the managed session is properly closed even when the archiver
 * goes out of scope without an explicit Finish() call.
 */
BArchiver::~BArchiver()
{
	if (!fFinished)
		fManager->ArchiverLeaving(this, B_OK);
}


/**
 * @brief Archive \a archivable and store its token under \a name in the
 *        managed archive message.
 *
 * If \a archivable has not yet been archived, GetTokenForArchivable() is
 * called to archive it and obtain a token.  The token is then stored as an
 * int32 field named \a name in the session's top-level archive message.
 *
 * @param name        The field name to use in the archive message.
 * @param archivable  The object to archive.  May be NULL (NULL_TOKEN stored).
 * @param deep        Passed to BArchivable::Archive(); archive children too.
 * @return B_OK on success, or an error code from GetTokenForArchivable() or
 *         BMessage::AddInt32().
 * @see GetTokenForArchivable(), BUnarchiver::FindObject()
 */
status_t
BArchiver::AddArchivable(const char* name, BArchivable* archivable, bool deep)
{
	int32 token;
	status_t err = GetTokenForArchivable(archivable, deep, token);

	if (err != B_OK)
		return err;

	return fArchive->AddInt32(name, token);
}


/**
 * @brief Obtain a numeric token for \a archivable, archiving it first if
 *        necessary.
 *
 * Delegates to BArchiveManager::ArchiveObject().  The returned token can be
 * stored in the archive message and later used by BUnarchiver::GetObject() to
 * retrieve the reconstructed object.
 *
 * @param archivable  The object to archive.  May be NULL (NULL_TOKEN returned).
 * @param deep        If true, child objects should be archived recursively.
 * @param _token      Receives the assigned token on success.
 * @return B_OK on success, or an error from BArchivable::Archive().
 * @see AddArchivable(), BUnarchiver::GetObject()
 */
status_t
BArchiver::GetTokenForArchivable(BArchivable* archivable,
	bool deep, int32& _token)
{
	return fManager->ArchiveObject(archivable, deep, _token);
}


/**
 * @brief Test whether \a archivable has already been archived in this session.
 *
 * @param archivable  The object to test.  NULL is considered "already archived".
 * @return true if \a archivable has a token in the current session, false
 *         otherwise.
 */
bool
BArchiver::IsArchived(BArchivable* archivable)
{
	return fManager->IsArchived(archivable);
}


/**
 * @brief Signal completion of the archiving session, propagating any error.
 *
 * Must be called exactly once per BArchiver; calling it more than once
 * triggers a debugger call.  After Finish() the BArchiver must not be used
 * further.
 *
 * @param err  An error code to propagate to the session.  Pass B_OK if
 *             archiving completed successfully.
 * @return The combined session error, or B_OK if everything succeeded.
 * @see BArchiver::~BArchiver()
 */
status_t
BArchiver::Finish(status_t err)
{
	if (fFinished)
		debugger("Finish() called multiple times on same BArchiver.");

	fFinished = true;

	return fManager->ArchiverLeaving(this, err);
}


/**
 * @brief Return the top-level archive BMessage associated with this archiver.
 *
 * @return The BMessage passed to the BArchiver constructor.
 */
BMessage*
BArchiver::ArchiveMessage() const
{
	return fArchive;
}


/**
 * @brief Register \a archivable as the root object of this archiving session.
 *
 * Must be called from BArchivable::Archive() before adding any fields, so the
 * manager can assign token 0 to the top-level object.
 *
 * @param archivable  The root object being archived.
 */
void
BArchiver::RegisterArchivable(const BArchivable* archivable)
{
	fManager->RegisterArchivable(archivable);
}


// #pragma mark - BUnarchiver


/**
 * @brief Construct a BUnarchiver bound to \a archive.
 *
 * Locates the BUnarchiveManager already associated with \a archive via
 * BManagerBase::UnarchiveManager().  The manager must have been set up by a
 * prior call to BUnarchiver::PrepareArchive().
 *
 * @param archive  The archive message being unarchived.
 * @see PrepareArchive(), BUnarchiver::Finish()
 */
BUnarchiver::BUnarchiver(const BMessage* archive)
	:
	fManager(BManagerBase::UnarchiveManager(archive)),
	fArchive(archive),
	fFinished(false)
{
}


/**
 * @brief Destructor — calls UnarchiverLeaving() with B_OK if Finish() was not
 *        called explicitly.
 */
BUnarchiver::~BUnarchiver()
{
	if (!fFinished && fManager)
		fManager->UnarchiverLeaving(this, B_OK);
}


/**
 * @brief Retrieve the BArchivable object corresponding to \a token, obeying
 *        the requested ownership policy.
 *
 * This is the base-type specialisation of the GetObject<T> template.  If the
 * object for \a token has not yet been instantiated it will be instantiated
 * now (only possible while a session is active, i.e. Finish() has not been
 * called).
 *
 * @param token   The numeric token assigned during archiving.
 * @param owning  B_ASSUME_OWNERSHIP to take ownership of the object, or
 *                B_DONT_ASSUME_OWNERSHIP to leave it with the manager.
 * @param object  Receives the pointer on success.
 * @return B_OK on success, B_BAD_VALUE if \a token is out of range, or
 *         B_ERROR if instantiation fails.
 * @see FindObject(), BArchiver::GetTokenForArchivable()
 */
template<>
status_t
BUnarchiver::GetObject<BArchivable>(int32 token,
	ownership_policy owning, BArchivable*& object)
{
	_CallDebuggerIfManagerNull();
	return fManager->GetArchivableForToken(token, owning, object);
}


/**
 * @brief Look up a token stored under \a name in the archive and retrieve the
 *        corresponding BArchivable object.
 *
 * This is the base-type specialisation of the FindObject<T> template.
 * Combines BMessage::FindInt32() with GetObject<BArchivable>().
 *
 * @param name        The field name in the archive message.
 * @param index       The field index (for repeated fields).
 * @param owning      Ownership policy for the returned object.
 * @param archivable  Receives the pointer on success, or NULL on failure.
 * @return B_OK on success, or an error from FindInt32() or GetObject().
 * @see GetObject()
 */
template<>
status_t
BUnarchiver::FindObject<BArchivable>(const char* name,
	int32 index, ownership_policy owning, BArchivable*& archivable)
{
	archivable = NULL;
	int32 token;
	status_t err = fArchive->FindInt32(name, index, &token);
	if (err != B_OK)
		return err;

	return GetObject(token, owning, archivable);
}


/**
 * @brief Test whether the object identified by \a token has been instantiated.
 *
 * @param token  The numeric token to query.
 * @return true if the token maps to a live object, false if it has not yet
 *         been instantiated or the token is out of range.
 * @see IsInstantiated(const char*, int32)
 */
bool
BUnarchiver::IsInstantiated(int32 token)
{
	_CallDebuggerIfManagerNull();
	return fManager->IsInstantiated(token);
}


/**
 * @brief Test whether the object stored under a named archive field has been
 *        instantiated.
 *
 * Reads the token from BMessage field \a field at \a index and delegates to
 * IsInstantiated(int32).
 *
 * @param field  The field name in the archive message.
 * @param index  The field index for repeated fields.
 * @return true if the corresponding object exists, false otherwise.
 * @see IsInstantiated(int32)
 */
bool
BUnarchiver::IsInstantiated(const char* field, int32 index)
{
	int32 token;
	if (fArchive->FindInt32(field, index, &token) == B_OK)
		return IsInstantiated(token);

	return false;
}


/**
 * @brief Signal completion of the unarchiving session, propagating any error.
 *
 * Must be called exactly once; calling it more than once triggers a debugger
 * call.  When the last BUnarchiver in a session calls Finish(), the manager
 * dispatches AllUnarchived() to every reconstructed object.
 *
 * @param err  Pass B_OK if unarchiving completed successfully, or an error
 *             code to abort the session and trigger cleanup.
 * @return The combined session error, or B_OK on success.
 * @see BUnarchiver::~BUnarchiver()
 */
status_t
BUnarchiver::Finish(status_t err)
{
	if (fFinished)
		debugger("Finish() called multiple times on same BArchiver.");

	fFinished = true;
	if (fManager)
		return fManager->UnarchiverLeaving(this, err);
	else
		return B_OK;
}


/**
 * @brief Return the archive BMessage associated with this unarchiver.
 *
 * @return A const pointer to the BMessage passed to the constructor.
 */
const BMessage*
BUnarchiver::ArchiveMessage() const
{
	return fArchive;
}


/**
 * @brief Transfer ownership of \a archivable to the calling code.
 *
 * After this call the BUnarchiveManager will no longer delete \a archivable
 * during session cleanup.  The caller is responsible for deleting the object.
 *
 * @param archivable  The object whose ownership should be assumed.
 * @see RelinquishOwnership()
 */
void
BUnarchiver::AssumeOwnership(BArchivable* archivable)
{
	_CallDebuggerIfManagerNull();
	fManager->AssumeOwnership(archivable);
}


/**
 * @brief Return ownership of \a archivable to the BUnarchiveManager.
 *
 * After this call the manager will delete \a archivable during session cleanup
 * if an error occurs.  This is the inverse of AssumeOwnership().
 *
 * @param archivable  The object to return to manager ownership.
 * @see AssumeOwnership()
 */
void
BUnarchiver::RelinquishOwnership(BArchivable* archivable)
{
	_CallDebuggerIfManagerNull();
	fManager->RelinquishOwnership(archivable);
}


/**
 * @brief Test whether \a archive belongs to an active managed archiving
 *        session.
 *
 * Checks for a manager pointer embedded in the message as well as the
 * kManagedField marker that is added to top-level archives.
 *
 * @param archive  The archive message to inspect.  May be NULL.
 * @return true if \a archive is part of a managed session, false otherwise.
 */
bool
BUnarchiver::IsArchiveManaged(const BMessage* archive)
{
	// managed child archives will return here
	if (BManagerBase::ManagerPointer(archive))
		return true;

	if (archive == NULL)
		return false;

	// managed top level archives return here
	bool dummy;
	if (archive->FindBool(kManagedField, &dummy) == B_OK)
		return true;

	return false;
}


/**
 * @brief Instantiate a BArchivable object from \a from inside a managed
 *        unarchiving session.
 *
 * This is the base-type specialisation of InstantiateObject<T>.  Prepares
 * \a from for managed unarchiving, calls instantiate_object(), and finalises
 * the inner session via Finish().
 *
 * @param from    The child archive message to instantiate.
 * @param object  Receives the instantiated object on success.
 * @return B_OK on success, or an error from instantiate_object() or Finish().
 * @see PrepareArchive(), BUnarchiver::Finish()
 */
template<>
status_t
BUnarchiver::InstantiateObject<BArchivable>(BMessage* from,
	BArchivable* &object)
{
	BUnarchiver unarchiver(BUnarchiver::PrepareArchive(from));
	object = instantiate_object(from);
	return unarchiver.Finish();
}


/**
 * @brief Prepare \a archive for managed unarchiving, creating or acquiring
 *        a BUnarchiveManager as needed.
 *
 * If \a archive is already managed this call increments the session reference
 * count via Acquire().  For brand-new archives a new BUnarchiveManager is
 * allocated.  This method is idempotent for legacy (non-managed) archives.
 *
 * @param archive  Reference to the archive pointer to prepare.
 * @return The (unmodified) \a archive pointer, for call-chaining convenience.
 * @see IsArchiveManaged(), BUnarchiveManager::Acquire()
 */
BMessage*
BUnarchiver::PrepareArchive(BMessage* &archive)
{
	// this check allows PrepareArchive to be
	// called on new or old-style archives
	if (BUnarchiver::IsArchiveManaged(archive)) {
		BUnarchiveManager* manager = BManagerBase::UnarchiveManager(archive);
		if (!manager)
			manager = new BUnarchiveManager(archive);

		manager->Acquire();
	}

	return archive;
}


/**
 * @brief Register this object as the root of a managed unarchiving session.
 *
 * Must be called from a BArchivable constructor that takes a BMessage* after
 * PrepareArchive() has been invoked, so the manager can record the object at
 * token slot 0.
 *
 * @param archivable  The newly constructed root object.
 */
void
BUnarchiver::RegisterArchivable(BArchivable* archivable)
{
	_CallDebuggerIfManagerNull();
	fManager->RegisterArchivable(archivable);
}


/**
 * @brief Trigger a debugger break if the internal manager pointer is NULL.
 *
 * Used internally to guard all operations that require an active managed
 * session.  A NULL manager indicates that this BUnarchiver was constructed
 * from a legacy or un-prepared archive.
 */
void
BUnarchiver::_CallDebuggerIfManagerNull()
{
	if (!fManager)
		debugger("BUnarchiver used with legacy or unprepared archive.");
}


// #pragma mark -


/**
 * @brief Reconstruct a BArchivable-derived object from a BMessage archive,
 *        optionally returning the image_id from which the factory was loaded.
 *
 * Reads the class name from the B_CLASS_FIELD in \a archive, locates the
 * static Instantiate(BMessage*) symbol for that class across all images loaded
 * into the current team, and calls it.  If the symbol is not found in any
 * currently loaded image and the archive contains a B_ADD_ON_FIELD signature,
 * the corresponding add-on is loaded via the BRoster and searched as well.
 *
 * @param archive  The BMessage containing the archived object.  Must not be
 *                 NULL.
 * @param _id      If non-NULL, receives the image_id of the image that
 *                 provided the Instantiate symbol (or the loaded add-on id).
 *                 On failure the value at *_id receives the error code.
 * @return A heap-allocated object on success, or NULL on failure.
 * @see instantiate_object(BMessage*), validate_instantiation(),
 *      find_instantiation_func()
 */
BArchivable*
instantiate_object(BMessage* archive, image_id* _id)
{
	status_t statusBuffer;
	status_t* status = &statusBuffer;
	if (_id != NULL)
		status = _id;

	// Check our params
	if (archive == NULL) {
		syslog(LOG_ERR, "instantiate_object failed: NULL BMessage argument");
		*status = B_BAD_VALUE;
		return NULL;
	}

	// Get class name from archive
	const char* className = NULL;
	status_t err = archive->FindString(B_CLASS_FIELD, &className);
	if (err) {
		syslog(LOG_ERR, "instantiate_object failed: Failed to find an entry "
			"defining the class name (%s).", strerror(err));
		*status = B_BAD_VALUE;
		return NULL;
	}

	// Get sig from archive
	const char* signature = NULL;
	bool hasSignature = archive->FindString(B_ADD_ON_FIELD, &signature) == B_OK;

	instantiation_func instantiationFunc = BPrivate::find_instantiation_func(
		className, signature, _id);

	// if find_instantiation_func() can't locate Class::Instantiate()
	// and a signature was specified
	if (!instantiationFunc && hasSignature) {
		// use BRoster::FindApp() to locate an app or add-on with the symbol
		BRoster Roster;
		entry_ref ref;
		err = Roster.FindApp(signature, &ref);

		// if an entry_ref is obtained
		BEntry entry;
		if (err == B_OK)
			err = entry.SetTo(&ref);

		BPath path;
		if (err == B_OK)
			err = entry.GetPath(&path);

		if (err != B_OK) {
			syslog(LOG_ERR, "instantiate_object failed: Error finding app "
				"with signature \"%s\" (%s)", signature, strerror(err));
			*status = err;
			return NULL;
		}

		// load the app/add-on
		image_id addOn = load_add_on(path.Path());
		if (addOn < B_OK) {
			syslog(LOG_ERR, "instantiate_object failed: Could not load "
				"add-on %s: %s.", path.Path(), strerror(addOn));
			*status = addOn;
			return NULL;
		}

		// Save the image_id
		if (_id != NULL)
			*_id = addOn;

		BString name = className;
		for (int32 pass = 0; pass < 2; pass++) {
			BString funcName;
			build_function_name(name, funcName);

			instantiationFunc = find_function_in_image(funcName, addOn, err);
			if (instantiationFunc != NULL)
				break;

			// Check if we have a private class, and add the BPrivate namespace
			// (for backwards compatibility)
			if (!add_private_namespace(name))
				break;
		}

		if (instantiationFunc == NULL) {
			syslog(LOG_ERR, "instantiate_object failed: Failed to find exported "
				"Instantiate static function for class %s.", className);
			*status = B_NAME_NOT_FOUND;
			return NULL;
		}
	} else if (instantiationFunc == NULL) {
		syslog(LOG_ERR, "instantiate_object failed: No signature specified "
			"in archive, looking for class \"%s\".", className);
		*status = B_NAME_NOT_FOUND;
		return NULL;
	}

	// if Class::Instantiate(BMessage*) was found
	if (instantiationFunc != NULL) {
		// use to create and return an object instance
		return instantiationFunc(archive);
	}

	return NULL;
}


/**
 * @brief Reconstruct a BArchivable-derived object from a BMessage archive.
 *
 * Convenience overload that discards the image_id.  Delegates to
 * instantiate_object(BMessage*, image_id*).
 *
 * @param from  The BMessage archive.
 * @return A heap-allocated object on success, or NULL on failure.
 * @see instantiate_object(BMessage*, image_id*)
 */
BArchivable*
instantiate_object(BMessage* from)
{
	return instantiate_object(from, NULL);
}


//	#pragma mark - support_globals


/**
 * @brief Verify that the B_CLASS_FIELD stored in \a from matches \a className.
 *
 * Walks all B_CLASS_FIELD entries in \a from looking for an exact match with
 * \a className.  On mismatch, a second pass is performed with "BPrivate::"
 * prepended for backwards-compatibility with private classes.
 *
 * @param from       The archive message to inspect.  Must not be NULL.
 * @param className  The expected class name.
 * @return true if a match is found (errno set to B_OK), false otherwise
 *         (errno set to B_MISMATCHED_VALUES).
 * @see instantiate_object()
 */
bool
validate_instantiation(BMessage* from, const char* className)
{
	// Make sure our params are kosher -- original skimped here =P
	if (!from) {
		errno = B_BAD_VALUE;
		return false;
	}

	BString name = className;
	for (int32 pass = 0; pass < 2; pass++) {
		const char* archiveClassName;
		for (int32 index = 0; from->FindString(B_CLASS_FIELD, index,
				&archiveClassName) == B_OK; ++index) {
			if (name == archiveClassName) {
				errno = B_OK;
				return true;
			}
		}

		if (!add_private_namespace(name))
			break;
	}

	errno = B_MISMATCHED_VALUES;
	syslog(LOG_ERR, "validate_instantiation failed on class %s.", className);

	return false;
}


/**
 * @brief Locate the Instantiate function for \a className restricted to images
 *        with the given MIME \a signature.
 *
 * @param className  The demangled class name to search for.
 * @param signature  The MIME application signature to restrict the search, or
 *                   NULL to search all loaded images.
 * @return A pointer to the Instantiate function, or NULL if not found.
 * @see BPrivate::find_instantiation_func(), instantiate_object()
 */
instantiation_func
find_instantiation_func(const char* className, const char* signature)
{
	return BPrivate::find_instantiation_func(className, signature, NULL);
}


/**
 * @brief Locate the Instantiate function for \a className in any loaded image.
 *
 * Convenience overload with no signature restriction.
 *
 * @param className  The demangled class name to search for.
 * @return A pointer to the Instantiate function, or NULL if not found.
 * @see find_instantiation_func(const char*, const char*)
 */
instantiation_func
find_instantiation_func(const char* className)
{
	return find_instantiation_func(className, NULL);
}


/**
 * @brief Extract class name and signature from \a archive and locate its
 *        Instantiate function.
 *
 * Reads B_CLASS_FIELD and B_ADD_ON_FIELD from \a archive, then delegates to
 * find_instantiation_func(const char*, const char*).
 *
 * @param archive  The archive message to extract keys from.  Must not be NULL.
 * @return A pointer to the Instantiate function, or NULL if the fields are
 *         missing or the function cannot be found (errno set to B_BAD_VALUE).
 * @see find_instantiation_func(const char*, const char*)
 */
instantiation_func
find_instantiation_func(BMessage* archive)
{
	if (archive == NULL) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	const char* name = NULL;
	const char* signature = NULL;
	if (archive->FindString(B_CLASS_FIELD, &name) != B_OK
		|| archive->FindString(B_ADD_ON_FIELD, &signature)) {
		errno = B_BAD_VALUE;
		return NULL;
	}

	return find_instantiation_func(name, signature);
}


//	#pragma mark - BArchivable binary compatibility

/**
 * @brief Binary-compatibility thunks for AllUnarchived() and AllArchived().
 *
 * These extern "C" functions are GCC-ABI-specific entry points that forward
 * the PERFORM_CODE_ALL_UNARCHIVED and PERFORM_CODE_ALL_ARCHIVED perform codes
 * through BArchivable::Perform(), preserving binary compatibility with
 * objects compiled against older versions of the kit that used reserved
 * virtual-function slots for these two methods.  They must not be called
 * directly by application code.
 */
#if __GNUC__ == 2

extern "C" status_t
_ReservedArchivable1__11BArchivable(BArchivable* archivable,
	const BMessage* archive)
{
	// AllUnarchived
	perform_data_all_unarchived performData;
	performData.archive = archive;

	archivable->Perform(PERFORM_CODE_ALL_UNARCHIVED, &performData);
	return performData.return_value;
}


extern "C" status_t
_ReservedArchivable2__11BArchivable(BArchivable* archivable,
	BMessage* archive)
{
	// AllArchived
	perform_data_all_archived performData;
	performData.archive = archive;

	archivable->Perform(PERFORM_CODE_ALL_ARCHIVED, &performData);
	return performData.return_value;
}


#elif __GNUC__ > 2

extern "C" status_t
_ZN11BArchivable20_ReservedArchivable1Ev(BArchivable* archivable,
	const BMessage* archive)
{
	// AllUnarchived
	perform_data_all_unarchived performData;
	performData.archive = archive;

	archivable->Perform(PERFORM_CODE_ALL_UNARCHIVED, &performData);
	return performData.return_value;
}


extern "C" status_t
_ZN11BArchivable20_ReservedArchivable2Ev(BArchivable* archivable,
	BMessage* archive)
{
	// AllArchived
	perform_data_all_archived performData;
	performData.archive = archive;

	archivable->Perform(PERFORM_CODE_ALL_ARCHIVED, &performData);
	return performData.return_value;
}

#endif // _GNUC__ > 2


void BArchivable::_ReservedArchivable3() {}
