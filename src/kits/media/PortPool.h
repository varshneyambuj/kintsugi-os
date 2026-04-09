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
 * MIT License. Copyright 2019, Ryan Leavengood.
 * Copyright 2009, Axel Dörfler, axeld@pinc-software.de.
 */

/** @file PortPool.h
    @brief Pool of reusable message ports for media server communication. */

#ifndef _PORT_POOL_H
#define _PORT_POOL_H


#include <ServerInterface.h>

#include <set>


namespace BPrivate {
namespace media {


/** @brief Thread-safe pool that recycles OS ports to avoid repeated creation overhead. */
class PortPool : BLocker {
public:
								PortPool();
								~PortPool();

			/** @brief Acquires a port from the pool, creating one if the pool is empty.
			    @return A valid port_id ready for use. */
			port_id				GetPort();

			/** @brief Returns a port to the pool for future reuse.
			    @param port The port_id to recycle. */
			void				PutPort(port_id port);

private:
			typedef std::set<port_id> PortSet;

			PortSet				fPool;
};


/** @brief Global PortPool instance used by the media kit internals. */
extern PortPool* gPortPool;


}	// namespace media
}	// namespace BPrivate


#endif // _PORT_POOL_H
