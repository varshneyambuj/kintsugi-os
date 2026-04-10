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
   Copyright 2005, Ingo Weinhold, bonefish@users.sf.net.
   Distributed under the terms of the MIT License.
 */

/** @file AppInfoListMessagingTargetSet.cpp
 *  @brief Adapts AppInfoList to the MessagingTargetSet interface for message delivery. */
#include <Application.h>
#include <TokenSpace.h>

#include "AppInfoListMessagingTargetSet.h"
#include "RosterAppInfo.h"

/**
 * @brief Constructs a messaging target set that iterates over an AppInfoList.
 *
 * Positions the internal iterator at the first non-filtered entry in the list.
 *
 * @param list           The application info list to iterate.
 * @param skipRegistrar  If @c true, the registrar's own team is excluded from
 *                       the target set.
 */
AppInfoListMessagingTargetSet::AppInfoListMessagingTargetSet(
		AppInfoList &list, bool skipRegistrar)
	: fList(list),
	  fIterator(list.It()),
	  fSkipRegistrar(skipRegistrar)
{
	_SkipFilteredOutInfos();
}

/** @brief Destroys the messaging target set. */
AppInfoListMessagingTargetSet::~AppInfoListMessagingTargetSet()
{
}

/**
 * @brief Returns whether there are more targets remaining in the set.
 *
 * @return @c true if the iterator points to a valid entry, @c false if
 *         all targets have been consumed.
 */
bool
AppInfoListMessagingTargetSet::HasNext() const
{
	return fIterator.IsValid();
}

/**
 * @brief Advances to the next target and returns its port and token.
 *
 * On success the current entry's port is written to @a port, the token is
 * set to B_PREFERRED_TOKEN, and the iterator advances past any filtered entries.
 *
 * @param port  Output parameter receiving the target's message port.
 * @param token Output parameter receiving the messaging token.
 * @return @c true if a target was returned, @c false if the set is exhausted.
 */
bool
AppInfoListMessagingTargetSet::Next(port_id &port, int32 &token)
{
	if (!fIterator.IsValid())
		return false;

	port = (*fIterator)->port;
	token = B_PREFERRED_TOKEN;

	++fIterator;
	_SkipFilteredOutInfos();

	return true;
}

/**
 * @brief Resets the iterator back to the beginning of the list.
 */
void
AppInfoListMessagingTargetSet::Rewind()
{
	fIterator = fList.It();
}

/**
 * @brief Determines whether the given app info should be included in the target set.
 *
 * When fSkipRegistrar is true, entries belonging to the registrar's own team
 * are excluded.
 *
 * @param info The application info entry to test.
 * @return @c true if the entry should be included, @c false to skip it.
 */
bool
AppInfoListMessagingTargetSet::Filter(const RosterAppInfo *info)
{
	if (!fSkipRegistrar)
		return true;

	return (!fSkipRegistrar || info->team != be_app->Team());
}

/**
 * @brief Advances the iterator past any entries rejected by Filter().
 */
void
AppInfoListMessagingTargetSet::_SkipFilteredOutInfos()
{
	while (fIterator.IsValid() && !Filter(*fIterator))
		++fIterator;
}

