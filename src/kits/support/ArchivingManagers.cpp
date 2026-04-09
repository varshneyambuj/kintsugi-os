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
 *   Copyright 2010 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Alex Wilson, yourpalal2@gmail.com
 */

/**
 * @file ArchivingManagers.cpp
 * @brief Private implementation of the managed archiving and unarchiving
 *        session managers.
 *
 * This file implements BArchiveManager and BUnarchiveManager, the internal
 * classes that coordinate multi-object archiving sessions.  They track every
 * object archived or restored in a session, assign numeric tokens for
 * cross-object references, handle AllArchived()/AllUnarchived() dispatch, and
 * manage object ownership during error recovery.  These classes are not part
 * of the public API; callers interact with them exclusively through BArchiver
 * and BUnarchiver.
 *
 * @see BArchiver, BUnarchiver, BArchivable
 */


#include "ArchivingManagers.h"

#include <syslog.h>
#include <typeinfo>

#include <StackOrHeapArray.h>


namespace BPrivate {
namespace Archiving {
	const char* kArchivableField = "_managed_archivable";
	const char* kManagedField = "_managed_archive";
}
}


using namespace BPrivate::Archiving;


/**
 * @brief Retrieve the BArchiveManager embedded in \a archive, if any.
 *
 * Casts the generic manager pointer stored in \a archive to BArchiveManager.
 * Calls debugger() if the pointer exists but refers to a BUnarchiveManager
 * instead, which indicates overlapping archive/unarchive sessions.
 *
 * @param archive  The BMessage to inspect.
 * @return The active BArchiveManager, or NULL if none is attached.
 */
BArchiveManager*
BManagerBase::ArchiveManager(const BMessage* archive)
{
	BManagerBase* manager = ManagerPointer(archive);
	if (!manager)
		return NULL;

	if (manager->fType == ARCHIVE_MANAGER)
		return static_cast<BArchiveManager*>(manager);

	debugger("Overlapping managed unarchive/archive sessions.");
	return NULL;
}


/**
 * @brief Retrieve the BUnarchiveManager embedded in \a archive, if any.
 *
 * Casts the generic manager pointer stored in \a archive to
 * BUnarchiveManager.  Calls debugger() if the pointer exists but refers to a
 * BArchiveManager, which indicates more PrepareArchive() calls were made than
 * BUnarchivers were created.
 *
 * @param archive  The BMessage to inspect.
 * @return The active BUnarchiveManager, or NULL if none is attached.
 */
BUnarchiveManager*
BManagerBase::UnarchiveManager(const BMessage* archive)
{
	BManagerBase* manager = ManagerPointer(archive);
	if (!manager)
		return NULL;

	if (manager->fType == UNARCHIVE_MANAGER)
		return static_cast<BUnarchiveManager*>(manager);

	debugger("More calls to BUnarchiver::PrepareArchive()"
		" than BUnarchivers created.");

	return NULL;
}


// #pragma mark -


struct BArchiveManager::ArchiveInfo {
	ArchiveInfo()
		:
		token(-1),
		archive(NULL)
	{
	}


	~ArchiveInfo()
	{
		delete archive;
	}


	int32		token;
	BMessage*	archive;
};


/**
 * @brief Construct a BArchiveManager owned by \a creator.
 *
 * Associates the manager with the archive message returned by
 * creator->ArchiveMessage() and marks it as an ARCHIVE_MANAGER session.
 *
 * @param creator  The BArchiver that is initiating the session.  The manager
 *                 self-destructs when this archiver calls ArchiverLeaving().
 */
BArchiveManager::BArchiveManager(const BArchiver* creator)
	:
	BManagerBase(creator->ArchiveMessage(), BManagerBase::ARCHIVE_MANAGER),
	fTokenMap(),
	fCreator(creator),
	fError(B_OK)
{
}


/**
 * @brief Destructor — marks the top-level archive as a managed archive.
 *
 * Adds the kManagedField boolean to the top-level archive message so that
 * BUnarchiver::IsArchiveManaged() can identify it later.
 */
BArchiveManager::~BArchiveManager()
{
	fTopLevelArchive->AddBool(kManagedField, true);
}


/**
 * @brief Look up the token already assigned to \a archivable without
 *        archiving it.
 *
 * @param archivable  The object to look up.  NULL maps to NULL_TOKEN / B_OK.
 * @param _token      Receives the token on success.
 * @return B_OK if the token was found or \a archivable is NULL,
 *         B_ENTRY_NOT_FOUND if the object has not yet been archived.
 * @see ArchiveObject()
 */
