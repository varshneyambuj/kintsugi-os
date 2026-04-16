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
 *   Copyright 2008-2011, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file realtime_sem.cpp
 * @brief POSIX realtime named and unnamed semaphore implementation.
 *
 * Implements the kernel side of POSIX.1 realtime semaphores (sem_open(),
 * sem_close(), sem_unlink(), sem_wait(), sem_post(), sem_getvalue()). Named
 * semaphores live in a global hash table keyed by path-like name and carry
 * owner UID/GID plus mode bits; unnamed semaphores live only inside a team's
 * per-team context. Each team maintains a realtime_sem_context that maps
 * opaque sem_ids onto the underlying kernel sem_ids returned by the Haiku
 * semaphore facility, and reference counts the shared NamedSem objects so
 * that an unlinked name persists until the last opener closes it.
 */

#include <posix/realtime_sem.h>

#include <string.h>

#include <new>

#include <OS.h>

#include <AutoDeleter.h>
#include <fs/KPath.h>
#include <kernel.h>
#include <lock.h>
#include <syscall_restart.h>
#include <team.h>
#include <thread.h>
#include <util/atomic.h>
#include <util/AutoLock.h>
#include <util/OpenHashTable.h>
#include <util/StringHash.h>


namespace {

/**
 * @brief Abstract base wrapping a kernel semaphore used by POSIX realtime sems.
 *
 * Holds the underlying Haiku sem_id and exposes a Clone()/Delete() protocol
 * for subclasses so that named sems can be reference counted while unnamed
 * sems are destroyed immediately.
 */
class SemInfo {
public:
	/**
	 * @brief Construct an empty SemInfo with no backing kernel semaphore.
	 */
	SemInfo()
		:
		fSemaphoreID(-1)
	{
	}

	/**
	 * @brief Destroy the wrapper and release the kernel semaphore if allocated.
	 */
	virtual ~SemInfo()
	{
		if (fSemaphoreID >= 0)
			delete_sem(fSemaphoreID);
	}

	/**
	 * @brief Accessor for the underlying kernel sem_id.
	 *
	 * @return The Haiku sem_id used to implement this POSIX semaphore.
	 */
	sem_id SemaphoreID() const			{ return fSemaphoreID; }

	/**
	 * @brief Create the backing kernel semaphore with the given initial count.
	 *
	 * @param semCount Initial value for the semaphore.
	 * @param name Debug name attached to the kernel semaphore.
	 * @return B_OK on success, or a negative error code from create_sem().
	 */
	status_t Init(int32 semCount, const char* name)
	{
		fSemaphoreID = create_sem(semCount, name);
		if (fSemaphoreID < 0)
			return fSemaphoreID;

		return B_OK;
	}

	/**
	 * @brief Return the logical id used by userland (named or private).
	 *
	 * @return Logical semaphore id visible through the POSIX API.
	 */
	virtual sem_id ID() const = 0;

	/**
	 * @brief Acquire an additional reference (named) or duplicate (unnamed).
	 *
	 * @return Pointer to the cloned/shared SemInfo, or NULL on failure.
	 */
	virtual SemInfo* Clone() = 0;

	/**
	 * @brief Release a reference; destroys the object when the count hits zero.
	 */
	virtual void Delete() = 0;

private:
	sem_id	fSemaphoreID;
};


/**
 * @brief Reference-counted named POSIX semaphore.
 *
 * Named semaphores are shared across all teams that sem_open() the same name
 * and are kept alive by both the global name table and by each opener. Carries
 * the POSIX-standard owner UID/GID and mode bits consulted by HasPermissions().
 */
class NamedSem : public SemInfo {
public:
	/**
	 * @brief Construct an uninitialised named sem with refcount 1.
	 */
	NamedSem()
		:
		fName(NULL),
		fRefCount(1)
	{
	}

	/**
	 * @brief Destroy the named sem and free its cached name string.
	 */
	virtual ~NamedSem()
	{
		free(fName);
	}

	/**
	 * @brief Return the POSIX name this semaphore was registered under.
	 *
	 * @return Pointer to the internally owned name string.
	 */
	const char* Name() const		{ return fName; }

	/**
	 * @brief Initialise the named sem, storing name/mode and creator uid/gid.
	 *
	 * @param name POSIX name (path-like) for the semaphore.
	 * @param mode POSIX mode bits controlling subsequent open permissions.
	 * @param semCount Initial value for the semaphore.
	 * @return B_OK on success, B_NO_MEMORY if the name cannot be duplicated,
	 *         or a negative status from the underlying create_sem().
	 */
	status_t Init(const char* name, mode_t mode, int32 semCount)
	{
		status_t error = SemInfo::Init(semCount, name);
		if (error != B_OK)
			return error;

		fName = strdup(name);
		if (fName == NULL)
			return B_NO_MEMORY;

		fUID = geteuid();
		fGID = getegid();
		fPermissions = mode;

		return B_OK;
	}

