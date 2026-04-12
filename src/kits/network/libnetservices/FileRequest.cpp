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
 *   Copyright 2013-2014 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Adrien Destugues, pulkomandy@pulkomandy.tk
 */


/**
 * @file FileRequest.cpp
 * @brief Implementation of BFileRequest, the file: URI scheme handler.
 *
 * BFileRequest handles file: URIs by reading from the local filesystem.
 * For regular files it queries the MIME type via BNodeInfo, streams the
 * file in 4 KiB chunks, and reports progress to the listener.  For
 * directories it generates EPLF (Easily Parsed List Format) output, which
 * allows WebKit's FTP directory renderer to display the listing.
 *
 * @see BUrlRequest, BUrlProtocolRoster
 */


#include <assert.h>
#include <stdlib.h>

#include <Directory.h>
#include <File.h>
#include <FileRequest.h>
#include <NodeInfo.h>
#include <Path.h>

using namespace BPrivate::Network;


/**
 * @brief Construct a BFileRequest for a file: URI.
 *
 * @param url       The file: URI pointing to the local path.
 * @param output    BDataIO that receives the file bytes or directory listing.
 * @param listener  Optional BUrlProtocolListener for progress callbacks.
 * @param context   BUrlContext providing shared session state.
 */
BFileRequest::BFileRequest(const BUrl& url, BDataIO* output,
	BUrlProtocolListener* listener, BUrlContext* context)
	:
	BUrlRequest(url, output, listener, context, "BUrlProtocol.File", "file"),
	fResult()
{
}


/**
 * @brief Destructor — stops the worker thread if still running.
 */
BFileRequest::~BFileRequest()
{
	status_t status = Stop();
	if (status == B_OK)
		wait_for_thread(fThreadId, &status);
}


/**
 * @brief Return the result object for this request.
 *
 * @return Const reference to the BUrlResult populated after execution.
 */
const BUrlResult&
BFileRequest::Result() const
{
	return fResult;
}


/**
 * @brief Read the local file or directory and deliver bytes to the output.
 *
 * For files: determines the MIME type, opens the file, streams it in 4 KiB
 * chunks, and sends progress notifications.  For directories: generates an
 * EPLF listing with file sizes, modification times, permissions, and inode
 * references.  Symlinks are traversed before deciding on file vs. directory.
 *
 * @return B_OK on success, B_IO_ERROR if an incomplete read occurs,
 *         B_INTERRUPTED if the request is cancelled, or another error code.
 */
status_t
BFileRequest::_ProtocolLoop()
{
	BNode node(fUrl.Path().String());

	if (node.IsSymLink()) {
		// Traverse the symlink and start over
		BEntry entry(fUrl.Path().String(), true);
		node = BNode(&entry);
	}

	ssize_t transferredSize = 0;
	if (node.IsFile()) {
		BFile file(fUrl.Path().String(), B_READ_ONLY);
		status_t error = file.InitCheck();
		if (error != B_OK)
			return error;

		BNodeInfo info(&file);
		char mimeType[B_MIME_TYPE_LENGTH + 1];
		if (info.GetType(mimeType) != B_OK)
			update_mime_info(fUrl.Path().String(), false, true, false);
		if (info.GetType(mimeType) == B_OK)
			fResult.SetContentType(mimeType);

		// Send all notifications to listener, if any
		if (fListener != NULL)
			fListener->ConnectionOpened(this);

		off_t size = 0;
		error = file.GetSize(&size);
		if (error != B_OK)
			return error;
		fResult.SetLength(size);

		if (fListener != NULL)
			fListener->HeadersReceived(this);

		if (fOutput != NULL) {
			ssize_t chunkSize = 0;
			char chunk[4096];
			while (!fQuit) {
				chunkSize = file.Read(chunk, sizeof(chunk));
				if (chunkSize > 0) {
					size_t written = 0;
					error = fOutput->WriteExactly(chunk, chunkSize, &written);
					if (fListener != NULL && written > 0)
						fListener->BytesWritten(this, written);
					if (error != B_OK)
						return error;
					transferredSize += chunkSize;
					if (fListener != NULL)
						fListener->DownloadProgress(this, transferredSize,
							size);
				} else
					break;
			}
			if (fQuit)
				return B_INTERRUPTED;
			// Return error if we didn't transfer everything
			if (transferredSize != size) {
				if (chunkSize < 0)
					return (status_t)chunkSize;
				else
					return B_IO_ERROR;
			}
		}

		return B_OK;
	}

	node_ref ref;
	status_t error = node.GetNodeRef(&ref);

	// Stop here, and don't hit the assert below, if the file doesn't exist.
	if (error != B_OK)
		return error;

	assert(node.IsDirectory());
	BDirectory directory(&ref);

	fResult.SetContentType("application/x-ftp-directory; charset=utf-8");
		// This tells WebKit to use its FTP directory rendering code.

	if (fListener != NULL) {
		fListener->ConnectionOpened(this);
		fListener->HeadersReceived(this);
	}

	if (fOutput != NULL) {
		// Add a parent directory entry.
		size_t written = 0;
		error = fOutput->WriteExactly("+/,\t..\r\n", 8, &written);
		if (fListener != NULL && written > 0)
			fListener->BytesWritten(this, written);
		if (error != B_OK)
			return error;
		transferredSize += written;
		if (fListener != NULL)
			fListener->DownloadProgress(this, transferredSize, 0);

		char name[B_FILE_NAME_LENGTH];
		BEntry entry;
		while (!fQuit && directory.GetNextEntry(&entry) != B_ENTRY_NOT_FOUND) {
			// We read directories using the EPLF (Easily Parsed List Format)
			// This happens to be one of the formats that WebKit can understand,
			// and it is not too hard to parse or generate.
			// http://tools.ietf.org/html/draft-bernstein-eplf-02
			BString eplf("+");
			if (entry.IsFile() || entry.IsSymLink()) {
				eplf += "r,";
				off_t fileSize;
				if (entry.GetSize(&fileSize) == B_OK)
					eplf << "s" << fileSize << ",";
			} else if (entry.IsDirectory())
				eplf += "/,";

			time_t modification;
			if (entry.GetModificationTime(&modification) == B_OK)
				eplf << "m" << modification << ",";

			mode_t permissions;
			if (entry.GetPermissions(&permissions) == B_OK)
				eplf << "up" << BString().SetToFormat("%03o", permissions) << ",";

			node_ref ref;
			if (entry.GetNodeRef(&ref) == B_OK)
				eplf << "i" << ref.device << "." << ref.node << ",";

			entry.GetName(name);
			eplf << "\t" << name << "\r\n";
			size_t written = 0;
			error = fOutput->WriteExactly(eplf.String(), eplf.Length(),
				&written);
			if (fListener != NULL && written > 0)
				fListener->BytesWritten(this, written);
			if (error != B_OK)
				return error;
			transferredSize += written;
			if (fListener != NULL)
				fListener->DownloadProgress(this, transferredSize, 0);
		}

		if (!fQuit)
			fResult.SetLength(transferredSize);
	}

	return fQuit ? B_INTERRUPTED : B_OK;
}
