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

/** @file LocalTargetHostInterfaceInfo.h
    @brief Descriptor advertising the local-host debugger transport. */

#ifndef LOCAL_TARGET_HOST_INTERFACE_INFO_H
#define LOCAL_TARGET_HOST_INTERFACE_INFO_H

#include "TargetHostInterfaceInfo.h"


/** @brief Factory descriptor that creates LocalTargetHostInterface instances for
           debugging teams running on the same machine. */
class LocalTargetHostInterfaceInfo : public TargetHostInterfaceInfo {
public:
								LocalTargetHostInterfaceInfo();
	virtual						~LocalTargetHostInterfaceInfo();

	virtual	status_t			Init();

	virtual	bool				IsLocal() const;
	virtual	bool				IsConfigured(Settings* settings) const;
	virtual	SettingsDescription* GetSettingsDescription() const;

	virtual	status_t			CreateInterface(Settings* settings,
									TargetHostInterface*& _interface) const;

private:
			BString				fName;
};

#endif	// TARGET_HOST_INTERFACE_INFO_H
