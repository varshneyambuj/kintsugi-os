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
 *   Copyright 2017, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Brian Hill, supernova@tycho.email
 */


/**
 * @file AppRefFilter.cpp
 * @brief Implementation of AppRefFilter, the BRefFilter used by the
 *        Notifications preflet to restrict its "Add..." file panel to
 *        runnable applications.
 */


#include "AppRefFilter.h"

#include <string.h>


/**
 * @brief Constructs the filter. Holds no state of its own.
 */
AppRefFilter::AppRefFilter()
	:
	BRefFilter()
{
}


/**
 * @brief Decides whether @a ref should be visible in the file panel.
 *
 * Resolves symbolic links to their target so the link's destination is what
 * gets evaluated, then accepts directories, volumes, and ELF executables.
 *
 * @param ref       Entry being considered for display.
 * @param node      Open node for @a ref (unused).
 * @param st        BeOS-style stat record for @a ref (unused).
 * @param filetype  MIME type discovered for @a ref by the file panel.
 * @return true to show the entry; false to hide it.
 */
bool
AppRefFilter::Filter(const entry_ref *ref, BNode *node,
	struct stat_beos *st, const char *filetype)
{
	char* type = NULL;
	const char *constFileType;
	// resolve symlinks
	bool isSymlink = strcmp("application/x-vnd.Be-symlink", filetype) == 0;
	if (isSymlink) {
		BEntry linkedEntry(ref, true);
		if (linkedEntry.InitCheck()!=B_OK)
			return false;
		BNode linkedNode(&linkedEntry);
		if (linkedNode.InitCheck()!=B_OK)
			return false;
		BNodeInfo linkedNodeInfo(&linkedNode);
		if (linkedNodeInfo.InitCheck()!=B_OK)
			return false;
		type = new char[B_ATTR_NAME_LENGTH];
		if (linkedNodeInfo.GetType(type)!=B_OK) {
			delete[] type;
			return false;
		}
		constFileType = type;
	} else
		constFileType = filetype;

	bool pass = false;
	//folders
	if (strcmp("application/x-vnd.Be-directory", constFileType) == 0)
		pass = true;
	//volumes
	else if (strcmp("application/x-vnd.Be-volume", constFileType) == 0)
		pass = true;
	//apps
	else if (strcmp("application/x-vnd.Be-elfexecutable", constFileType) == 0)
		pass = true;
	//hack for Haiku?  Some apps are defined by MIME this way
	else if (strcmp("application/x-vnd.be-elfexecutable", constFileType) == 0)
		pass = true;

	if (isSymlink)
		delete[] type;
	return pass;
}