	/**
	 * @brief Atomically add one to the reference count.
	 */
	void AcquireReference()
	{
		atomic_add(&fRefCount, 1);
	}

	/**
	 * @brief Drop a reference, deleting the object when the count reaches zero.
	 */
	void ReleaseReference()
	{
		if (atomic_add(&fRefCount, -1) == 1)
			delete this;
	}

	/**
	 * @brief Check whether the effective user/group can open/unlink this sem.
	 *
	 * Applies classic POSIX permission semantics: other-write always passes,
	 * root always passes, owner is checked against S_IWUSR and the primary
	 * group is checked against S_IWGRP.
	 *
	 * @return true if the caller has write permission, false otherwise.
	 */
	bool HasPermissions() const
	{
		if ((fPermissions & S_IWOTH) != 0)
			return true;

		uid_t uid = geteuid();
		if (uid == 0 || (uid == fUID && (fPermissions & S_IWUSR) != 0))
			return true;

		gid_t gid = getegid();
		if (gid == fGID && (fPermissions & S_IWGRP) != 0)
			return true;

		return false;
	}

	/**
	 * @brief Logical id of a named sem is simply its underlying sem_id.
	 *
	 * @return The kernel sem_id, which is also the id exposed to userland.
	 */
	virtual sem_id ID() const
	{
		return SemaphoreID();
	}

	/**
	 * @brief Cloning a named sem just increments the shared reference count.
	 *
	 * @return This same NamedSem, with one extra reference.
	 */
	virtual SemInfo* Clone()
	{
		AcquireReference();
		return this;
	}

	/**
	 * @brief Deletion of a named sem is a reference release.
	 */
	virtual void Delete()
	{
		ReleaseReference();
	}

	/**
	 * @brief Accessor used by the open hash table to link into its chain.
	 *
	 * @return Reference to the hash-link next pointer.
	 */
	NamedSem*& HashLink()
	{
		return fHashLink;
	}

private:
	char*		fName;
	int32		fRefCount;
	uid_t		fUID;
	gid_t		fGID;
	mode_t		fPermissions;

	NamedSem*	fHashLink;
};


/**
 * @brief Open-hash-table policy keying NamedSem entries by their name string.
 */
struct NamedSemHashDefinition {
	typedef const char*	KeyType;
	typedef NamedSem	ValueType;

	/**
	 * @brief Hash a lookup key (a POSIX semaphore name).
	 *
	 * @param key Null-terminated semaphore name.
	 * @return Hash value used for bucket selection.
	 */
	size_t HashKey(const KeyType& key) const
	{
		return hash_hash_string(key);
	}

	/**
	 * @brief Hash an existing NamedSem by its stored name.
	 *
	 * @param semaphore The semaphore to hash.
	 * @return Same hash that the semaphore's name would produce via HashKey().
	 */
	size_t Hash(NamedSem* semaphore) const
	{
		return HashKey(semaphore->Name());
	}

	/**
	 * @brief Compare a key against an entry by string-equality of names.
	 *
	 * @param key Lookup key.
	 * @param semaphore Candidate entry.
	 * @return true if the names match exactly.
	 */
	bool Compare(const KeyType& key, NamedSem* semaphore) const
	{
		return strcmp(key, semaphore->Name()) == 0;
	}

	/**
	 * @brief Access the intrusive hash-chain link stored inside a NamedSem.
	 *
	 * @param semaphore Entry whose link field is needed.
	 * @return Reference to the entry's hash-link next pointer.
	 */
	NamedSem*& GetLink(NamedSem* semaphore) const
	{
		return semaphore->HashLink();
	}
};


/**
 * @brief System-wide registry of named POSIX semaphores.
 *
 * One instance (sSemTable) is shared by all teams. Holds a mutex-guarded hash
 * table from name to NamedSem and enforces the MAX_POSIX_SEMS cap.
 */
class GlobalSemTable {
public:
	/**
	 * @brief Construct the table and initialise its guarding mutex.
	 */
	GlobalSemTable()
		:
		fSemaphoreCount(0)
	{
		mutex_init(&fLock, "global named sem table");
	}

	/**
	 * @brief Destroy the mutex; the table must already be empty.
	 */
	~GlobalSemTable()
	{
		mutex_destroy(&fLock);
	}

