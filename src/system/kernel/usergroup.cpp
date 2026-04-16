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

/** @file usergroup.cpp
 *  @brief POSIX user/group identity management for kernel teams.
 *
 * Implements get/set of real, effective, and saved user/group ids,
 * supplementary group lists, and the privilege checks that gate them. */


#include <usergroup.h>

#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#include <new>

#include <heap.h>
#include <kernel.h>
#include <syscalls.h>
#include <team.h>
#include <thread.h>
#include <thread_types.h>
#include <util/AutoLock.h>
#include <util/ThreadAutoLock.h>
#include <vfs.h>

#include <AutoDeleter.h>


// #pragma mark - Implementation Private


/**
 * @brief Test whether @p team runs with privileges sufficient to bypass the
 *        POSIX id-change permission checks.
 *
 * Today this is simply the "effective uid is root (0)" check. Kept in a
 * helper so that a future capability model can replace it in one place.
 *
 * @param team Team whose credentials are inspected.
 * @return true if the team is effectively root.
 */
static bool
is_privileged(Team* team)
{
	// currently only the root user is privileged
	return team->effective_uid == 0;
}


/**
 * @brief Shared implementation of setresgid()/setregid()/setgid() semantics.
 *
 * Updates the team's real (@c real_gid), effective (@c effective_gid), and
 * saved-set (@c saved_set_gid) group ids atomically under the team lock,
 * enforcing POSIX rules: a non-privileged caller may only switch each id
 * among the current real/effective/saved set, while a privileged caller may
 * set any value. An argument of @c (gid_t)-1 leaves the corresponding field
 * untouched. Does not modify the supplementary-group list.
 *
 * @param rgid               New real gid, or @c (gid_t)-1 to leave unchanged.
 * @param egid               New effective gid, or @c (gid_t)-1.
 * @param ssgid              New saved-set gid, or @c (gid_t)-1 to inherit it
 *                           from the current value / from @p rgid per
 *                           setregid() common-practice semantics.
 * @param setAllIfPrivileged When true, apply setgid()-style semantics: a
 *                           privileged caller sets all three ids to @p rgid;
 *                           when false, use setregid()/setresgid() semantics.
 * @param kernel             Treat the caller as privileged regardless of the
 *                           team's effective uid (used by the @c _kern_ entry
 *                           points).
 * @retval B_OK  The ids were updated.
 * @retval EPERM The caller lacked privilege for the requested change.
 */
static status_t
common_setresgid(gid_t rgid, gid_t egid, gid_t ssgid, bool setAllIfPrivileged, bool kernel)
{
	Team* team = thread_get_current_thread()->team;

	TeamLocker teamLocker(team);

	bool privileged = kernel || is_privileged(team);

	if (ssgid == (gid_t)-1 || !privileged)
		ssgid = team->saved_set_gid;

	// real gid
	if (rgid == (gid_t)-1) {
		rgid = team->real_gid;
	} else {
		if (setAllIfPrivileged) {
			// setgid() semantics: If privileged set both, real, effective and
			// saved set-gid, otherwise set the effective gid.
			if (privileged) {
				team->saved_set_gid = rgid;
				team->real_gid = rgid;
				team->effective_gid = rgid;
				return B_OK;
			}

			// not privileged -- set only the effective gid
			egid = rgid;
			rgid = team->real_gid;
		} else {
			// setregid() semantics: set the real gid, if allowed to
			// Note: We allow setting the real gid to the effective gid. This
			// is unspecified by the specs, but is common practice.
			if (!privileged && rgid != team->real_gid
				&& rgid != team->effective_gid) {
				return EPERM;
			}

			// Note: Also common practice is to set the saved set-gid when the
			// real gid is set.
			if (rgid != team->real_gid)
				ssgid = rgid;
		}
	}

	// effective gid
	if (egid == (gid_t)-1) {
		egid = team->effective_gid;
	} else {
		if (!privileged && egid != team->effective_gid
			&& egid != team->real_gid && egid != team->saved_set_gid) {
			return EPERM;
		}
	}

	// Getting here means all checks were successful -- set the gids.
	team->real_gid = rgid;
	team->effective_gid = egid;
	team->saved_set_gid = ssgid;

	return B_OK;
}


