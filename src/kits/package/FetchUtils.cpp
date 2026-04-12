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
 *   Copyright 2020, Haiku, Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 */


/**
 * @file FetchUtils.cpp
 * @brief Utility functions for tracking and annotating downloaded files via BFS attributes.
 *
 * Provides static helpers to read and write a boolean "download complete"
 * attribute (Meta:DownloadCompleted) on BNode objects and to set the MIME-type
 * attribute (BEOS:TYPE), enabling FetchFileJob to resume interrupted downloads
 * and skip already-complete ones.
 *
 * @see FetchFileJob, DownloadFileRequest
 */


#include "FetchUtils.h"
#include "string.h"

#include <Entry.h>
#include <Node.h>
#include <TypeConstants.h>

namespace BPackageKit {

namespace BPrivate {


/** @brief Name of the BFS attribute that marks a download as fully complete. */
#define DL_COMPLETE_ATTR "Meta:DownloadCompleted"


/**
 * @brief Check whether the file at \a path was completely downloaded.
 *
 * Resolves symlinks, opens the node, and delegates to the BNode overload.
 *
 * @param path  Filesystem path of the file to check.
 * @return true if the download-complete attribute is set and true, false otherwise.
 */
/*static*/ bool
FetchUtils::IsDownloadCompleted(const char* path)
{
	BEntry entry(path, true);
	BNode node(&entry);
	return IsDownloadCompleted(node);
}


/**
 * @brief Check whether \a node carries the download-complete attribute.
 *
 * Reads the boolean attribute DL_COMPLETE_ATTR.  If the attribute is absent
 * (e.g. file was written by an older package-kit version), returns false so
 * that the download is retried as a no-op range request at worst.
 *
 * @param node  The BNode to inspect.
 * @return true if the attribute exists and is true, false otherwise.
 */
/*static*/ bool
FetchUtils::IsDownloadCompleted(const BNode& node)
{
    bool isComplete;
    status_t status = _GetAttribute(node, DL_COMPLETE_ATTR,
        B_BOOL_TYPE, &isComplete, sizeof(isComplete));
    if (status != B_OK) {
        // Most likely cause is that the attribute was not written,
        // for example by previous versions of the Package Kit.
        // Worst outcome of assuming a partial download should be
        // a no-op range request.
        isComplete = false;
    }
    return isComplete;
}


/**
 * @brief Write the download-complete attribute to \a node.
 *
 * Sets DL_COMPLETE_ATTR to true so that subsequent runs know the file is
 * intact and can skip the download.
 *
 * @param node  The BNode to annotate (must be writable).
 * @return B_OK on success, or an error code if the attribute could not be written.
 */
/*static*/ status_t
FetchUtils::MarkDownloadComplete(BNode& node)
{
    bool isComplete = true;
    return _SetAttribute(node, DL_COMPLETE_ATTR,
        B_BOOL_TYPE, &isComplete, sizeof(isComplete));
}


/**
 * @brief Set the BEOS:TYPE MIME-type attribute on \a node.
 *
 * @param node  The BNode to annotate (must be writable).
 * @param type  Null-terminated MIME type string.
 * @return B_OK on success, or an error code if the attribute could not be written.
 */
/*static*/ status_t
FetchUtils::SetFileType(BNode& node, const char* type)
{
	return _SetAttribute(node, "BEOS:TYPE",
        B_MIME_STRING_TYPE, type, strlen(type) + 1);
}


/**
 * @brief Write a typed BFS attribute to \a node.
 *
 * @param node      Target BNode (must be writable and initialised).
 * @param attrName  Name of the attribute to write.
 * @param type      Type code for the attribute data.
 * @param data      Pointer to the data to write.
 * @param size      Size in bytes of \a data.
 * @return B_OK on success, B_IO_ERROR if fewer bytes than expected were written,
 *         or the node's InitCheck() error if the node is uninitialised.
 */
status_t
FetchUtils::_SetAttribute(BNode& node, const char* attrName,
    type_code type, const void* data, size_t size)
{
	if (node.InitCheck() != B_OK)
		return node.InitCheck();

	ssize_t written = node.WriteAttr(attrName, type, 0, data, size);
	if (written != (ssize_t)size) {
		if (written < 0)
			return (status_t)written;
		return B_IO_ERROR;
	}
	return B_OK;
}


/**
 * @brief Read a typed BFS attribute from \a node.
 *
 * @param node      Source BNode.
 * @param attrName  Name of the attribute to read.
 * @param type      Expected type code.
 * @param data      Buffer to receive the attribute data.
 * @param size      Number of bytes to read.
 * @return B_OK on success, B_IO_ERROR if fewer bytes than expected were read,
 *         or the node's InitCheck() error if the node is uninitialised.
 */
status_t
FetchUtils::_GetAttribute(const BNode& node, const char* attrName,
    type_code type, void* data, size_t size)
{
	if (node.InitCheck() != B_OK)
		return node.InitCheck();

	ssize_t read = node.ReadAttr(attrName, type, 0, data, size);
	if (read != (ssize_t)size) {
		if (read < 0)
			return (status_t)read;
		return B_IO_ERROR;
	}
	return B_OK;
}


}	// namespace BPrivate

}	// namespace BPackageKit
