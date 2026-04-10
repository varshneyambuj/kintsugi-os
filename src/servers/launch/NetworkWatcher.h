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

/** @file NetworkWatcher.h
 *  @brief Watcher converting kernel network availability changes into NetworkListener callbacks. */

#ifndef NETWORK_WATCHER_H
#define NETWORK_WATCHER_H


#include <Handler.h>
#include <ObjectList.h>


/** @brief Callback interface for NetworkWatcher subscribers. */
class NetworkListener {
public:
	virtual						~NetworkListener();

	/** @brief Invoked whenever overall network availability flips on or off. */
	virtual	void				NetworkAvailabilityChanged(bool available) = 0;
};


/** @brief BHandler that turns kernel network state changes into NetworkListener callbacks. */
class NetworkWatcher : public BHandler {
public:
								NetworkWatcher();
	virtual						~NetworkWatcher();

	/** @brief Adds @p listener to the notification set. */
			void				AddListener(NetworkListener* listener);
	/** @brief Removes @p listener from the notification set. */
			void				RemoveListener(NetworkListener* listener);
	/** @brief Returns the number of subscribers. */
			int32				CountListeners() const;

	/** @brief Dispatches kernel network change messages to listeners. */
	virtual	void				MessageReceived(BMessage* message);

	/** @brief Begins delivering network availability events to @p listener. */
	static	void				Register(NetworkListener* listener);
	/** @brief Stops delivering network availability events to @p listener. */
	static	void				Unregister(NetworkListener* listener);

	/** @brief Returns the current network availability.
	 *  @param immediate If true, queries the kernel directly instead of returning the cached value. */
	static	bool				NetworkAvailable(bool immediate);

protected:
	/** @brief Recomputes @c fAvailable and notifies listeners if it changed. */
			void				UpdateAvailability();

protected:
			BObjectList<NetworkListener>
								fListeners;   /**< Subscribed listeners (not owned). */
			bool				fAvailable;   /**< Cached network-available state. */
};


#endif // NETWORK_WATCHER_H