/**
 * @brief Shared implementation of setresuid()/setreuid()/setuid() semantics.
 *
 * Mirror of common_setresgid() for user ids. Updates the team's @c real_uid,
 * @c effective_uid, and @c saved_set_uid atomically under the team lock,
 * enforcing POSIX setresuid() semantics: the three arguments name the
 * _real_, _effective_, and _saved_set_ uid respectively, any of which may be
 * @c (uid_t)-1 to leave the existing value in place. A non-privileged caller
 * may only switch each id among the current real/effective/saved set.
 *
 * @param ruid               New real uid, or @c (uid_t)-1.
 * @param euid               New effective uid, or @c (uid_t)-1.
 * @param ssuid              New saved-set uid, or @c (uid_t)-1.
 * @param setAllIfPrivileged When true, apply setuid()-style semantics: a
 *                           privileged caller sets all three ids to @p ruid;
 *                           when false, use setreuid()/setresuid() semantics.
 * @param kernel             Treat the caller as privileged regardless of the
 *                           team's effective uid.
 * @retval B_OK  The ids were updated.
 * @retval EPERM The caller lacked privilege for the requested change.
 */
static status_t
common_setresuid(uid_t ruid, uid_t euid, uid_t ssuid, bool setAllIfPrivileged, bool kernel)
{
	Team* team = thread_get_current_thread()->team;

	TeamLocker teamLocker(team);

	bool privileged = kernel || is_privileged(team);

	if (ssuid == (uid_t)-1)
		ssuid = team->saved_set_uid;
	else {
		if (!privileged && ssuid != team->effective_uid
			&& ssuid != team->real_uid && ssuid != team->saved_set_uid) {
			return EPERM;
		}
	}

	// real uid
	if (ruid == (uid_t)-1) {
		ruid = team->real_uid;
	} else {
		if (setAllIfPrivileged) {
			// setuid() semantics: If privileged set both, real, effective and
			// saved set-uid, otherwise set the effective uid.
			if (privileged) {
				team->saved_set_uid = ruid;
				team->real_uid = ruid;
				team->effective_uid = ruid;
				return B_OK;
			}

			// not privileged -- set only the effective uid
			euid = ruid;
			ruid = team->real_uid;
		} else {
			// setreuid() semantics: set the real uid, if allowed to
			// Note: We allow setting the real uid to the effective uid. This
			// is unspecified by the specs, but is common practice.
			if (!privileged && ruid != team->real_uid
				&& ruid != team->effective_uid) {
				return EPERM;
			}

			// Note: Also common practice is to set the saved set-uid when the
			// real uid is set.
			if (ruid != team->real_uid)
				ssuid = ruid;
		}
	}

	// effective uid
	if (euid == (uid_t)-1) {
		euid = team->effective_uid;
	} else {
		if (!privileged && euid != team->effective_uid
			&& euid != team->real_uid && euid != team->saved_set_uid) {
			return EPERM;
		}
	}

	// Getting here means all checks were successful -- set the uids.
	team->real_uid = ruid;
	team->effective_uid = euid;
	team->saved_set_uid = ssuid;

	return B_OK;
}


/**
 * @brief Shared implementation of getgroups() for kernel and userspace callers.
 *
 * Reads the team's supplementary-group list (or falls back to the effective
 * gid when the list is empty, per POSIX which mandates at least one entry)
 * and copies up to @p groupCount entries into @p groupList. When @p kernel
 * is false the destination is validated as a user pointer and the copy goes
 * through user_memcpy().
 *
 * @param groupCount Capacity of @p groupList; a value of 0 requests only the
 *                   current count without copying.
 * @param groupList  Destination buffer for the gids.
 * @param kernel     True when the caller is in the kernel and @p groupList
 *                   is a kernel pointer; false for a userspace destination.
 * @retval >=0               Number of groups written, or the current group
 *                           count when @p groupCount is 0.
 * @retval B_BAD_VALUE       @p groupList is too small for the list.
 * @retval B_BAD_ADDRESS     Userspace destination is not valid.
 */