	/**
	 * @brief Initialise the underlying open hash table.
	 *
	 * @return B_OK on success, B_NO_MEMORY on allocation failure.
	 */
	status_t Init()
	{
		return fNamedSemaphores.Init();
	}

	/**
	 * @brief Look up or create a named semaphore, honoring O_CREAT and O_EXCL.
	 *
	 * Implements the global half of sem_open(): returns EEXIST when both
	 * O_CREAT|O_EXCL are set and the name already exists, EACCES when the
	 * existing semaphore denies the caller, ENOENT when the name is missing
	 * without O_CREAT, and ENOSPC when the system-wide cap is reached.
	 *
	 * @param name POSIX semaphore name to look up or create.
	 * @param openFlags Bitwise OR of O_CREAT / O_EXCL.
	 * @param mode Mode bits applied only when creating.
	 * @param semCount Initial value used only when creating.
	 * @param[out] _sem Returns a newly referenced pointer to the NamedSem.
	 * @param[out] _created Set to true iff this call created the semaphore.
	 * @return B_OK on success, otherwise a POSIX errno (EEXIST, EACCES,
	 *         ENOENT, ENOSPC, B_NO_MEMORY, ...).
	 */
	status_t OpenNamedSem(const char* name, int openFlags, mode_t mode,
		uint32 semCount, NamedSem*& _sem, bool& _created)
	{
		MutexLocker _(fLock);

		NamedSem* sem = fNamedSemaphores.Lookup(name);
		if (sem != NULL) {
			if ((openFlags & O_EXCL) != 0)
				return EEXIST;

			if (!sem->HasPermissions())
				return EACCES;

			sem->AcquireReference();
			_sem = sem;
			_created = false;
			return B_OK;
		}

		if ((openFlags & O_CREAT) == 0)
			return ENOENT;

		// does not exist yet -- create
		if (fSemaphoreCount >= MAX_POSIX_SEMS)
			return ENOSPC;

		sem = new(std::nothrow) NamedSem;
		if (sem == NULL)
			return B_NO_MEMORY;

		status_t error = sem->Init(name, mode, semCount);
		if (error != B_OK) {
			delete sem;
			return error;
		}

		error = fNamedSemaphores.Insert(sem);
		if (error != B_OK) {
			delete sem;
			return error;
		}

		// add one reference for the table
		sem->AcquireReference();

		fSemaphoreCount++;

		_sem = sem;
		_created = true;
		return B_OK;
	}

	/**
	 * @brief Remove a name from the global table after a permission check.
	 *
	 * Matches POSIX sem_unlink(): the name is severed from its semaphore
	 * immediately (so it can be reused), but the semaphore itself survives
	 * until all openers sem_close() it.
	 *
	 * @param name POSIX semaphore name to unlink.
	 * @return B_OK on success, ENOENT if the name is unknown, or EACCES if
	 *         the caller lacks write permission on it.
	 */
	status_t UnlinkNamedSem(const char* name)
	{
		MutexLocker _(fLock);

		NamedSem* sem = fNamedSemaphores.Lookup(name);
		if (sem == NULL)
			return ENOENT;

		if (!sem->HasPermissions())
			return EACCES;

		fNamedSemaphores.Remove(sem);
		sem->ReleaseReference();
			// release the table reference
		fSemaphoreCount--;

		return B_OK;
	}

private:
	typedef BOpenHashTable<NamedSemHashDefinition, true> NamedSemTable;

	mutex			fLock;
	NamedSemTable	fNamedSemaphores;
	int32			fSemaphoreCount;
};


static GlobalSemTable sSemTable;


/**
 * @brief Per-team binding between a shared SemInfo and a userland sem_t*.
 *
 * A team may sem_open() the same name multiple times; the open count tracks
 * how many sem_close() calls are still outstanding. The userland sem_t* is
 * remembered so that the kernel can tell the caller which pointer to free
 * once the last close happens.
 */
class TeamSemInfo {
public:
	/**
	 * @brief Bind a SemInfo (owning one reference) to a userland sem_t.
	 *
	 * @param semaphore SemInfo transferred into this object.
	 * @param userSem Userspace pointer associated with the first open.
	 */
	TeamSemInfo(SemInfo* semaphore, sem_t* userSem)
		:
		fSemaphore(semaphore),
		fUserSemaphore(userSem),
		fOpenCount(1)
	{
	}

	/**
	 * @brief Release our SemInfo reference, potentially destroying it.
	 */
	~TeamSemInfo()
	{
		if (fSemaphore != NULL)
			fSemaphore->Delete();
	}

	/**
	 * @brief Userland-visible id for this open.
	 *
	 * @return Logical sem id (named sem_id or private negative id).
	 */
	sem_id ID() const				{ return fSemaphore->ID(); }

