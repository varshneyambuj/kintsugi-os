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
 *   Copyright 2009, Stephan Aßmus <superstippi@gmx.de>
 *   Copyright 2002-2004, Marcus Overhagen <marcus@overhagen.de>
 *   All rights reserved. Distributed under the terms of the MIT license.
 */

/** @file MediaFile.cpp
 *  @brief Implementation of BMediaFile for reading and writing media files and streams.
 */

#include <MediaFile.h>

#include <new>

#include <stdlib.h>
#include <string.h>

#include <File.h>
#include <MediaTrack.h>
#include <Url.h>

#include "MediaDebug.h"

#include "MediaExtractor.h"
#include "MediaStreamer.h"
#include "MediaWriter.h"


/** @brief Constructs a BMediaFile for reading from the file referenced by @p ref.
 *  @param ref Entry reference of the file to open for reading.
 */
BMediaFile::BMediaFile(const entry_ref* ref)
{
	CALLED();
	_Init();
	fDeleteSource = true;
	_InitReader(new(std::nothrow) BFile(ref, O_RDONLY));
}


/** @brief Constructs a BMediaFile for reading from an existing data source.
 *  @param source Pointer to the BDataIO source object.
 */
BMediaFile::BMediaFile(BDataIO* source)
{
	CALLED();
	_Init();
	_InitReader(source);
}


/** @brief Constructs a BMediaFile for reading from a file with additional flags.
 *  @param ref   Entry reference of the file to open.
 *  @param flags Reader flags controlling behaviour.
 */
BMediaFile::BMediaFile(const entry_ref* ref, int32 flags)
{
	CALLED();
	_Init();
	fDeleteSource = true;
	_InitReader(new(std::nothrow) BFile(ref, O_RDONLY), NULL, flags);
}


/** @brief Constructs a BMediaFile for reading from a data source with flags.
 *  @param source Pointer to the BDataIO source object.
 *  @param flags  Reader flags controlling behaviour.
 */
BMediaFile::BMediaFile(BDataIO* source, int32 flags)
{
	CALLED();
	_Init();
	_InitReader(source, NULL, flags);
}


/** @brief Constructs a BMediaFile for writing to the file referenced by @p ref.
 *  @param ref       Entry reference of the file to create or overwrite.
 *  @param mfi       Pointer to the desired output media file format info.
 *  @param flags     Writer flags controlling behaviour.
 */
BMediaFile::BMediaFile(const entry_ref* ref, const media_file_format* mfi,
	int32 flags)
{
	CALLED();
	_Init();
	fDeleteSource = true;
	_InitWriter(new(std::nothrow) BFile(ref, B_CREATE_FILE | B_ERASE_FILE
		| B_WRITE_ONLY), NULL, mfi, flags);
}


/** @brief Constructs a BMediaFile for writing to an existing data destination.
 *  @param destination Pointer to the BDataIO destination object.
 *  @param mfi         Pointer to the desired output media file format info.
 *  @param flags       Writer flags controlling behaviour.
 */
BMediaFile::BMediaFile(BDataIO* destination, const media_file_format* mfi,
	int32 flags)
{
	CALLED();
	_Init();
	_InitWriter(destination, NULL, mfi, flags);
}


/** @brief Constructs a BMediaFile whose target file will be set later via SetTo().
 *  @param mfi   Pointer to the desired output media file format info.
 *  @param flags Writer flags (currently unused).
 */
// File will be set later by SetTo()
BMediaFile::BMediaFile(const media_file_format* mfi, int32 flags)
{
	debugger("BMediaFile::BMediaFile not implemented");
}


/** @brief Constructs a BMediaFile for reading from a URL.
 *  @param url The URL to stream from.
 */
BMediaFile::BMediaFile(const BUrl& url)
{
	CALLED();
	_Init();
	fDeleteSource = true;
	_InitReader(NULL, &url);
}


/** @brief Constructs a BMediaFile for reading from a URL with flags.
 *  @param url   The URL to stream from.
 *  @param flags Reader flags controlling behaviour.
 */
BMediaFile::BMediaFile(const BUrl& url, int32 flags)
{
	CALLED();
	_Init();
	fDeleteSource = true;
	_InitReader(NULL, &url, flags);
}


/** @brief Constructs a BMediaFile for writing to a URL destination.
 *  @param destination The URL to write to (streaming server support is TODO).
 *  @param mfi         Pointer to the desired output media file format info.
 *  @param flags       Writer flags controlling behaviour.
 */
