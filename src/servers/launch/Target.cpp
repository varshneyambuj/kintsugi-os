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

/** @file Target.cpp
 *  @brief Implements the launch target that groups related jobs under a named launch milestone. */


#include "Target.h"


Target::Target(const char* name)
	:
	BaseJob(name),
	fLaunched(false)
{
}


status_t
Target::AddData(const char* name, BMessage& data)
{
	return fData.AddMessage(name, &data);
}


void
Target::SetLaunched(bool launched)
{
	fLaunched = launched;
}


status_t
Target::Execute()
{
	return B_OK;
}
