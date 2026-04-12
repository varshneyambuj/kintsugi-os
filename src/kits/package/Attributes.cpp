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
 *   Copyright 2011, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Tappe <zooey@hirschkaefer.de>
 */


/**
 * @file Attributes.cpp
 * @brief BFS extended-attribute name constants for the package kit.
 *
 * Defines the canonical string constants used both inside HPKG package files
 * and as BFS extended-attribute names on installed package entries.  Grouping
 * them here allows other subsystems to share a single definition without
 * pulling in heavier package-kit headers.
 *
 * @see BPackageInfo, BPackageRoster
 */


#include <package/Attributes.h>


namespace BPackageKit {


/** @brief BFS/HPKG attribute key for the package name field. */
// attributes used in package and as BFS-attribute
const char* kPackageNameAttribute		= "PKG:name";

/** @brief BFS/HPKG attribute key for the target platform. */
const char* kPackagePlatformAttribute	= "PKG:platform";

/** @brief BFS/HPKG attribute key for the package vendor string. */
const char* kPackageVendorAttribute		= "PKG:vendor";

/** @brief BFS/HPKG attribute key for the encoded package version. */
const char* kPackageVersionAttribute	= "PKG:version";

/** @brief HPKG-only attribute key for the copyright list. */
// attributes kept local to packages
const char* kPackageCopyrightsAttribute	= "PKG:copyrights";

/** @brief HPKG-only attribute key for the license list. */
const char* kPackageLicensesAttribute	= "PKG:licenses";

/** @brief HPKG-only attribute key for the packager identity string. */
const char* kPackagePackagerAttribute	= "PKG:packager";

/** @brief HPKG-only attribute key for the provides resolvable list. */
const char* kPackageProvidesAttribute	= "PKG:provides";

/** @brief HPKG-only attribute key for the requires resolvable list. */
const char* kPackageRequiresAttribute	= "PKG:requires";


}	// namespace BPackageKit