BMediaFile::BMediaFile(const BUrl& destination, const media_file_format* mfi,
	int32 flags)
{
	CALLED();
	_Init();
	fDeleteSource = true;
	_InitWriter(NULL, &destination, mfi, flags);
	// TODO: Implement streaming server support, it's
	// a pretty complex thing compared to client mode
	// and will require to expand the current BMediaFile
	// design to be aware of it.
}


/** @brief Re-initialises the BMediaFile to read from a new file reference.
 *  @param ref Entry reference of the new file.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaFile::SetTo(const entry_ref* ref)
{
	CALLED();

	if (ref == NULL)
		return B_BAD_VALUE;

	_UnInit();
	fDeleteSource = true;
	_InitReader(new(std::nothrow) BFile(ref, O_RDONLY));

	return fErr;
}


/** @brief Re-initialises the BMediaFile to read from a new data source.
 *  @param destination Pointer to the new BDataIO source.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaFile::SetTo(BDataIO* destination)
{
	CALLED();

	if (destination == NULL)
		return B_BAD_VALUE;

	_UnInit();
	_InitReader(destination);

	return fErr;
}


/** @brief Re-initialises the BMediaFile to read from a new URL.
 *  @param url The new URL to stream from.
 *  @return B_OK on success, or an error code.
 */
status_t
BMediaFile::SetTo(const BUrl& url)
{
	CALLED();

	_UnInit();
	_InitReader(NULL, &url);

	return fErr;
}


/** @brief Destructor; releases all tracks and frees internal resources. */
BMediaFile::~BMediaFile()
{
	CALLED();

	_UnInit();
}


/** @brief Returns the initialisation status of the BMediaFile.
 *  @return B_OK if initialised successfully, or an error code.
 */
status_t
BMediaFile::InitCheck() const
{
	CALLED();
	return fErr;
}


/** @brief Retrieves format information about the media file.
 *  @param mfi Pointer to a media_file_format struct to fill in.
 *  @return B_OK on success, B_BAD_VALUE if @p mfi is NULL, B_ERROR otherwise.
 */
status_t
BMediaFile::GetFileFormatInfo(media_file_format* mfi) const
{
	CALLED();
	if (mfi == NULL)
		return B_BAD_VALUE;
	if (fErr)
		return B_ERROR;
	*mfi = fMFI;
	return B_OK;
}


/** @brief Retrieves global metadata from the media file into a BMessage.
 *  @param _data Pointer to a BMessage to be filled with metadata key/value pairs.
 *  @return B_OK on success, B_NO_INIT if no extractor, B_BAD_VALUE if @p _data is NULL.
 */
status_t
BMediaFile::GetMetaData(BMessage* _data) const
{
	if (fExtractor == NULL)
		return B_NO_INIT;
	if (_data == NULL)
		return B_BAD_VALUE;

	_data->MakeEmpty();

	return fExtractor->GetMetaData(_data);
}


/** @brief Returns the copyright string embedded in the media file.
 *  @return A pointer to the copyright string, or NULL if none.
 */
const char*
BMediaFile::Copyright() const
{
	return fExtractor->Copyright();
}


/** @brief Returns the total number of tracks in the media file.
 *  @return The track count.
 */
int32
BMediaFile::CountTracks() const
{
	return fTrackNum;
}


/** @brief Returns a BMediaTrack for the track at the given index.
 *         Can be called multiple times with the same index. Call
 *         ReleaseTrack() when done with the returned object.
 *  @param index Zero-based track index.
 *  @return Pointer to a BMediaTrack, or NULL on error or out-of-range index.
 */
BMediaTrack*
BMediaFile::TrackAt(int32 index)
{
	CALLED();
	if (fTrackList == NULL || fExtractor == NULL
		|| index < 0 || index >= fTrackNum) {
		return NULL;
	}
	if (fTrackList[index] == NULL) {
		TRACE("BMediaFile::TrackAt, creating new track for index %"
			B_PRId32 "\n", index);
		fTrackList[index] = new(std::nothrow) BMediaTrack(fExtractor, index);
		TRACE("BMediaFile::TrackAt, new track is %p\n", fTrackList[index]);
	}
	return fTrackList[index];
}


/** @brief Releases the resources used by the given BMediaTrack.
 *         The track pointer becomes invalid after this call, but a new
 *         track can be obtained by calling TrackAt() with the same index.
 *  @param track Pointer to the BMediaTrack to release.
 *  @return B_OK on success, B_ERROR if the track was not found.
 */
