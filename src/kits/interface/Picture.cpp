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
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2014 Haiku Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marc Flerackers, mflerackers@androme.be
 */


/**
 * @file Picture.cpp
 * @brief Implementation of BPicture, a recorded sequence of drawing instructions
 *
 * BPicture records a sequence of BView drawing calls that can be replayed at any time.
 * It stores data as an opaque stream understood by the app_server. Used by BPictureButton
 * and BDragger for resolution-independent graphics.
 *
 * @see BView, BPictureButton, BPicturePlayer
 */


#include <Picture.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//#define DEBUG 1
#include <ByteOrder.h>
#include <Debug.h>
#include <List.h>
#include <Message.h>

#include <AppServerLink.h>
#include <ObjectList.h>
#include <PicturePlayer.h>
#include <ServerProtocol.h>
#include <locks.h>

#include "PicturePrivate.h"


/** @brief Global list of all live BPicture objects, used to reconnect them after a server restart. */
static BObjectList<BPicture> sPictureList;
/** @brief Lock protecting sPictureList against concurrent access. */
static recursive_lock sPictureListLock = RECURSIVE_LOCK_INITIALIZER("BPicture list");


/**
 * @brief Re-upload all live BPicture objects to a freshly started app_server.
 *
 * Called by the system after an app_server reconnection event. Iterates the
 * global picture list and calls ReconnectToAppServer() on each entry so that
 * every picture's drawing data is sent back to the new server instance.
 */
void
reconnect_pictures_to_app_server()
{
	RecursiveLocker _(sPictureListLock);
	for (int32 i = 0; i < sPictureList.CountItems(); i++) {
		BPicture::Private picture(sPictureList.ItemAt(i));
		picture.ReconnectToAppServer();
	}
}


/**
 * @brief Construct a Private accessor wrapping the given BPicture.
 *
 * @param picture The BPicture to wrap; must not be NULL.
 */
BPicture::Private::Private(BPicture* picture)
	:
	fPicture(picture)
{
}


/**
 * @brief Re-upload the wrapped BPicture's data to the app_server.
 *
 * Delegates to BPicture::_Upload() to transmit the local picture data after
 * a server reconnection.
 */
void
BPicture::Private::ReconnectToAppServer()
{
	fPicture->_Upload();
}


struct _BPictureExtent_ {
							_BPictureExtent_(int32 size = 0);
							~_BPictureExtent_();

			const void*		Data() const { return fNewData; }
			status_t		ImportData(const void* data, int32 size);

			status_t		Flatten(BDataIO* stream);
			status_t		Unflatten(BDataIO* stream);

			int32			Size() const { return fNewSize; }
			status_t		SetSize(int32 size);

			bool			AddPicture(BPicture* picture)
								{ return fPictures.AddItem(picture); }
			void			DeletePicture(int32 index)
								{ delete static_cast<BPicture*>
									(fPictures.RemoveItem(index)); }

			BList*			Pictures() { return &fPictures; }
			BPicture*		PictureAt(int32 index)
								{ return static_cast<BPicture*>
									(fPictures.ItemAt(index)); }

			int32			CountPictures() const
								{ return fPictures.CountItems(); }

private:
			void*	fNewData;
			int32	fNewSize;

			BList	fPictures;
				// In R5 this is a BArray<BPicture*>
				// which is completely inline.
};


struct picture_header {
	int32 magic1; // version ?
	int32 magic2; // endianess ?
};


/**
 * @brief Construct an empty BPicture.
 *
 * Allocates a new _BPictureExtent_ to hold the drawing stream and registers
 * this picture in the global list. The picture is not yet connected to the
 * app_server; it will be uploaded on demand.
 */
BPicture::BPicture()
	:
	fToken(-1),
	fExtent(NULL),
	fUsurped(NULL)
{
	_InitData();
}


/**
 * @brief Construct a BPicture as a deep copy of another.
 *
 * Clones the server-side picture object (via AS_CLONE_PICTURE) and copies
 * the local data extent together with all referenced sub-pictures.
 *
 * @param otherPicture The picture to copy.
 */
