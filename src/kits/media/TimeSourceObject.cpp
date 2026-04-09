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
 *   Copyright 2002 Marcus Overhagen. All Rights Reserved.
 *   This file may be used under the terms of the MIT License.
 */

/** @file TimeSourceObject.cpp
 *  @brief Shadow proxy object returned by BMediaRoster::MakeTimeSourceFor(). */


/*!	The object returned by BMediaRoster's
	MakeTimeSourceFor(const media_node& forNode);
*/


#include "TimeSourceObject.h"

#include <stdio.h>
#include <string.h>

#include <MediaRoster.h>
#include <OS.h>

#include <MediaMisc.h>
#include <MediaDebug.h>

#include "TimeSourceObjectManager.h"


/** @brief Constructs a shadow proxy for the given time source node.
 *
 *  Reuses the control port of the real time source so that all messages are
 *  forwarded directly to it.  Sets fIsRealtime for the system clock node.
 *
 *  @param node  The media_node describing the real time source. */
TimeSourceObject::TimeSourceObject(const media_node& node)
	:
	BMediaNode("some timesource object", node.node, node.kind),
	BTimeSource(node.node)
{
	TRACE("TimeSourceObject::TimeSourceObject enter, id = %"
		B_PRId32 "\n", node.node);

	if (fControlPort > 0)
		delete_port(fControlPort);

	// We use the control port of the real time source object.
	// this way, all messages are send to the real time source,
	// and this shadow object won't receive any.
	fControlPort = node.port;

	ASSERT(fNodeID == node.node);
	ASSERT(fKinds == node.kind);

	if (node.node == NODE_SYSTEM_TIMESOURCE_ID) {
		strcpy(fName, "System clock");
		fIsRealtime = true;
	} else {
		live_node_info liveNodeInfo;
		if (BMediaRoster::Roster()->GetLiveNodeInfo(node, &liveNodeInfo)
				== B_OK)
			strlcpy(fName, liveNodeInfo.name, B_MEDIA_NAME_LENGTH);
		else {
			snprintf(fName, B_MEDIA_NAME_LENGTH, "timesource %" B_PRId32,
				node.node);
		}
	}

	AddNodeKind(NODE_KIND_SHADOW_TIMESOURCE);
	AddNodeKind(NODE_KIND_NO_REFCOUNTING);

	TRACE("TimeSourceObject::TimeSourceObject leave, node id %" B_PRId32 "\n",
		fNodeID);
}


/** @brief No-op implementation of the TimeSourceOp hook; shadow objects do not
 *         handle time source operations directly.
 *  @param op         The time source operation descriptor.
 *  @param _reserved  Reserved; must be \c NULL.
 *  @return B_OK always. */
status_t
TimeSourceObject::TimeSourceOp(const time_source_op_info& op, void* _reserved)
{
	// we don't get anything here
	return B_OK;
}


/** @brief Returns \c NULL because shadow TimeSourceObjects are not add-on nodes.
 *  @param _id  If non-NULL, set to 0.
 *  @return \c NULL always. */
BMediaAddOn*
TimeSourceObject::AddOn(int32* _id) const
{
	if (_id != NULL)
		*_id = 0;

	return NULL;
}


/** @brief Notifies the TimeSourceObjectManager that this proxy is being deleted,
 *         then delegates to BTimeSource::DeleteHook().
 *  @param node  The BMediaNode being deleted (same as \c this).
 *  @return B_OK on success, or an error code from BTimeSource::DeleteHook(). */
status_t
TimeSourceObject::DeleteHook(BMediaNode* node)
{
//	if (fIsRealtime) {
//		ERROR("TimeSourceObject::DeleteHook: system time source clone delete hook called\n");
//		return B_ERROR;
//	}
	PRINT(1, "TimeSourceObject::DeleteHook enter\n");
	gTimeSourceObjectManager->ObjectDeleted(this);
	status_t status = BTimeSource::DeleteHook(node);
	PRINT(1, "TimeSourceObject::DeleteHook leave\n");
	return status;
}