status_t
BArchiveManager::GetTokenForArchivable(BArchivable* archivable, int32& _token)
{
	if (!archivable) {
		_token = NULL_TOKEN;
		return B_OK;
	}

	TokenMap::iterator it = fTokenMap.find(archivable);

	if (it == fTokenMap.end())
		return B_ENTRY_NOT_FOUND;

	_token = it->second.token;
	return B_OK;
}


/**
 * @brief Archive \a archivable if it has not been archived yet, and return
 *        its session token.
 *
 * On the first call for a given archivable a new BMessage is allocated,
 * MarkArchive() is called to embed the manager pointer, and
 * archivable->Archive() is invoked.  Subsequent calls return the cached
 * token immediately.  On failure the entry is removed from the token map and
 * NULL_TOKEN is returned.
 *
 * @param archivable  The object to archive.  NULL maps to NULL_TOKEN / B_OK.
 * @param deep        Forwarded to BArchivable::Archive().
 * @param _token      Receives the assigned token on success.
 * @return B_OK on success, or the error from BArchivable::Archive().
 * @see GetTokenForArchivable()
 */
status_t
BArchiveManager::ArchiveObject(BArchivable* archivable,
	bool deep, int32& _token)
{
	if (!archivable) {
		_token = NULL_TOKEN;
		return B_OK;
	}

	ArchiveInfo& info = fTokenMap[archivable];

	status_t err = B_OK;

	if (!info.archive) {
		info.archive = new BMessage();
		info.token = fTokenMap.size() - 1;

		MarkArchive(info.archive);
		err = archivable->Archive(info.archive, deep);
	}

	if (err != B_OK) {
		fTokenMap.erase(archivable);
			// info.archive gets deleted here
		_token = NULL_TOKEN;
	} else
		_token = info.token;

	return err;
}


/**
 * @brief Test whether \a archivable has already been registered in this
 *        session.
 *
 * @param archivable  The object to query.  NULL is always considered archived.
 * @return true if the object has a token entry, false otherwise.
 */
bool
BArchiveManager::IsArchived(BArchivable* archivable)
{
	if (!archivable)
		return true;

	return fTokenMap.find(archivable) != fTokenMap.end();
}


/**
 * @brief Notify the manager that \a archiver is done archiving.
 *
 * Accumulates any error from \a err.  When the creating BArchiver leaves, the
 * manager sorts all archived objects by token order, calls AllArchived() on
 * each, and appends each child's BMessage to the top-level archive under
 * kArchivableField.  Logs an error via syslog and aborts if AllArchived()
 * fails for any object.  The manager self-destructs once the creator leaves.
 *
 * @param archiver  The BArchiver that is leaving the session.
 * @param err       The error code from the archiver's own work (B_OK if OK).
 * @return The combined session error code.
 */
status_t
BArchiveManager::ArchiverLeaving(const BArchiver* archiver, status_t err)
{
	if (fError == B_OK)
		fError = err;

	if (archiver == fCreator && fError == B_OK) {
		// first, we must sort the objects into the order they were archived in
		typedef std::pair<BMessage*, const BArchivable*> ArchivePair;
		BStackOrHeapArray<ArchivePair, 64> pairs(fTokenMap.size());

		for(TokenMap::iterator it = fTokenMap.begin(), end = fTokenMap.end();
				it != end; it++) {
			ArchiveInfo& info = it->second;
			pairs[info.token].first = info.archive;
			pairs[info.token].second = it->first;

			// make sure fTopLevelArchive isn't deleted
			if (info.archive == fTopLevelArchive)
				info.archive = NULL;
		}

		int32 count = fTokenMap.size();
		for (int32 i = 0; i < count; i++) {
			const ArchivePair& pair = pairs[i];
			fError = pair.second->AllArchived(pair.first);

			if (fError == B_OK && i > 0) {
				fError = fTopLevelArchive->AddMessage(kArchivableField,
					pair.first);
			}

			if (fError != B_OK) {
				syslog(LOG_ERR, "AllArchived failed for object of type %s.",
					typeid(*pairs[i].second).name());
				break;
			}
		}
	}

	status_t result = fError;
	if (archiver == fCreator)
		delete this;

	return result;
}