BPicture::BPicture(const BPicture& otherPicture)
	:
	fToken(-1),
	fExtent(NULL),
	fUsurped(NULL)
{
	_InitData();

	if (otherPicture.fToken != -1) {
		BPrivate::AppServerLink link;
		link.StartMessage(AS_CLONE_PICTURE);
		link.Attach<int32>(otherPicture.fToken);

		status_t status = B_ERROR;
		if (link.FlushWithReply(status) == B_OK && status == B_OK)
			link.Read<int32>(&fToken);

		if (status < B_OK)
			return;
	}

	if (otherPicture.fExtent->Size() > 0) {
		fExtent->ImportData(otherPicture.fExtent->Data(),
			otherPicture.fExtent->Size());

		for (int32 i = 0; i < otherPicture.fExtent->CountPictures(); i++) {
			BPicture* picture
				= new BPicture(*otherPicture.fExtent->PictureAt(i));
			fExtent->AddPicture(picture);
		}
	}
}


/**
 * @brief Reconstruct a BPicture from an archived BMessage.
 *
 * Reads the version, endianness, raw drawing data, and any embedded sub-picture
 * messages. Only version 1 archives are supported; version 0 triggers a
 * debugger call.
 *
 * @param data The archive message produced by Archive().
 * @see Archive(), Instantiate()
 */
BPicture::BPicture(BMessage* data)
	:
	fToken(-1),
	fExtent(NULL),
	fUsurped(NULL)
{
	_InitData();

	int32 version;
	if (data->FindInt32("_ver", &version) != B_OK)
		version = 0;

	int8 endian;
	if (data->FindInt8("_endian", &endian) != B_OK)
		endian = 0;

	const void* pictureData;
	ssize_t size;
	if (data->FindData("_data", B_RAW_TYPE, &pictureData, &size) != B_OK)
		return;

	// Load sub pictures
	BMessage pictureMessage;
	int32 i = 0;
	while (data->FindMessage("piclib", i++, &pictureMessage) == B_OK) {
		BPicture* picture = new BPicture(&pictureMessage);
		fExtent->AddPicture(picture);
	}

	if (version == 0) {
		// TODO: For now. We'll see if it's worth to support old style data
		debugger("old style BPicture data is not supported");
	} else if (version == 1) {
		fExtent->ImportData(pictureData, size);

//		swap_data(fExtent->fNewData, fExtent->fNewSize);

		if (fExtent->Size() > 0)
			_AssertServerCopy();
	}

	// Do we just free the data now?
	if (fExtent->Size() > 0)
		fExtent->SetSize(0);

	// What with the sub pictures?
	for (i = fExtent->CountPictures() - 1; i >= 0; i--)
		fExtent->DeletePicture(i);
}


/**
 * @brief Construct a BPicture from a legacy (R4-era) raw data pointer.
 *
 * Old-style picture data is not supported; this constructor immediately calls
 * the debugger.
 *
 * @param data Pointer to the old-format picture data.
 * @param size Byte size of \a data.
 * @note This constructor exists for binary compatibility only.
 */
BPicture::BPicture(const void* data, int32 size)
{
	_InitData();
	// TODO: For now. We'll see if it's worth to support old style data
	debugger("old style BPicture data is not supported");
}


/**
 * @brief Shared initialisation helper called by all constructors.
 *
 * Resets fToken and fUsurped, allocates fExtent, and registers this picture
 * in the global sPictureList under sPictureListLock.
 */
void
BPicture::_InitData()
{
	fToken = -1;
	fUsurped = NULL;

	fExtent = new (std::nothrow) _BPictureExtent_;

	RecursiveLocker _(sPictureListLock);
	sPictureList.AddItem(this);
}


/**
 * @brief Destroy the BPicture, releasing server-side and local resources.
 *
 * Removes this picture from the global list before calling _DisposeData() to
 * ensure that reconnect_pictures_to_app_server() never sees a half-destroyed
 * object.
 */
BPicture::~BPicture()
{
	RecursiveLocker _(sPictureListLock);
	sPictureList.RemoveItem(this, false);
	_DisposeData();
}


/**
 * @brief Release the server-side picture token and the local extent.
 *
 * Sends AS_DELETE_PICTURE to the app_server if a valid token exists, then
 * deletes the _BPictureExtent_ and NULLs fExtent.
 */
void
BPicture::_DisposeData()
{
	if (fToken != -1) {
		BPrivate::AppServerLink link;

		link.StartMessage(AS_DELETE_PICTURE);
		link.Attach<int32>(fToken);
		link.Flush();
		SetToken(-1);
	}

	delete fExtent;
	fExtent = NULL;
}


