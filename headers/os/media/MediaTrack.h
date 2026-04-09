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
 * Copyright 2002-2010, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/** @file MediaTrack.h
 *  @brief Defines BMediaTrack for reading and writing individual streams within a media file.
 */

#ifndef _MEDIA_TRACK_H
#define _MEDIA_TRACK_H


#include <MediaFormats.h>


namespace BPrivate { namespace media {
	class Decoder;
	class Encoder;
	class MediaExtractor;
	class MediaWriter;
} }

class BMessage;
class BView;
class BParameterWeb;


/** @brief Flags for SeekToTime() and SeekToFrame() controlling seek direction. */
enum media_seek_type {
	B_MEDIA_SEEK_CLOSEST_FORWARD	= 1, /**< Seek to nearest key frame after the target. */
	B_MEDIA_SEEK_CLOSEST_BACKWARD	= 2, /**< Seek to nearest key frame before the target. */
	B_MEDIA_SEEK_DIRECTION_MASK		= 3  /**< Mask for extracting direction bits. */
};


/** @brief Provides access to one audio or video stream within a BMediaFile.
 *
 *  BMediaTrack objects are obtained from BMediaFile::TrackAt() (for reading)
 *  or BMediaFile::CreateTrack() (for writing).  Access is either read-only or
 *  write-only unless B_MEDIA_REPLACE_MODE was used.
 */
class BMediaTrack {
protected:
	// Use BMediaFile::ReleaseTrack() instead -- or it will go away
	// on its own when the MediaFile is deleted.
	virtual						~BMediaTrack();

public:

	/** @brief Returns the initialization status.
	 *  @return B_OK if the track is ready, or an error code.
	 */
			status_t			InitCheck() const;

	/** @brief Returns information about the codec in use for this track.
	 *  @param _codecInfo On return, filled with the media_codec_info.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetCodecInfo(
									media_codec_info* _codecInfo) const;

	/** @brief Returns the native compressed format of this track.
	 *  @param _format On return, the encoded media_format.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			EncodedFormat(media_format* _format) const;

	/** @brief Negotiates the raw output format for decoding.
	 *  @param _format In/out: desired format; set to the format actually used.
	 *  @param flags Reserved; pass 0.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			DecodedFormat(media_format* _format,
									uint32 flags = 0);

	/** @brief Returns the total number of frames in this track.
	 *  @return Frame count.
	 */
			int64				CountFrames() const;

	/** @brief Returns the total duration of this track in microseconds.
	 *  @return Duration in microseconds.
	 */
			bigtime_t			Duration() const;

	/** @brief Returns hierarchical meta-data about this track.
	 *  @param _data On return, a BMessage containing named metadata fields.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetMetaData(BMessage* _data) const;

	/** @brief Returns the current read position as a frame index.
	 *  @return Current frame index.
	 */
			int64				CurrentFrame() const;

	/** @brief Returns the current read position in microseconds.
	 *  @return Current performance time in microseconds.
	 */
			bigtime_t			CurrentTime() const;

