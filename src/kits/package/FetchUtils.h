/*
 * Copyright 2026, Kintsugi OS Contributors. All rights reserved.
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
 * MIT License. Copyright 2020, Haiku.
 * Original author: Stephan Aßmus <superstippi@gmx.de>.
 */

/** @file FetchUtils.h
    @brief Static helpers for tagging downloaded package files with completion attributes. */

#ifndef _PACKAGE__PRIVATE__FETCH_UTILS_H_
#define _PACKAGE__PRIVATE__FETCH_UTILS_H_


#include "SupportDefs.h"
#include <Node.h>

namespace BPackageKit {

namespace BPrivate {


/**
 * @brief Static utility class that records and inspects download-completion
 *        state on package files via filesystem attributes.
 */
class FetchUtils {
public:
	static	bool				IsDownloadCompleted(const char* path);
	static	bool				IsDownloadCompleted(const BNode& node);

	static	status_t			MarkDownloadComplete(const char* path);
	static	status_t			MarkDownloadComplete(BNode& node);

	static	status_t			SetFileType(BNode& node, const char* type);

private:
	static	status_t			_SetAttribute(BNode& node,
									const char* attrName,
									type_code type, const void* data,
									size_t size);
	static	status_t			_GetAttribute(const BNode& node,
									const char* attrName,
									type_code type, void* data,
									size_t size);
};


}	// namespace BPrivate

}	// namespace BPackageKit


#endif // _PACKAGE__PRIVATE__FETCH_UTILS_H_
