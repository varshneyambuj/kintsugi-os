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

   Implements the MessagingTargetSet interface for AppInfoLists, so that
   no other representation (array/list) is needed to feed them into the
   MessageDeliverer.
 */

/** @file AppInfoListMessagingTargetSet.h
 *  @brief Adapts an AppInfoList to the MessagingTargetSet interface for bulk message delivery. */
#ifndef APP_INFO_LIST_MESSAGING_TARGET_SET_H
#define APP_INFO_LIST_MESSAGING_TARGET_SET_H

#include "AppInfoList.h"
#include "MessageDeliverer.h"

struct RosterAppInfo;

/** @brief Iterates over an AppInfoList producing messaging targets for the MessageDeliverer. */
class AppInfoListMessagingTargetSet : public MessagingTargetSet {
public:
	AppInfoListMessagingTargetSet(AppInfoList &list,
		bool skipRegistrar = true);
	virtual ~AppInfoListMessagingTargetSet();

	/** @brief Returns whether there are more targets to iterate. */
	virtual bool HasNext() const;
	/** @brief Advances to the next target and returns its port and token. */
	virtual bool Next(port_id &port, int32 &token);
	/** @brief Resets the iterator to the beginning of the target set. */
	virtual void Rewind();

	/** @brief Hook to exclude specific RosterAppInfo entries from the target set. */
	virtual bool Filter(const RosterAppInfo *info);

private:
	void _SkipFilteredOutInfos();

	AppInfoList				&fList;
	AppInfoList::Iterator	fIterator;
	bool					fSkipRegistrar;
};

#endif	// APP_INFO_LIST_MESSAGING_TARGET_SET_H