	/** @brief Decodes the next frame(s) into the supplied buffer.
	 *  @param buffer Destination buffer for decoded data.
	 *  @param _frameCount On return, the number of frames decoded.
	 *  @param header If non-NULL, receives the media_header for the frame.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReadFrames(void* buffer, int64* _frameCount,
									media_header* header = NULL);

	/** @brief Decodes the next frame(s) with decode control parameters.
	 *  @param buffer Destination buffer for decoded data.
	 *  @param _frameCount On return, the number of frames decoded.
	 *  @param header On return, the media_header for the frame.
	 *  @param info Decode control parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReadFrames(void* buffer, int64* _frameCount,
									media_header* header,
									media_decode_info* info);

	/** @brief Replaces frames at the current position (B_MEDIA_REPLACE_MODE only).
	 *  @param buffer Pointer to the replacement frame data.
	 *  @param _frameCount On return, the number of frames replaced.
	 *  @param header The media_header for the replacement data.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReplaceFrames(const void* buffer,
									int64* _frameCount,
									const media_header* header);

	/** @brief Seeks to a position in the track expressed as a time in microseconds.
	 *  @param _time In/out: target time; updated to the actual seek position.
	 *  @param flags Seek direction flags (B_MEDIA_SEEK_CLOSEST_*).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SeekToTime(bigtime_t* _time, int32 flags = 0);

	/** @brief Seeks to a position in the track expressed as a frame index.
	 *  @param _frame In/out: target frame; updated to the actual seek position.
	 *  @param flags Seek direction flags (B_MEDIA_SEEK_CLOSEST_*).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SeekToFrame(int64* _frame, int32 flags = 0);

	/** @brief Finds the nearest key frame at or around the given time.
	 *  @param _time In/out: target time; updated to the key frame time.
	 *  @param flags Seek direction flags.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			FindKeyFrameForTime(bigtime_t* _time,
									int32 flags = 0) const;

	/** @brief Finds the nearest key frame at or around the given frame index.
	 *  @param _frame In/out: target frame; updated to the key frame index.
	 *  @param flags Seek direction flags.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			FindKeyFrameForFrame(int64* _frame,
									int32 flags = 0) const;

	/** @brief Reads the next raw encoded chunk from the track without decoding.
	 *  @param _buffer On return, points to the encoded chunk data.
	 *  @param _size On return, the size of the chunk in bytes.
	 *  @param _header If non-NULL, receives the media_header.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			ReadChunk(char** _buffer, int32* _size,
									media_header* _header = NULL);

	/** @brief Embeds a copyright string in this track.
	 *  @param copyright The copyright text.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			AddCopyright(const char* copyright);

	/** @brief Adds codec-specific track metadata.
	 *  @param code Format-defined metadata code.
	 *  @param data Pointer to the metadata value.
	 *  @param size Size of the value in bytes.
	 *  @param flags Reserved; pass 0.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			AddTrackInfo(uint32 code, const void* data,
									size_t size, uint32 flags = 0);

	/** @brief Encodes and writes frames to the track.
	 *  @param data Pointer to the raw input data.
	 *  @param frameCount Number of frames to write.
	 *  @param flags B_MEDIA_KEY_FRAME if this is a key frame.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			WriteFrames(const void* data, int32 frameCount,
									int32 flags = 0);

	/** @brief Encodes and writes frames with encode control parameters.
	 *  @param data Pointer to the raw input data.
	 *  @param frameCount Number of frames to write.
	 *  @param info Encoding control parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			WriteFrames(const void* data, int64 frameCount,
									media_encode_info* info);

	/** @brief Writes a raw encoded chunk to the track.
	 *  @param data Pointer to the encoded data.
	 *  @param size Size of the data in bytes.
	 *  @param flags B_MEDIA_KEY_FRAME if this is a key frame.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			WriteChunk(const void* data, size_t size,
									uint32 flags = 0);

	/** @brief Writes a raw encoded chunk with encode control parameters.
	 *  @param data Pointer to the encoded data.
	 *  @param size Size of the data in bytes.
	 *  @param info Encoding metadata for this chunk.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			WriteChunk(const void* data, size_t size,
									media_encode_info* info);

	/** @brief Flushes all buffered encoded data to the file.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			Flush();

	/** @brief Returns a copy of the encoder parameter web for this track.
	 *  @param _web On return, a pointer to the BParameterWeb (caller owns it).
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetParameterWeb(BParameterWeb** _web);

	/** @brief Reads an encoder parameter value.
	 *  @param id The parameter ID to query.
	 *  @param value Buffer that receives the current value.
	 *  @param size In: buffer capacity; Out: bytes written.
	 *  @return B_OK on success, or an error code.
	 */
			status_t 			GetParameterValue(int32 id, void* value,
									size_t* size);

