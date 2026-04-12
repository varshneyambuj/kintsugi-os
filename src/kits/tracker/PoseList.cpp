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
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file PoseList.cpp
 * @brief Ordered list of BPose pointers with node-ref and entry-ref lookup.
 *
 * PoseList extends BObjectList<BPose> with helper methods to locate poses by
 * node_ref, entry_ref, Model pointer, or filename.  DeepFindPose also follows
 * symlinks so callers can find poses by the target node.
 *
 * @see BPose, BPoseView
 */


#include <Debug.h>

#include "PoseList.h"


/**
 * @brief Find the pose whose target model has the given node_ref.
 *
 * @param node            The node_ref to search for.
 * @param resultingIndex  Optional output receiving the list index on match.
 * @return Pointer to the matching BPose, or NULL if not found.
 */
BPose*
PoseList::FindPose(const node_ref* node, int32* resultingIndex) const
{
	int32 count = CountItems();
	for (int32 index = 0; index < count; index++) {
		BPose* pose = ItemAt(index);
		ASSERT(pose->TargetModel());
		if (*pose->TargetModel()->NodeRef() == *node) {
			if (resultingIndex != NULL)
				*resultingIndex = index;

			return pose;
		}
	}

	return NULL;
}


/**
 * @brief Find the pose whose target model matches the given entry_ref.
 *
 * @param entry           The entry_ref to search for.
 * @param resultingIndex  Optional output receiving the list index on match.
 * @return Pointer to the matching BPose, or NULL if not found.
 */
BPose*
PoseList::FindPose(const entry_ref* entry, int32* resultingIndex) const
{
	int32 count = CountItems();
	for (int32 index = 0; index < count; index++) {
		BPose* pose = ItemAt(index);
		ASSERT(pose->TargetModel());
		if (*pose->TargetModel()->EntryRef() == *entry) {
			if (resultingIndex != NULL)
				*resultingIndex = index;

			return pose;
		}
	}
	return NULL;
}


/**
 * @brief Find the pose whose target model matches @p model by node_ref.
 *
 * @param model           The Model to search for.
 * @param resultingIndex  Optional output receiving the list index on match.
 * @return Pointer to the matching BPose, or NULL if not found.
 */
BPose*
PoseList::FindPose(const Model* model, int32* resultingIndex) const
{
	return FindPose(model->NodeRef(), resultingIndex);
}


/**
 * @brief Find a pose by node_ref, also checking through symlinks.
 *
 * Like FindPose() but additionally tests symlink poses by comparing @p node
 * against the link target's node_ref.
 *
 * @param node            The node_ref to search for.
 * @param resultingIndex  Optional output receiving the list index on match.
 * @return Pointer to the matching BPose, or NULL if not found.
 */
BPose*
PoseList::DeepFindPose(const node_ref* node, int32* resultingIndex) const
{
	int32 count = CountItems();
	for (int32 index = 0; index < count; index++) {
		BPose* pose = ItemAt(index);
		Model* model = pose->TargetModel();
		if (*model->NodeRef() == *node) {
			if (resultingIndex != NULL)
				*resultingIndex = index;

			return pose;
		}
		// if model is a symlink, try matching node with the target
		// of the link
		if (model->IsSymLink()) {
			model = model->LinkTo();
			if (model != NULL && *model->NodeRef() == *node) {
				if (resultingIndex != NULL)
					*resultingIndex = index;

				return pose;
			}
		}
	}

	return NULL;
}


/**
 * @brief Collect all poses that match @p node, including through symlinks.
 *
 * Returns a heap-allocated PoseList containing every pose whose model node_ref
 * or symlink-target node_ref equals @p node.  The caller is responsible for
 * deleting the returned list.
 *
 * @param node  The node_ref to search for.
 * @return A new PoseList (never NULL) containing all matching poses.
 */
PoseList*
PoseList::FindAllPoses(const node_ref* node) const
{
	int32 count = CountItems();
	PoseList *result = new PoseList(5);
	for (int32 index = 0; index < count; index++) {
		BPose *pose = ItemAt(index);
		Model *model = pose->TargetModel();
		if (*model->NodeRef() == *node) {
			result->AddItem(pose, 0);
			continue;
		}

		if (!model->IsSymLink())
			continue;

		model = model->LinkTo();
		if (model != NULL && *model->NodeRef() == *node) {
			result->AddItem(pose);
			continue;
		}

		if (model == NULL) {
			Model model(pose->TargetModel()->EntryRef(), true);
			if (*model.NodeRef() == *node)
				result->AddItem(pose);
		}
	}

	return result;
}


/**
 * @brief Find a pose by the file name of its target model entry.
 *
 * @param name    The entry name to search for (exact, case-sensitive).
 * @param _index  Optional output receiving the list index on match.
 * @return Pointer to the matching BPose, or NULL if not found.
 */
BPose*
PoseList::FindPoseByFileName(const char* name, int32* _index) const
{
	int32 count = CountItems();
	for (int32 index = 0; index < count; index++) {
		BPose* pose = ItemAt(index);
		ASSERT(pose->TargetModel());
		if (strcmp(pose->TargetModel()->EntryRef()->name, name) == 0) {
			if (_index != NULL)
				*_index = index;

			return pose;
		}
	}

	return NULL;
}