/**
 * @brief Instantiate a BPicture from an archived BMessage.
 *
 * @param data The archive message to instantiate from.
 * @return A new BPicture if \a data is a valid BPicture archive, or NULL on failure.
 * @see Archive()
 */
BArchivable*
BPicture::Instantiate(BMessage* data)
{
	if (validate_instantiation(data, "BPicture"))
		return new BPicture(data);

	return NULL;
}


/**
 * @brief Archive this BPicture into a BMessage.
 *
 * Writes the version (1), host endianness, raw drawing data, and one "piclib"
 * sub-message for each referenced sub-picture. Requires a local copy of the
 * data; _AssertLocalCopy() is called internally.
 *
 * @param data The message to archive into.
 * @param deep If true, sub-pictures are archived recursively.
 * @return B_OK on success, or an error code on failure.
 * @see Instantiate()
 */
status_t
BPicture::Archive(BMessage* data, bool deep) const
{
	if (!const_cast<BPicture*>(this)->_AssertLocalCopy())
		return B_ERROR;

	status_t err = BArchivable::Archive(data, deep);
	if (err != B_OK)
		return err;

	err = data->AddInt32("_ver", 1);
	if (err != B_OK)
		return err;

	err = data->AddInt8("_endian", B_HOST_IS_BENDIAN);
	if (err != B_OK)
		return err;

	err = data->AddData("_data", B_RAW_TYPE, fExtent->Data(), fExtent->Size());
	if (err != B_OK)
		return err;

	for (int32 i = 0; i < fExtent->CountPictures(); i++) {
		BMessage pictureMessage;

		err = fExtent->PictureAt(i)->Archive(&pictureMessage, deep);
		if (err != B_OK)
			break;

		err = data->AddMessage("piclib", &pictureMessage);
		if (err != B_OK)
			break;
	}

	return err;
}


/**
 * @brief Dispatch a perform_code to the base class.
 *
 * @param code The perform code identifying the operation.
 * @param arg  Opaque argument passed to BArchivable::Perform().
 * @return The result from BArchivable::Perform().
 */
status_t
BPicture::Perform(perform_code code, void* arg)
{
	return BArchivable::Perform(code, arg);
}


/**
 * @brief Replay the recorded drawing instructions through a callback table.
 *
 * Obtains a local copy of the picture data if necessary, then constructs a
 * BPrivate::PicturePlayer and invokes it with the provided table.
 *
 * @param callBackTable Array of function pointers indexed by drawing opcode.
 * @param tableEntries  Number of entries in \a callBackTable.
 * @param user          Caller-supplied context pointer passed to each callback.
 * @return B_OK on success, or an error code if the local copy could not be obtained.
 */
status_t
BPicture::Play(void** callBackTable, int32 tableEntries, void* user)
{
	if (!_AssertLocalCopy())
		return B_ERROR;

	BPrivate::PicturePlayer player(fExtent->Data(), fExtent->Size(),
		fExtent->Pictures());

	return player.Play(callBackTable, tableEntries, user);
}


/**
 * @brief Write the picture data to a BDataIO stream in a portable format.
 *
 * Writes a two-int32 magic header followed by the flattened extent (sub-picture
 * count, each sub-picture recursively, data size, data bytes). Requires a local
 * copy of the data.
 *
 * @param stream The output stream to write to.
 * @return B_OK on success.
 * @retval B_IO_ERROR If fewer bytes than expected were written.
 * @see Unflatten()
 */
status_t
BPicture::Flatten(BDataIO* stream)
{
	// TODO: what about endianess?

	if (!_AssertLocalCopy())
		return B_ERROR;

	const picture_header header = { 2, 0 };
	ssize_t bytesWritten = stream->Write(&header, sizeof(header));
	if (bytesWritten < B_OK)
		return bytesWritten;

	if (bytesWritten != (ssize_t)sizeof(header))
		return B_IO_ERROR;

	return fExtent->Flatten(stream);
}