	/**
	 * @brief Underlying kernel sem_id used by acquire/release/get_count.
	 *
	 * @return Haiku kernel sem_id.
	 */
	sem_id SemaphoreID() const		{ return fSemaphore->SemaphoreID(); }

	/**
	 * @brief Userland sem_t pointer associated with this binding.
	 *
	 * @return Pointer the caller originally passed to sem_open().
	 */
	sem_t* UserSemaphore() const	{ return fUserSemaphore; }

	/**
	 * @brief Increment the per-team open count for this sem.
	 */
	void Open()
	{
		fOpenCount++;
	}

	/**
	 * @brief Decrement the open count.
	 *
	 * @return true if this was the last outstanding sem_close().
	 */
	bool Close()
	{
		return --fOpenCount == 0;
	}

	/**
	 * @brief Duplicate this binding for team-fork cloning.
	 *
	 * @return Newly allocated TeamSemInfo with one extra SemInfo reference,
	 *         or NULL on allocation failure.
	 */
	TeamSemInfo* Clone() const
	{
		SemInfo* sem = fSemaphore->Clone();
		if (sem == NULL)
			return NULL;

		TeamSemInfo* clone = new(std::nothrow) TeamSemInfo(sem, fUserSemaphore);
		if (clone == NULL) {
			sem->Delete();
			return NULL;
		}

		clone->fOpenCount = fOpenCount;

		return clone;
	}

	/**
	 * @brief Accessor used by the per-team hash table to chain entries.
	 *
	 * @return Reference to the hash-link next pointer.
	 */
	TeamSemInfo*& HashLink()
	{
		return fHashLink;
	}

private:
	SemInfo*		fSemaphore;
	sem_t*			fUserSemaphore;
	int32			fOpenCount;

	TeamSemInfo*	fHashLink;
};


/**
 * @brief Open-hash policy that keys TeamSemInfo entries by logical sem_id.
 */
struct TeamSemHashDefinition {
	typedef sem_id		KeyType;
	typedef TeamSemInfo	ValueType;

	/**
	 * @brief Hash a lookup key (sem_id) into a table slot.
	 *
	 * @param key Logical sem id.
	 * @return Hash value.
	 */
	size_t HashKey(const KeyType& key) const
	{
		return (size_t)key;
	}

	/**
	 * @brief Hash an existing entry by its logical id.
	 *
	 * @param semaphore Entry to hash.
	 * @return Hash value matching that of its id.
	 */
	size_t Hash(TeamSemInfo* semaphore) const
	{
		return HashKey(semaphore->ID());
	}

	/**
	 * @brief Compare a key against an entry by id equality.
	 *
	 * @param key Lookup key.
	 * @param semaphore Candidate entry.
	 * @return true if the ids match.
	 */
	bool Compare(const KeyType& key, TeamSemInfo* semaphore) const
	{
		return key == semaphore->ID();
	}

	/**
	 * @brief Access the hash-chain link field of an entry.
	 *
	 * @param semaphore Entry whose link field is needed.
	 * @return Reference to the hash-link next pointer.
	 */
	TeamSemInfo*& GetLink(TeamSemInfo* semaphore) const
	{
		return semaphore->HashLink();
	}
};

} // namespace


/**
 * @brief Per-team context tracking all POSIX realtime semaphores the team has open.
 *
 * Holds a hash table keyed by logical sem_id (named sems use their global id,
 * unnamed/private sems use negative ids allocated from fNextPrivateSemID),
 * plus a counter enforcing MAX_POSIX_SEMS_PER_TEAM. Supplied to the Team
 * structure as an opaque realtime_sem_context*.
 */
struct realtime_sem_context {
	/**
	 * @brief Construct an empty context and initialise its lock.
	 */
	realtime_sem_context()
		:
		fSemaphoreCount(0)
	{
		mutex_init(&fLock, "realtime sem context");
	}

	/**
	 * @brief Tear down the context, closing every remaining open sem.
	 */
	~realtime_sem_context()
	{
		mutex_lock(&fLock);

		// delete all semaphores.
		SemTable::Iterator it = fSemaphores.GetIterator();
		while (TeamSemInfo* sem = it.Next()) {
			// Note, this uses internal knowledge about how the iterator works.
			// Ugly, but there's no good alternative.
			fSemaphores.RemoveUnchecked(sem);
			delete sem;
		}

		mutex_destroy(&fLock);
	}

	/**
	 * @brief Second-phase initialisation for a newly constructed context.
	 *
	 * @return B_OK on success, B_NO_MEMORY if the internal hash table cannot
	 *         be allocated.
	 */
	status_t Init()
	{
		fNextPrivateSemID = -1;
		return fSemaphores.Init();
	}

