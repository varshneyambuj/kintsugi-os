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
 *   Copyright 2002, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file NodeManager.h
 *  @brief Central registry for live and dormant media nodes, add-ons, and flavors. */

#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H


#include <map>
#include <vector>

#include <Locker.h>

#include "TStack.h"
#include "DataExchange.h"


class DefaultManager;
class BufferManager;


typedef std::map<team_id, int32> TeamCountMap;
typedef std::vector<media_input> InputList;
typedef std::vector<media_output> OutputList;
typedef std::vector<live_node_info> LiveNodeList;


struct registered_node {
	media_node_id			node_id;           /**< Unique node identifier */
	media_node_id			timesource_id;     /**< Associated time source node */
	media_addon_id			add_on_id;         /**< Add-on that provides this node */
	int32					flavor_id;         /**< Flavor ID within the add-on */
	char					name[B_MEDIA_NAME_LENGTH]; /**< Human-readable node name */
	uint64					kinds;             /**< Node capability flags */
	port_id					port;              /**< Control port of the node */
	team_id					creator;           /**< Team that created the node */
	team_id					containing_team;   /**< Team containing the node */
	int32					ref_count;         /**< Total reference count */
	TeamCountMap			team_ref_count;    /**< Per-team reference counts */
	InputList				input_list;        /**< Published media inputs */
	OutputList				output_list;       /**< Published media outputs */
};

struct dormant_add_on_flavor_info {
	media_addon_id			add_on_id;              /**< Add-on identifier */
	int32					flavor_id;              /**< Flavor identifier */

	int32					max_instances_count;    /**< Maximum allowed instances */
	int32					instances_count;        /**< Current instance count */

	TeamCountMap		 	team_instances_count;   /**< Per-team instance counts */

	bool					info_valid;             /**< Whether info has been populated */
	dormant_flavor_info		info;                   /**< Cached dormant flavor info */
};


/** @brief Central registry for live media nodes, dormant flavors, and add-on paths. */
class NodeManager : BLocker {
public:
	/** @brief Construct the node manager and its default manager. */
								NodeManager();
	/** @brief Destroy the node manager and free the default manager. */
								~NodeManager();

	// Management of system wide default nodes
	/** @brief Set the system-wide default node for the given type. */
			status_t			SetDefaultNode(node_type type,
									const media_node* node,
									const dormant_node_info* info,
									const media_input* input);
	/** @brief Retrieve the system-wide default node for the given type. */
			status_t			GetDefaultNode(node_type type,
									media_node_id* _nodeID, char* inputName,
									int32* _inputID);
	/** @brief Trigger a rescan of default nodes after add-on loading. */
			status_t			RescanDefaultNodes();

	// Management of live nodes
	/** @brief Register a new live node and assign it a unique ID. */
			status_t			RegisterNode(media_addon_id addOnID,
									int32 flavorID, const char* name,
									uint64 kinds, port_id port, team_id team,
									media_node_id timesource,
									media_node_id* _nodeID);
	/** @brief Unregister a live node and return its add-on and flavor IDs. */
			status_t			UnregisterNode(media_node_id nodeID,
									team_id team, media_addon_id* addOnID,
									int32* _flavorID);
	/** @brief Release a single team reference to a node. */
			status_t			ReleaseNodeReference(media_node_id id,
									team_id team);
	/** @brief Release all references to a node. */
			status_t			ReleaseNodeAll(media_node_id id);
	/** @brief Get a clone of the node for the requesting team. */
			status_t			GetCloneForID(media_node_id id, team_id team,
									media_node* node);
	/** @brief Get a clone of the default node of the given type. */
			status_t			GetClone(node_type type, team_id team,
									media_node* node, char* inputName,
									int32* _id);
	/** @brief Release a node reference held by the given team. */
			status_t			ReleaseNode(const media_node& node,
									team_id team);
	/** @brief Store the published input list for a node. */
			status_t			PublishInputs(const media_node& node,
									const media_input* inputs, int32 count);
	/** @brief Store the published output list for a node. */
			status_t			PublishOutputs(const media_node& node,
									const media_output* outputs, int32 count);
	/** @brief Find the node ID owning the given port. */
			status_t			FindNodeID(port_id port, media_node_id* _id);
	/** @brief Retrieve live_node_info for a registered node. */
			status_t			GetLiveNodeInfo(const media_node& node,
									live_node_info* liveInfo);
	/** @brief Get node IDs of running instances for an add-on flavor. */
			status_t			GetInstances(media_addon_id addOnID,
									int32 flavorID, media_node_id* ids,
									int32* _count, int32 maxCount);
	/** @brief Query live nodes matching format, name, and kind criteria. */
			status_t			GetLiveNodes(LiveNodeList& liveNodes,
									int32 maxCount,
									const media_format* inputFormat = NULL,
									const media_format* outputFormat = NULL,
									const char* name = NULL,
									uint64 requireKinds = 0);
	/** @brief Get dormant node info for a live node. */
			status_t			GetDormantNodeInfo(const media_node& node,
									dormant_node_info* nodeInfo);
	/** @brief Set the creator team for a node. */
			status_t			SetNodeCreator(media_node_id id,
									team_id creator);

