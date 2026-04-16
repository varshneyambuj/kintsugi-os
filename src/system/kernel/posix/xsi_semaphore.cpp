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
 *   Copyright 2008-2023, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *   		Salvatore Benedetto <salvatore.benedetto@gmail.com>
 */

/**
 * @file xsi_semaphore.cpp
 * @brief XSI (System V) IPC semaphore kernel implementation.
 *
 * Implements semget(), semctl() and the multi-op semop() semantics, plus the
 * semadj/SEM_UNDO bookkeeping that must survive team exit. A key_t is mapped
 * through sIpcHashTable to an Ipc wrapper holding the current semid; an
 * integer semid is mapped through sSemaphoreHashTable to an XsiSemaphoreSet
 * containing an array of XsiSemaphore slots. Each slot owns two condition
 * variables (waiters-for-zero and waiters-to-increase) so semop() can block
 * exactly the right set of threads. SEM_UNDO operations are recorded into
 * per-set and per-team lists; the team-exit hook xsi_sem_undo() walks the
 * team list and replays negated deltas to preserve the classic invariant
 * that a terminated process cannot leave a semaphore permanently decremented.
 * Waiters detect IPC_RMID during their sleep by snapshotting the set
 * sequence number before blocking and re-checking it after wake; a mismatch
 * is reported back as EIDRM, while a signal becomes EINTR.
 */

#include <posix/xsi_semaphore.h>

#include <new>

#include <sys/ipc.h>
#include <sys/types.h>

#include <OS.h>

#include <kernel.h>
#include <syscall_restart.h>

#include <util/atomic.h>
#include <util/AutoLock.h>
#include <util/DoublyLinkedList.h>
#include <util/OpenHashTable.h>
#include <AutoDeleter.h>
#include <StackOrHeapArray.h>


//#define TRACE_XSI_SEM
#ifdef TRACE_XSI_SEM
#	define TRACE(x)			dprintf x
#	define TRACE_ERROR(x)	dprintf x
#else
#	define TRACE(x)			/* nothing */
#	define TRACE_ERROR(x)	dprintf x
#endif


namespace {

class XsiSemaphoreSet;

/**
 * @brief Per-(team,set) SEM_UNDO record linking into two lists.
 *
 * Invariant: every sem_undo is simultaneously a member of its owning
 * XsiSemaphoreSet's fUndoList (via the primary link) and of its team's
 * xsi_sem_context->undo_list (via team_link). Tearing either side down
 * must remove the node from both lists.
 */
struct sem_undo : DoublyLinkedListLinkImpl<sem_undo> {
	/**
	 * @brief Build a sem_undo binding a team to a semaphore set.
	 *
	 * @param semaphoreSet Set this undo record belongs to.
	 * @param team Team owning the adjustments.
	 * @param undoValues Heap array of per-semaphore undo deltas (semadj).
	 */
	sem_undo(XsiSemaphoreSet *semaphoreSet, Team *team, int16 *undoValues)
		:
		semaphore_set(semaphoreSet),
		team(team),
		undo_values(undoValues)
	{
	}

	DoublyLinkedListLink<sem_undo>		team_link;
	XsiSemaphoreSet						*semaphore_set;
	Team								*team;
	int16								*undo_values;
};

typedef DoublyLinkedList<sem_undo> UndoList;
typedef DoublyLinkedList<sem_undo,
	DoublyLinkedListMemberGetLink<sem_undo, &sem_undo::team_link> > TeamList;

} // namespace


// Forward declared in global namespace.
/**
 * @brief Per-team SEM_UNDO context (lives on Team::xsi_sem_context).
 *
 * Holds the list of every sem_undo belonging to this team, protected by a
 * private mutex that is always acquired inside the owning set's lock to
 * preserve the "set-then-team" lock ordering used throughout the file.
 */
struct xsi_sem_context {
	/**
	 * @brief Construct the context and initialise its mutex.
	 */
	xsi_sem_context()
	{
		mutex_init(&lock, "Private team undo_list lock");
	}

	/**
	 * @brief Destroy the context's mutex; the team-list must already be empty.
	 */
	~xsi_sem_context()
	{
		mutex_destroy(&lock);
	}

	TeamList	undo_list;
	mutex		lock;
};


namespace {

/**
 * @brief One slot in an XSI semaphore set.
 *
 * Stores the XSI-visible semval and sempid together with two condition
 * variables: fWaitingToIncrease wakes threads that need the counter to go
 * above zero, fWaitingToBeZero wakes threads that need it to return to zero.
 */
class XsiSemaphore {
public:
	/**
	 * @brief Initialise an empty semaphore slot with value 0 and no owner pid.
	 */
	XsiSemaphore()
		:
		fLastPidOperation(0),
		fValue(0)
	{
		fWaitingToIncrease.Init(this, "XsiSemaphore");
		fWaitingToBeZero.Init(this, "XsiSemaphore");
	}

	/**
	 * @brief Destroy the slot, waking every waiter with EIDRM.
	 *
	 * sem_undo records referencing this slot are intentionally left in
	 * place; the team-exit path xsi_sem_undo() handles the case where the
	 * backing set has been destroyed by looking the set up and skipping it.
	 */
	~XsiSemaphore()
	{
		// For some reason the semaphore is getting destroyed.
		// Wake up any remaing awaiting threads
		fWaitingToIncrease.NotifyAll(EIDRM);
		fWaitingToBeZero.NotifyAll(EIDRM);

		// No need to remove any sem_undo request still
		// hanging. When the process exit and doesn't found
		// the semaphore set, it'll just ignore the sem_undo
		// request. That's better than iterating trough the
		// whole sUndoList. Beside we don't know our semaphore
		// number nor our semaphore set id.
	}

	/**
	 * @brief Attempt to apply a sem_op delta; report whether the caller must sleep.
	 *
	 * The caller inspects the return: true means value would have gone
	 * negative so the caller must undo previously-applied ops from the same
	 * semop() call and then block. A successful add wakes zero-waiters if
	 * the value hit zero, or increase-waiters if it went positive.
	 *
	 * @param value Signed delta to apply.
	 * @return true if the operation would block (nothing was mutated),
	 *         false if value was applied.
	 */
	bool Add(short value)
	{
		if ((int)(fValue + value) < 0) {
			TRACE(("XsiSemaphore::Add: potentially going to sleep\n"));
			return true;
		} else {
			fValue += value;
			if (fValue == 0)
				WakeUpThreads(true);
			else if (fValue > 0)
				WakeUpThreads(false);
			return false;
		}
	}

	/**
	 * @brief Unregister a waiter entry without actually sleeping on it.
	 *
	 * Called on the EINTR path so the entry does not linger on the
	 * condition variable after the thread decides not to retry.
	 *
	 * @param queueEntry Entry to unregister.
	 */
	static void Dequeue(ConditionVariableEntry *queueEntry)
	{
		queueEntry->Wait(B_RELATIVE_TIMEOUT, 0);
	}

