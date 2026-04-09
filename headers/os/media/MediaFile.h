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
 * Copyright 2002-2009, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 */

/** @file MediaFile.h
 *  @brief Defines BMediaFile, the top-level interface for reading and writing media files.
 */

#ifndef _MEDIA_FILE_H
#define	_MEDIA_FILE_H


#include <image.h>
#include <List.h>
#include <MediaDefs.h>
#include <MediaFormats.h>
#include <StorageDefs.h>


namespace BPrivate {
	namespace media {
		class MediaExtractor;
		class MediaStreamer;
		class MediaWriter;
	}
	class _AddonManager;
}


// forward declarations
class BMediaTrack;
class BMessage;
class BParameterWeb;
class BUrl;
class BView;


/** @brief Flags for the BMediaFile constructor controlling read/write behaviour. */
enum {
	B_MEDIA_FILE_REPLACE_MODE    = 0x00000001, /**< Replace the existing file. */
	B_MEDIA_FILE_NO_READ_AHEAD   = 0x00000002, /**< Disable read-ahead buffering. */
	B_MEDIA_FILE_UNBUFFERED      = 0x00000006, /**< Disable all buffering. */
	B_MEDIA_FILE_BIG_BUFFERS     = 0x00000008  /**< Use large I/O buffers. */
};

/** @brief Represents a media container file (AVI, QuickTime, MPEG, AIFF, WAV, etc.).
 *
 *  To read a file, construct BMediaFile with an entry_ref or BDataIO source,
 *  then use TrackAt() to obtain BMediaTrack objects for each stream.
 *
 *  To write a file, construct BMediaFile with an entry_ref and a media_file_format
 *  obtained from get_next_file_format(); create tracks with CreateTrack(), call
 *  CommitHeader() once all tracks are defined, write data via BMediaTrack, and
 *  finally call CloseFile().
 */
class BMediaFile {
public:
	/** @brief Opens an existing file for read-only access.
	 *  @param ref Entry reference identifying the file to open.
	 */
								BMediaFile(const entry_ref* ref);

	/** @brief Opens a BDataIO source for read-only access.
	 *  @param source The data source to read from (e.g. a BFile).
	 */
								BMediaFile(BDataIO* source);

	/** @brief Opens an existing file for read-only access with flags.
	 *  @param ref Entry reference identifying the file.
	 *  @param flags Combination of B_MEDIA_FILE_* flag values.
	 */
								BMediaFile(const entry_ref* ref, int32 flags);

	/** @brief Opens a BDataIO source for read-only access with flags.
	 *  @param source The data source to read from.
	 *  @param flags Combination of B_MEDIA_FILE_* flag values.
	 */
								BMediaFile(BDataIO* source, int32 flags);

	/** @brief Creates or opens a file for read-write access with a specific format.
	 *  @param ref Entry reference for the file.
	 *  @param mfi The container format to use for writing.
	 *  @param flags Combination of B_MEDIA_FILE_* flag values.
	 */
								BMediaFile(const entry_ref* ref,
									const media_file_format* mfi,
									int32 flags = 0);

	/** @brief Creates or opens a BDataIO destination for read-write access.
	 *  @param destination The data sink to write to.
	 *  @param mfi The container format to use for writing.
	 *  @param flags Combination of B_MEDIA_FILE_* flag values.
	 */
								BMediaFile(BDataIO* destination,
								   const media_file_format* mfi,
								   int32 flags = 0);

	/** @brief Constructs a writer with no file yet; call SetTo() to bind it.
	 *  @param mfi The container format to use for writing.
	 *  @param flags Combination of B_MEDIA_FILE_* flag values.
	 */
								BMediaFile(const media_file_format* mfi,
								   	int32 flags = 0);

	/** @brief Opens a URL-based stream for read-only access.
	 *  @param url The URL to stream from.
	 */
								BMediaFile(const BUrl& url);

	/** @brief Opens a URL-based stream for read-only access with flags.
	 *  @param url The URL to stream from.
	 *  @param flags Combination of B_MEDIA_FILE_* flag values.
	 */
								BMediaFile(const BUrl& url, int32 flags);

	/** @brief Creates a URL-based stream for read-write access.
	 *  @param destination The destination URL.
	 *  @param mfi The container format to use.
	 *  @param flags Combination of B_MEDIA_FILE_* flag values.
	 */
								BMediaFile(const BUrl& destination,
								   const media_file_format* mfi,
								   int32 flags = 0);

	virtual						~BMediaFile();

	/** @brief Binds this object to a file entry for reading.
	 *  @param ref Entry reference identifying the file.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetTo(const entry_ref* ref);

	/** @brief Binds this object to a BDataIO source for reading.
	 *  @param destination The data source or sink.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetTo(BDataIO* destination);

	/** @brief Binds this object to a URL for streaming.
	 *  @param url The URL to open.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetTo(const BUrl& url);

	/** @brief Returns the initialization status.
	 *  @return B_OK if the file is ready, or an error code.
	 */
			status_t			InitCheck() const;

	/** @brief Returns the container format of the open file.
	 *  @param mfi On return, filled with the media_file_format.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFileFormatInfo(
									media_file_format* mfi) const;

	/** @brief Returns hierarchical meta-data about the file.
	 *  @param _data On return, a BMessage containing named metadata fields.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetMetaData(BMessage* _data) const;

	/** @brief Returns the copyright string embedded in the file, if any.
	 *  @return Pointer to the copyright string, or NULL.
	 */
			const char*			Copyright() const;

