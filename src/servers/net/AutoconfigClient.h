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
   Copyright 2008, Haiku, Inc. All Rights Reserved.
   Distributed under the terms of the MIT License.
   
   Authors:
   		Axel Dörfler, axeld@pinc-software.de
 */
/** @file AutoconfigClient.h
 *  @brief Abstract base class for network auto-configuration clients. */
#ifndef AUTOCONFIG_CLIENT_H
#define AUTOCONFIG_CLIENT_H


#include <Handler.h>
#include <Messenger.h>
#include <String.h>


/** @brief Abstract base for network auto-configuration clients. */
class AutoconfigClient : public BHandler {
public:
								/** @brief Construct with a name, messenger target, and network device. */
								AutoconfigClient(const char* name,
									BMessenger target, const char* device);
	/** @brief Destructor. */
	virtual						~AutoconfigClient();

	/** @brief Begin the auto-configuration process. */
	virtual	status_t			Start();

			/** @brief Return the messenger target for configuration messages. */
			const BMessenger&	Target() const { return fTarget; }
			/** @brief Return the network device name. */
			const char*			Device() const { return fDevice.String(); }

private:
			BMessenger			fTarget; /**< Target messenger for configuration results */
			BString				fDevice; /**< Network device name */
};

#endif	// AUTOCONFIG_CLIENT_H