	/**
	 * @brief Register a waiter on the appropriate condition variable.
	 *
	 * @param queueEntry Entry to register.
	 * @param waitForZero true for "waiting for semval == 0" (sem_op == 0),
	 *                    false for "waiting for semval to increase" (sem_op < 0).
	 */
	void Enqueue(ConditionVariableEntry *queueEntry, bool waitForZero)
	{
		if (waitForZero) {
			fWaitingToBeZero.Add(queueEntry);
		} else {
			fWaitingToIncrease.Add(queueEntry);
		}
	}

	/**
	 * @brief Last pid that modified this slot (GETPID).
	 *
	 * @return sempid value.
	 */
	pid_t LastPid() const
	{
		return fLastPidOperation;
	}

	/**
	 * @brief Reverse an earlier Add(), used for rollback and SEM_UNDO.
	 *
	 * Invariant: Revert(v) followed by Add(v) is a no-op modulo wakeups.
	 * Wakes zero-waiters or increase-waiters as appropriate after the
	 * adjustment.
	 *
	 * @param value Delta that was previously added; will be subtracted out.
	 */
	void Revert(short value)
	{
		fValue -= value;
		if (fValue == 0)
			WakeUpThreads(true);
		else if (fValue > 0)
			WakeUpThreads(false);
	}

	/**
	 * @brief Record the pid of the last process to touch this slot.
	 *
	 * @param pid Process id to store.
	 */
	void SetPid(pid_t pid)
	{
		fLastPidOperation = pid;
	}

	/**
	 * @brief Directly overwrite the semaphore value (SETVAL / SETALL).
	 *
	 * @param value New semval.
	 */
	void SetValue(ushort value)
	{
		fValue = value;
	}

	/**
	 * @brief Count of threads blocked waiting for semval to increase (GETNCNT).
	 *
	 * @return Number of waiters on fWaitingToIncrease.
	 */
	ushort ThreadsWaitingToIncrease()
	{
		return fWaitingToIncrease.EntriesCount();
	}

	/**
	 * @brief Count of threads blocked waiting for semval to hit zero (GETZCNT).
	 *
	 * @return Number of waiters on fWaitingToBeZero.
	 */
	ushort ThreadsWaitingToBeZero()
	{
		return fWaitingToBeZero.EntriesCount();
	}

	/**
	 * @brief Accessor for the current semaphore value (GETVAL).
	 *
	 * @return Current semval.
	 */
	ushort Value() const
	{
		return fValue;
	}

	/**
	 * @brief Wake every thread on exactly one of the two condition variables.
	 *
	 * Both kinds of waiters are broadcast en-masse because multiple waiters
	 * may be able to complete their semop() after one Add(), and picking
	 * exactly the right subset would require per-waiter op knowledge.
	 *
	 * @param waitingForZero true to wake fWaitingToBeZero, false for
	 *                       fWaitingToIncrease.
	 */
	void WakeUpThreads(bool waitingForZero)
	{
		if (waitingForZero) {
			fWaitingToBeZero.NotifyAll();
		} else {
			fWaitingToIncrease.NotifyAll();
		}
	}

private:
	pid_t				fLastPidOperation;				// sempid
	ushort				fValue;							// semval

	ConditionVariable	fWaitingToIncrease;
	ConditionVariable	fWaitingToBeZero;
};

#define MAX_XSI_SEMS_PER_TEAM	128

/**
 * @brief A set of XSI semaphores accessible through a single semid.
 *
 * Mirrors the XSI semid_ds record plus internal state: an array of
 * XsiSemaphore slots, the permission mask, timestamps, a per-set mutex,
 * a sequence number used to detect IPC_RMID across blocking, and the
 * per-set fUndoList of sem_undo records.
 */
class XsiSemaphoreSet {
public:
	/**
	 * @brief Allocate the slot array and initialise metadata.
	 *
	 * Success is reported through InitOK() so the caller can distinguish
	 * a well-constructed-but-unallocated set from a good one.
	 *
	 * @param numberOfSemaphores Number of slots to allocate.
	 * @param flags Permission bits (low nine bits).
	 */
	XsiSemaphoreSet(int numberOfSemaphores, int flags)
		: fInitOK(false),
		fLastSemctlTime((time_t)real_time_clock()),
		fLastSemopTime(0),
		fNumberOfSemaphores(numberOfSemaphores),
		fSemaphores(0)
	{
		mutex_init(&fLock, "XsiSemaphoreSet private mutex");
		SetIpcKey((key_t)-1);
		SetPermissions(flags);
		fSemaphores = new(std::nothrow) XsiSemaphore[numberOfSemaphores];
		if (fSemaphores == NULL) {
			TRACE_ERROR(("XsiSemaphoreSet::XsiSemaphore(): failed to allocate "
				"XsiSemaphore object\n"));
		} else
			fInitOK = true;
	}

	/**
	 * @brief Destroy the set, releasing the slot array and the lock.
	 *
	 * Waking of threads blocked on the individual slots is performed by
	 * each XsiSemaphore's own destructor as part of deleting fSemaphores.
	 */
	~XsiSemaphoreSet()
	{
		TRACE(("XsiSemaphoreSet::~XsiSemaphoreSet(): removing semaphore "
			"set %d\n", fID));
		mutex_destroy(&fLock);
		delete[] fSemaphores;
	}

	/**
	 * @brief Zero the calling team's undo delta for one slot (SETVAL helper).
	 *
	 * Invariant: after SETVAL the semaphore's value no longer depends on
	 * earlier operations, so any pending semadj contribution from the
	 * calling team must be discarded.
	 *
	 * @param semaphoreNumber Slot index whose undo value is cleared.
	 */
	void ClearUndo(ushort semaphoreNumber)
	{
		Team *team = thread_get_current_thread()->team;
		UndoList::Iterator iterator = fUndoList.GetIterator();
		while (iterator.HasNext()) {
			struct sem_undo *current = iterator.Next();
			if (current->team == team) {
				TRACE(("XsiSemaphoreSet::ClearUndo: teamID = %d, "
					"semaphoreSetID = %d, semaphoreNumber = %d\n",
					fID, semaphoreNumber, (int)team->id));
				MutexLocker _(team->xsi_sem_context->lock);
				current->undo_values[semaphoreNumber] = 0;
				return;
			}
		}
	}

	/**
	 * @brief Zero every undo delta for the calling team (SETALL helper).
	 *
	 * Invariant: SETALL rewrites all slots, so every semadj contribution
	 * from the calling team becomes meaningless and is cleared in one pass.
	 */
	void ClearUndos()
	{
		// Clear all undo_values (POSIX semadj equivalent)
		// of the calling team. This happens only on semctl SETALL.
		Team *team = thread_get_current_thread()->team;
		DoublyLinkedList<sem_undo>::Iterator iterator = fUndoList.GetIterator();
		while (iterator.HasNext()) {
			struct sem_undo *current = iterator.Next();
			if (current->team == team) {
				TRACE(("XsiSemaphoreSet::ClearUndos: teamID = %d, "
					"semaphoreSetID = %d\n", (int)team->id, fID));
				MutexLocker _(team->xsi_sem_context->lock);
				memset(current->undo_values, 0,
					sizeof(int16) * fNumberOfSemaphores);
				return;
			}
		}
	}