/**
 * @brief Read picture data from a BDataIO stream produced by Flatten().
 *
 * Validates the magic header, reads the extent data (sub-pictures and raw
 * drawing bytes), and uploads the result to the app_server. The local copy
 * is freed once the server has a copy.
 *
 * @param stream The input stream to read from.
 * @return B_OK on success.
 * @retval B_BAD_TYPE If the stream header is invalid or unrecognised.
 * @see Flatten()
 */
status_t
BPicture::Unflatten(BDataIO* stream)
{
	// TODO: clear current picture data?

	picture_header header;
	ssize_t bytesRead = stream->Read(&header, sizeof(header));
	if (bytesRead < B_OK)
		return bytesRead;

	if (bytesRead != (ssize_t)sizeof(header)
		|| header.magic1 != 2 || header.magic2 != 0)
		return B_BAD_TYPE;

	status_t status = fExtent->Unflatten(stream);
	if (status < B_OK)
		return status;

//	swap_data(fExtent->fNewData, fExtent->fNewSize);

	if (!_AssertServerCopy())
		return B_ERROR;

	// Data is now kept server side, remove the local copy
	if (fExtent->Data() != NULL)
		fExtent->SetSize(0);

	return status;
}


/**
 * @brief Import legacy R4/R5 picture data (currently a no-op).
 *
 * Old-format picture data is not supported. This method exists as a placeholder
 * for future implementation.
 *
 * @param data Pointer to the old-format data buffer.
 * @param size Byte size of \a data.
 */
void
BPicture::_ImportOldData(const void* data, int32 size)
{
	// TODO: We don't support old data for now
}


/**
 * @brief Set the app_server token associated with this picture.
 *
 * @param token The new token value; use -1 to indicate no server copy.
 */
void
BPicture::SetToken(int32 token)
{
	fToken = token;
}


/**
 * @brief Return the app_server token for this picture.
 *
 * @return The current token, or -1 if no server copy exists.
 */
int32
BPicture::Token() const
{
	return fToken;
}


/**
 * @brief Ensure that a local copy of the picture data is available.
 *
 * Returns true immediately if local data already exists. If only a server copy
 * is present, downloads it via _Download(). Returns false if neither source
 * is available.
 *
 * @return true if local data is available after the call.
 * @see _Download()
 */
bool
BPicture::_AssertLocalCopy()
{
	if (fExtent->Data() != NULL)
		return true;

	if (fToken == -1)
		return false;

	return _Download() == B_OK;
}


/**
 * @brief Stub that always returns false (old-format local copies are unsupported).
 *
 * @return Always false.
 */
bool
BPicture::_AssertOldLocalCopy()
{
	// TODO: We don't support old data for now

	return false;
}


/**
 * @brief Ensure that a server-side copy of the picture data exists.
 *
 * Returns true immediately if a valid token is already held. If only local data
 * is present, recursively ensures all sub-pictures are uploaded, then uploads
 * this picture's data via _Upload().
 *
 * @return true if a server copy is available after the call.
 * @see _Upload()
 */
bool
BPicture::_AssertServerCopy()
{
	if (fToken != -1)
		return true;

	if (fExtent->Data() == NULL)
		return false;

	for (int32 i = 0; i < fExtent->CountPictures(); i++) {
		if (!fExtent->PictureAt(i)->_AssertServerCopy())
			return false;
	}

	return _Upload() == B_OK;
}


/**
 * @brief Send the local picture data to the app_server and store the returned token.
 *
 * Transmits sub-picture tokens followed by the raw drawing-data buffer via
 * AS_CREATE_PICTURE. On success, fToken is updated.
 *
 * @return B_OK on success, or B_BAD_VALUE / B_ERROR on failure.
 */
status_t
BPicture::_Upload()
{
	if (fExtent == NULL || fExtent->Data() == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;

	link.StartMessage(AS_CREATE_PICTURE);
	link.Attach<int32>(fExtent->CountPictures());

	for (int32 i = 0; i < fExtent->CountPictures(); i++) {
		BPicture* picture = fExtent->PictureAt(i);
		if (picture != NULL)
			link.Attach<int32>(picture->fToken);
		else
			link.Attach<int32>(-1);
	}
	link.Attach<int32>(fExtent->Size());
	link.Attach(fExtent->Data(), fExtent->Size());

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK
		&& status == B_OK) {
		link.Read<int32>(&fToken);
	}

	return status;
}


