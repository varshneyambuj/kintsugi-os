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
 *   Copyright 2002-2006, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Michael Pfeiffer
 */

/** @file ResourceManager.h
 *  @brief Manages shared printing resources with semaphore-based locking. */

#ifndef RESOURCE_MANAGER_H
#define RESOURCE_MANAGER_H

#include "ObjectList.h"

#include <Locker.h>
#include <String.h>

#include "BeUtils.h"

/** @brief Represents a shared transport resource that may require exclusive access. */
class Resource : public Object {
private:
	BString	 fTransport; /**< Transport add-on name */
	BString  fTransportAddress; /**< Transport address or path */
	BString  fConnection; /**< Connection type (Local/Network) */
	sem_id   fResourceAvailable; /**< Semaphore for exclusive access */

public:
	/** @brief Construct a resource for the given transport, address, and connection. */
	Resource(const char* transport, const char* address, const char* connection);
	/** @brief Destructor releasing the semaphore. */
	~Resource();

	/** @brief Determine whether this resource requires locking for serial access. */
	bool NeedsLocking();

	/** @brief Check whether this resource matches the given transport parameters. */
	bool Equals(const char* transport, const char* address, const char* connection);

	/** @brief Return the transport name. */
	const BString& Transport() const { return fTransport; }

	/** @brief Acquire exclusive access to this resource. */
	bool Lock();
	/** @brief Release exclusive access to this resource. */
	void Unlock();
};

/** @brief Allocates and tracks shared printing resources across printers. */
class ResourceManager {
private:
	BObjectList<Resource> fResources; /**< List of allocated resources */

	Resource* Find(const char* transport, const char* address, const char* connection);

public:
	/** @brief Destructor ensuring all resources have been freed. */
	~ResourceManager();

	/** @brief Allocate or reuse a resource for the given transport parameters. */
	Resource* Allocate(const char* transport, const char* address, const char* connection);
	/** @brief Release a resource, deleting it when no longer referenced. */
	void Free(Resource* r);
};

#endif
