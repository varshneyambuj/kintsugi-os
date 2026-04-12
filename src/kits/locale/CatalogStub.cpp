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
 *   Copyright 2010-2016, Adrien Destugues <pulkomandy@pulkomandy.tk>.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file CatalogStub.cpp
 * @brief Lazy-initialization stub for per-shared-library translation catalogs.
 *
 * Each shared library that uses the locale kit macros gets its own
 * BLocaleRoster::GetCatalog() function via this translation unit.
 * The catalog is allocated as a static local and initialized exactly once
 * using the init-once mechanism, keyed on sCatalogInitOnce. The hidden
 * visibility of GetCatalog() prevents symbol collisions between different
 * shared objects in the same process.
 *
 * @see BCatalog, BLocaleRoster
 */


#include <Catalog.h>
#include <LocaleRoster.h>

#include <locks.h>


/** @brief Guards one-time initialization of the per-library static catalog. */
static int32 sCatalogInitOnce = INIT_ONCE_UNINITIALIZED;


/**
 * @brief Return the per-library BCatalog singleton, initializing it on first call.
 *
 * The function is given hidden ELF visibility so that each shared library
 * that links this translation unit gets its own independent catalog instance
 * rather than sharing one with other libraries in the process.
 *
 * @return Pointer to the statically allocated BCatalog for this library.
 */
BCatalog*
BLocaleRoster::GetCatalog()
{
	static BCatalog sCatalog;

	#if (__GNUC__ < 3)
		asm volatile(".hidden GetCatalog__13BLocaleRoster");
	#else
		asm volatile(".hidden _ZN13BLocaleRoster10GetCatalogEv");
	#endif

	return _GetCatalog(&sCatalog, &sCatalogInitOnce);
}


namespace BPrivate{

/**
 * @brief Reset the catalog initialization guard to force a reload on next use.
 *
 * This is primarily used by tests and tools that need to unload and reload
 * catalogs at runtime without restarting the process.
 */
	void ForceUnloadCatalog()
	{
		sCatalogInitOnce = INIT_ONCE_UNINITIALIZED;
	}
}
