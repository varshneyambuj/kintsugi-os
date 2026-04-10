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
 *   Copyright 2010, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Marcus Overhagen
 *       Jérôme Duval
 *
 *   Copyright 2002, Marcus Overhagen. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/** @file DefaultManager.h
 *  @brief Manages default audio/video input and output nodes and their connections. */

#ifndef _DEFAULT_MANAGER_H
#define _DEFAULT_MANAGER_H


#include "DataExchange.h"

#include <Autolock.h>
#include <MediaRoster.h>
#include <Message.h>

class NodeManager;


/** @brief Manages default audio and video input/output node assignments. */
class DefaultManager {
public:
	/** @brief Construct the default manager and initialize default node IDs. */
								DefaultManager();
	/** @brief Destroy the default manager. */
								~DefaultManager();

	/** @brief Load saved default node assignments from persistent storage. */
			status_t 			LoadState();
	/** @brief Save current default node assignments to persistent storage. */
			status_t			SaveState(NodeManager *node_manager);

	/** @brief Set the default node for the given type. */
			status_t			Set(media_node_id nodeid,
									const char *input_name, int32 input_id,
									node_type type);
	/** @brief Get the default node ID for the given type. */
			status_t			Get(media_node_id *nodeid, char *input_name,
									int32 *input_id, node_type type);
	/** @brief Trigger a background rescan of default physical nodes. */
			status_t			Rescan();

	/** @brief Dump current default node state to stdout. */
			void				Dump();

	/** @brief Clean up resources associated with the given team. */
			void 				CleanupTeam(team_id team);

private:
			static int32		rescan_thread(void *arg);
			void				_RescanThread();

			void				_FindPhysical(volatile media_node_id *id,
									uint32 default_type, bool isInput,
									media_type type);
			void				_FindAudioMixer();
			void				_FindTimeSource();

			status_t			_ConnectMixerToOutput();

private:
			volatile bool 		fMixerConnected;
			volatile media_node_id fPhysicalVideoOut;
			volatile media_node_id fPhysicalVideoIn;
			volatile media_node_id fPhysicalAudioOut;
			volatile media_node_id fPhysicalAudioIn;
			volatile media_node_id fSystemTimeSource;
			volatile media_node_id fTimeSource;
			volatile media_node_id fAudioMixer;
			volatile int32 		fPhysicalAudioOutInputID;
			char fPhysicalAudioOutInputName[B_MEDIA_NAME_LENGTH];

			BList				fMsgList;

			uint32				fBeginHeader[3];
			uint32				fEndHeader[3];
			thread_id			fRescanThread;
			int32 				fRescanRequested;
			BLocker				fRescanLock;
			BMediaRoster*		fRoster;
};

#endif // _DEFAULT_MANAGER_H