static ssize_t
common_getgroups(int groupCount, gid_t* groupList, bool kernel)
{
	Team* team = thread_get_current_thread()->team;

	TeamLocker teamLocker(team);

	const gid_t* groups = NULL;
	int actualCount = 0;

	if (team->supplementary_groups != NULL) {
		groups = team->supplementary_groups->groups;
		actualCount = team->supplementary_groups->count;
	}

	// follow the specification and return always at least one group
	if (actualCount == 0) {
		groups = &team->effective_gid;
		actualCount = 1;
	}

	// if groupCount 0 is supplied, we only return the number of groups
	if (groupCount == 0)
		return actualCount;

	// check for sufficient space
	if (groupCount < actualCount)
		return B_BAD_VALUE;

	// copy
	if (kernel) {
		memcpy(groupList, groups, actualCount * sizeof(gid_t));
	} else {
		if (!IS_USER_ADDRESS(groupList)
			|| user_memcpy(groupList, groups,
					actualCount * sizeof(gid_t)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}

	return actualCount;
}


/**
 * @brief Shared implementation of setgroups() for kernel and userspace callers.
 *
 * Allocates a fresh BKernel::GroupsArray (reference counted) holding a copy
 * of the supplied gids and replaces the team's supplementary-group list with
 * it. The previous list is released after the team lock is dropped, so the
 * last reference cannot be dropped while the lock is held.
 *
 * @param groupCount Number of gids in @p groupList; 0 clears the list; must
 *                   not exceed NGROUPS_MAX.
 * @param groupList  Source array of gids.
 * @param kernel     True when @p groupList is a kernel pointer; false for a
 *                   userspace source copied via user_memcpy().
 * @retval B_OK          The list was replaced.
 * @retval B_BAD_VALUE   @p groupCount was negative or exceeded NGROUPS_MAX.
 * @retval B_NO_MEMORY   Allocation of the new list failed.
 * @retval B_BAD_ADDRESS Userspace source was not valid.
 */
static status_t
common_setgroups(int groupCount, const gid_t* groupList, bool kernel)
{
	if (groupCount < 0 || groupCount > NGROUPS_MAX)
		return B_BAD_VALUE;

	BKernel::GroupsArray* newGroups = NULL;
	if (groupCount > 0) {
		newGroups = (BKernel::GroupsArray*)malloc(sizeof(BKernel::GroupsArray)
			+ (sizeof(gid_t) * groupCount));
		if (newGroups == NULL)
			return B_NO_MEMORY;
		new(newGroups) BKernel::GroupsArray;

		if (kernel) {
			memcpy(newGroups->groups, groupList, sizeof(gid_t) * groupCount);
		} else {
			if (!IS_USER_ADDRESS(groupList)
				|| user_memcpy(newGroups->groups, groupList, sizeof(gid_t) * groupCount) != B_OK) {
				free(newGroups);
				return B_BAD_ADDRESS;
			}
		}
		newGroups->count = groupCount;
	}

	Team* team = thread_get_current_thread()->team;
	TeamLocker teamLocker(team);

	BReference<BKernel::GroupsArray> previous = team->supplementary_groups;
		// so it will not be (potentially) destroyed until after we unlock
	team->supplementary_groups.SetTo(newGroups, true);

	teamLocker.Unlock();

	return B_OK;
}


// #pragma mark - Kernel Private


/**
 * @brief Copy the full user/group identity block from @p parent to @p team.
 *
 * Used during team creation so that a child starts life with the same real,
 * effective, and saved uid/gid plus the same (reference-counted) supplementary
 * groups list as its parent. The caller must hold both team locks.
 *
 * @param team   Newly created team that receives the credentials.
 * @param parent Parent team whose credentials are copied.
 */
void
inherit_parent_user_and_group(Team* team, Team* parent)
{
	team->saved_set_uid = parent->saved_set_uid;
	team->real_uid = parent->real_uid;
	team->effective_uid = parent->effective_uid;
	team->saved_set_gid = parent->saved_set_gid;
	team->real_gid = parent->real_gid;
	team->effective_gid = parent->effective_gid;
	team->supplementary_groups = parent->supplementary_groups;
}


/**
 * @brief Apply set-user-id / set-group-id bits of an executable to @p team.
 *
 * Invoked during exec of @p file. If the file has the S_ISUID bit set, the
 * team's saved-set and effective uid are replaced with the file's owner uid;
 * S_ISGID likewise updates the saved-set and effective gid. The real uid/gid
 * are not modified. The supplementary-group list is unaffected.
 *
 * @param team Team executing the new image.
 * @param file Path of the executable being loaded.
 * @retval B_OK  The relevant ids were updated (or no set-id bits were set).
 * @retval other An error from vfs_read_stat() when the file cannot be stat'd.
 */
status_t
update_set_id_user_and_group(Team* team, const char* file)
{
	struct stat st;
	status_t status = vfs_read_stat(-1, file, true, &st, false);
	if (status != B_OK)
		return status;

	TeamLocker teamLocker(team);

	if ((st.st_mode & S_ISUID) != 0) {
		team->saved_set_uid = st.st_uid;
		team->effective_uid = st.st_uid;
	}

	if ((st.st_mode & S_ISGID) != 0) {
		team->saved_set_gid = st.st_gid;
		team->effective_gid = st.st_gid;
	}

	return B_OK;
}


/**
 * @brief Test whether @p team has @p gid as its effective or supplementary group.
 *
 * Checks the effective gid first and then iterates the team's supplementary
 * groups list, under the team lock. Used by the VFS permission checks.
 *
 * @param team Team to query.
 * @param gid  Group id to look for.
 * @return true if @p gid matches the team's effective gid or any entry in
 *         its supplementary-groups list.
 */
bool
is_in_group(Team* team, gid_t gid)
{
	TeamLocker teamLocker(team);

	if (team->effective_gid == gid)
		return true;

	if (team->supplementary_groups == NULL)
		return false;

	for (int i = 0; i < team->supplementary_groups->count; i++) {
		if (gid == team->supplementary_groups->groups[i])
			return true;
	}

	return false;
}


/**
 * @brief Kernel-internal setresgid(): update the current team's real,
 *        effective, and saved-set gids without privilege checks.
 *
 * The trailing @c true argument to common_setresgid() disables the
 * privilege check so the kernel itself can always set any gid.
 *
 * @param rgid               See common_setresgid().
 * @param egid               See common_setresgid().
 * @param ssgid              See common_setresgid().
 * @param setAllIfPrivileged See common_setresgid().
 * @return Status from common_setresgid().
 */
status_t
_kern_setresgid(gid_t rgid, gid_t egid, gid_t ssgid, bool setAllIfPrivileged)
{
	return common_setresgid(rgid, egid, ssgid, setAllIfPrivileged, true);
}


/**
 * @brief Kernel-internal setresuid(): update the current team's real,
 *        effective, and saved-set uids without privilege checks.
 *
 * @param ruid               See common_setresuid().
 * @param euid               See common_setresuid().
 * @param ssuid              See common_setresuid().
 * @param setAllIfPrivileged See common_setresuid().
 * @return Status from common_setresuid().
 */
status_t
_kern_setresuid(uid_t ruid, uid_t euid, uid_t ssuid, bool setAllIfPrivileged)
{
	return common_setresuid(ruid, euid, ssuid, setAllIfPrivileged, true);
}


/**
 * @brief Kernel-internal getresgid(): read the current team's real,
 *        effective, and saved-set gids.
 *
 * Each out-pointer may be NULL to indicate that the caller is not interested
 * in that particular value.
 *
 * @param rgid  Out-pointer for the real gid, or NULL.
 * @param egid  Out-pointer for the effective gid, or NULL.
 * @param ssgid Out-pointer for the saved-set gid, or NULL.
 * @return Always B_OK.
 */
status_t
_kern_getresgid(gid_t *rgid, gid_t *egid, gid_t *ssgid)
{
	Team* team = thread_get_current_thread()->team;
	if (rgid != NULL)
		*rgid = team->real_gid;
	if (egid != NULL)
		*egid = team->effective_gid;
	if (ssgid != NULL)
		*ssgid = team->saved_set_gid;
	return B_OK;
}


/**
 * @brief Kernel-internal getresuid(): read the current team's real,
 *        effective, and saved-set uids.
 *
 * @param ruid  Out-pointer for the real uid, or NULL.
 * @param euid  Out-pointer for the effective uid, or NULL.
 * @param ssuid Out-pointer for the saved-set uid, or NULL.
 * @return Always B_OK.
 */
status_t
_kern_getresuid(uid_t *ruid, uid_t *euid, gid_t *ssuid)
{
	Team* team = thread_get_current_thread()->team;
	if (ruid != NULL)
		*ruid = team->real_uid;
	if (euid != NULL)
		*euid = team->effective_uid;
	if (ssuid != NULL)
		*ssuid = team->saved_set_uid;
	return B_OK;
}


/**
 * @brief Kernel-internal getgroups(): read the supplementary-groups list.
 *
 * Thin wrapper over common_getgroups() that copies into a kernel buffer.
 *
 * @param groupCount Capacity of @p groupList; 0 returns only the count.
 * @param groupList  Kernel-space destination buffer.
 * @return See common_getgroups().
 */
ssize_t
_kern_getgroups(int groupCount, gid_t* groupList)
{
	return common_getgroups(groupCount, groupList, true);
}


/**
 * @brief Kernel-internal setgroups(): replace the supplementary-groups list
 *        from a kernel-space source buffer.
 *
 * The kernel bypass of the privilege check is implicit since the privilege
 * check lives in the userspace entry point below.
 *
 * @param groupCount Number of gids in @p groupList; must be in [0, NGROUPS_MAX].
 * @param groupList  Kernel-space source buffer.
 * @return See common_setgroups().
 */
status_t
_kern_setgroups(int groupCount, const gid_t* groupList)
{
	return common_setgroups(groupCount, groupList, true);
}


// #pragma mark - Syscalls


/**
 * @brief Syscall: setresgid()/setregid()/setgid() from user space.
 *
 * Forwards to common_setresgid() with the privilege-override flag cleared so
 * the normal POSIX permission checks are enforced.
 *
 * @param rgid               New real gid, or @c (gid_t)-1.
 * @param egid               New effective gid, or @c (gid_t)-1.
 * @param ssgid              New saved-set gid, or @c (gid_t)-1.
 * @param setAllIfPrivileged See common_setresgid().
 * @return See common_setresgid().
 */
status_t
_user_setresgid(gid_t rgid, gid_t egid, gid_t ssgid, bool setAllIfPrivileged)
{
	return common_setresgid(rgid, egid, ssgid, setAllIfPrivileged, false);
}


/**
 * @brief Syscall: setresuid()/setreuid()/setuid() from user space.
 *
 * The three arguments are the POSIX _real_, _effective_, and _saved_set_
 * uid respectively. Each may be @c (uid_t)-1 to leave the corresponding
 * team field unchanged.
 *
 * @param ruid               New real uid, or @c (uid_t)-1.
 * @param euid               New effective uid, or @c (uid_t)-1.
 * @param ssuid              New saved-set uid, or @c (uid_t)-1.
 * @param setAllIfPrivileged See common_setresuid().
 * @return See common_setresuid().
 */
status_t
_user_setresuid(uid_t ruid, uid_t euid, uid_t ssuid, bool setAllIfPrivileged)
{
	return common_setresuid(ruid, euid, ssuid, setAllIfPrivileged, false);
}


/**
 * @brief Syscall: getresgid() from user space.
 *
 * Copies the current team's real, effective, and saved-set gids into the
 * userspace destinations (each may be NULL to skip). Every non-NULL
 * destination is validated as a user address and written via user_memcpy().
 *
 * @param rgid  User pointer for the real gid, or NULL.
 * @param egid  User pointer for the effective gid, or NULL.
 * @param ssgid User pointer for the saved-set gid, or NULL.
 * @retval B_OK          All requested fields were written successfully.
 * @retval B_BAD_ADDRESS One of the user pointers was not valid.
 */
status_t
_user_getresgid(gid_t *rgid, gid_t *egid, gid_t *ssgid)
{
	Team* team = thread_get_current_thread()->team;
	if (rgid != NULL) {
		if (!IS_USER_ADDRESS(rgid)
			|| user_memcpy(rgid, &team->real_gid, sizeof(uid_t)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	if (egid != NULL) {
		if (!IS_USER_ADDRESS(egid)
			|| user_memcpy(egid, &team->effective_gid, sizeof(uid_t)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	if (ssgid != NULL) {
		if (!IS_USER_ADDRESS(ssgid)
			|| user_memcpy(ssgid, &team->saved_set_gid, sizeof(uid_t)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	return B_OK;
}


/**
 * @brief Syscall: getresuid() from user space.
 *
 * Counterpart of _user_getresgid() for user ids.
 *
 * @param ruid  User pointer for the real uid, or NULL.
 * @param euid  User pointer for the effective uid, or NULL.
 * @param ssuid User pointer for the saved-set uid, or NULL.
 * @retval B_OK          All requested fields were written successfully.
 * @retval B_BAD_ADDRESS One of the user pointers was not valid.
 */
status_t
_user_getresuid(uid_t *ruid, uid_t *euid, gid_t *ssuid)
{
	Team* team = thread_get_current_thread()->team;
	if (ruid != NULL) {
		if (!IS_USER_ADDRESS(ruid)
			|| user_memcpy(ruid, &team->real_uid, sizeof(uid_t)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	if (euid != NULL) {
		if (!IS_USER_ADDRESS(euid)
			|| user_memcpy(euid, &team->effective_uid, sizeof(uid_t)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	if (ssuid != NULL) {
		if (!IS_USER_ADDRESS(ssuid)
			|| user_memcpy(ssuid, &team->saved_set_uid, sizeof(uid_t)) != B_OK) {
			return B_BAD_ADDRESS;
		}
	}
	return B_OK;
}


/**
 * @brief Syscall: getgroups() from user space.
 *
 * Copies the current team's supplementary-groups list into @p groupList.
 * Always returns at least one entry (the effective gid) per POSIX.
 *
 * @param groupCount Capacity of @p groupList; 0 returns only the count.
 * @param groupList  Userspace destination buffer.
 * @return See common_getgroups().
 */
ssize_t
_user_getgroups(int groupCount, gid_t* groupList)
{
	return common_getgroups(groupCount, groupList, false);
}


/**
 * @brief Syscall: setgroups() from user space.
 *
 * Replaces the team's supplementary-groups list from a userspace array.
 * POSIX restricts this to privileged callers; if the caller is not
 * effectively root the call fails with EPERM before any data is read.
 *
 * @param groupCount Number of gids in @p groupList; must be in [0, NGROUPS_MAX].
 * @param groupList  Userspace source buffer.
 * @retval B_OK            The list was replaced.
 * @retval EPERM           Caller is not privileged.
 * @retval other           Errors propagated from common_setgroups().
 */
ssize_t
_user_setgroups(int groupCount, const gid_t* groupList)
{
	// check privilege
	Team* team = thread_get_current_thread()->team;
	if (!is_privileged(team))
		return EPERM;

	return common_setgroups(groupCount, groupList, false);
}
