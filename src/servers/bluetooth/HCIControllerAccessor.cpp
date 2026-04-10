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
 *   Copyright 2007-2008 Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *   All rights reserved. Distributed under the terms of the MIT License.
 */

/** @file HCIControllerAccessor.cpp
 *  @brief HCI delegate that talks to the controller via the kernel HCI socket. */

#include "HCIControllerAccessor.h"

HCIControllerAccessor::HCIControllerAccessor(BPath* path) : HCIDelegate(path)
{

}


HCIControllerAccessor::~HCIControllerAccessor()
{

}


status_t
HCIControllerAccessor::IssueCommand(raw_command rc, size_t size)
{

	if (Id() < 0)
		return B_ERROR;

	return B_OK;
}


status_t 
HCIControllerAccessor::Launch() {

	return B_OK;

}
