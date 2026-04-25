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
 *   Copyright 2016, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file LocalTargetHostInterfaceInfo.cpp
 * @brief Descriptor implementation for the on-machine debugger transport.
 *
 * Advertises a transport that talks directly to the kernel debug nub on the
 * same host and constructs LocalTargetHostInterface objects on demand.
 */


#include "LocalTargetHostInterfaceInfo.h"

#include "LocalTargetHostInterface.h"


/**
 * @brief Constructs the descriptor with the user-visible name "Local".
 */
LocalTargetHostInterfaceInfo::LocalTargetHostInterfaceInfo()
	:
	TargetHostInterfaceInfo("Local")
{
}


/**
 * @brief Destructor; no owned resources to release.
 */
LocalTargetHostInterfaceInfo::~LocalTargetHostInterfaceInfo()
{
}


/**
 * @brief One-time initialization for the descriptor.
 *
 * @return Always B_OK; the local transport needs no setup.
 */
status_t
LocalTargetHostInterfaceInfo::Init()
{
	return B_OK;
}


/**
 * @brief Identifies this descriptor as describing a local-host transport.
 *
 * @return Always true.
 */
bool
LocalTargetHostInterfaceInfo::IsLocal() const
{
	return true;
}


/**
 * @brief Reports whether the supplied settings produce a usable interface.
 *
 * @param settings  Ignored; the local transport is always configured.
 * @return Always true.
 */
bool
LocalTargetHostInterfaceInfo::IsConfigured(Settings* settings) const
{
	return true;
}


/**
 * @brief Returns the schema for user-editable settings.
 *
 * @return Always @c NULL since the local transport requires no configuration.
 */
SettingsDescription*
LocalTargetHostInterfaceInfo::GetSettingsDescription() const
{
	// the local interface requires no configuration, therefore
	// it returns no settings description, and has no real work
	// to do as far as settings validation is concerned.
	return NULL;
}


/**
 * @brief Builds a new LocalTargetHostInterface from the descriptor.
 *
 * @param settings    Settings forwarded to the interface's Init() (unused here).
 * @param _interface  On success, set to the freshly-initialized interface;
 *                    ownership transfers to the caller.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
 *         propagated from LocalTargetHostInterface::Init().
 */
status_t
LocalTargetHostInterfaceInfo::CreateInterface(Settings* settings,
	TargetHostInterface*& _interface) const
{
	LocalTargetHostInterface* interface
		= new(std::nothrow) LocalTargetHostInterface;
	if (interface == NULL)
		return B_NO_MEMORY;

	status_t error = interface->Init(settings);
	if (error != B_OK) {
		delete interface;
		return error;
	}

	_interface = interface;
	return B_OK;
}

