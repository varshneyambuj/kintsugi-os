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
 *   Copyright (c) 2002-2004 Haiku Project
 *   Distributed under the terms of the MIT License.
 *
 *   This is the InputServerFilter implementation.
 */

/** @file InputServerFilter.cpp
 *  @brief BInputServerFilter kit-side implementation used by input event filter add-ons. */


#include <Region.h>
#include <InputServerFilter.h>
#include "InputServer.h"

/**
 *  Method: BInputServerFilter::BInputServerFilter()
 *   Descr: 
 */
BInputServerFilter::BInputServerFilter()
{
	CALLED();
}


/**
 *  Method: BInputServerFilter::~BInputServerFilter()
 *   Descr: 
 */
BInputServerFilter::~BInputServerFilter()
{
	CALLED();
}


/**
 *  Method: BInputServerFilter::InitCheck()
 *   Descr: 
 */
status_t
BInputServerFilter::InitCheck()
{
	CALLED();
	return B_OK;
}


/**
 *  Method: BInputServerFilter::Filter()
 *   Descr: 
 */
filter_result
BInputServerFilter::Filter(BMessage *message,
                           BList *outList)
{	
	CALLED();
	return B_DISPATCH_MESSAGE;
}


/**
 *  Method: BInputServerFilter::GetScreenRegion()
 *   Descr: 
 */
status_t
BInputServerFilter::GetScreenRegion(BRegion *region) const
{
	if (!region)
		return B_BAD_VALUE;

	*region = BRegion(((InputServer*)be_app)->ScreenFrame());
	return B_OK;
}


/**
 *  Method: BInputServerFilter::_ReservedInputServerFilter1()
 *   Descr: 
 */
void
BInputServerFilter::_ReservedInputServerFilter1()
{
}


/**
 *  Method: BInputServerFilter::_ReservedInputServerFilter2()
 *   Descr: 
 */
void
BInputServerFilter::_ReservedInputServerFilter2()
{
}


/**
 *  Method: BInputServerFilter::_ReservedInputServerFilter3()
 *   Descr: 
 */
void
BInputServerFilter::_ReservedInputServerFilter3()
{
}


/**
 *  Method: BInputServerFilter::_ReservedInputServerFilter4()
 *   Descr: 
 */
void
BInputServerFilter::_ReservedInputServerFilter4()
{
}


