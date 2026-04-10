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
   Copyright 2006-2008, Haiku, Inc. All Rights Reserved.
   Distributed under the terms of the MIT License.
   
   Authors:
   		Axel Dörfler, axeld@pinc-software.de
 */
/** @file AutoconfigLooper.h
 *  @brief Looper that drives network interface auto-configuration. */
#ifndef AUTOCONFIG_LOOPER_H
#define AUTOCONFIG_LOOPER_H


#include <Looper.h>
#include <Messenger.h>
#include <String.h>
#include <netinet6/in6.h>

class AutoconfigClient;

/** @brief Looper driving DHCP and link-change auto-configuration. */
class AutoconfigLooper : public BLooper {
public:
								/** @brief Construct for a target messenger and network device. */
								AutoconfigLooper(BMessenger target,
									const char* device);
	/** @brief Destructor; removes the current client. */
	virtual						~AutoconfigLooper();

	/** @brief Handle ready-to-run, failure, and network monitor messages. */
	virtual	void				MessageReceived(BMessage* message);

			/** @brief Return the configuration target messenger. */
			BMessenger			Target() const { return fTarget; }

private:
			void				_RemoveClient();
			void				_ConfigureIPv4();
			void				_ConfigureIPv4Failed();
			void				_ReadyToRun();
			void				_NetworkMonitorNotification(BMessage* message);

			BMessenger			fTarget; /**< Target messenger for configuration results */
			BString				fDevice; /**< Network device name */
			AutoconfigClient*	fCurrentClient; /**< Active auto-configuration client */
			int32				fLastMediaStatus; /**< Last known media link status flags */
			bool				fJoiningNetwork; /**< True if currently joining a wireless network */
};

#endif	// AUTOCONFIG_LOOPER_H