	/** @brief Returns the number of media tracks in the file.
	 *  @return Track count.
	 */
			int32				CountTracks() const;

	/** @brief Returns the BMediaTrack for the given track index.
	 *  @param index Zero-based track index.
	 *  @return Pointer to the BMediaTrack; call ReleaseTrack() when done.
	 */
			BMediaTrack*		TrackAt(int32 index);

	/** @brief Releases the resources held by a specific track object.
	 *  @param track The track to release.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReleaseTrack(BMediaTrack* track);

	/** @brief Releases all tracks; also called automatically when the file is deleted.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReleaseAllTracks();

	/** @brief Creates and adds an encoded track to the file.
	 *  @param mf The raw format of data that will be written to this track.
	 *  @param mci The codec to use for encoding.
	 *  @param flags Reserved; pass 0.
	 *  @return Pointer to the new BMediaTrack, or NULL on error.
	 */
			BMediaTrack*		CreateTrack(media_format* mf,
									const media_codec_info* mci,
									uint32 flags = 0);

	/** @brief Creates and adds a raw (unencoded) track to the file.
	 *  @param mf The raw format of data to store in this track.
	 *  @param flags Reserved; pass 0.
	 *  @return Pointer to the new BMediaTrack, or NULL on error.
	 */
			BMediaTrack*		CreateTrack(media_format* mf,
									uint32 flags = 0);

	/** @brief Embeds a copyright string in the file.
	 *  @param data The copyright text.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			AddCopyright(const char* data);

	/** @brief Adds a format-specific data chunk to the file.
	 *  @param type Format-defined chunk type code.
	 *  @param data Pointer to the chunk data.
	 *  @param size Size of the chunk in bytes.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			AddChunk(int32 type, const void* data,
									size_t size);

	/** @brief Commits the file header after all tracks have been created.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			CommitHeader();

	/** @brief Finalizes the file after all data has been written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			CloseFile();

	/** @brief Returns a copy of the parameter web for file-format control.
	 *  @param outWeb On return, a pointer to the BParameterWeb (caller owns it).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetParameterWeb(BParameterWeb** outWeb);

	/** @brief Reads a file-format parameter value.
	 *  @param id The parameter ID to query.
	 *  @param value Buffer that receives the current value.
	 *  @param size In: buffer capacity; Out: bytes written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t 			GetParameterValue(int32 id,	void* value,
									size_t* size);

	/** @brief Sets a file-format parameter value.
	 *  @param id The parameter ID to set.
	 *  @param value Pointer to the new value data.
	 *  @param size Size of the value in bytes.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetParameterValue(int32 id,	const void* value,
									size_t size);

	/** @brief Returns a view suitable for displaying this file's parameters.
	 *  @return Pointer to a BView, or NULL if unavailable.
	 */
			BView*				GetParameterView();

	/** @brief Extensibility hook for future use.
	 *  @param selector Selector code.
	 *  @param data Arbitrary data pointer.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Perform(int32 selector, void* data);

private:
	// deprecated, but for R5 compatibility
			BParameterWeb*		Web();

	// Does nothing, returns B_ERROR, for Zeta compatiblity only
			status_t			ControlFile(int32 selector, void* ioData,
									size_t size);

			BPrivate::media::MediaExtractor* fExtractor;
			int32				_reserved_BMediaFile_was_fExtractorID;
			int32				fTrackNum;
			status_t			fErr;

			BPrivate::_AddonManager* fEncoderMgr;
			BPrivate::_AddonManager* fWriterMgr;
			BPrivate::media::MediaWriter* fWriter;
			int32				fWriterID;
			media_file_format	fMFI;

			BPrivate::media::MediaStreamer* fStreamer;

			bool				fFileClosed;
			bool				fDeleteSource;
			bool				_reserved_was_fUnused[2];
			BMediaTrack**		fTrackList;

			void				_Init();
			void				_UnInit();
			void				_InitReader(BDataIO* source,
									const BUrl* url = NULL,
									int32 flags = 0);
			void				_InitWriter(BDataIO* target,
									const BUrl* url,
									const media_file_format* fileFormat,
									int32 flags);
			void				_InitStreamer(const BUrl& url,
									BDataIO** adapter);

								BMediaFile();
								BMediaFile(const BMediaFile&);
			BMediaFile&			operator=(const BMediaFile&);

			BDataIO*			fSource;

	// FBC data and virtuals

			uint32				_reserved_BMediaFile_[31];

	virtual	status_t			_Reserved_BMediaFile_0(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_1(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_2(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_3(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_4(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_5(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_6(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_7(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_8(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_9(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_10(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_11(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_12(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_13(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_14(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_15(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_16(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_17(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_18(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_19(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_20(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_21(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_22(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_23(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_24(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_25(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_26(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_27(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_28(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_29(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_30(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_31(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_32(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_33(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_34(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_35(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_36(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_37(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_38(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_39(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_40(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_41(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_42(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_43(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_44(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_45(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_46(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaFile_47(int32 arg, ...);
};

#endif