status_t
BMediaFile::ReleaseTrack(BMediaTrack* track)
{
	CALLED();
	if (!fTrackList || !track)
		return B_ERROR;
	for (int32 i = 0; i < fTrackNum; i++) {
		if (fTrackList[i] == track) {
			TRACE("BMediaFile::ReleaseTrack, releasing track %p with index "
				"%" B_PRId32 "\n", track, i);
			delete track;
			fTrackList[i] = NULL;
			return B_OK;
		}
	}
	fprintf(stderr, "BMediaFile::ReleaseTrack track %p not found\n", track);
	return B_ERROR;
}


/** @brief Releases the resources used by all currently held BMediaTrack objects.
 *  @return B_OK on success, B_ERROR if no track list exists.
 */
status_t
BMediaFile::ReleaseAllTracks()
{
	CALLED();
	if (!fTrackList)
		return B_ERROR;
	for (int32 i = 0; i < fTrackNum; i++) {
		if (fTrackList[i]) {
			TRACE("BMediaFile::ReleaseAllTracks, releasing track %p with "
				"index %" B_PRId32 "\n", fTrackList[i], i);
			delete fTrackList[i];
			fTrackList[i] = NULL;
		}
	}
	return B_OK;
}


/** @brief Creates and adds a new encoded track to the media file.
 *         Passing NULL for @p codecInfo creates a raw track usable only
 *         with WriteChunk().
 *  @param mediaFormat Pointer to the desired media format for the track.
 *  @param codecInfo   Pointer to codec info, or NULL for a raw (chunk-only) track.
 *  @param flags       Optional flags.
 *  @return Pointer to the new BMediaTrack, or NULL on failure.
 */
BMediaTrack*
BMediaFile::CreateTrack(media_format* mediaFormat,
	const media_codec_info* codecInfo, uint32 flags)
{
	if (mediaFormat == NULL)
		return NULL;

	// NOTE: It is allowed to pass NULL for codecInfo. In that case, the
	// track won't have an Encoder and you can only use WriteChunk() with
	// already encoded data.

	// Make room for the new track.
	BMediaTrack** trackList = (BMediaTrack**)realloc(fTrackList,
		(fTrackNum + 1) * sizeof(BMediaTrack*));
	if (trackList == NULL)
		return NULL;

	int32 streamIndex = fTrackNum;
	fTrackList = trackList;
	fTrackNum += 1;

	BMediaTrack* track = new(std::nothrow) BMediaTrack(fWriter, streamIndex,
		mediaFormat, codecInfo);

	fTrackList[streamIndex] = track;

	return track;
}


/** @brief Creates and adds a raw (encoder-less) track to the media file.
 *  @param mf    Pointer to the desired media format.
 *  @param flags Optional flags.
 *  @return Pointer to the new BMediaTrack, or NULL on failure.
 */
BMediaTrack*
BMediaFile::CreateTrack(media_format* mf, uint32 flags)
{
	return CreateTrack(mf, NULL, flags);
}


/** @brief BeOS R5 compatibility shim for CreateTrack with codec info.
 *  @param self Pointer to the BMediaFile instance.
 *  @param mf   Pointer to the media format.
 *  @param mci  Pointer to the media codec info.
 *  @return Pointer to the new BMediaTrack, or NULL on failure.
 */
// For BeOS R5 compatibility
extern "C" BMediaTrack*
CreateTrack__10BMediaFileP12media_formatPC16media_codec_info(
	BMediaFile* self, media_format* mf, const media_codec_info* mci);
BMediaTrack*
CreateTrack__10BMediaFileP12media_formatPC16media_codec_info(BMediaFile* self,
	media_format* mf, const media_codec_info* mci)
{
	return self->CreateTrack(mf, mci, 0);
}


/** @brief BeOS R5 compatibility shim for CreateTrack without codec info.
 *  @param self Pointer to the BMediaFile instance.
 *  @param mf   Pointer to the media format.
 *  @return Pointer to the new BMediaTrack, or NULL on failure.
 */
// For BeOS R5 compatibility
extern "C" BMediaTrack* CreateTrack__10BMediaFileP12media_format(
	BMediaFile* self, media_format* mf);
BMediaTrack*
CreateTrack__10BMediaFileP12media_format(BMediaFile* self, media_format* mf)
{
	return self->CreateTrack(mf, NULL, 0);
}


/** @brief Sets the copyright string for the entire output media file.
 *  @param copyright Null-terminated copyright string.
 *  @return B_OK on success, B_NO_INIT if no writer is active.
 */
