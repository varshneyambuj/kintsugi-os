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
 * Incorporates work from Haiku, Inc. covered by:
 * Copyright 2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file FileInterface.h
 *  @brief Defines BFileInterface, the mixin for media nodes that read or write files.
 */

#ifndef _FILE_INTERFACE_H
#define _FILE_INTERFACE_H


#include <MediaNode.h>


struct entry_ref;


/** @brief Mixin base class for media nodes that operate on file-system entries.
 *
 *  BFileInterface lets a media node advertise the file formats it can read or
 *  write and binds it to a specific file at runtime.  Nodes that implement
 *  this interface have the B_FILE_INTERFACE kind flag set.
 */
class BFileInterface : public virtual BMediaNode {
protected:
	virtual						~BFileInterface();

protected:
	/** @brief Default constructor. */
								BFileInterface();

	/** @brief Dispatches an incoming port message to the appropriate handler.
	 *  @param message The message code.
	 *  @param data Pointer to the message payload.
	 *  @param size Size of the payload in bytes.
	 *  @return B_OK if handled, or an error code.
	 */
	virtual	status_t			HandleMessage(int32 message, const void* data,
									size_t size);

	/** @brief Iterates over the file formats this node supports.
	 *  @param cookie In/out iteration cookie; initialize to 0 before first call.
	 *  @param _format On return, the next supported media_file_format.
	 *  @return B_OK while more formats exist, B_BAD_INDEX when done.
	 */
	virtual	status_t			GetNextFileFormat(int32* cookie,
									media_file_format* _format) = 0;

	/** @brief Releases resources held by a file-format iteration cookie.
	 *  @param cookie The cookie returned by GetNextFileFormat().
	 */
	virtual	void				DisposeFileFormatCookie(int32 cookie) = 0;

	/** @brief Returns the duration of the currently open file.
	 *  @param _time On return, the duration in microseconds.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetDuration(bigtime_t* _time) = 0;

	/** @brief Probes a file to determine whether this node can handle it.
	 *  @param file The file to examine.
	 *  @param _mimeType Buffer (at least 256 bytes) that receives the MIME type.
	 *  @param _quality On return, a 0.0-1.0 confidence score.
	 *  @return B_OK if the node can handle the file, or an error code.
	 */
	virtual	status_t			SniffRef(const entry_ref& file,
									char* _mimeType, // 256 bytes
									float* _quality) = 0;

	/** @brief Binds this node to a file entry.
	 *  @param file The entry_ref identifying the file.
	 *  @param create True to create or truncate the file for writing.
	 *  @param _time On return, the duration of the file in microseconds.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SetRef(const entry_ref& file,
									bool create, bigtime_t* _time) = 0;

	/** @brief Returns the entry_ref and MIME type of the currently open file.
	 *  @param _ref On return, the entry_ref of the current file.
	 *  @param _mimeType Buffer that receives the MIME type string.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetRef(entry_ref* _ref, char* _mimeType) = 0;

	// TODO: Needs a Perform() virtual method!

private:
	// FBC padding and forbidden methods
	friend class BMediaNode;

								BFileInterface(const BFileInterface& other);
			BFileInterface&		operator=(const BFileInterface& other);

	virtual	status_t			_Reserved_FileInterface_0(void*);
	virtual	status_t			_Reserved_FileInterface_1(void*);
	virtual	status_t			_Reserved_FileInterface_2(void*);
	virtual	status_t			_Reserved_FileInterface_3(void*);
	virtual	status_t			_Reserved_FileInterface_4(void*);
	virtual	status_t			_Reserved_FileInterface_5(void*);
	virtual	status_t			_Reserved_FileInterface_6(void*);
	virtual	status_t			_Reserved_FileInterface_7(void*);
	virtual	status_t			_Reserved_FileInterface_8(void*);
	virtual	status_t			_Reserved_FileInterface_9(void*);
	virtual	status_t			_Reserved_FileInterface_10(void*);
	virtual	status_t			_Reserved_FileInterface_11(void*);
	virtual	status_t			_Reserved_FileInterface_12(void*);
	virtual	status_t			_Reserved_FileInterface_13(void*);
	virtual	status_t			_Reserved_FileInterface_14(void*);
	virtual	status_t			_Reserved_FileInterface_15(void*);

			uint32				_reserved_file_interface_[16];
};

#endif // _FILE_INTERFACE_H