	/**
	 * @brief Apply a user-supplied semid_ds to this set (IPC_SET handler).
	 *
	 * Only uid, gid, and the low nine permission bits are copied; cuid and
	 * cgid remain fixed per XSI.
	 *
	 * @param result User-supplied semid_ds already in kernel memory.
	 */
	void DoIpcSet(struct semid_ds *result)
	{
		fPermissions.uid = result->sem_perm.uid;
		fPermissions.gid = result->sem_perm.gid;
		fPermissions.mode = (fPermissions.mode & ~0x01ff)
			| (result->sem_perm.mode & 0x01ff);
	}

	/**
	 * @brief Check whether the effective uid/gid may modify this set.
	 *
	 * Invariant implemented: S_IWOTH always grants, root always grants,
	 * S_IWUSR grants to the owner, S_IWGRP grants to the primary group.
	 *
	 * @return true if the caller has write permission.
	 */
	bool HasPermission() const
	{
		if ((fPermissions.mode & S_IWOTH) != 0)
			return true;

		uid_t uid = geteuid();
		if (uid == 0 || (uid == fPermissions.uid
			&& (fPermissions.mode & S_IWUSR) != 0))
			return true;

		gid_t gid = getegid();
		if (gid == fPermissions.gid && (fPermissions.mode & S_IWGRP) != 0)
			return true;

		return false;
	}

	/**
	 * @brief Read-permission check (currently delegates to HasPermission).
	 *
	 * @return true if the caller may read status from this set.
	 */
	bool HasReadPermission() const
	{
		// TODO: fix this
		return HasPermission();
	}

	/**
	 * @brief Accessor for the kernel-assigned semid.
	 *
	 * @return semid of this set.
	 */
	int ID() const
	{
		return fID;
	}

	/**
	 * @brief Report whether construction successfully allocated the slot array.
	 *
	 * @return true when the constructor succeeded in allocating fSemaphores.
	 */
	bool InitOK()
	{
		return fInitOK;
	}

	/**
	 * @brief Accessor for the associated IPC key.
	 *
	 * @return key_t, or (key_t)-1 for a private (IPC_PRIVATE) set.
	 */
	key_t IpcKey() const
	{
		return fPermissions.key;
	}

	/**
	 * @brief Return a copy of this set's ipc_perm record.
	 *
	 * @return ipc_perm snapshot suitable for IPC_STAT userland copy-out.
	 */
	struct ipc_perm IpcPermission() const
	{
		return fPermissions;
	}

	/**
	 * @brief Timestamp of the last semctl() call (sem_ctime).
	 *
	 * @return Unix time in seconds.
	 */
	time_t LastSemctlTime() const
	{
		return fLastSemctlTime;
	}

	/**
	 * @brief Timestamp of the last successful semop() call (sem_otime).
	 *
	 * @return Unix time in seconds, or 0 if semop() has never succeeded.
	 */
	time_t LastSemopTime() const
	{
		return fLastSemopTime;
	}

	/**
	 * @brief Accessor for the per-set mutex.
	 *
	 * @return Reference to the mutex guarding this set's state.
	 */
	mutex &Lock()
	{
		return fLock;
	}

	/**
	 * @brief Accessor for the number of slots in the set (sem_nsems).
	 *
	 * @return Slot count set at construction time.
	 */
	ushort NumberOfSemaphores() const
	{
		return fNumberOfSemaphores;
	}

	/**
	 * @brief Record (or extend) a SEM_UNDO adjustment for the calling team.
	 *
	 * Invariants upheld:
	 *   * At most one sem_undo per (team, set) pair.
	 *   * undo_values[i] stays in the [-USHRT_MAX, USHRT_MAX] window so
	 *     replay during team exit cannot overflow a short.
	 *   * The record is linked into BOTH fUndoList (per-set) and the
	 *     team's xsi_sem_context->undo_list before this function returns,
	 *     so either traversal path can later find and destroy it.
	 * On the first recorded undo for a team, the team's xsi_sem_context is
	 * lazily created with an atomic compare-exchange so concurrent callers
	 * agree on a single context.
	 *
	 * @param semaphoreNumber Slot the operation applies to.
	 * @param value Delta that was just applied to the semaphore (to be
	 *              subtracted at team exit).
	 * @return B_OK on success, ERANGE if the accumulated delta would
	 *         overflow the semadj range, B_NO_MEMORY on allocation failure.
	 */
	int RecordUndo(ushort semaphoreNumber, short value)
	{
		// Look if there is already a record from the team caller
		// for the same semaphore set
		bool notFound = true;
		Team *team = thread_get_current_thread()->team;
		DoublyLinkedList<sem_undo>::Iterator iterator = fUndoList.GetIterator();
		while (iterator.HasNext()) {
			struct sem_undo *current = iterator.Next();
			if (current->team == team) {
				// Update its undo value
				MutexLocker _(team->xsi_sem_context->lock);
				int newValue = current->undo_values[semaphoreNumber] + value;
				if (newValue > USHRT_MAX || newValue < -USHRT_MAX) {
					TRACE_ERROR(("XsiSemaphoreSet::RecordUndo: newValue %d "
						"out of range\n", newValue));
					return ERANGE;
				}
				current->undo_values[semaphoreNumber] = newValue;
				notFound = false;
				TRACE(("XsiSemaphoreSet::RecordUndo: found record. Team = %d, "
					"semaphoreSetID = %d, semaphoreNumber = %d, value = %d\n",
					(int)team->id, fID, semaphoreNumber,
					current->undo_values[semaphoreNumber]));
				break;
			}
		}

		if (notFound) {
			// First sem_undo request from this team for this
			// semaphore set
			int16 *undoValues
				= (int16 *)malloc(sizeof(int16) * fNumberOfSemaphores);
			if (undoValues == NULL)
				return B_NO_MEMORY;
			struct sem_undo *request
				= new(std::nothrow) sem_undo(this, team, undoValues);
			if (request == NULL) {
				free(undoValues);
				return B_NO_MEMORY;
			}
			memset(request->undo_values, 0, sizeof(int16) * fNumberOfSemaphores);
			request->undo_values[semaphoreNumber] = value;

			// Check if it's the very first sem_undo request for this team
			xsi_sem_context *context = atomic_pointer_get(&team->xsi_sem_context);
			if (context == NULL) {
				// Create the context
				context = new(std::nothrow) xsi_sem_context;
				if (context == NULL) {
					free(request->undo_values);
					delete request;
					return B_NO_MEMORY;
				}
				// Since we don't hold any global lock, someone
				// else could have been quicker than us, so we have
				// to delete the one we just created and use the one
				// in place.
				if (atomic_pointer_test_and_set(&team->xsi_sem_context, context,
					(xsi_sem_context *)NULL) != NULL)
					delete context;
			}

			// Add the request to both XsiSemaphoreSet and team list
			fUndoList.Add(request);
			MutexLocker _(team->xsi_sem_context->lock);
			team->xsi_sem_context->undo_list.Add(request);
			TRACE(("XsiSemaphoreSet::RecordUndo: new record added. Team = %d, "
				"semaphoreSetID = %d, semaphoreNumber = %d, value = %d\n",
				(int)team->id, fID, semaphoreNumber, value));
		}
		return B_OK;
	}

