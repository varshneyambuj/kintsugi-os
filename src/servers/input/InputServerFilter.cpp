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
 * @brief Constructs a new input server filter add-on.
 */
BInputServerFilter::BInputServerFilter()
{
	CALLED();
}


/**
 * @brief Destructor.
 */
BInputServerFilter::~BInputServerFilter()
{
	CALLED();
}


/**
 * @brief Returns the initialization status of this filter add-on.
 *
 * The default implementation always succeeds.
 *
 * @return B_OK.
 */
status_t
BInputServerFilter::InitCheck()
{
	CALLED();
	return B_OK;
}


/**
 * @brief Filters an incoming input event message.
 *
 * Subclasses override this to inspect or modify the message, optionally
 * producing replacement messages in @a outList.  The default implementation
 * dispatches the message unmodified.
 *
 * @param message The input event to filter.
 * @param outList Output list for any replacement messages.
 * @return B_DISPATCH_MESSAGE to forward the message, or B_SKIP_MESSAGE to
 *         consume it.
 */
filter_result
BInputServerFilter::Filter(BMessage *message,
                           BList *outList)
{	
	CALLED();
	return B_DISPATCH_MESSAGE;
}


/**
 * @brief Retrieves the screen region as a BRegion.
 *
 * Queries the InputServer application for the current screen frame and
 * copies it into the caller-supplied region.
 *
 * @param region Output region; must not be NULL.
 * @return B_OK on success, or B_BAD_VALUE if @a region is NULL.
 */
status_t
BInputServerFilter::GetScreenRegion(BRegion *region) const
{
	if (!region)
		return B_BAD_VALUE;

	*region = BRegion(((InputServer*)be_app)->ScreenFrame());
	return B_OK;
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerFilter::_ReservedInputServerFilter1()
{
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerFilter::_ReservedInputServerFilter2()
{
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerFilter::_ReservedInputServerFilter3()
{
}


/** @brief Reserved for future binary compatibility. */
void
BInputServerFilter::_ReservedInputServerFilter4()
{
}


