/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2010, Haiku, Inc.
 */

/** @file ArchivingManagers.h
    @brief Internal coordinators that drive deep BArchivable serialisation
           sessions and the matching unarchive instantiation passes. */

#ifndef _ARCHIVING_MANAGERS_H
#define _ARCHIVING_MANAGERS_H


#include <map>

#include <String.h>
#include <ObjectList.h>
#include <MessagePrivate.h>

#include <Archivable.h>


#define NULL_TOKEN -42


namespace BPrivate {
namespace Archiving {

extern const char* kManagedField;


/** @brief Common base for archive and unarchive session managers; handles
           pointer stashing on the top-level BMessage and policy enforcement. */
class BManagerBase {
public:
	enum manager_type {
		ARCHIVE_MANAGER,
		UNARCHIVE_MANAGER
	};

	/** @brief Construct the manager and stamp \a topLevelArchive with a
	           back-pointer so nested archivers can locate this session. */
	BManagerBase(BMessage* topLevelArchive, manager_type type)
		:
		fTopLevelArchive(topLevelArchive),
		fType(type)
	{
		MarkArchive(topLevelArchive);
	}


	/** @brief Return the BManagerBase associated with \a constArchive, or NULL. */
	static BManagerBase*
	ManagerPointer(const BMessage* constArchive)
	{
		if (!constArchive)
			return NULL;

		BMessage* archive = const_cast<BMessage*>(constArchive);

		return static_cast<BManagerBase*>(
			BMessage::Private(archive).ArchivingPointer());
	}


	/** @brief Stash \a manager into the BMessage private archiving pointer slot. */
	static void
	SetManagerPointer(BMessage* archive, BManagerBase* manager)
	{
		BMessage::Private(archive).SetArchivingPointer(manager);
	}


	/** @brief Claim \a archive for this manager; aborts if another session owns it. */
	void
	MarkArchive(BMessage* archive)
	{
		BManagerBase* manager = ManagerPointer(archive);
		if (manager != NULL)
			debugger("Overlapping managed archiving/unarchiving sessions!");

		SetManagerPointer(archive, this);
	}


	/** @brief Release ownership of \a archive previously taken by MarkArchive(). */
	void
	UnmarkArchive(BMessage* archive)
	{
		BManagerBase* manager = ManagerPointer(archive);
		if (manager == this)
			SetManagerPointer(archive, NULL);
		else
			debugger("Overlapping managed archiving/unarchiving sessions!");
	}


	static	BArchiveManager*	ArchiveManager(const BMessage* archive);
	static 	BUnarchiveManager*	UnarchiveManager(const BMessage* archive);

protected:
	~BManagerBase()
	{
		UnmarkArchive(fTopLevelArchive);
	}

protected:
			BMessage*			fTopLevelArchive;
			manager_type		fType;
};


/** @brief Tracks BArchivable instances during a deep-archive session, assigns
           tokens, and writes nested archives into the top-level BMessage. */
class BArchiveManager: public BManagerBase {
public:
								BArchiveManager(const BArchiver* creator);

			status_t			GetTokenForArchivable(BArchivable* archivable,
									int32& _token);

			status_t			ArchiveObject(BArchivable* archivable,
									bool deep, int32& _token);

			bool				IsArchived(BArchivable* archivable);

			status_t			ArchiverLeaving(const BArchiver* archiver,
									status_t err);

			void				Acquire();
			void				RegisterArchivable(
									const BArchivable* archivable);

private:
								~BArchiveManager();

			struct ArchiveInfo;
			typedef std::map<const BArchivable*, ArchiveInfo> TokenMap;

			TokenMap			fTokenMap;
			const BArchiver*	fCreator;
			status_t			fError;
};




/** @brief Drives the reverse pass: instantiates BArchivable objects from a
           deep archive, hands ownership to consumers, and coordinates errors. */
class BUnarchiveManager: public BManagerBase {
public:
								BUnarchiveManager(BMessage* topLevelArchive);

			status_t			GetArchivableForToken(int32 token,
									BUnarchiver::ownership_policy owning,
									BArchivable*& _archivable);

			bool				IsInstantiated(int32 token);

			void				RegisterArchivable(BArchivable* archivable);
			status_t			UnarchiverLeaving(const BUnarchiver* archiver,
									status_t err);
			void				Acquire();

			void				RelinquishOwnership(BArchivable* archivable);
			void				AssumeOwnership(BArchivable* archivable);
private:
								~BUnarchiveManager();

			status_t			_ExtractArchiveAt(int32 index);

			struct ArchiveInfo;

			ArchiveInfo*		fObjects;
			int32				fObjectCount;
			int32				fTokenInProgress;
			int32				fRefCount;
			status_t			fError;
};


} // namespace Archiving
} // namespace BPrivate


#endif	// _ARCHIVING_MANAGERS_H