	/**
	 * @brief Undo a prior RecordUndo-driven Add() after a later op failed.
	 *
	 * Invariant: only invoked on the rollback path of a multi-op semop()
	 * that has already recorded some undos and must now reverse them.
	 * Unlike ClearUndo(), this does not touch the undo_values array; only
	 * the live semval is adjusted back.
	 *
	 * @param semaphoreNumber Slot to revert.
	 * @param value Delta previously applied (will be subtracted).
	 */
	void RevertUndo(ushort semaphoreNumber, short value)
	{
		// This can be called only when RecordUndo fails.
		Team *team = thread_get_current_thread()->team;
		DoublyLinkedList<sem_undo>::Iterator iterator = fUndoList.GetIterator();
		while (iterator.HasNext()) {
			struct sem_undo *current = iterator.Next();
			if (current->team == team) {
				MutexLocker _(team->xsi_sem_context->lock);
				fSemaphores[semaphoreNumber].Revert(value);
				break;
			}
		}
	}

	/**
	 * @brief Access the nth slot in the set.
	 *
	 * @param nth Zero-based slot index; caller is responsible for bounds.
	 * @return Pointer to the XsiSemaphore at that index.
	 */
	XsiSemaphore* Semaphore(int nth) const
	{
		return &fSemaphores[nth];
	}

	/**
	 * @brief Sequence number assigned by SetID(); used for IPC_RMID detection.
	 *
	 * @return Monotonically increasing number unique per creation event.
	 */
	uint32 SequenceNumber() const
	{
		return fSequenceNumber;
	}

	// Implemented after sGlobalSequenceNumber is declared
	void SetID();

	/**
	 * @brief Bind this set to an IPC key.
	 *
	 * @param key New key, or (key_t)-1 to mark the set private.
	 */
	void SetIpcKey(key_t key)
	{
		fPermissions.key = key;
	}

	/**
	 * @brief Stamp the sem_ctime timestamp with the current real-time clock.
	 */
	void SetLastSemctlTime()
	{
		fLastSemctlTime = real_time_clock();
	}

	/**
	 * @brief Stamp the sem_otime timestamp with the current real-time clock.
	 */
	void SetLastSemopTime()
	{
		fLastSemopTime = real_time_clock();
	}

	/**
	 * @brief Initialise owner/creator uid/gid and permission bits.
	 *
	 * Invariant: after this call cuid/cgid are fixed for the set's lifetime.
	 *
	 * @param flags Low nine bits carry the mode bits.
	 */
	void SetPermissions(int flags)
	{
		fPermissions.uid = fPermissions.cuid = geteuid();
		fPermissions.gid = fPermissions.cgid = getegid();
		fPermissions.mode = (flags & 0x01ff);
	}

	/**
	 * @brief Access the per-set list of sem_undo records.
	 *
	 * @return Reference to the fUndoList list.
	 */
	UndoList &GetUndoList()
	{
		return fUndoList;
	}

	/**
	 * @brief Accessor used by the hash table for its intrusive chain link.
	 *
	 * @return Reference to the hash-link next pointer.
	 */
	XsiSemaphoreSet*& Link()
	{
		return fLink;
	}

private:
	int							fID;					// semaphore set id
	bool						fInitOK;
	time_t						fLastSemctlTime;		// sem_ctime
	time_t						fLastSemopTime;			// sem_otime
	mutex 						fLock;					// private lock
	ushort						fNumberOfSemaphores;	// sem_nsems
	struct ipc_perm				fPermissions;			// sem_perm
	XsiSemaphore				*fSemaphores;			// array of semaphores
	uint32						fSequenceNumber;		// used as a second id
	UndoList					fUndoList;				// undo list requests

	XsiSemaphoreSet*			fLink;
};

/**
 * @brief Open-hash-table policy keying XsiSemaphoreSet entries by semid.
 */
struct SemaphoreHashTableDefinition {
	typedef int					KeyType;
	typedef XsiSemaphoreSet		ValueType;

	/**
	 * @brief Hash an integer semid into a table slot.
	 *
	 * @param key semid.
	 * @return Hash value.
	 */
	size_t HashKey (const int key) const
	{
		return (size_t)key;
	}

	/**
	 * @brief Hash an existing set by its stored semid.
	 *
	 * @param variable Set entry.
	 * @return Hash value matching its id.
	 */
	size_t Hash(XsiSemaphoreSet *variable) const
	{
		return (size_t)variable->ID();
	}

	/**
	 * @brief Compare a key against an entry by semid equality.
	 *
	 * @param key Lookup semid.
	 * @param variable Candidate entry.
	 * @return true when the ids match.
	 */
	bool Compare(const int key, XsiSemaphoreSet *variable) const
	{
		return (int)key == (int)variable->ID();
	}

	/**
	 * @brief Return the chain-link field embedded in the entry.
	 *
	 * @param variable Entry whose link is needed.
	 * @return Reference to the hash-link next pointer.
	 */
	XsiSemaphoreSet*& GetLink(XsiSemaphoreSet *variable) const
	{
		return variable->Link();
	}
};


/**
 * @brief Key-to-semid mapping stored in sIpcHashTable.
 *
 * Created the first time a non-private key is requested and destroyed when
 * the corresponding set is removed via IPC_RMID.
 */
class Ipc {
public:
	/**
	 * @brief Construct an Ipc entry for a key with no bound set yet.
	 *
	 * @param key XSI IPC key this entry represents.
	 */
	Ipc(key_t key)
		: fKey(key),
		fSemaphoreSetId(-1)
	{
	}

	/**
	 * @brief Accessor for the IPC key.
	 *
	 * @return key_t associated with this entry.
	 */
	key_t Key() const
	{
		return fKey;
	}

	/**
	 * @brief Accessor for the bound semid (-1 if none).
	 *
	 * @return semid currently bound to this key, or -1 if unbound.
	 */
	int SemaphoreSetID() const
	{
		return fSemaphoreSetId;
	}

	/**
	 * @brief Bind this key entry to a semaphore set by copying its id.
	 *
	 * @param semaphoreSet The set to bind.
	 */
	void SetSemaphoreSetID(XsiSemaphoreSet *semaphoreSet)
	{
		fSemaphoreSetId = semaphoreSet->ID();
	}