status_t
BMediaFile::AddCopyright(const char* copyright)
{
	if (fWriter == NULL)
		return B_NO_INIT;

	return fWriter->SetCopyright(copyright);
}


/** @brief Adds a user-defined chunk to the file (if supported by the format).
 *  @param type Chunk type identifier.
 *  @param data Pointer to the chunk data.
 *  @param size Size of the chunk data in bytes.
 *  @return B_OK (currently unimplemented).
 */
status_t
BMediaFile::AddChunk(int32 type, const void* data, size_t size)
{
	UNIMPLEMENTED();
	return B_OK;
}


/** @brief Commits the file header after all tracks have been added.
 *         Call this once before writing any track data.
 *  @return B_OK on success, B_NO_INIT if no writer is active.
 */
status_t
BMediaFile::CommitHeader()
{
	if (fWriter == NULL)
		return B_NO_INIT;

	return fWriter->CommitHeader();
}


/** @brief Finalises and closes the output media file.
 *         Call this after all track data has been written.
 *  @return B_OK on success, B_NO_INIT if no writer is active.
 */
status_t
BMediaFile::CloseFile()
{
	if (fWriter == NULL)
		return B_NO_INIT;

	return fWriter->Close();
}

/** @brief Retrieves a copy of the parameter web for controlling file format parameters.
 *  @param outWeb Output pointer to receive the BParameterWeb.
 *  @return B_ERROR (currently unimplemented).
 */
// This is for controlling file format parameters

// returns a copy of the parameter web
status_t
BMediaFile::GetParameterWeb(BParameterWeb** outWeb)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


/** @brief Deprecated BeOS R5 API for obtaining the parameter web.
 *  @return NULL (currently unimplemented).
 */
// deprecated BeOS R5 API
BParameterWeb*
BMediaFile::Web()
{
	UNIMPLEMENTED();
	return 0;
}


/** @brief Retrieves the value of a format parameter by ID.
 *  @param id    Parameter identifier.
 *  @param value Buffer to receive the parameter value.
 *  @param size  In/out size of the value buffer.
 *  @return B_OK (currently unimplemented).
 */
status_t
BMediaFile::GetParameterValue(int32 id,	void* value, size_t* size)
{
	UNIMPLEMENTED();
	return B_OK;
}


/** @brief Sets a format parameter value by ID.
 *  @param id    Parameter identifier.
 *  @param value Pointer to the new parameter value.
 *  @param size  Size of the value data in bytes.
 *  @return B_OK (currently unimplemented).
 */
status_t
BMediaFile::SetParameterValue(int32 id,	const void* value, size_t size)
{
	UNIMPLEMENTED();
	return B_OK;
}


/** @brief Returns a BView for visually controlling format parameters.
 *  @return NULL (currently unimplemented).
 */
BView*
BMediaFile::GetParameterView()
{
	UNIMPLEMENTED();
	return 0;
}


/** @brief General-purpose perform hook for subclass extensions.
 *  @param selector Operation selector code.
 *  @param data     Pointer to operation-specific data.
 *  @return B_OK (currently unimplemented).
 */
status_t
BMediaFile::Perform(int32 selector, void* data)
{
	UNIMPLEMENTED();
	return B_OK;
}


/** @brief Low-level file control hook.
 *  @param selector Operation selector code.
 *  @param ioData   Pointer to input/output data buffer.
 *  @param size     Size of the data buffer in bytes.
 *  @return B_ERROR (currently unimplemented).
 */
status_t
BMediaFile::ControlFile(int32 selector, void* ioData, size_t size)
{
	UNIMPLEMENTED();
	return B_ERROR;
}


// #pragma mark - private


/** @brief Initialises all member variables to safe defaults. */
void
BMediaFile::_Init()
{
	CALLED();

	fSource = NULL;
	fTrackNum = 0;
	fTrackList = NULL;
	fExtractor = NULL;
	fStreamer = NULL;
	fWriter = NULL;
	fWriterID = 0;
	fErr = B_OK;
	fDeleteSource = false;

	// not used so far:
	fEncoderMgr = NULL;
	fWriterMgr = NULL;
	fFileClosed = false;
}


/** @brief Releases all tracks, deletes the extractor/writer/streamer, and
 *         optionally deletes the owned source object.
 */
