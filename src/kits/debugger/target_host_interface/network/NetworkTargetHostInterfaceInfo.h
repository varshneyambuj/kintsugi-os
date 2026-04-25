/*
 * Copyright 2025, Kintsugi OS Contributors. All rights reserved.
 *
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
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 *
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2016, Rene Gollent, rene@gollent.com.
 */

/** @file NetworkTargetHostInterfaceInfo.h
    @brief Descriptor advertising the network-attached debugger transport. */

#ifndef NETWORK_TARGET_HOST_INTERFACE_INFO_H
#define NETWORK_TARGET_HOST_INTERFACE_INFO_H

#include "TargetHostInterfaceInfo.h"


/** @brief Factory descriptor that creates NetworkTargetHostInterface instances for
           debugging teams on a remote host reached over the network. */
class NetworkTargetHostInterfaceInfo : public TargetHostInterfaceInfo {
public:
								NetworkTargetHostInterfaceInfo();
	virtual						~NetworkTargetHostInterfaceInfo();

	virtual	status_t			Init();

	virtual	bool				IsLocal() const;
	virtual	bool				IsConfigured(Settings* settings) const;
	virtual	SettingsDescription* GetSettingsDescription() const;

	virtual	status_t			CreateInterface(Settings* settings,
									TargetHostInterface*& _interface) const;

private:
			BString				fName;
			SettingsDescription* fDescription;
};

#endif	// NETWORK_TARGET_HOST_INTERFACE_INFO_H
