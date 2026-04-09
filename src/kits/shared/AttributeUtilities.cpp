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
 *   Copyright 2005-2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Copyright 2016, Axel Dörfler, axeld@pinc-software.de.
 *   Distributed under the terms of the MIT License.
 */

/** @file AttributeUtilities.cpp
 *  @brief Provides \c BPrivate::CopyAttributes(), a utility that bulk-copies
 *         all extended attributes from one \c BNode to another.
 */

#include "AttributeUtilities.h"

#include <fs_attr.h>
#include <Node.h>
#include <StorageDefs.h>


/** Internal read buffer size used when streaming attribute data (64 KiB). */
static const int kCopyBufferSize = 64 * 1024;
	// 64 KB


namespace BPrivate {


/**
 * @brief Copies every extended attribute from \a source to \a destination.
 *
 * Iterates over all attribute names reported by \a source. For each attribute
 * the type and size are queried via \c GetAttrInfo(), then the data is
 * transferred in chunks of at most \c kCopyBufferSize bytes. Empty attributes
 * (size == 0) are created on the destination node as well.
 *
 * @param source      The node whose attributes are read.
 * @param destination The node to which the attributes are written.
 * @return \c B_OK on success, or a negative error code if any read or write
 *         operation fails.
 */
status_t
CopyAttributes(BNode& source, BNode& destination)
{
	char attrName[B_ATTR_NAME_LENGTH];
	while (source.GetNextAttrName(attrName) == B_OK) {
		// get attr info
		attr_info attrInfo;
		status_t status = source.GetAttrInfo(attrName, &attrInfo);
		if (status != B_OK)
			return status;

		// copy the attribute
		char buffer[kCopyBufferSize];
		off_t offset = 0;
		off_t bytesLeft = attrInfo.size;

		// Go at least once through the loop, so that an empty attribute will be
		// created as well
		do {
			size_t toRead = kCopyBufferSize;
			if ((off_t)toRead > bytesLeft)
				toRead = bytesLeft;

			// read
			ssize_t bytesRead = source.ReadAttr(attrName, attrInfo.type,
				offset, buffer, toRead);
			if (bytesRead < 0)
				return bytesRead;

			// write
			ssize_t bytesWritten = destination.WriteAttr(attrName,
				attrInfo.type, offset, buffer, bytesRead);
			if (bytesWritten < 0)
				return bytesWritten;

			bytesLeft -= bytesRead;
			offset += bytesRead;
		} while (bytesLeft > 0);
	}
	return B_OK;
}


}	// namespace BPrivate