/**
 * @brief Download the picture data from the app_server into the local extent.
 *
 * Sends AS_DOWNLOAD_PICTURE with the current token and reads back the
 * sub-picture token list followed by the raw drawing-data buffer.
 *
 * @return B_OK on success, or an error code if the download fails.
 */
status_t
BPicture::_Download()
{
	ASSERT(fExtent->Data() == NULL);
	ASSERT(fToken != -1);

	BPrivate::AppServerLink link;

	link.StartMessage(AS_DOWNLOAD_PICTURE);
	link.Attach<int32>(fToken);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) == B_OK && status == B_OK) {
		int32 count = 0;
		link.Read<int32>(&count);

		// Read sub picture tokens
		for (int32 i = 0; i < count; i++) {
			BPicture* picture = new BPicture;
			link.Read<int32>(&picture->fToken);
			fExtent->AddPicture(picture);
		}

		int32 size;
		link.Read<int32>(&size);
		status = fExtent->SetSize(size);
		if (status == B_OK)
			link.Read(const_cast<void*>(fExtent->Data()), size);
	}

	return status;
}


/**
 * @brief Return a pointer to the raw picture data, downloading it if necessary.
 *
 * @return Pointer to the internal picture data buffer, or NULL on failure.
 */
const void*
BPicture::Data() const
{
	if (fExtent->Data() == NULL)
		const_cast<BPicture*>(this)->_AssertLocalCopy();

	return fExtent->Data();
}


/**
 * @brief Return the byte size of the raw picture data, downloading it if necessary.
 *
 * @return Number of bytes in the picture data buffer, or 0 on failure.
 */
int32
BPicture::DataSize() const
{
	if (fExtent->Data() == NULL)
		const_cast<BPicture*>(this)->_AssertLocalCopy();

	return fExtent->Size();
}


/**
 * @brief Replace this picture's content with a fresh empty state, saving the old state.
 *
 * Disposes the current data, reinitialises the object, and stores \a lameDuck
 * in fUsurped so it can be retrieved later by StepDown(). Used internally
 * during recording to nest picture scopes.
 *
 * @param lameDuck The picture whose state should be saved and restored later.
 * @see StepDown()
 */
void
BPicture::Usurp(BPicture* lameDuck)
{
	_DisposeData();

	// Reinitializes the BPicture
	_InitData();

	// Do the Usurping
	fUsurped = lameDuck;
}


/**
 * @brief Restore the previously usurped picture and clear the saved reference.
 *
 * Returns the picture saved by the last Usurp() call and sets fUsurped to NULL.
 *
 * @return The previously usurped BPicture, or NULL if Usurp() was never called.
 * @see Usurp()
 */
BPicture*
BPicture::StepDown()
{
	BPicture* lameDuck = fUsurped;
	fUsurped = NULL;

	return lameDuck;
}


void BPicture::_ReservedPicture1() {}
void BPicture::_ReservedPicture2() {}
void BPicture::_ReservedPicture3() {}


/**
 * @brief Assignment operator (intentionally a no-op for binary compatibility).
 *
 * BPicture objects are not copyable via assignment; this operator returns
 * *this unchanged. Use the copy constructor to duplicate a picture.
 *
 * @return A reference to this object, unchanged.
 */
BPicture&
BPicture::operator=(const BPicture&)
{
	return* this;
}


// _BPictureExtent_

/**
 * @brief Construct a _BPictureExtent_ with an optional initial buffer size.
 *
 * @param size Initial byte capacity to allocate; 0 means no allocation.
 */
_BPictureExtent_::_BPictureExtent_(int32 size)
	:
	fNewData(NULL),
	fNewSize(0)
{
	SetSize(size);
}


/**
 * @brief Destroy the _BPictureExtent_, freeing the data buffer and all sub-pictures.
 */
_BPictureExtent_::~_BPictureExtent_()
{
	free(fNewData);
	for (int32 i = 0; i < fPictures.CountItems(); i++)
		delete static_cast<BPicture*>(fPictures.ItemAtFast(i));
}


/**
 * @brief Copy raw picture data into this extent, reallocating as needed.
 *
 * @param data Pointer to the source data buffer; must not be NULL.
 * @param size Number of bytes to copy from \a data.
 * @return B_OK on success.
 * @retval B_BAD_VALUE If \a data is NULL.
 * @retval B_NO_MEMORY If reallocation fails.
 */
