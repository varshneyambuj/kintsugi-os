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
 * Distributed under the terms of the MIT license.
 */

/** @file MediaFormats.h
 *  @brief Defines BMediaFormats and related types for media codec and format registration.
 */

#ifndef _MEDIA_TYPES_H
#define _MEDIA_TYPES_H


#include <MediaDefs.h>

#include <Messenger.h>
#include <List.h>
#include <Locker.h>


/** @brief Identifies a specific media codec (encoder or decoder). */
struct media_codec_info {
	char	pretty_name[96];   /* eg: "SuperSqueeze Encoder by Foo Inc" */
	char	short_name[32];    /* eg: "SuperSqueeze" */

	int32	id;                /* opaque id passed to
								  BMediaFile::CreateTrack() */
	int32	sub_id;

	int32	pad[63];
};

/** @brief Iterates over encoders available for a given file format and input type.
 *  @param cookie In/out iteration cookie; initialize to 0 before first call.
 *  @param fileFormat The target container format.
 *  @param inputFormat The raw input format to be encoded.
 *  @param _outputFormat On return, the compressed output format the encoder produces.
 *  @param _codecInfo On return, information about the found encoder.
 *  @return B_OK on success, B_BAD_INDEX when no more encoders are available.
 */
status_t get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, media_format* _outputFormat,
	media_codec_info* _codecInfo);

/** @brief Iterates over encoders with full input/output format constraints; wildcards accepted.
 *  @param cookie In/out iteration cookie; initialize to 0 before first call.
 *  @param fileFormat The target container format, or NULL to ignore.
 *  @param inputFormat Desired input format (wildcards accepted).
 *  @param outputFormat Desired output format (wildcards accepted).
 *  @param _codecInfo On return, information about the found encoder.
 *  @param _acceptedInputFormat On return, the actual input format the encoder accepts.
 *  @param _acceptedOutputFormat On return, the actual output format the encoder produces.
 *  @return B_OK on success, B_BAD_INDEX when no more encoders are available.
 */
status_t get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, const media_format* outputFormat,
	media_codec_info* _codecInfo, media_format* _acceptedInputFormat,
	media_format* _acceptedOutputFormat);


/** @brief Iterates over all available encoders without format restrictions.
 *  @param cookie In/out iteration cookie; initialize to 0 before first call.
 *  @param _codecInfo On return, information about the found encoder.
 *  @return B_OK on success, B_BAD_INDEX when no more encoders are available.
 */
status_t get_next_encoder(int32* cookie, media_codec_info* _codecInfo);


/** @brief Flags controlling how does_file_accept_format() treats wildcards. */
enum media_file_accept_format_flags {
	B_MEDIA_REJECT_WILDCARDS = 0x1  /**< Treat wildcards in the format as non-matching. */
};

/** @brief Returns true if the given file format supports the specified media format.
 *  @param fileFormat The container format to test.
 *  @param format The media format to check; may be updated with specializations.
 *  @param flags Optional flags (e.g. B_MEDIA_REJECT_WILDCARDS).
 *  @return True if the format is supported by the container.
 */
bool does_file_accept_format(const media_file_format* fileFormat,
	media_format* format, uint32 flags = 0);


/** @brief A 128-bit GUID used to identify ASF format variants. */
typedef struct {
	uint8 data[16];
} GUID;


/** @brief Well-known codec identifiers for the native BeOS/Haiku format family. */
enum beos_format {
	B_BEOS_FORMAT_RAW_AUDIO = 'rawa',
	B_BEOS_FORMAT_RAW_VIDEO = 'rawv'
};


/** @brief Format description for the native BeOS/Haiku format family. */
typedef struct {
	int32 format;
} media_beos_description;


/** @brief Format description for QuickTime codec variants. */
typedef struct {
	uint32 codec;
	uint32 vendor;
} media_quicktime_description;


/** @brief Format description for AVI codec variants. */
typedef struct {
	uint32 codec;
} media_avi_description;


/** @brief Format description for AVR file format variants. */
typedef struct {
	uint32 id;
} media_avr_description;


/** @brief Format description for ASF (Windows Media) codec variants. */
typedef struct {
	GUID guid;
} media_asf_description;


/** @brief MPEG codec type identifiers. */
enum mpeg_id {
	B_MPEG_ANY = 0,
	B_MPEG_1_AUDIO_LAYER_1 = 0x101,
	B_MPEG_1_AUDIO_LAYER_2 = 0x102,
	B_MPEG_1_AUDIO_LAYER_3 = 0x103,		/* "MP3" */
	B_MPEG_1_VIDEO = 0x111,
	B_MPEG_2_AUDIO_LAYER_1 = 0x201,
	B_MPEG_2_AUDIO_LAYER_2 = 0x202,
	B_MPEG_2_AUDIO_LAYER_3 = 0x203,
	B_MPEG_2_VIDEO = 0x211,
	B_MPEG_2_5_AUDIO_LAYER_1 = 0x301,
	B_MPEG_2_5_AUDIO_LAYER_2 = 0x302,
	B_MPEG_2_5_AUDIO_LAYER_3 = 0x303,
};


/** @brief Format description for MPEG codec variants. */
typedef struct {
	uint32 id;
} media_mpeg_description;


/** @brief Format description for WAV codec variants. */
typedef struct {
	uint32 codec;
} media_wav_description;


/** @brief Format description for AIFF codec variants. */
typedef struct {
	uint32 codec;
} media_aiff_description;


/** @brief Format description for miscellaneous container/codec combinations. */
typedef struct {
	uint32 file_format;
	uint32 codec;
} media_misc_description;


/** @brief Identifies a specific codec within a particular format family.
 *
 *  The union selects the appropriate sub-description based on the family field.
 */
