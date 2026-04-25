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
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file TargetHost.cpp
 * @brief Implementation of TargetHost, the per-host enumeration of teams
 *        the debugger can attach to.
 *
 * TargetHost owns a sorted-by-team-id list of TeamInfo records and
 * dispatches add/remove/rename notifications to subscribed Listener
 * instances. The "host" abstraction lets the debugger UI list teams
 * either on the local machine or on a remote target.
 */

#include "TargetHost.h"

#include <AutoLocker.h>

#include "TeamInfo.h"


/**
 * @brief Constructs a TargetHost named @a name with no teams.
 *
 * @param name Display name of the host (e.g. "localhost").
 */
TargetHost::TargetHost(const BString& name)
	:
	BReferenceable(),
	fName(name),
	fLock(),
	fListeners(),
	fTeams()
{
}


/**
 * @brief Deletes every owned TeamInfo, draining the list.
 */
TargetHost::~TargetHost()
{
	while (!fTeams.IsEmpty())
		delete fTeams.RemoveItemAt((int32)0);
}


/**
 * @brief Subscribes @a listener for team add/remove/rename notifications.
 *
 * @param listener Listener to register; caller retains ownership.
 */
void
TargetHost::AddListener(Listener* listener)
{
	AutoLocker<TargetHost> hostLocker(this);
	fListeners.Add(listener);
}


/**
 * @brief Unsubscribes a previously registered listener.
 *
 * @param listener Listener previously passed to @c AddListener().
 */
void
TargetHost::RemoveListener(Listener* listener)
{
	AutoLocker<TargetHost> hostLocker(this);
	fListeners.Remove(listener);
}


/**
 * @brief Returns the number of teams currently known on this host.
 *
 * @return Team count.
 */
int32
TargetHost::CountTeams() const
{
	return fTeams.CountItems();
}


/**
 * @brief Inserts a new TeamInfo derived from @a info into the sorted list.
 *
 * @param info Kernel team_info describing the new team.
 * @return    @c B_OK on success, @c B_NO_MEMORY on allocation/insert failure.
 */
status_t
TargetHost::AddTeam(const team_info& info)
{
	TeamInfo* teamInfo = new (std::nothrow) TeamInfo(info.team, info);
	if (teamInfo == NULL)
		return B_NO_MEMORY;

	if (!fTeams.BinaryInsert(teamInfo, &_CompareTeams))
		return B_NO_MEMORY;

	_NotifyTeamAdded(teamInfo);
	return B_OK;
}


/**
 * @brief Removes the TeamInfo for @a team and notifies listeners.
 *
 * Silently does nothing if no matching team is present.
 *
 * @param team Team identifier to remove.
 */
void
TargetHost::RemoveTeam(team_id team)
{
	int32 index = fTeams.BinarySearchIndexByKey(team,
		&_FindTeamByKey);
	if (index < 0)
		return;

	_NotifyTeamRemoved(team);
	TeamInfo* info = fTeams.RemoveItemAt(index);
	delete info;
}


/**
 * @brief Updates the cached team_info for an existing team and notifies listeners.
 *
 * Silently does nothing if no matching team is present.
 *
 * @param info Replacement team_info; @c info.team identifies which team to update.
 */
void
TargetHost::UpdateTeam(const team_info& info)
{
	int32 index = fTeams.BinarySearchIndexByKey(info.team,
		&_FindTeamByKey);
	if (index < 0)
		return;

	TeamInfo* teamInfo = fTeams.ItemAt(index);
	teamInfo->SetTo(info.team, info);
	_NotifyTeamRenamed(teamInfo);
}


/**
 * @brief Returns the TeamInfo at @a index, or NULL if out of range.
 *
 * @param index Zero-based index into the sorted team list.
 * @return     Pointer to the TeamInfo, or NULL.
 */
TeamInfo*
TargetHost::TeamInfoAt(int32 index) const
{
	return fTeams.ItemAt(index);
}


/**
 * @brief Looks up a team by id via binary search.
 *
 * @param team Team identifier to look up.
 * @return    Pointer to the matching TeamInfo, or NULL if not present.
 */
TeamInfo*
TargetHost::TeamInfoByID(team_id team) const
{
	return fTeams.BinarySearchByKey(team, &_FindTeamByKey);
}


/**
 * @brief Comparator ordering two TeamInfo entries by team id.
 *
 * @param a First TeamInfo.
 * @param b Second TeamInfo.
 * @return -1 if @a a precedes @a b, 1 otherwise.
 */
/*static*/ int
TargetHost::_CompareTeams(const TeamInfo* a, const TeamInfo* b)
{
	return a->TeamID() < b->TeamID() ? -1 : 1;
}


/**
 * @brief Comparator locating a TeamInfo by team id (search-key form).
 *
 * @param id   Team id being searched for.
 * @param info Candidate TeamInfo.
 * @return    -1, 0, or 1 in the standard search-key ordering.
 */
/*static*/ int
TargetHost::_FindTeamByKey(const team_id* id, const TeamInfo* info)
{
	if (*id < info->TeamID())
		return -1;
	else if (*id > info->TeamID())
		return 1;
	return 0;
}


/**
 * @brief Dispatches the team-added event to every subscribed listener.
 *
 * @param info Newly-added team's info.
 */
void
TargetHost::_NotifyTeamAdded(TeamInfo* info)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->TeamAdded(info);
	}
}


/**
 * @brief Dispatches the team-removed event to every subscribed listener.
 *
 * @param team Identifier of the removed team.
 */
void
TargetHost::_NotifyTeamRemoved(team_id team)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->TeamRemoved(team);
	}
}


/**
 * @brief Dispatches the team-renamed event to every subscribed listener.
 *
 * @param info Updated team info (carries the new arguments/name).
 */
void
TargetHost::_NotifyTeamRenamed(TeamInfo* info)
{
	for (ListenerList::Iterator it = fListeners.GetIterator();
			Listener* listener = it.Next();) {
		listener->TeamRenamed(info);
	}
}


// #pragma mark - TargetHost::Listener


/**
 * @brief Virtual destructor anchor for the Listener interface.
 */
TargetHost::Listener::~Listener()
{
}


/**
 * @brief Default no-op implementation of the team-added callback.
 *
 * @param info Newly-added team info (unused in default implementation).
 */
void
TargetHost::Listener::TeamAdded(TeamInfo* info)
{
}


/**
 * @brief Default no-op implementation of the team-removed callback.
 *
 * @param team Removed team id (unused in default implementation).
 */
void
TargetHost::Listener::TeamRemoved(team_id team)
{
}


/**
 * @brief Default no-op implementation of the team-renamed callback.
 *
 * @param info Updated team info (unused in default implementation).
 */
void
TargetHost::Listener::TeamRenamed(TeamInfo* info)
{
}
