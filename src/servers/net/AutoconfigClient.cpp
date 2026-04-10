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
/** @file AutoconfigClient.cpp
 *  @brief Base implementation of the network auto-configuration client. */
#include "AutoconfigClient.h"


/**
 * @brief Constructs an auto-configuration client for a given network device.
 *
 * Stores the target messenger and device name for use by subclasses
 * that implement specific auto-configuration protocols.
 *
 * @param name   Name for this BHandler.
 * @param target Messenger to receive configuration results.
 * @param device Name of the network device to configure.
 */
AutoconfigClient::AutoconfigClient(const char* name, BMessenger target,
		const char* device)
	: BHandler(name),
	fTarget(target),
	fDevice(device)
{
}


/** @brief Destroys the auto-configuration client. */
AutoconfigClient::~AutoconfigClient()
{
}


/**
 * @brief Starts the auto-configuration process.
 *
 * The default implementation returns B_NOT_SUPPORTED; subclasses should
 * override this to initiate their specific protocol (e.g., DHCP).
 *
 * @return B_NOT_SUPPORTED by default; subclasses return B_OK on success.
 */
status_t
AutoconfigClient::Start()
{
	return B_NOT_SUPPORTED;
}