	/**
	 * @brief Deep-copy this context for a fork()ed child team.
	 *
	 * Each open sem is cloned (named sems get a refcount bump, unnamed sems
	 * get a fresh backing kernel sem). If any step fails, the partially built
	 * clone is destroyed and NULL is returned.
	 *
	 * @return Newly allocated context mirroring this one, or NULL on failure.
	 */
	realtime_sem_context* Clone()
	{
		// create new context
		realtime_sem_context* context = new(std::nothrow) realtime_sem_context;
		if (context == NULL)
			return NULL;
		ObjectDeleter<realtime_sem_context> contextDeleter(context);

		MutexLocker _(fLock);

		context->fNextPrivateSemID = fNextPrivateSemID;

		// clone all semaphores
		SemTable::Iterator it = fSemaphores.GetIterator();
		while (TeamSemInfo* sem = it.Next()) {
			TeamSemInfo* clonedSem = sem->Clone();
			if (clonedSem == NULL)
				return NULL;

			if (context->fSemaphores.Insert(clonedSem) != B_OK) {
				delete clonedSem;
				return NULL;
			}
			context->fSemaphoreCount++;
		}

		contextDeleter.Detach();
		return context;
	}

	/**
	 * @brief Open a named sem within this team, binding it to a userland sem_t.
	 *
	 * Consults the global table via sSemTable.OpenNamedSem() and then, if this
	 * team has already opened the same name, simply bumps the per-team open
	 * count and returns the original userland pointer. Rolls back the global
	 * creation if the per-team cap MAX_POSIX_SEMS_PER_TEAM is reached.
	 *
	 * @param name POSIX semaphore name.
	 * @param openFlags Open flags (O_CREAT, O_EXCL, ...).
	 * @param mode Permission bits for creation.
	 * @param semCount Initial value for creation.
	 * @param userSem Userland sem_t* passed to sem_open().
	 * @param[out] _usedUserSem The sem_t* actually used (matches an earlier
	 *                         open if this name was already open by the team).
	 * @param[out] _id Logical id for the opened semaphore.
	 * @param[out] _created True iff this call brought the name into existence.
	 * @return B_OK on success, otherwise an errno (ENOSPC, B_NO_MEMORY, ...).
	 */
	status_t OpenSem(const char* name, int openFlags, mode_t mode,
		uint32 semCount, sem_t* userSem, sem_t*& _usedUserSem, int32_t& _id,
		bool& _created)
	{
		NamedSem* sem = NULL;
		status_t error = sSemTable.OpenNamedSem(name, openFlags, mode, semCount,
			sem, _created);
		if (error != B_OK)
			return error;

		MutexLocker _(fLock);

		TeamSemInfo* teamSem = fSemaphores.Lookup(sem->ID());
		if (teamSem != NULL) {
			// already open -- just increment the open count
			teamSem->Open();
			sem->ReleaseReference();
			_usedUserSem = teamSem->UserSemaphore();
			_id = teamSem->ID();
			return B_OK;
		}

		// not open yet -- create a new team sem

		// first check the semaphore limit, though
		if (fSemaphoreCount >= MAX_POSIX_SEMS_PER_TEAM) {
			sem->ReleaseReference();
			if (_created)
				sSemTable.UnlinkNamedSem(name);
			return ENOSPC;
		}

		teamSem = new(std::nothrow) TeamSemInfo(sem, userSem);
		if (teamSem == NULL) {
			sem->ReleaseReference();
			if (_created)
				sSemTable.UnlinkNamedSem(name);
			return B_NO_MEMORY;
		}

		error = fSemaphores.Insert(teamSem);
		if (error != B_OK) {
			delete teamSem;
			if (_created)
				sSemTable.UnlinkNamedSem(name);
			return error;
		}

		fSemaphoreCount++;

		_usedUserSem = teamSem->UserSemaphore();
		_id = teamSem->ID();

		return B_OK;
	}

	/**
	 * @brief Decrement the per-team open count, freeing backing on last close.
	 *
	 * @param id Logical sem id previously returned by OpenSem().
	 * @param[out] deleteUserSem Set to the userland sem_t* when the last
	 *                           close happens, or NULL otherwise. The caller
	 *                           uses this to unmap the userland backing.
	 * @return B_OK on success, B_BAD_VALUE if id is not open in this team.
	 */
	status_t CloseSem(sem_id id, sem_t*& deleteUserSem)
	{
		deleteUserSem = NULL;

		MutexLocker _(fLock);

		TeamSemInfo* sem = fSemaphores.Lookup(id);
		if (sem == NULL)
			return B_BAD_VALUE;

		if (sem->Close()) {
			// last reference closed
			fSemaphores.Remove(sem);
			fSemaphoreCount--;
			deleteUserSem = sem->UserSemaphore();
			delete sem;
		}

		return B_OK;
	}

