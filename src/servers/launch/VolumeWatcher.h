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
 *   Copyright 2015, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file VolumeWatcher.h
 *  @brief Watcher converting kernel mount/unmount events into VolumeListener callbacks. */

#ifndef VOLUME_WATCHER_H
#define VOLUME_WATCHER_H


#include <Handler.h>
#include <ObjectList.h>


/** @brief Callback interface for VolumeWatcher subscribers. */
class VolumeListener {
public:
	virtual						~VolumeListener();

	/** @brief Invoked when a new volume is mounted. */
	virtual	void				VolumeMounted(dev_t device) = 0;
	/** @brief Invoked when a volume is unmounted. */
	virtual	void				VolumeUnmounted(dev_t device) = 0;
};


/** @brief BHandler that turns kernel mount/unmount notifications into VolumeListener callbacks. */
class VolumeWatcher : public BHandler {
public:
								VolumeWatcher();
	virtual						~VolumeWatcher();

	/** @brief Adds @p listener to the notification set. */
			void				AddListener(VolumeListener* listener);
	/** @brief Removes @p listener from the notification set. */
			void				RemoveListener(VolumeListener* listener);
	/** @brief Returns the number of subscribers. */
			int32				CountListeners() const;

	/** @brief Dispatches kernel mount/unmount messages to listeners. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Begins delivering volume events to @p listener. */
	static	void				Register(VolumeListener* listener);
	/** @brief Stops delivering volume events to @p listener. */
	static	void				Unregister(VolumeListener* listener);

protected:
			BObjectList<VolumeListener>
								fListeners;  /**< Subscribed listeners (not owned). */
};


#endif // VOLUME_WATCHER_H