	/** @brief Add all live node IDs to a BMessage. */
			status_t			GetLiveNodes(BMessage* message);

	/** @brief Register a media add-on from an entry_ref and assign an ID. */
			void				RegisterAddOn(const entry_ref& ref,
									media_addon_id* _newID);
	/** @brief Unregister a media add-on by its ID. */
			void				UnregisterAddOn(media_addon_id id);

	/** @brief Add dormant flavor information for a registered add-on. */
			status_t			AddDormantFlavorInfo(
									const dormant_flavor_info& flavorInfo);
	/** @brief Mark all dormant flavor info for an add-on as invalid. */
			void				InvalidateDormantFlavorInfo(media_addon_id id);
	/** @brief Remove all dormant flavor info for an add-on. */
			void				RemoveDormantFlavorInfo(media_addon_id id);
	/** @brief Remove dormant flavors whose add-ons are no longer registered. */
			void				CleanupDormantFlavorInfos();

	/** @brief Increment the instance count for a dormant flavor. */
			status_t			IncrementFlavorInstancesCount(
									media_addon_id addOnID, int32 flavorID,
									team_id team);
	/** @brief Decrement the instance count for a dormant flavor. */
			status_t			DecrementFlavorInstancesCount(
									media_addon_id addOnID, int32 flavorID,
									team_id team);

	/** @brief Retrieve the entry_ref for a registered add-on. */
			status_t			GetAddOnRef(media_addon_id addOnID,
									entry_ref* ref);
	/** @brief Query dormant nodes matching format, name, and kind criteria. */
			status_t			GetDormantNodes(dormant_node_info* infos,
									int32* _count, const media_format* hasInput,
									const media_format* hasOutput,
									const char* name, uint64 requireKinds,
									uint64 denyKinds);

	/** @brief Retrieve the full dormant flavor info for a specific flavor. */
			status_t			GetDormantFlavorInfoFor(media_addon_id addOnID,
									 int32 flavorID,
									 dormant_flavor_info* flavorInfo);

	/** @brief Change the time source assignment for a node. */
			status_t			SetNodeTimeSource(media_node_id node,
									media_node_id timesource);

	/** @brief Release all node references held by the given team. */
			void				CleanupTeam(team_id team);

	/** @brief Load persisted default node state from disk. */
			status_t			LoadState();
	/** @brief Save current default node state to disk. */
			status_t			SaveState();

	/** @brief Dump all registered nodes to stdout. */
			void				Dump();

private:
			status_t			_AcquireNodeReference(media_node_id id,
									team_id team);
			void				_NotifyTimeSource(registered_node& node);

private:
			typedef std::map<media_addon_id, registered_node> NodeMap;
			typedef std::vector<dormant_add_on_flavor_info> DormantFlavorList;
			typedef std::map<media_addon_id, entry_ref> PathMap;

			media_addon_id		fNextAddOnID;
			media_node_id		fNextNodeID;

			DormantFlavorList	fDormantFlavors;
			PathMap				fPathMap;
			NodeMap				fNodeMap;
			DefaultManager*		fDefaultManager;
};

#endif	// NODE_MANAGER_H