	/**
	 * @brief Block until this semaphore can be decremented (sem_wait/sem_timedwait).
	 *
	 * The table lookup happens under fLock but the actual blocking
	 * acquire_sem_etc() is issued without the lock held. POSIX-side callers
	 * pass B_ABSOLUTE_REAL_TIME_TIMEOUT for sem_timedwait(); the call is
	 * always interruptible so sem_wait() correctly reports EINTR on signal.
	 *
	 * @param id Logical sem id.
	 * @param flags Timeout mode flags (absolute / relative / none).
	 * @param timeout Timeout value in microseconds.
	 * @return B_OK on success, B_BAD_VALUE for an unknown id, B_WOULD_BLOCK /
	 *         B_TIMED_OUT / B_INTERRUPTED as mapped from acquire_sem_etc().
	 */
	status_t AcquireSem(sem_id id, uint32 flags, bigtime_t timeout)
	{
		MutexLocker locker(fLock);

		TeamSemInfo* sem = fSemaphores.Lookup(id);
		if (sem == NULL)
			return B_BAD_VALUE;
		else
			id = sem->SemaphoreID();

		locker.Unlock();

		status_t error = acquire_sem_etc(id, 1, flags | B_CAN_INTERRUPT, timeout);
		return error == B_BAD_SEM_ID ? B_BAD_VALUE : error;
	}

	/**
	 * @brief Atomically increment the semaphore, waking one waiter (sem_post).
	 *
	 * @param id Logical sem id.
	 * @return B_OK on success, B_BAD_VALUE for an unknown id.
	 */
	status_t ReleaseSem(sem_id id)
	{
		MutexLocker locker(fLock);

		TeamSemInfo* sem = fSemaphores.Lookup(id);
		if (sem == NULL)
			return B_BAD_VALUE;
		else
			id = sem->SemaphoreID();

		locker.Unlock();

		status_t error = release_sem(id);
		return error == B_BAD_SEM_ID ? B_BAD_VALUE : error;
	}

	/**
	 * @brief Return the current value of the semaphore (sem_getvalue).
	 *
	 * POSIX allows this to be racy; here we snapshot the kernel sem count
	 * after releasing the context lock, which matches user expectations.
	 *
	 * @param id Logical sem id.
	 * @param[out] _count Current semaphore value.
	 * @return B_OK on success, B_BAD_VALUE for an unknown id.
	 */
	status_t GetSemCount(sem_id id, int& _count)
	{
		MutexLocker locker(fLock);

		TeamSemInfo* sem = fSemaphores.Lookup(id);
		if (sem == NULL)
				return B_BAD_VALUE;
		else
			id = sem->SemaphoreID();

		locker.Unlock();

		int32 count;
		status_t error = get_sem_count(id, &count);
		if (error != B_OK)
			return error;

		_count = count;
		return B_OK;
	}

private:
	/**
	 * @brief Allocate the next unused negative id for an unnamed semaphore.
	 *
	 * Unnamed sems are distinguished by negative ids and cycle monotonically
	 * downward. Wraps back to -1 on overflow and skips any id that is
	 * currently occupied.
	 *
	 * @return A unique sem_id for a new unnamed semaphore.
	 */
	sem_id _NextPrivateSemID()
	{
		while (true) {
			if (fNextPrivateSemID >= 0)
				fNextPrivateSemID = -1;

			sem_id id = fNextPrivateSemID--;
			if (fSemaphores.Lookup(id) == NULL)
				return id;
		}
	}

private:
	typedef BOpenHashTable<TeamSemHashDefinition, true> SemTable;

	mutex		fLock;
	SemTable	fSemaphores;
	int32		fSemaphoreCount;
	sem_id		fNextPrivateSemID;
};


// #pragma mark - implementation private


/**
 * @brief Return the calling team's realtime_sem_context, creating it lazily.
 *
 * Uses an atomic compare-and-exchange to install the fresh context so that
 * concurrent first-touch callers in the same team do not race. Any loser
 * frees its unused context.
 *
 * @return Pointer to the team's realtime sem context, or NULL on allocation
 *         failure.
 */
