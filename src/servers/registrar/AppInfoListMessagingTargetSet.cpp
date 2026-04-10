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

// constructor
AppInfoListMessagingTargetSet::AppInfoListMessagingTargetSet(
		AppInfoList &list, bool skipRegistrar)
	: fList(list),
	  fIterator(list.It()),
	  fSkipRegistrar(skipRegistrar)
{
	_SkipFilteredOutInfos();
}

// destructor
AppInfoListMessagingTargetSet::~AppInfoListMessagingTargetSet()
{
}

// HasNext
bool
AppInfoListMessagingTargetSet::HasNext() const
{
	return fIterator.IsValid();
}

// Next
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

// Rewind
void
AppInfoListMessagingTargetSet::Rewind()
{
	fIterator = fList.It();
}

// Filter
bool
AppInfoListMessagingTargetSet::Filter(const RosterAppInfo *info)
{
	if (!fSkipRegistrar)
		return true;

	return (!fSkipRegistrar || info->team != be_app->Team());
}

// _SkipFilteredOutInfos
void
AppInfoListMessagingTargetSet::_SkipFilteredOutInfos()
{
	while (fIterator.IsValid() && !Filter(*fIterator))
		++fIterator;
}