	/**
	 * @brief Accessor used by the IPC hash table for its chain link.
	 *
	 * @return Reference to the hash-link next pointer.
	 */
	Ipc*& Link()
	{
		return fLink;
	}

private:
	key_t				fKey;
	int					fSemaphoreSetId;
	Ipc*				fLink;
};


/**
 * @brief Open-hash-table policy keying Ipc entries by key_t.
 */
struct IpcHashTableDefinition {
	typedef key_t	KeyType;
	typedef Ipc		ValueType;

	/**
	 * @brief Hash a key_t into a table slot.
	 *
	 * @param key Lookup key.
	 * @return Hash value.
	 */
	size_t HashKey (const key_t key) const
	{
		return (size_t)(key);
	}

	/**
	 * @brief Hash an existing Ipc entry by its stored key.
	 *
	 * @param variable Ipc entry.
	 * @return Hash value matching the entry's key.
	 */
	size_t Hash(Ipc *variable) const
	{
		return (size_t)HashKey(variable->Key());
	}

	/**
	 * @brief Compare a key against an entry's key.
	 *
	 * @param key Lookup key.
	 * @param variable Candidate entry.
	 * @return true when the keys match.
	 */
	bool Compare(const key_t key, Ipc *variable) const
	{
		return (key_t)key == (key_t)variable->Key();
	}

	/**
	 * @brief Expose the chain-link field used by the hash table.
	 *
	 * @param variable Ipc entry whose link is needed.
	 * @return Reference to the hash-link next pointer.
	 */
	Ipc*& GetLink(Ipc *variable) const
	{
		return variable->Link();
	}
};

} // namespace


// Arbitrary limit
#define MAX_XSI_SEMAPHORE		4096
#define MAX_XSI_SEMAPHORE_SET	2048
static BOpenHashTable<IpcHashTableDefinition> sIpcHashTable;
static BOpenHashTable<SemaphoreHashTableDefinition> sSemaphoreHashTable;

static mutex sIpcLock;
static mutex sXsiSemaphoreSetLock;

static uint32 sGlobalSequenceNumber = 1;
static int32 sXsiSemaphoreCount = 0;
static int32 sXsiSemaphoreSetCount = 0;


//	#pragma mark -


/**
 * @brief Assign a unique semid and sequence number under the global lock.
 *
 * Invariant: caller holds sXsiSemaphoreSetLock. Starts the id at the
 * current wall-clock seconds and linearly probes upward (mod INT_MAX) until
 * the id is free. The global sequence number is then advanced and copied
 * in so that post-IPC_RMID lookups can detect reused ids.
 */
void
XsiSemaphoreSet::SetID()
{
	fID = real_time_clock();
	// The lock is held before calling us
	while (true) {
		if (sSemaphoreHashTable.Lookup(fID) == NULL)
			break;
		fID = (fID + 1) % INT_MAX;
	}
	sGlobalSequenceNumber = (sGlobalSequenceNumber + 1) % UINT_MAX;
	fSequenceNumber = sGlobalSequenceNumber;
}


//	#pragma mark - Kernel exported API


/**
 * @brief One-time kernel initialiser for the XSI semaphore subsystem.
 *
 * Brings up the two hash tables (Ipc and semaphore set) and their guarding
 * mutexes. Panics if either hash table fails to initialise.
 */
void
xsi_sem_init()
{
	// Initialize hash tables
	status_t status = sIpcHashTable.Init();
	if (status != B_OK)
		panic("xsi_sem_init() failed to initialize ipc hash table\n");
	status =  sSemaphoreHashTable.Init();
	if (status != B_OK)
		panic("xsi_sem_init() failed to initialize semaphore hash table\n");

	mutex_init(&sIpcLock, "global POSIX semaphore IPC table");
	mutex_init(&sXsiSemaphoreSetLock, "global POSIX xsi sem table");
}


/**
 * @brief Replay and tear down every SEM_UNDO request on team exit.
 *
 * Invariant the helper preserves: when a team terminates, every slot it
 * touched with SEM_UNDO is rolled back by the accumulated semadj value.
 * The semaphore-set hash lock is held across the traversal so IPC_RMID
 * cannot free sets out from under us; per-set and team locks are taken in
 * the conventional order (set first, then team).
 *
 * @param team Exiting team.
 */
void
xsi_sem_undo(Team *team)
{
	if (team->xsi_sem_context == NULL)
		return;

	// By acquiring first the semaphore hash table lock
	// we make sure the semaphore set in our sem_undo
	// list won't get removed by IPC_RMID call
	MutexLocker _(sXsiSemaphoreSetLock);

	// Process all sem_undo request in the team sem undo list
	// if any
	TeamList::Iterator iterator
		= team->xsi_sem_context->undo_list.GetIterator();
	while (iterator.HasNext()) {
		struct sem_undo *current = iterator.Next();
		XsiSemaphoreSet *semaphoreSet = current->semaphore_set;
		// Acquire the set lock in order to prevent race
		// condition with RecordUndo
		MutexLocker setLocker(semaphoreSet->Lock());
		MutexLocker _(team->xsi_sem_context->lock);
		// Revert the changes done by this process
		for (int i = 0; i < semaphoreSet->NumberOfSemaphores(); i++)
			if (current->undo_values[i] != 0) {
				TRACE(("xsi_sem_undo: TeamID = %d, SemaphoreSetID = %d, "
					"SemaphoreNumber = %d, undo value = %d\n", (int)team->id,
					semaphoreSet->ID(), i, (int)current->undo_values[i]));
				semaphoreSet->Semaphore(i)->Revert(current->undo_values[i]);
			}

		// Remove and free the sem_undo structure from both lists
		iterator.Remove();
		semaphoreSet->GetUndoList().Remove(current);
		delete current;
	}
	delete team->xsi_sem_context;
	team->xsi_sem_context = NULL;
}


//	#pragma mark - Syscalls


/**
 * @brief Syscall entry point for semget().
 *
 * Looks up (or lazily creates) the Ipc record for the key, enforces
 * IPC_CREAT/IPC_EXCL semantics, and allocates a new XsiSemaphoreSet if
 * required. IPC_PRIVATE always creates a fresh unkeyed set. Honors the
 * global MAX_XSI_SEMAPHORE and MAX_XSI_SEMAPHORE_SET caps.
 *
 * @param key XSI key or IPC_PRIVATE.
 * @param numberOfSemaphores Slot count (must be > 0 and < MAX_XSI_SEMS_PER_TEAM
 *        when creating; may be 0 to attach to an existing set).
 * @param flags Permission bits plus IPC_CREAT/IPC_EXCL.
 * @return New or existing semid on success, or a negative errno (ENOENT,
 *         EEXIST, EACCES, EINVAL, ENOSPC, ENOMEM).
 */