static realtime_sem_context*
get_current_team_context()
{
	Team* team = thread_get_current_thread()->team;

	// get context
	realtime_sem_context* context = atomic_pointer_get(
		&team->realtime_sem_context);
	if (context != NULL)
		return context;

	// no context yet -- create a new one
	context = new(std::nothrow) realtime_sem_context;
	if (context == NULL || context->Init() != B_OK) {
		delete context;
		return NULL;
	}

	// set the allocated context
	realtime_sem_context* oldContext = atomic_pointer_test_and_set(
		&team->realtime_sem_context, context, (realtime_sem_context*)NULL);
	if (oldContext == NULL)
		return context;

	// someone else was quicker
	delete context;
	return oldContext;
}


/**
 * @brief Safely copy a userland POSIX sem name into a kernel KPath buffer.
 *
 * Validates that the pointer is in user space, allocates a path-sized buffer,
 * and returns ENAMETOOLONG for oversize names.
 *
 * @param userName User pointer to the null-terminated name.
 * @param buffer Path buffer sized to B_PATH_NAME_LENGTH.
 * @param[out] name On success, points at the buffer's kernel copy of the name.
 * @return B_OK on success, B_BAD_VALUE, B_BAD_ADDRESS, ENAMETOOLONG, or
 *         B_NO_MEMORY on failure.
 */
