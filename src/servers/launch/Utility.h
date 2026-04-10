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

/** @file Utility.h
 *  @brief Free helper functions used throughout the launch daemon (volume probes, path translation). */

#ifndef UTILITY_H
#define UTILITY_H


#include <String.h>


/** @brief Free helpers used by jobs and watchers in the launch daemon. */
namespace Utility {
	/** @brief Returns true if the volume identified by @p device is mounted read-only. */
	bool IsReadOnlyVolume(dev_t device);
	/** @brief Returns true if the volume containing @p path is mounted read-only. */
	bool IsReadOnlyVolume(const char* path);

	/** @brief Blocks or unblocks the media driver underlying @p path. */
	status_t BlockMedia(const char* path, bool block);
	/** @brief Asks the media driver underlying @p path to eject. */
	status_t EjectMedia(const char* path);

	/** @brief Resolves shell-style placeholders in @p path against the daemon environment. */
	BString TranslatePath(const char* path);
}


#endif // UTILITY_H
