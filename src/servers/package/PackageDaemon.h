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
   /*
    * Copyright 2013, Haiku, Inc. All Rights Reserved.
    * Distributed under the terms of the MIT License.
    *
    * Authors:
    *		Ingo Weinhold <ingo_weinhold@gmx.de>
    */
 */

/** @file PackageDaemon.h
 *  @brief Main daemon server that monitors volumes and manages package roots */

#ifndef PACKAGE_DAEMON_H
#define PACKAGE_DAEMON_H


#include <fs_info.h>
#include <Node.h>
#include <ObjectList.h>
#include <VolumeRoster.h>

#include <Server.h>


class Root;
class Volume;


/** @brief BServer subclass that watches volume mounts and dispatches work to Root objects */
class PackageDaemon : public BServer {
public:
	/** @brief Construct the daemon and report initialization status */
								PackageDaemon(status_t* _error);
	/** @brief Destructor */
	virtual						~PackageDaemon();

	/** @brief Register existing volumes and start the volume roster watcher */
			status_t			Init();

	/** @brief Handle incoming messages including volume mount/unmount and commit requests */
	virtual	void				MessageReceived(BMessage* message);

private:
			typedef BObjectList<Root, true> RootList;

private:
			status_t			_RegisterVolume(dev_t device);
			void				_UnregisterVolume(Volume* volume);

			status_t			_GetOrCreateRoot(const node_ref& nodeRef,
									Root*& _root);
			Root*				_FindRoot(const node_ref& nodeRef) const;
			void				_PutRoot(Root* root);

			Volume*				_FindVolume(dev_t deviceID) const;

			void				_HandleVolumeMounted(const BMessage* message);
			void				_HandleVolumeUnmounted(const BMessage* message);

private:
			Root*				fSystemRoot;    /**< The primary system root */
			RootList			fRoots;         /**< All known package roots */
			BVolumeRoster		fVolumeWatcher; /**< Watches for volume mount/unmount events */
};



#endif	// PACKAGE_DAEMON_H