	/** @brief Sets an encoder parameter value.
	 *  @param id The parameter ID to set.
	 *  @param value Pointer to the new value.
	 *  @param size Size of the value in bytes.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			SetParameterValue(int32 id, const void* value,
									size_t size);

	/** @brief Returns a view for displaying encoder parameters.
	 *  @return Pointer to a BView, or NULL if unavailable.
	 */
			BView*				GetParameterView();

	/** @brief Returns the encoding quality as a normalized value.
	 *  @param _quality On return, quality in [0.0, 1.0].
	 *  @return B_OK on success, B_ERROR if not supported.
	 */
			status_t			GetQuality(float* _quality);

	/** @brief Sets the encoding quality.
	 *  @param quality Desired quality in [0.0, 1.0].
	 *  @return B_OK on success, B_ERROR if not supported.
	 */
			status_t			SetQuality(float quality);

	/** @brief Retrieves the current encoding parameters.
	 *  @param parameters On return, the current encode_parameters.
	 *  @return B_OK on success, or an error code.
	 */
			status_t 			GetEncodeParameters(
									encode_parameters* parameters) const;

	/** @brief Applies new encoding parameters.
	 *  @param parameters The encode_parameters to apply.
	 *  @return B_OK on success, or an error code.
	 */
			status_t 			SetEncodeParameters(
									encode_parameters* parameters);

	/** @brief Extensibility hook for future or platform-specific use.
	 *  @param code Selector code.
	 *  @param data Arbitrary data pointer.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Perform(int32 code, void* data);

private:
	friend class BMediaFile;

	// deprecated, but for BeOS R5 compatibility
			BParameterWeb*	Web();

	// Does nothing, returns B_ERROR, for Zeta compatiblity only
			status_t			ControlCodec(int32 selector, void* _inOutData,
									size_t size);

	// For read-only access to a BMediaTrack
								BMediaTrack(
									BPrivate::media::MediaExtractor* extractor,
									int32 streamIndex);

	// For write-only access to a BMediaTrack
								BMediaTrack(
									BPrivate::media::MediaWriter* writer,
									int32 streamIndex, media_format* format,
									const media_codec_info* codecInfo);

			void				SetupWorkaround();
			bool				SetupFormatTranslation(
									const media_format& from,
									media_format* _to);

private:
			status_t			fInitStatus;
			BPrivate::media::Decoder* fDecoder;
			BPrivate::media::Decoder* fRawDecoder;
			BPrivate::media::MediaExtractor* fExtractor;

			int32				fStream;
			int64				fCurrentFrame;
			bigtime_t			fCurrentTime;

			media_codec_info	fCodecInfo;

			BPrivate::media::Encoder* fEncoder;
			int32				fEncoderID;
			BPrivate::media::MediaWriter* fWriter;
			media_format		fFormat;

			uint32				fWorkaroundFlags;

protected:
			int32				EncoderID() { return fEncoderID; };

private:
								BMediaTrack();
								BMediaTrack(const BMediaTrack&);
			BMediaTrack&		operator=(const BMediaTrack&);


			double				_FrameRate() const;

	// FBC data and virtuals
			uint32				_reserved_BMediaTrack_[31];

	virtual	status_t			_Reserved_BMediaTrack_0(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_1(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_2(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_3(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_4(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_5(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_6(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_7(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_8(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_9(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_10(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_11(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_12(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_13(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_14(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_15(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_16(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_17(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_18(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_19(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_20(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_21(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_22(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_23(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_24(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_25(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_26(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_27(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_28(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_29(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_30(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_31(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_32(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_33(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_34(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_35(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_36(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_37(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_38(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_39(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_40(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_41(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_42(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_43(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_44(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_45(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_46(int32 arg, ...);
	virtual	status_t			_Reserved_BMediaTrack_47(int32 arg, ...);
};

#endif // _MEDIA_TRACK_H