/**
 * @brief Register the root archivable at token slot 0.
 *
 * Must be the first call to the manager during a session.  The root object's
 * archive is the top-level BMessage itself rather than a freshly allocated
 * child BMessage.
 *
 * @param archivable  The root object being archived.
 */
void
BArchiveManager::RegisterArchivable(const BArchivable* archivable)
{
	if (fTokenMap.size() == 0) {
		ArchiveInfo& info = fTokenMap[archivable];
		info.archive = fTopLevelArchive;
		info.token = 0;
	}
}


// #pragma mark -


struct BUnarchiveManager::ArchiveInfo {
	ArchiveInfo()
		:
		archivable(NULL),
		archive(),
		adopted(false)
	{
	}

	bool
	operator<(const ArchiveInfo& other)
	{
		return archivable < other.archivable;
	}

	BArchivable*	archivable;
	BMessage		archive;
	bool			adopted; ///< true if a BUnarchiver has assumed ownership
};


// #pragma mark -


/**
 * @brief Construct a BUnarchiveManager from a managed top-level archive.
 *
 * Reads the count of archived child objects from the kArchivableField in
 * \a archive, allocates a ArchiveInfo slot array, and extracts each child
 * BMessage into the slot array.  Slot 0 is reserved for the root object.
 *
 * @param archive  The top-level BMessage to unarchive.
 */
BUnarchiveManager::BUnarchiveManager(BMessage* archive)
	:
	BManagerBase(archive, BManagerBase::UNARCHIVE_MANAGER),
	fObjects(NULL),
	fObjectCount(0),
	fTokenInProgress(0),
	fRefCount(0),
	fError(B_OK)
{
	archive->GetInfo(kArchivableField, NULL, &fObjectCount);
	fObjectCount++;
		// root object needs a spot too
	fObjects = new ArchiveInfo[fObjectCount];

	// fObjects[0] is a placeholder for the object that started
	// this unarchiving session.
	for (int32 i = 0; i < fObjectCount - 1; i++) {
		BMessage* into = &fObjects[i + 1].archive;
		status_t err = archive->FindMessage(kArchivableField, i, into);
		MarkArchive(into);

		if (err != B_OK)
			syslog(LOG_ERR, "Failed to find managed archivable");
	}
}


/**
 * @brief Destructor — releases the ArchiveInfo slot array.
 *
 * Individual archivable objects are not deleted here; ownership is managed
 * through the adopted flag and cleanup in UnarchiverLeaving().
 */
BUnarchiveManager::~BUnarchiveManager()
{
	delete[] fObjects;
}


/**
 * @brief Retrieve the archivable object identified by \a token, instantiating
 *        it on demand if necessary.
 *
 * If the object has already been instantiated the cached pointer is returned.
 * If not and the session is still active (fRefCount > 0), instantiate_object()
 * is called on the token's stored BMessage.  Requests for objects from
 * AllUnarchived() (fRefCount == 0) cannot trigger on-demand instantiation and
 * return B_ERROR.
 *
 * @param token       The numeric token assigned during archiving.
 * @param owning      B_ASSUME_OWNERSHIP marks the slot as adopted by the
 *                    caller so it won't be deleted during error cleanup.
 * @param _archivable Receives the pointer on success.
 * @return B_OK on success, B_BAD_VALUE if \a token is out of range, or
 *         B_ERROR if on-demand instantiation fails or is not possible.
 */
status_t
BUnarchiveManager::GetArchivableForToken(int32 token,
	BUnarchiver::ownership_policy owning, BArchivable*& _archivable)
{
	if (token >= fObjectCount)
		return B_BAD_VALUE;

	if (token < 0) {
		_archivable = NULL;
		return B_OK;
	}

	status_t err = B_OK;
	ArchiveInfo& info = fObjects[token];
	if (!info.archivable) {
		if (fRefCount > 0) {
			fTokenInProgress = token;
			if(!instantiate_object(&info.archive))
				err = B_ERROR;
		} else {
			syslog(LOG_ERR, "Object requested from AllUnarchived()"
				" was not previously instantiated");
			err = B_ERROR;
		}
	}

	if (owning == BUnarchiver::B_ASSUME_OWNERSHIP)
		info.adopted = true;

	_archivable = info.archivable;
	return err;
}


/**
 * @brief Test whether the object at \a token has been instantiated.
 *
 * @param token  The numeric token to query.
 * @return true if the slot holds a non-NULL archivable pointer, false if the
 *         token is out of range or the object has not yet been created.
 */