void
BMediaFile::_UnInit()
{
	ReleaseAllTracks();
	free(fTrackList);
	fTrackList = NULL;
	fTrackNum = 0;

	// Tells the extractor to stop its asynchronous processing
	// before deleting its source
	if (fExtractor != NULL)
		fExtractor->StopProcessing();

	if (fDeleteSource) {
		delete fSource;
		fDeleteSource = false;
	}
	fSource = NULL;

	// Deleting the extractor or writer can cause unloading of the plugins.
	// The source must be deleted before that, because it can come from a
	// plugin (for example the http_streamer)
	delete fExtractor;
	fExtractor = NULL;
	delete fWriter;
	fWriter = NULL;
	delete fStreamer;
	fStreamer = NULL;
}


/** @brief Sets up the MediaExtractor from the given source or URL.
 *  @param source Pointer to the BDataIO source (may be NULL if @p url is set).
 *  @param url    Pointer to a BUrl (may be NULL if @p source is set).
 *  @param flags  Reader flags.
 */
void
BMediaFile::_InitReader(BDataIO* source, const BUrl* url, int32 flags)
{
	CALLED();

	if (source == NULL && url == NULL) {
		fErr = B_NO_MEMORY;
		return;
	}

	if (source == NULL)
		_InitStreamer(*url, &source);
	else if (BFile* file = dynamic_cast<BFile*>(source))
		fErr = file->InitCheck();

	if (fErr != B_OK)
		return;

	fExtractor = new(std::nothrow) MediaExtractor(source, flags);

	if (fExtractor == NULL)
		fErr = B_NO_MEMORY;
	else
		fErr = fExtractor->InitCheck();

	if (fErr != B_OK)
		return;

	fSource = source;

	fExtractor->GetFileFormatInfo(&fMFI);
	fTrackNum = fExtractor->StreamCount();
	fTrackList = (BMediaTrack**)malloc(fTrackNum * sizeof(BMediaTrack*));
	if (fTrackList == NULL) {
		fErr = B_NO_MEMORY;
		return;
	}
	memset(fTrackList, 0, fTrackNum * sizeof(BMediaTrack*));
}


/** @brief Sets up the MediaWriter for the given target or URL.
 *  @param target      Pointer to the BDataIO target (may be NULL if @p url is set).
 *  @param url         Pointer to a BUrl (may be NULL if @p target is set).
 *  @param fileFormat  Pointer to the desired output media file format.
 *  @param flags       Writer flags.
 */
void
BMediaFile::_InitWriter(BDataIO* target, const BUrl* url,
	const media_file_format* fileFormat, int32 flags)
{
	CALLED();

	if (fileFormat == NULL) {
		fErr = B_BAD_VALUE;
		return;
	}

	if (target == NULL && url == NULL) {
		fErr = B_NO_MEMORY;
		return;
	}

	fMFI = *fileFormat;

	if (target == NULL) {
		_InitStreamer(*url, &target);
		if (fErr != B_OK)
			return;
	}

	fWriter = new(std::nothrow) MediaWriter(target, fMFI);

	if (fWriter == NULL)
		fErr = B_NO_MEMORY;
	else
		fErr = fWriter->InitCheck();
	if (fErr != B_OK)
		return;

	// Get the actual source from the writer
	fSource = fWriter->Target();
	fTrackNum = 0;
}


/** @brief Creates a MediaStreamer for the given URL and obtains a BDataIO adapter.
 *  @param url     The URL to stream.
 *  @param adapter Output pointer to receive the created BDataIO adapter.
 */
void
BMediaFile::_InitStreamer(const BUrl& url, BDataIO** adapter)
{
	if (fStreamer != NULL)
		delete fStreamer;

	TRACE(url.UrlString());

	fStreamer = new(std::nothrow) MediaStreamer(url);
	if (fStreamer == NULL) {
		fErr = B_NO_MEMORY;
		return;
	}

	fErr = fStreamer->CreateAdapter(adapter);
}

/*
//unimplemented
BMediaFile::BMediaFile();
BMediaFile::BMediaFile(const BMediaFile&);
 BMediaFile::BMediaFile& operator=(const BMediaFile&);
*/

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_0(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_1(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_2(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_3(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_4(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_5(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_6(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_7(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_8(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_9(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_10(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_11(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_12(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_13(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_14(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_15(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_16(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_17(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_18(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_19(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_20(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_21(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_22(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_23(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_24(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_25(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_26(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_27(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_28(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_29(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_30(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_31(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_32(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_33(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_34(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_35(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_36(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_37(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_38(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_39(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_40(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_41(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_42(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_43(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_44(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_45(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_46(int32 arg, ...) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaFile::_Reserved_BMediaFile_47(int32 arg, ...) { return B_ERROR; }

