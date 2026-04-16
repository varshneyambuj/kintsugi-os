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
 *   Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 *   Distributed under the terms of the MIT License.
 */

/**
 * @file openfirmware_devices.cpp
 * @brief Helpers for walking the Open Firmware device tree by device_type.
 *
 * Provides of_get_next_device(), a stateful depth-first iterator over the
 * firmware device tree that returns the next node whose "device_type"
 * property matches a caller-specified string. The traversal uses the
 * tree links themselves as its stack, so only a single intptr_t cookie
 * has to be kept by the caller.
 */

#include <platform/openfirmware/devices.h>
#include <platform/openfirmware/openfirmware.h>
#include <util/kernel_cpp.h>

#include <string.h>


/**
 * @brief Returns the next device-tree node whose "device_type" property
 *        equals the given string, using a depth-first walk.
 *
 * On the first call the cookie must be zero. If root is non-zero the
 * traversal is restricted to the subtree rooted there (inclusive);
 * otherwise the entire device tree is walked.
 *
 * @param _cookie  In/out cookie tracking the traversal (must be 0 initially).
 * @param root     Optional subtree root phandle (0 for the whole tree).
 * @param type     Value of the "device_type" property to match exactly.
 * @param path     Buffer that receives the node path of the next match.
 * @param pathSize Size of path in bytes.
 * @return B_OK if a match is found; B_ENTRY_NOT_FOUND when the walk is
 *         complete; B_ERROR on firmware failure.
 */
status_t
of_get_next_device(intptr_t *_cookie, intptr_t root, const char *type,
	char *path, size_t pathSize)
{
	intptr_t node = *_cookie;

	while (true) {
		intptr_t next;

		if (node == 0) {
			// node is NULL, meaning that this is the initial function call.
			// If a root was supplied, we take that, otherwise the device tree
			// root.
			if (root != 0)
				node = root;
			else
				node = of_peer(0);

			if (node == OF_FAILED)
				return B_ERROR;
			if (node == 0)
				return B_ENTRY_NOT_FOUND;

			// We want to visit the root first.
			next = node;				
		} else
			next = of_child(node);

		if (next == OF_FAILED)
			return B_ERROR;

		if (next == 0) {
			// no child node found
			next = of_peer(node);
			if (next == OF_FAILED)
				return B_ERROR;

			while (next == 0) {
				// no peer node found, we are using the device
				// tree itself as our search stack

				next = of_parent(node);
				if (next == OF_FAILED)
					return B_ERROR;

				if (next == root || next == 0) {
					// We have searched the whole device tree
					return B_ENTRY_NOT_FOUND;
				}

				// look into the next tree
				node = next;
				next = of_peer(node);
			}
		}

		*_cookie = node = next;

		char nodeType[16];
		int length;
		if (of_getprop(node, "device_type", nodeType, sizeof(nodeType))
				== OF_FAILED
			|| strcmp(nodeType, type)
			|| (length = of_package_to_path(node, path, pathSize - 1))
					== OF_FAILED) {
			continue;
		}

		path[length] = '\0';
		return B_OK;
	}
}