bool
BUnarchiveManager::IsInstantiated(int32 token)
{
	if (token < 0 || token >= fObjectCount)
		return false;
	return fObjects[token].archivable;
}


/**
 * @brief Record \a archivable as the object for the token currently being
 *        instantiated.
 *
 * Called from BArchivable constructors (via BUnarchiver::RegisterArchivable())
 * to bind the newly constructed object to its session token.  Also stores the
 * token back into the object's fArchivingToken field.
 *
 * @param archivable  The newly constructed object.  Must not be NULL.
 */
void
BUnarchiveManager::RegisterArchivable(BArchivable* archivable)
{
	if (!archivable)
		debugger("Cannot register NULL pointer");

	fObjects[fTokenInProgress].archivable = archivable;
	archivable->fArchivingToken = fTokenInProgress;
}


/**
 * @brief Notify the manager that \a unarchiver is done with its portion of
 *        the session.
 *
 * Decrements the reference count.  When it reaches zero all objects have been
 * instantiated and AllUnarchived() is dispatched to each object in token order.
 * If any AllUnarchived() call fails, an error is logged and non-adopted objects
 * are deleted.  The manager self-destructs once the ref-count reaches zero.
 *
 * @param unarchiver  The BUnarchiver that is leaving.
 * @param err         The error code from the unarchiver's work.
 * @return The combined session error code.
 */
status_t
BUnarchiveManager::UnarchiverLeaving(const BUnarchiver* unarchiver,
	status_t err)
{
	if (--fRefCount >= 0 && fError == B_OK)
		fError = err;

	if (fRefCount != 0)
		return fError;

	if (fError == B_OK) {
		BArchivable* archivable = fObjects[0].archivable;
		if (archivable) {
			fError = archivable->AllUnarchived(fTopLevelArchive);
			archivable->fArchivingToken = NULL_TOKEN;
		}

		for (int32 i = 1; i < fObjectCount && fError == B_OK; i++) {
			archivable = fObjects[i].archivable;
			if (archivable) {
				fError = archivable->AllUnarchived(&fObjects[i].archive);
				archivable->fArchivingToken = NULL_TOKEN;
			}
		}
		if (fError != B_OK) {
			syslog(LOG_ERR, "Error in AllUnarchived"
				" method of object of type %s", typeid(*archivable).name());
		}
	}

	if (fError != B_OK) {
		syslog(LOG_ERR, "An error occured during unarchival, cleaning up.");
		for (int32 i = 1; i < fObjectCount; i++) {
			if (!fObjects[i].adopted)
				delete fObjects[i].archivable;
		}
	}

	status_t result = fError;
	delete this;
	return result;
}


/**
 * @brief Mark the slot for \a archivable as not owned by the caller.
 *
 * After this call the manager will delete the object during error cleanup.
 * This is the inverse of AssumeOwnership().
 *
 * @param archivable  The object to relinquish.  Silently ignored if the token
 *                    is out of range or does not match the stored pointer.
 * @see AssumeOwnership()
 */
void
BUnarchiveManager::RelinquishOwnership(BArchivable* archivable)
{
	int32 token = NULL_TOKEN;
	if (archivable)
		token = archivable->fArchivingToken;

	if (token < 0 || token >= fObjectCount
		|| fObjects[token].archivable != archivable)
		return;

	fObjects[token].adopted = false;
}


/**
 * @brief Mark the slot for \a archivable as owned by external code.
 *
 * After this call the manager will not delete the object during error cleanup.
 *
 * @param archivable  The object to take ownership of.  Silently ignored if the
 *                    token is out of range or does not match the stored pointer.
 * @see RelinquishOwnership()
 */
void
BUnarchiveManager::AssumeOwnership(BArchivable* archivable)
{
	int32 token = NULL_TOKEN;
	if (archivable)
		token = archivable->fArchivingToken;

	if (token < 0 || token >= fObjectCount
		|| fObjects[token].archivable != archivable)
		return;

	fObjects[token].adopted = true;
}


/**
 * @brief Increment the session reference count.
 *
 * Each call to BUnarchiver::PrepareArchive() that encounters an already-managed
 * archive increments the count via this method, ensuring the manager is not
 * destroyed until all BUnarchivers have called Finish().
 */
void
BUnarchiveManager::Acquire()
{
	if (fRefCount >= 0)
		fRefCount++;
}