int
_user_xsi_semget(key_t key, int numberOfSemaphores, int flags)
{
	TRACE(("xsi_semget: key = %d, numberOfSemaphores = %d, flags = %d\n",
		(int)key, numberOfSemaphores, flags));
	XsiSemaphoreSet *semaphoreSet = NULL;
	Ipc *ipcKey = NULL;

	// Default assumption
	bool isPrivate = true;

	MutexLocker ipcLocker(sIpcLock);
	if (key != IPC_PRIVATE) {
		isPrivate = false;
		// Check if key already exist, if it does it already has a semaphore
		// set associated with it
		ipcKey = sIpcHashTable.Lookup(key);
		if (ipcKey != NULL) {
			// The IPC key exist and it already has a semaphore
			if ((flags & IPC_CREAT) && (flags & IPC_EXCL)) {
				TRACE(("xsi_semget: key %d already exist\n", (int)key));
				return EEXIST;
			}
			int semaphoreSetID = ipcKey->SemaphoreSetID();

			MutexLocker semaphoreSetLocker(sXsiSemaphoreSetLock);
			semaphoreSet = sSemaphoreHashTable.Lookup(semaphoreSetID);
			if (semaphoreSet == NULL) {
				TRACE(("xsi_semget: calling process has no semaphore, "
					"key %d\n", (int)key));
				return EINVAL;
			}
			if (!semaphoreSet->HasPermission()) {
				TRACE(("xsi_semget: calling process has no permission "
					"on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)key));
				return EACCES;
			}
			if (numberOfSemaphores > semaphoreSet->NumberOfSemaphores()
					&& numberOfSemaphores != 0) {
				TRACE(("xsi_semget: numberOfSemaphores greater than the "
					"one associated with semaphore %d, key %d\n",
					semaphoreSet->ID(), (int)key));
				return EINVAL;
			}

			return semaphoreSet->ID();
		}

		// The ipc key does not exist. Create it and add it to the system
		if (!(flags & IPC_CREAT)) {
			TRACE(("xsi_semget: key %d does not exist, but the "
				"caller did not ask for creation\n",(int)key));
			return ENOENT;
		}
		ipcKey = new(std::nothrow) Ipc(key);
		if (ipcKey == NULL) {
			TRACE_ERROR(("xsi_semget: failed to create new Ipc object "
				"for key %d\n",	(int)key));
			return ENOMEM;
		}
	}

	// Create a new semaphore set for this key
	if (numberOfSemaphores <= 0
			|| numberOfSemaphores >= MAX_XSI_SEMS_PER_TEAM) {
		TRACE_ERROR(("xsi_semget: numberOfSemaphores out of range\n"));
		delete ipcKey;
		return EINVAL;
	}
	if (sXsiSemaphoreCount >= MAX_XSI_SEMAPHORE
			|| sXsiSemaphoreSetCount >= MAX_XSI_SEMAPHORE_SET) {
		TRACE_ERROR(("xsi_semget: reached limit of maximum number of "
			"semaphores allowed\n"));
		delete ipcKey;
		return ENOSPC;
	}

	semaphoreSet = new(std::nothrow) XsiSemaphoreSet(numberOfSemaphores, flags);
	if (semaphoreSet == NULL || !semaphoreSet->InitOK()) {
		TRACE_ERROR(("xsi_semget: failed to allocate a new xsi "
			"semaphore set\n"));
		delete semaphoreSet;
		delete ipcKey;
		return ENOMEM;
	}

	atomic_add(&sXsiSemaphoreCount, numberOfSemaphores);
	atomic_add(&sXsiSemaphoreSetCount, 1);

	MutexLocker semaphoreSetLocker(sXsiSemaphoreSetLock);
	semaphoreSet->SetID();
	if (isPrivate) {
		semaphoreSet->SetIpcKey((key_t)-1);
	} else {
		sIpcHashTable.Insert(ipcKey);
		semaphoreSet->SetIpcKey(key);
		ipcKey->SetSemaphoreSetID(semaphoreSet);
	}
	sSemaphoreHashTable.Insert(semaphoreSet);
	TRACE(("semget: new set = %d created, sequence = %ld\n",
		semaphoreSet->ID(), semaphoreSet->SequenceNumber()));

	return semaphoreSet->ID();
}


/**
 * @brief Syscall entry point for semctl().
 *
 * Dispatches the full XSI command table: GETVAL/SETVAL/GETPID/GETNCNT/
 * GETZCNT/GETALL/SETALL for individual slots and slot vectors, plus
 * IPC_STAT/IPC_SET/IPC_RMID for the set as a whole. Read operations check
 * read permission; mutating operations check write permission; IPC_RMID
 * removes the set, drops the Ipc entry, sweeps out every queued sem_undo,
 * and wakes all sleepers via the destructor chain.
 *
 * Lock ordering: the ipc-hash and set-hash locks are acquired first; for
 * non-RMID commands both are released once the per-set mutex is held so
 * concurrent callers on other sets can make progress. IPC_RMID keeps both
 * hash locks held until the set is gone so no waiter can latch on to a
 * destroyed mutex.
 *
 * @param semaphoreID semid.
 * @param semaphoreNumber Slot index (only consulted for slot commands).
 * @param command One of GETVAL/SETVAL/GETPID/GETNCNT/GETZCNT/GETALL/SETALL/
 *                IPC_STAT/IPC_SET/IPC_RMID.
 * @param _args User pointer to a semun (interpretation depends on command).
 * @return Command-specific non-negative value on success (e.g. GETVAL
 *         returns the semval) or a negative errno on failure.
 */
int
_user_xsi_semctl(int semaphoreID, int semaphoreNumber, int command,
	union semun *_args)
{
	TRACE(("xsi_semctl: semaphoreID = %d, semaphoreNumber = %d, command = %d\n",
		semaphoreID, semaphoreNumber, command));

	union semun args = {0};
	if (_args != NULL) {
		if (!IS_USER_ADDRESS(_args)
				|| user_memcpy(&args, _args, sizeof(union semun)) != B_OK)
			return B_BAD_ADDRESS;
	}

	MutexLocker ipcHashLocker(sIpcLock);
	MutexLocker setHashLocker(sXsiSemaphoreSetLock);
	XsiSemaphoreSet *semaphoreSet = sSemaphoreHashTable.Lookup(semaphoreID);
	if (semaphoreSet == NULL) {
		TRACE(("xsi_semctl: semaphore set id %d not valid\n",
			semaphoreID));
		return EINVAL;
	}
	if (semaphoreNumber < 0
		|| semaphoreNumber > semaphoreSet->NumberOfSemaphores()) {
		TRACE(("xsi_semctl: semaphore number %d not valid for "
			"semaphore %d\n", semaphoreNumber, semaphoreID));
		return EINVAL;
	}

	// Lock the semaphore set itself and release both the semaphore
	// set hash table lock and the ipc hash table lock _only_ if
	// the command it's not IPC_RMID, this prevents undesidered
	// situation from happening while (hopefully) improving the
	// concurrency.
	MutexLocker setLocker(semaphoreSet->Lock());
	if (command != IPC_RMID) {
		setHashLocker.Unlock();
		ipcHashLocker.Unlock();
	}

	int result = 0;
	XsiSemaphore *semaphore = semaphoreSet->Semaphore(semaphoreNumber);
	switch (command) {
		case GETVAL: {
			if (!semaphoreSet->HasReadPermission()) {
				TRACE(("xsi_semctl: calling process has not permission "
					"on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else
				result = semaphore->Value();
			break;
		}

		case SETVAL: {
			if (!semaphoreSet->HasPermission()) {
				TRACE(("xsi_semctl: calling process has not permission "
					"on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else {
				if (args.val > USHRT_MAX) {
					TRACE(("xsi_semctl: value %d out of range\n", args.val));
					result = ERANGE;
				} else {
					semaphore->SetValue(args.val);
					semaphoreSet->ClearUndo(semaphoreNumber);
				}
			}
			break;
		}

		case GETPID: {
			if (!semaphoreSet->HasReadPermission()) {
				TRACE(("xsi_semctl: calling process has not permission "
					"on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else
				result = semaphore->LastPid();
			break;
		}

		case GETNCNT: {
			if (!semaphoreSet->HasReadPermission()) {
				TRACE(("xsi_semctl: calling process has not permission "
					"on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else
				result = semaphore->ThreadsWaitingToIncrease();
			break;
		}

		case GETZCNT: {
			if (!semaphoreSet->HasReadPermission()) {
				TRACE(("xsi_semctl: calling process has not permission "
					"on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else
				result = semaphore->ThreadsWaitingToBeZero();
			break;
		}

		case GETALL: {
			if (!semaphoreSet->HasReadPermission()) {
				TRACE(("xsi_semctl: calling process has not read "
					"permission on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else
				for (int i = 0; i < semaphoreSet->NumberOfSemaphores(); i++) {
					semaphore = semaphoreSet->Semaphore(i);
					unsigned short value = semaphore->Value();
					if (user_memcpy(args.array + i, &value,
							sizeof(unsigned short)) != B_OK) {
						TRACE_ERROR(("xsi_semctl: user_memcpy failed\n"));
						result = B_BAD_ADDRESS;
						break;
					}
				}
			break;
		}

		case SETALL: {
			if (!semaphoreSet->HasPermission()) {
				TRACE(("xsi_semctl: calling process has not permission "
					"on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else {
				bool doClear = true;
				for (int i = 0; i < semaphoreSet->NumberOfSemaphores(); i++) {
					semaphore = semaphoreSet->Semaphore(i);
					unsigned short value;
					if (user_memcpy(&value, args.array + i,
							sizeof(unsigned short)) != B_OK) {
						TRACE_ERROR(("xsi_semctl: user_memcpy failed\n"));
						result = B_BAD_ADDRESS;
						doClear = false;
						break;
					} else
						semaphore->SetValue(value);
				}
				if (doClear)
					semaphoreSet->ClearUndos();
			}
			break;
		}

		case IPC_STAT: {
			if (!semaphoreSet->HasReadPermission()) {
				TRACE(("xsi_semctl: calling process has not read "
					"permission on semaphore %d, key %d\n", semaphoreSet->ID(),
					(int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else {
				struct semid_ds sem;
				sem.sem_perm = semaphoreSet->IpcPermission();
				sem.sem_nsems = semaphoreSet->NumberOfSemaphores();
				sem.sem_otime = semaphoreSet->LastSemopTime();
				sem.sem_ctime = semaphoreSet->LastSemctlTime();
				if (user_memcpy(args.buf, &sem, sizeof(struct semid_ds))
						< B_OK) {
					TRACE_ERROR(("xsi_semctl: user_memcpy failed\n"));
					result = B_BAD_ADDRESS;
				}
			}
			break;
		}

		case IPC_SET: {
			if (!semaphoreSet->HasPermission()) {
				TRACE(("xsi_semctl: calling process has not "
					"permission on semaphore %d, key %d\n",
					semaphoreSet->ID(), (int)semaphoreSet->IpcKey()));
				result = EACCES;
			} else {
				struct semid_ds sem;
				if (user_memcpy(&sem, args.buf, sizeof(struct semid_ds))
						!= B_OK) {
					TRACE_ERROR(("xsi_semctl: user_memcpy failed\n"));
					result = B_BAD_ADDRESS;
				} else
					semaphoreSet->DoIpcSet(&sem);
			}
			break;
		}

		case IPC_RMID: {
			// If this was the command, we are still holding
			// the semaphore set hash table lock along with the
			// ipc hash table lock and the semaphore set lock
			// itself, this way we are sure there is not
			// one waiting in the queue of the mutex.
			if (!semaphoreSet->HasPermission()) {
				TRACE(("xsi_semctl: calling process has not "
					"permission on semaphore %d, key %d\n",
					semaphoreSet->ID(), (int)semaphoreSet->IpcKey()));
				return EACCES;
			}
			key_t key = semaphoreSet->IpcKey();
			Ipc *ipcKey = NULL;
			if (key != -1) {
				ipcKey = sIpcHashTable.Lookup(key);
				sIpcHashTable.Remove(ipcKey);
			}
			sSemaphoreHashTable.Remove(semaphoreSet);
			// Wake up of threads waiting on this set
			// happens in the destructor
			if (key != -1)
				delete ipcKey;
			atomic_add(&sXsiSemaphoreCount, -semaphoreSet->NumberOfSemaphores());
			atomic_add(&sXsiSemaphoreSetCount, -1);
			// Remove any sem_undo request
			while (struct sem_undo *entry
					= semaphoreSet->GetUndoList().RemoveHead()) {
				MutexLocker _(entry->team->xsi_sem_context->lock);
				entry->team->xsi_sem_context->undo_list.Remove(entry);
				delete entry;
			}

			setLocker.Detach();
			delete semaphoreSet;
			return 0;
		}

		default:
			TRACE_ERROR(("xsi_semctl: command %d not valid\n", command));
			result = EINVAL;
	}

	return result;
}


/**
 * @brief Syscall entry point for semop().
 *
 * Atomic multi-operation primitive. The loop attempts to apply every
 * sembuf entry in order; if any entry would block, operations already
 * applied in this pass are reverted before the thread enqueues on the
 * offending slot's condition variable and sleeps. Post-wake, the set is
 * re-looked-up and the sequence number compared so that an IPC_RMID that
 * ran during the sleep becomes EIDRM, while signal-driven wakeups become
 * EINTR (distinct from the IPC_NOWAIT fast-fail path that returns EAGAIN).
 * SEM_UNDO entries are recorded only after the whole set of ops succeeds;
 * a late RecordUndo() failure rolls back both the semaphore deltas and
 * any undo entries registered earlier in the same call.
 *
 * @param semaphoreID semid.
 * @param ops User pointer to an array of sembuf structures.
 * @param numOps Number of sembuf entries.
 * @return B_OK (0) on success, or an errno-style value (EINVAL, EAGAIN,
 *         EINTR, EIDRM, ENOSPC, ENOMEM, B_BAD_ADDRESS).
 */
status_t
_user_xsi_semop(int semaphoreID, struct sembuf *ops, size_t numOps)
{
	TRACE(("xsi_semop: semaphoreID = %d, ops = %p, numOps = %ld\n",
		semaphoreID, ops, numOps));

	if (!IS_USER_ADDRESS(ops)) {
		TRACE(("xsi_semop: sembuf address is not valid\n"));
		return B_BAD_ADDRESS;
	}
	if (numOps < 0 || numOps >= MAX_XSI_SEMS_PER_TEAM) {
		TRACE(("xsi_semop: numOps out of range\n"));
		return EINVAL;
	}

	MutexLocker setHashLocker(sXsiSemaphoreSetLock);
	XsiSemaphoreSet *semaphoreSet = sSemaphoreHashTable.Lookup(semaphoreID);
	if (semaphoreSet == NULL) {
		TRACE(("xsi_semop: semaphore set id %d not valid\n",
			semaphoreID));
		return EINVAL;
	}
	MutexLocker setLocker(semaphoreSet->Lock());
	setHashLocker.Unlock();

	BStackOrHeapArray<struct sembuf, 16> operations(numOps);
	if (!operations.IsValid()) {
		TRACE_ERROR(("xsi_semop: failed to allocate sembuf struct\n"));
		return B_NO_MEMORY;
	}

	if (user_memcpy(operations, ops,
			(sizeof(struct sembuf) * numOps)) != B_OK) {
		TRACE_ERROR(("xsi_semop: user_memcpy failed\n"));
		return B_BAD_ADDRESS;
	}

	// We won't do partial request, that is operations
	// only on some sempahores belonging to the set and then
	// going to sleep. If we must wait on a semaphore, we undo
	// all the operations already done and go to sleep, otherwise
	// we may caused some unwanted deadlock among threads
	// fighting for the same set.
	bool notDone = true;
	status_t result = 0;
	while (notDone) {
		XsiSemaphore *semaphore = NULL;
		const ushort numberOfSemaphores = semaphoreSet->NumberOfSemaphores();
		bool goToSleep = false;

		uint32 i = 0;
		for (; i < numOps; i++) {
			ushort semaphoreNumber = operations[i].sem_num;
			if (semaphoreNumber >= numberOfSemaphores) {
				TRACE(("xsi_semop: %" B_PRIu32 " invalid semaphore number"
					"\n", i));
				result = EINVAL;
				break;
			}
			semaphore = semaphoreSet->Semaphore(semaphoreNumber);
			unsigned short value = semaphore->Value();
			short operation = operations[i].sem_op;
			TRACE(("xsi_semop: semaphoreNumber = %d, value = %d\n",
				semaphoreNumber, value));
			if (operation < 0) {
				if (semaphore->Add(operation)) {
					if (operations[i].sem_flg & IPC_NOWAIT)
						result = EAGAIN;
					else
						goToSleep = true;
					break;
				}
			} else if (operation == 0) {
				if (value == 0)
					continue;
				else if (operations[i].sem_flg & IPC_NOWAIT) {
					result = EAGAIN;
					break;
				} else {
					goToSleep = true;
					break;
				}
			} else {
				// Operation must be greater than zero,
				// just add the value and continue
				semaphore->Add(operation);
			}
		}

		// Either we have to wait or an error occured
		if (goToSleep || result != 0) {
			// Undo all previously done operations
			for (uint32 j = 0; j < i; j++) {
				ushort semaphoreNumber = operations[j].sem_num;
				semaphore = semaphoreSet->Semaphore(semaphoreNumber);
				short operation = operations[j].sem_op;
				if (operation != 0)
					semaphore->Revert(operation);
			}
			if (result != 0)
				return result;

			// We have to wait: first enqueue the thread
			// in the appropriate set waiting list, then
			// unlock the set itself and block the thread.
			bool waitOnZero = true;
			if (operations[i].sem_op != 0)
				waitOnZero = false;

			ConditionVariableEntry queueEntry;
			semaphore->Enqueue(&queueEntry, waitOnZero);

			const uint32 sequenceNumber = semaphoreSet->SequenceNumber();

			TRACE(("xsi_semop: thread %d going to sleep\n", (int)thread->id));
			setLocker.Unlock();
			semaphoreSet = NULL;
			semaphore = NULL;
			result = queueEntry.Wait(B_CAN_INTERRUPT);
			TRACE(("xsi_semop: thread %d back to life\n", (int)thread->id));

			// We are back to life. Find out why!
			// Make sure the set hasn't been deleted or worst yet replaced.
			setHashLocker.Lock();
			semaphoreSet = sSemaphoreHashTable.Lookup(semaphoreID);
			if (result == EIDRM || semaphoreSet == NULL || (semaphoreSet != NULL
					&& sequenceNumber != semaphoreSet->SequenceNumber())) {
				TRACE(("xsi_semop: semaphore set id %d (sequence = "
					"%" B_PRIu32 ") got destroyed\n", semaphoreID,
					sequenceNumber));
				notDone = false;
				result = EIDRM;
			} else if (result == B_INTERRUPTED) {
				TRACE(("xsi_semop: thread %d got interrupted while "
					"waiting on semaphore set id %d\n", (int)thread_get_current_thread_id(),
					semaphoreID));
				XsiSemaphore::Dequeue(&queueEntry);
				result = EINTR;
				notDone = false;
			} else {
				setLocker.Lock();
				setHashLocker.Unlock();
			}
		} else {
			// everything worked like a charm (so far)
			notDone = false;
			TRACE(("xsi_semop: semaphore acquired succesfully\n"));
			// We acquired the semaphore, now records the sem_undo
			// requests
			for (uint32 i = 0; i < numOps; i++) {
				if ((operations[i].sem_flg & SEM_UNDO) == 0)
					continue;

				ushort semaphoreNumber = operations[i].sem_num;
				XsiSemaphore *semaphore = semaphoreSet->Semaphore(semaphoreNumber);
				short operation = operations[i].sem_op;

				if (semaphoreSet->RecordUndo(semaphoreNumber, operation) != B_OK) {
					// Unlikely scenario, but we might get here.
					// Undo everything!
					// Start with semaphore operations
					for (uint32 j = 0; j < numOps; j++) {
						ushort semaphoreNumber = operations[j].sem_num;
						semaphore = semaphoreSet->Semaphore(semaphoreNumber);
						short operation = operations[j].sem_op;
						if (operation != 0)
							semaphore->Revert(operation);
					}
					// Remove all previously registered sem_undo request
					for (uint32 j = 0; j < i; j++) {
						if (operations[j].sem_flg & SEM_UNDO) {
							semaphoreSet->RevertUndo(operations[j].sem_num,
								operations[j].sem_op);
						}
					}
					result = ENOSPC;
				}
			}
		}
	}

	// We did it. Set the pid of all semaphores used
	if (result == 0) {
		for (uint32 i = 0; i < numOps; i++) {
			ushort semaphoreNumber = operations[i].sem_num;
			XsiSemaphore *semaphore = semaphoreSet->Semaphore(semaphoreNumber);
			semaphore->SetPid(getpid());
		}
		semaphoreSet->SetLastSemopTime();
	}
	return result;
}
