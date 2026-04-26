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
 * MIT License. Copyright 2017, Haiku, Inc.
 */

/** @file AppRefFilter.h
    @brief BRefFilter that restricts a BFilePanel to applications, folders,
           and volumes when adding entries to the notifications app list. */

#ifndef _APP_REF_FILTER_H
#define _APP_REF_FILTER_H

#include <FilePanel.h>
#include <NodeInfo.h>


/**
 * @brief Reference filter used by the Notifications preflet's "Add..."
 *        file panel.
 *
 * Resolves symlinks, then accepts only entries whose MIME type identifies
 * them as ELF executables, directories, or volumes.
 */
class AppRefFilter : public BRefFilter {
public:
						AppRefFilter();
	virtual bool		Filter(const entry_ref *ref,
							BNode *node,
							struct stat_beos *st,
							const char *filetype);
};

#endif // _APP_REF_FILTER_H
