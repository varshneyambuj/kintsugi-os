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
 *   Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file PortPool.cpp
 *  @brief A recycling pool of reply ports used internally by the media kit. */


#include <set>

#include <Autolock.h>
#include <Locker.h>

#include <MediaDebug.h>

#include "PortPool.h"


namespace BPrivate {
namespace media {


/** @brief Global PortPool instance; managed by MediaRosterUndertaker. */
PortPool* gPortPool;
	// managed by MediaRosterUndertaker.


/** @brief Constructs a PortPool with an initially empty port set. */
PortPool::PortPool()
	:
	BLocker("port pool")
{
}


/** @brief Destructor; deletes all ports remaining in the pool. */
PortPool::~PortPool()
{
	PortSet::iterator iterator = fPool.begin();

	for (; iterator != fPool.end(); iterator++)
		delete_port(*iterator);
}


/** @brief Returns a reply port, reusing one from the pool or creating a new one.
 *
 *  If the pool is empty a fresh port is created with capacity 1.
 *
 *  @return A valid port_id ready for use as a reply channel. */
port_id
PortPool::GetPort()
{
	BAutolock _(this);

	if (fPool.empty())
		return create_port(1, "media reply port");

	port_id port = *fPool.begin();
	fPool.erase(port);

	ASSERT(port >= 0);
	return port;
}


/** @brief Returns a previously obtained port back to the pool for reuse.
 *
 *  If the insertion throws std::bad_alloc the port is deleted rather than leaked.
 *
 *  @param port The port_id to return; must be >= 0. */
void
PortPool::PutPort(port_id port)
{
	ASSERT(port >= 0);

	BAutolock _(this);

	try {
		fPool.insert(port);
	} catch (std::bad_alloc& exception) {
		delete_port(port);
	}
}


}	// namespace media
}	// namespace BPrivate