typedef struct _media_format_description {
								_media_format_description();
								~_media_format_description();
								_media_format_description(
									const _media_format_description& other);
	_media_format_description&	operator=(
									const _media_format_description& other);

	media_format_family family;
	uint32 _reserved_[3];
	union {
		media_beos_description beos;
		media_quicktime_description quicktime;
		media_avi_description avi;
		media_asf_description asf;
		media_mpeg_description mpeg;
		media_wav_description wav;
		media_aiff_description aiff;
		media_misc_description misc;
		media_avr_description avr;
		uint32 _reserved_[12];
	} u;
} media_format_description;


/** @brief Provides access to the system registry of media format-to-codec mappings.
 *
 *  BMediaFormats lets add-ons register new codec/format associations and
 *  applications enumerate or look up those associations.
 */
class BMediaFormats {
public:
	/** @brief Default constructor. */
								BMediaFormats();
	virtual						~BMediaFormats();

	/** @brief Returns the initialization status.
	 *  @return B_OK if the object is ready, or an error code.
	 */
			status_t			InitCheck();

	/** @brief Flags for MakeFormatFor() controlling registration behaviour. */
	// Make sure you memset() your descs to 0 before you start filling
	// them in! Else you may register some bogus value.
	enum make_format_flags {
		B_EXCLUSIVE = 0x1,		// Fail if this format has already been
								// registered.

		B_NO_MERGE = 0x2,		// Don't re-number any formats if there are
								// multiple clashing previous registrations,
								// but fail instead.

		B_SET_DEFAULT = 0x4		// Set the first format to be the default for
								// the format family (when registering more
								// than one in the same family). Only use in
								// Encoder add-ons.
	};

	/** @brief Registers one or more format descriptions and associates them with a codec.
	 *  @param descriptions Array of format descriptions to register.
	 *  @param descriptionCount Number of entries in descriptions.
	 *  @param _inOutFormat In/out: the media_format to associate; updated on return.
	 *  @param flags Combination of make_format_flags values.
	 *  @param _reserved Reserved; pass 0.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			MakeFormatFor(const media_format_description*
									descriptions, int32 descriptionCount,
									media_format* _inOutFormat,
									uint32 flags = 0, void* _reserved = 0);

	/** @brief Looks up the media_format associated with a specific format description.
	 *  @param description The format description to look up.
	 *  @param _outFormat On return, the matching media_format.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetFormatFor(const media_format_description&
									description, media_format* _outFormat);

	/** @brief Finds the format description for a given media_format and family.
	 *  @param format The media_format to look up.
	 *  @param family The target format family.
	 *  @param _outDescription On return, the format description.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			GetCodeFor(const media_format& format,
									media_format_family family,
									media_format_description* _outDescription);

	/** @brief Resets the format iterator to the beginning of the registry.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			RewindFormats();

	/** @brief Advances the format iterator and returns the next registered format.
	 *  @param _outFormat On return, the next media_format.
	 *  @param _outDescription On return, the matching format description.
	 *  @return B_OK while more formats exist, B_BAD_INDEX when done.
	 */
			status_t			GetNextFormat(media_format* _outFormat,
									media_format_description* _outDescription);

	/** @brief Acquires the registry lock; required before calling RewindFormats()/GetNextFormat().
	 *  @return True if the lock was acquired.
	 */
			bool				Lock();

	/** @brief Releases the registry lock. */
			void				Unlock();

	/** @brief Looks up a native BeOS/Haiku format by its four-character code.
	 *  @param fourcc The four-character codec code.
	 *  @param _outFormat On return, the matching media_format.
	 *  @param type Optional media_type filter.
	 *  @return B_OK on success, or an error code.
	 */
	static	status_t			GetBeOSFormatFor(uint32 fourcc,
									media_format* _outFormat,
									media_type type = B_MEDIA_UNKNOWN_TYPE);

	/** @brief Looks up an AVI format by its four-character codec code.
	 *  @param fourcc The four-character codec code.
	 *  @param _outFormat On return, the matching media_format.
	 *  @param type Optional media_type filter.
	 *  @return B_OK on success, or an error code.
	 */
	static	status_t			GetAVIFormatFor(uint32 fourcc,
									media_format* _outFormat,
									media_type type = B_MEDIA_UNKNOWN_TYPE);

	/** @brief Looks up a QuickTime format by its vendor and four-character codec code.
	 *  @param vendor The vendor code.
	 *  @param fourcc The four-character codec code.
	 *  @param _outFormat On return, the matching media_format.
	 *  @param type Optional media_type filter.
	 *  @return B_OK on success, or an error code.
	 */
	static	status_t			GetQuicktimeFormatFor(uint32 vendor,
									uint32 fourcc, media_format* _outFormat,
									media_type type = B_MEDIA_UNKNOWN_TYPE);

	// Deprecated:
	/** @brief Deprecated single-description registration; use the multi-description overload.
	 *  @param description The single format description to register.
	 *  @param inFormat The input media_format hint.
	 *  @param _outFormat On return, the registered media_format.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			MakeFormatFor(const media_format_description&
									description, const media_format& inFormat,
									media_format* _outFormat);

private:
			int32				fIteratorIndex;

			uint32				_reserved[30];
};


/** @brief Compares two format descriptions for equality. */
bool operator==(const media_format_description& a,
	const media_format_description& b);

/** @brief Provides a total order on format descriptions for sorted containers. */
bool operator<(const media_format_description& a,
	const media_format_description& b);


/** @brief Compares two GUID values for equality. */
bool operator==(const GUID& a, const GUID& b);

/** @brief Provides a total order on GUID values for sorted containers. */
bool operator<(const GUID& a, const GUID& b);


#endif	// _MEDIA_TYPES_H
