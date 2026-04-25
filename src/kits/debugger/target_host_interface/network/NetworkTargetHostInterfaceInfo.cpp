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
 *   Copyright 2016-2017, Rene Gollent, rene@gollent.com.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file NetworkTargetHostInterfaceInfo.cpp
 * @brief Descriptor implementation for the network-attached debugger transport.
 *
 * Publishes a settings schema (hostname + port) and constructs
 * NetworkTargetHostInterface instances configured from that schema.
 */


#include "NetworkTargetHostInterfaceInfo.h"

#include <AutoDeleter.h>

#include "NetworkTargetHostInterface.h"
#include "SettingsDescription.h"
#include "Settings.h"
#include "Setting.h"


/** @brief Settings key used to identify the remote host name. */
static const char* kHostnameSetting = "hostname";
/** @brief Settings key used to identify the remote TCP port. */
static const char* kPortSetting = "port";


/**
 * @brief Constructs the descriptor with the user-visible name "Network".
 */
NetworkTargetHostInterfaceInfo::NetworkTargetHostInterfaceInfo()
	:
	TargetHostInterfaceInfo("Network"),
	fDescription(NULL)
{
}


/**
 * @brief Releases the cached settings description if present.
 */
NetworkTargetHostInterfaceInfo::~NetworkTargetHostInterfaceInfo()
{
	if (fDescription != NULL)
		fDescription->ReleaseReference();
}


/**
 * @brief Builds the settings schema describing hostname and port.
 *
 * @return B_OK on success, B_NO_MEMORY if any settings object cannot be allocated.
 */
status_t
NetworkTargetHostInterfaceInfo::Init()
{
	fDescription = new(std::nothrow) SettingsDescription;
	if (fDescription == NULL)
		return B_NO_MEMORY;

	Setting* setting = new(std::nothrow) StringSettingImpl(kHostnameSetting,
		"Hostname", "");
	BReference<Setting> settingsReference(setting, true);
	if (setting == NULL)
		return B_NO_MEMORY;
	if (!fDescription->AddSetting(setting))
		return B_NO_MEMORY;

	setting = new(std::nothrow) BoundedSettingImpl(kPortSetting, "Port",
		(uint16)1, (uint16)65535, (uint16)8305);
	if (setting == NULL)
		return B_NO_MEMORY;

	settingsReference.SetTo(setting, true);
	if (!fDescription->AddSetting(setting))
		return B_NO_MEMORY;

	return B_OK;
}


/**
 * @brief Identifies this descriptor as describing a remote transport.
 *
 * @return Always false.
 */
bool
NetworkTargetHostInterfaceInfo::IsLocal() const
{
	return false;
}


/**
 * @brief Validates that @a settings carries a non-empty host name and a numeric port.
 *
 * @param settings  Settings instance to inspect.
 * @return true if the settings are usable, false otherwise.
 */
bool
NetworkTargetHostInterfaceInfo::IsConfigured(Settings* settings) const
{
	BVariant hostSetting = settings->Value(kHostnameSetting);
	BVariant portSetting = settings->Value(kPortSetting);

	if (hostSetting.Type() != B_STRING_TYPE || !portSetting.IsNumber())
		return false;

	if (strlen(hostSetting.ToString()) == 0)
		return false;

	return true;
}


/**
 * @brief Returns the settings schema produced by Init().
 *
 * @return Borrowed pointer to the cached SettingsDescription, or @c NULL if
 *         Init() has not yet succeeded.
 */
SettingsDescription*
NetworkTargetHostInterfaceInfo::GetSettingsDescription() const
{
	return fDescription;
}


/**
 * @brief Builds a new NetworkTargetHostInterface from the descriptor.
 *
 * @param settings    Settings carrying the connection parameters; passed through
 *                    to the interface's Init().
 * @param _interface  On success, set to the freshly-initialized interface;
 *                    ownership transfers to the caller.
 * @return B_OK on success, B_NO_MEMORY on allocation failure, or any error
 *         propagated from NetworkTargetHostInterface::Init().
 */
status_t
NetworkTargetHostInterfaceInfo::CreateInterface(Settings* settings,
	TargetHostInterface*& _interface) const
{
	NetworkTargetHostInterface* interface
		= new(std::nothrow) NetworkTargetHostInterface;
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