status_t
_BPictureExtent_::ImportData(const void* data, int32 size)
{
	if (data == NULL)
		return B_BAD_VALUE;

	status_t status = B_OK;
	if (Size() != size)
		status = SetSize(size);

	if (status == B_OK)
		memcpy(fNewData, data, size);

	return status;
}


/**
 * @brief Restore extent data from a BDataIO stream.
 *
 * Reads a sub-picture count, instantiates and unflattens each sub-picture, then
 * reads the data size and raw bytes into the internal buffer.
 *
 * @param stream The input stream; must not be NULL.
 * @return B_OK on success.
 * @retval B_BAD_VALUE If \a stream is NULL.
 * @retval B_BAD_DATA If the picture count field is truncated.
 * @retval B_IO_ERROR If any subsequent read returns fewer bytes than expected.
 */
status_t
_BPictureExtent_::Unflatten(BDataIO* stream)
{
	if (stream == NULL)
		return B_BAD_VALUE;

	int32 count = 0;
	ssize_t bytesRead = stream->Read(&count, sizeof(count));
	if (bytesRead < B_OK)
		return bytesRead;
	if (bytesRead != (ssize_t)sizeof(count))
		return B_BAD_DATA;

	for (int32 i = 0; i < count; i++) {
		BPicture* picture = new BPicture;
		status_t status = picture->Unflatten(stream);
		if (status < B_OK) {
			delete picture;
			return status;
		}

		AddPicture(picture);
	}

	int32 size;
	bytesRead = stream->Read(&size, sizeof(size));
	if (bytesRead < B_OK)
		return bytesRead;

	if (bytesRead != (ssize_t)sizeof(size))
		return B_IO_ERROR;

	status_t status = B_OK;
	if (Size() != size)
		status = SetSize(size);

	if (status < B_OK)
		return status;

	bytesRead = stream->Read(fNewData, size);
	if (bytesRead < B_OK)
		return bytesRead;

	if (bytesRead != (ssize_t)size)
		return B_IO_ERROR;

	return B_OK;
}


/**
 * @brief Write extent data to a BDataIO stream.
 *
 * Writes a sub-picture count, flattens each sub-picture recursively, then writes
 * the data size and raw bytes.
 *
 * @param stream The output stream to write to.
 * @return B_OK on success.
 * @retval B_IO_ERROR If any write returns fewer bytes than expected.
 */
status_t
_BPictureExtent_::Flatten(BDataIO* stream)
{
	int32 count = fPictures.CountItems();
	ssize_t bytesWritten = stream->Write(&count, sizeof(count));
	if (bytesWritten < B_OK)
		return bytesWritten;

	if (bytesWritten != (ssize_t)sizeof(count))
		return B_IO_ERROR;

	for (int32 i = 0; i < count; i++) {
		status_t status = PictureAt(i)->Flatten(stream);
		if (status < B_OK)
			return status;
	}

	bytesWritten = stream->Write(&fNewSize, sizeof(fNewSize));
	if (bytesWritten < B_OK)
		return bytesWritten;

	if (bytesWritten != (ssize_t)sizeof(fNewSize))
		return B_IO_ERROR;

	bytesWritten = stream->Write(fNewData, fNewSize);
	if (bytesWritten < B_OK)
		return bytesWritten;

	if (bytesWritten != fNewSize)
		return B_IO_ERROR;

	return B_OK;
}


/**
 * @brief Resize the internal data buffer.
 *
 * Uses realloc() to grow or shrink the buffer. Passing 0 frees the buffer
 * entirely and sets the pointer to NULL.
 *
 * @param size The desired buffer size in bytes; must be >= 0.
 * @return B_OK on success.
 * @retval B_BAD_VALUE If \a size is negative.
 * @retval B_NO_MEMORY If reallocation fails.
 */
status_t
_BPictureExtent_::SetSize(int32 size)
{
	if (size < 0)
		return B_BAD_VALUE;

	if (size == fNewSize)
		return B_OK;

	if (size == 0) {
		free(fNewData);
		fNewData = NULL;
	} else {
		void* data = realloc(fNewData, size);
		if (data == NULL)
			return B_NO_MEMORY;

		fNewData = data;
	}

	fNewSize = size;
	return B_OK;
}