static status_t
copy_sem_name_to_kernel(const char* userName, KPath& buffer, char*& name)
{
	if (userName == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;

	if (buffer.InitCheck() != B_OK)
		return B_NO_MEMORY;

	// copy userland path to kernel
	name = buffer.LockBuffer();
	ssize_t actualLength = user_strlcpy(name, userName, buffer.BufferSize());

	if (actualLength < 0)
		return B_BAD_ADDRESS;
	if ((size_t)actualLength >= buffer.BufferSize())
		return ENAMETOOLONG;

	return B_OK;
}


// #pragma mark - kernel internal


/**
 * @brief One-time kernel initialiser for the POSIX realtime sem subsystem.
 *
 * Constructs the global named-sem table using placement new into the static
 * storage and panics if it cannot initialise its hash table. Called once
 * during kernel boot.
 */
void
realtime_sem_init()
{
	new(&sSemTable) GlobalSemTable;
	if (sSemTable.Init() != B_OK)
		panic("realtime_sem_init() failed to init global sem table");
}


/**
 * @brief Tear down a team's realtime sem context.
 *
 * Called from team exit / cleanup paths. Accepts NULL as a no-op is
 * implicit in delete.
 *
 * @param context The context to destroy.
 */
void
delete_realtime_sem_context(realtime_sem_context* context)
{
	delete context;
}


/**
 * @brief Clone a parent team's sem context for a newly forked team.
 *
 * @param context Parent context, or NULL.
 * @return Newly allocated context mirroring the parent, NULL if the parent
 *         was NULL or on allocation failure.
 */
realtime_sem_context*
clone_realtime_sem_context(realtime_sem_context* context)
{
	if (context == NULL)
		return NULL;

	return context->Clone();
}


// #pragma mark - syscalls


/**
 * @brief Syscall entry point for sem_open().
 *
 * Validates all user pointers, copies the name into kernel memory, resolves
 * the semaphore against the global table, and copies the resulting id and
 * sem_t* back out. On any late failure (userland copy-back) the partially
 * created global entry is rolled back.
 *
 * @param userName User pointer to the POSIX semaphore name.
 * @param openFlagsOrShared Flags combining O_CREAT/O_EXCL (or, for unnamed
 *        sems via sem_init(), the pshared flag).
 * @param mode Permission bits for creation.
 * @param semCount Initial value.
 * @param userSem Userland sem_t to bind to this open.
 * @param[out] _usedUserSem User pointer receiving the actually used sem_t*.
 * @return B_OK on success, or an errno-style status code.
 */
status_t
_user_realtime_sem_open(const char* userName, int openFlagsOrShared,
	mode_t mode, uint32 semCount, sem_t* userSem, sem_t** _usedUserSem)
{
	realtime_sem_context* context = get_current_team_context();
	if (context == NULL)
		return B_NO_MEMORY;

	if (semCount > SEM_VALUE_MAX)
		return B_BAD_VALUE;

	// userSem must always be given
	if (userSem == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(userSem))
		return B_BAD_ADDRESS;

	// check user pointers
	if (_usedUserSem == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(_usedUserSem) || !IS_USER_ADDRESS(userName))
		return B_BAD_ADDRESS;

	// copy name to kernel
	KPath nameBuffer(B_PATH_NAME_LENGTH);
	char* name;
	status_t error = copy_sem_name_to_kernel(userName, nameBuffer, name);
	if (error != B_OK)
		return error;

	// open the semaphore
	sem_t* usedUserSem;
	bool created = false;
	int32_t id;
	error = context->OpenSem(name, openFlagsOrShared, mode, semCount, userSem,
		usedUserSem, id, created);
	if (error != B_OK)
		return error;

	// copy results back to userland
	if (user_memcpy(&userSem->u.named_sem_id, &id, sizeof(int32_t)) != B_OK
		|| user_memcpy(_usedUserSem, &usedUserSem, sizeof(sem_t*)) != B_OK) {
		if (created)
			sSemTable.UnlinkNamedSem(name);
		sem_t* dummy;
		context->CloseSem(id, dummy);
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Syscall entry point for sem_close().
 *
 * On the final close, returns the sem_t* pointer so that userland can munmap
 * or free the backing storage.
 *
 * @param semID Logical id returned by sem_open().
 * @param[out] _deleteUserSem Optional user pointer receiving the sem_t* to
 *                            free on the last close; NULL otherwise.
 * @return B_OK on success, B_BAD_VALUE, or B_BAD_ADDRESS.
 */
status_t
_user_realtime_sem_close(sem_id semID, sem_t** _deleteUserSem)
{
	if (_deleteUserSem != NULL && !IS_USER_ADDRESS(_deleteUserSem))
		return B_BAD_ADDRESS;

	realtime_sem_context* context = get_current_team_context();
	if (context == NULL)
		return B_BAD_VALUE;

	// close sem
	sem_t* deleteUserSem;
	status_t error = context->CloseSem(semID, deleteUserSem);
	if (error != B_OK)
		return error;

	// copy back result to userland
	if (_deleteUserSem != NULL
		&& user_memcpy(_deleteUserSem, &deleteUserSem, sizeof(sem_t*))
			!= B_OK) {
		return B_BAD_ADDRESS;
	}

	return B_OK;
}


/**
 * @brief Syscall entry point for sem_unlink().
 *
 * Removes the name from the global table after permission check; existing
 * openers continue to work until they sem_close().
 *
 * @param userName User pointer to the POSIX semaphore name.
 * @return B_OK, ENOENT, EACCES, or an error from the name-copy helper.
 */
status_t
_user_realtime_sem_unlink(const char* userName)
{
	// copy name to kernel
	KPath nameBuffer(B_PATH_NAME_LENGTH);
	char* name;
	status_t error = copy_sem_name_to_kernel(userName, nameBuffer, name);
	if (error != B_OK)
		return error;

	return sSemTable.UnlinkNamedSem(name);
}


/**
 * @brief Syscall entry point for sem_getvalue().
 *
 * @param semID Logical sem id.
 * @param _value User pointer receiving the current sem count.
 * @return B_OK on success, B_BAD_VALUE, or B_BAD_ADDRESS.
 */
status_t
_user_realtime_sem_get_value(sem_id semID, int* _value)
{
	if (_value == NULL)
		return B_BAD_VALUE;
	if (!IS_USER_ADDRESS(_value))
		return B_BAD_ADDRESS;

	realtime_sem_context* context = get_current_team_context();
	if (context == NULL)
		return B_BAD_VALUE;

	// get sem count
	int count;
	status_t error = context->GetSemCount(semID, count);
	if (error != B_OK)
		return error;

	// copy back result to userland
	if (user_memcpy(_value, &count, sizeof(int)) != B_OK)
		return B_BAD_ADDRESS;

	return B_OK;
}


/**
 * @brief Syscall entry point for sem_post().
 *
 * @param semID Logical sem id.
 * @return B_OK on success, B_BAD_VALUE for an unknown id.
 */
status_t
_user_realtime_sem_post(sem_id semID)
{
	realtime_sem_context* context = get_current_team_context();
	if (context == NULL)
		return B_BAD_VALUE;

	return context->ReleaseSem(semID);
}


/**
 * @brief Syscall entry point for sem_wait()/sem_timedwait()/sem_trywait().
 *
 * Delegates to AcquireSem(). The syscall_restart_handle_post() wrapper
 * ensures that an EINTR-aborted wait is correctly restarted with an adjusted
 * timeout for sem_timedwait().
 *
 * @param semID Logical sem id.
 * @param flags Timeout flags (absolute/relative, or B_TIMEOUT for trywait).
 * @param timeout Timeout value in microseconds.
 * @return B_OK, B_BAD_VALUE, B_WOULD_BLOCK (trywait), B_TIMED_OUT, or
 *         B_INTERRUPTED.
 */
status_t
_user_realtime_sem_wait(sem_id semID, uint32 flags, bigtime_t timeout)
{
	realtime_sem_context* context = get_current_team_context();
	if (context == NULL)
		return B_BAD_VALUE;

	return syscall_restart_handle_post(context->AcquireSem(semID, flags, timeout));
}
