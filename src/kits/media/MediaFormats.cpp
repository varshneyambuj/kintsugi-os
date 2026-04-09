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
 *   Copyright 2004-2009, The Haiku Project. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Axel Dörfler
 *       Marcus Overhagen
 */

/** @file MediaFormats.cpp
 *  @brief Implements BMediaFormats, meta_format helpers, and encoder/file-format
 *         iteration functions. */


#include "AddOnManager.h"
#include "DataExchange.h"
#include "FormatManager.h"
#include "MetaFormat.h"
#include "MediaDebug.h"

#include <MediaFormats.h>
#include <ObjectList.h>
#include <Message.h>
#include <Autolock.h>

#include <string.h>

using namespace BPrivate::media;


static BLocker sLock;
static BObjectList<meta_format> sFormats;
static bigtime_t sLastFormatsUpdate;


/** @brief Iterates through available encoders that match the given input format,
 *         optionally restricted to a specific file format family.
 *  @param cookie        Iterator cookie; start at 0 and increment between calls.
 *  @param fileFormat    Optional file format constraint (may be \c NULL).
 *  @param inputFormat   The source media format to match against.
 *  @param _outputFormat Receives the encoder's output format on success.
 *  @param _codecInfo    Receives the matching codec information.
 *  @return B_OK while encoders remain, B_BAD_INDEX when exhausted, or an error. */
status_t
get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, media_format* _outputFormat,
	media_codec_info* _codecInfo)
{
	// TODO: If fileFormat is provided (existing apps also pass NULL),
	// we could at least check fileFormat->capabilities against
	// outputFormat->type without even contacting the server.

	if (cookie == NULL || inputFormat == NULL || _codecInfo == NULL)
		return B_BAD_VALUE;

	while (true) {
		media_codec_info candidateCodecInfo;
		media_format_family candidateFormatFamily;
		media_format candidateInputFormat;
		media_format candidateOutputFormat;

		status_t ret = AddOnManager::GetInstance()->GetCodecInfo(
			&candidateCodecInfo, &candidateFormatFamily,
			&candidateInputFormat, &candidateOutputFormat, *cookie);

		if (ret != B_OK)
			return ret;

		*cookie = *cookie + 1;

		if (fileFormat != NULL && candidateFormatFamily != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != candidateFormatFamily) {
			continue;
		}

		if (!candidateInputFormat.Matches(inputFormat))
			continue;

		if (_outputFormat != NULL)
			*_outputFormat = candidateOutputFormat;

		*_codecInfo = candidateCodecInfo;
		break;
	}

	return B_OK;
}


/** @brief Iterates through available encoders that match both input and output
 *         formats, optionally restricted to a file format family.
 *  @param cookie                 Iterator cookie; start at 0.
 *  @param fileFormat             Optional file format constraint (may be \c NULL).
 *  @param inputFormat            Required input format to match.
 *  @param outputFormat           Required output format to match.
 *  @param _codecInfo             Receives the matching codec information.
 *  @param _acceptedInputFormat   Receives the accepted input format on success.
 *  @param _acceptedOutputFormat  Receives the accepted output format on success.
 *  @return B_OK while encoders remain, B_BAD_INDEX when exhausted, or an error. */
status_t
get_next_encoder(int32* cookie, const media_file_format* fileFormat,
	const media_format* inputFormat, const media_format* outputFormat,
	media_codec_info* _codecInfo, media_format* _acceptedInputFormat,
	media_format* _acceptedOutputFormat)
{
	// TODO: If fileFormat is provided (existing apps also pass NULL),
	// we could at least check fileFormat->capabilities against
	// outputFormat->type without even contacting the server.

	if (cookie == NULL || inputFormat == NULL || outputFormat == NULL
		|| _codecInfo == NULL) {
		return B_BAD_VALUE;
	}

	while (true) {
		media_codec_info candidateCodecInfo;
		media_format_family candidateFormatFamily;
		media_format candidateInputFormat;
		media_format candidateOutputFormat;

		status_t ret = AddOnManager::GetInstance()->GetCodecInfo(
			&candidateCodecInfo, &candidateFormatFamily, &candidateInputFormat,
			&candidateOutputFormat, *cookie);

		if (ret != B_OK)
			return ret;

		*cookie = *cookie + 1;

		if (fileFormat != NULL && candidateFormatFamily != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != B_ANY_FORMAT_FAMILY
			&& fileFormat->family != candidateFormatFamily) {
			continue;
		}

		if (!candidateInputFormat.Matches(inputFormat)
			|| !candidateOutputFormat.Matches(outputFormat)) {
			continue;
		}

		// TODO: These formats are currently way too generic. For example,
		// an encoder may want to adjust video width to a multiple of 16,
		// or overwrite the intput and or output color space. To make this
		// possible, we actually have to instantiate an Encoder here and
		// ask it to specifiy the format.
		if (_acceptedInputFormat != NULL)
			*_acceptedInputFormat = candidateInputFormat;
		if (_acceptedOutputFormat != NULL)
			*_acceptedOutputFormat = candidateOutputFormat;

		*_codecInfo = candidateCodecInfo;
		break;
	}

	return B_OK;
}


/** @brief Simplified encoder iterator that returns only codec information.
 *  @param cookie      Iterator cookie; start at 0.
 *  @param _codecInfo  Receives the codec information for the current encoder.
 *  @return B_OK while encoders remain, or B_BAD_INDEX when exhausted. */
status_t
get_next_encoder(int32* cookie, media_codec_info* _codecInfo)
{
	if (cookie == NULL || _codecInfo == NULL)
		return B_BAD_VALUE;

	media_format_family formatFamily;
	media_format inputFormat;
	media_format outputFormat;

	status_t ret = AddOnManager::GetInstance()->GetCodecInfo(_codecInfo,
		&formatFamily, &inputFormat, &outputFormat, *cookie);
	if (ret != B_OK)
		return ret;

	*cookie = *cookie + 1;

	return B_OK;
}


/** @brief Returns whether the given file format accepts the given media format.
 *  @param _fileFormat  The file format to test.
 *  @param format       The media format to test against.
 *  @param flags        Reserved; pass 0.
 *  @return \c true if compatible (currently unimplemented, always returns false). */
bool
does_file_accept_format(const media_file_format* _fileFormat,
	media_format* format, uint32 flags)
{
	UNIMPLEMENTED();
	return false;
}


//	#pragma mark -


/** @brief Default constructor; zero-fills the description union. */
_media_format_description::_media_format_description()
{
	memset(this, 0, sizeof(*this));
}


/** @brief Destructor. */
_media_format_description::~_media_format_description()
{
}


/** @brief Copy constructor; makes a binary copy of the description.
 *  @param other  The source description to copy. */
_media_format_description::_media_format_description(
	const _media_format_description& other)
{
	memcpy(this, &other, sizeof(*this));
}


/** @brief Assignment operator; makes a binary copy of the description.
 *  @param other  The source description to copy.
 *  @return Reference to \c *this. */
_media_format_description&
_media_format_description::operator=(const _media_format_description& other)
{
	memcpy(this, &other, sizeof(*this));
	return *this;
}


/** @brief Equality operator for media_format_description; compares family and
 *         family-specific codec/vendor fields.
 *  @param a  Left-hand side.
 *  @param b  Right-hand side.
 *  @return \c true if both descriptions identify the same codec. */
bool
operator==(const media_format_description& a,
	const media_format_description& b)
{
	if (a.family != b.family)
		return false;

	switch (a.family) {
		case B_BEOS_FORMAT_FAMILY:
			return a.u.beos.format == b.u.beos.format;
		case B_QUICKTIME_FORMAT_FAMILY:
			return a.u.quicktime.codec == b.u.quicktime.codec
				&& a.u.quicktime.vendor == b.u.quicktime.vendor;
		case B_AVI_FORMAT_FAMILY:
			return a.u.avi.codec == b.u.avi.codec;
		case B_ASF_FORMAT_FAMILY:
			return a.u.asf.guid == b.u.asf.guid;
		case B_MPEG_FORMAT_FAMILY:
			return a.u.mpeg.id == b.u.mpeg.id;
		case B_WAV_FORMAT_FAMILY:
			return a.u.wav.codec == b.u.wav.codec;
		case B_AIFF_FORMAT_FAMILY:
			return a.u.aiff.codec == b.u.aiff.codec;
		case B_AVR_FORMAT_FAMILY:
			return a.u.avr.id == b.u.avr.id;
		case B_MISC_FORMAT_FAMILY:
			return a.u.misc.file_format == b.u.misc.file_format
				&& a.u.misc.codec == b.u.misc.codec;

		default:
			return false;
	}
}


/** @brief Less-than operator for media_format_description; enables use in sorted
 *         containers.
 *  @param a  Left-hand side.
 *  @param b  Right-hand side.
 *  @return \c true if \a a sorts before \a b. */
bool
operator<(const media_format_description& a, const media_format_description& b)
{
	if (a.family != b.family)
		return a.family < b.family;

	switch (a.family) {
		case B_BEOS_FORMAT_FAMILY:
			return a.u.beos.format < b.u.beos.format;
		case B_QUICKTIME_FORMAT_FAMILY:
			if (a.u.quicktime.vendor == b.u.quicktime.vendor)
				return a.u.quicktime.codec < b.u.quicktime.codec;
			return a.u.quicktime.vendor < b.u.quicktime.vendor;
		case B_AVI_FORMAT_FAMILY:
			return a.u.avi.codec < b.u.avi.codec;
		case B_ASF_FORMAT_FAMILY:
			return a.u.asf.guid < b.u.asf.guid;
		case B_MPEG_FORMAT_FAMILY:
			return a.u.mpeg.id < b.u.mpeg.id;
		case B_WAV_FORMAT_FAMILY:
			return a.u.wav.codec < b.u.wav.codec;
		case B_AIFF_FORMAT_FAMILY:
			return a.u.aiff.codec < b.u.aiff.codec;
		case B_AVR_FORMAT_FAMILY:
			return a.u.avr.id < b.u.avr.id;
		case B_MISC_FORMAT_FAMILY:
			if (a.u.misc.file_format == b.u.misc.file_format)
				return a.u.misc.codec < b.u.misc.codec;
			return a.u.misc.file_format < b.u.misc.file_format;

		default:
			return true;
	}
}


/** @brief Equality operator for GUID values (byte-wise comparison).
 *  @param a  Left-hand side.
 *  @param b  Right-hand side.
 *  @return \c true if the GUIDs are identical. */
bool
operator==(const GUID& a, const GUID& b)
{
	return memcmp(&a, &b, sizeof(a)) == 0;
}


/** @brief Less-than operator for GUID values (lexicographic byte comparison).
 *  @param a  Left-hand side.
 *  @param b  Right-hand side.
 *  @return \c true if \a a is less than \a b. */
bool
operator<(const GUID& a, const GUID& b)
{
	return memcmp(&a, &b, sizeof(a)) < 0;
}


//	#pragma mark -
//
//	Some (meta) formats supply functions


/** @brief Default constructor; zero-initialises the meta_format. */
meta_format::meta_format()
	:
	id(0)
{
}



/** @brief Constructs a meta_format from a description, a format, and a codec ID.
 *  @param description  The format description identifying this codec.
 *  @param format       The associated media_format.
 *  @param id           The codec ID assigned by FormatManager. */
meta_format::meta_format(const media_format_description& description,
	const media_format& format, int32 id)
	:
	description(description),
	format(format),
	id(id)
{
}


/** @brief Constructs a meta_format from a description alone (no format or ID).
 *  @param description  The format description. */
meta_format::meta_format(const media_format_description& description)
	:
	description(description),
	id(0)
{
}


/** @brief Copy constructor.
 *  @param other  The meta_format to copy. */
meta_format::meta_format(const meta_format& other)
	:
	description(other.description),
	format(other.format)
{
}


/** @brief Returns whether this meta_format's format matches \a otherFormat for
 *         the given family.
 *  @param otherFormat  The media_format to compare against.
 *  @param family       The format family that must also match the description.
 *  @return \c true if both the family and the format match. */
bool
meta_format::Matches(const media_format& otherFormat,
	media_format_family family)
{
	if (family != description.family)
		return false;

	return format.Matches(&otherFormat);
}


/** @brief Comparison function by description alone, for use with BObjectList
 *         binary search.
 *  @param a  First meta_format.
 *  @param b  Second meta_format.
 *  @return Negative, 0, or positive depending on sort order. */
int
meta_format::CompareDescriptions(const meta_format* a, const meta_format* b)
{
	if (a->description == b->description)
		return 0;

	if (a->description < b->description)
		return -1;

	return 1;
}


/** @brief Full comparison function (description then ID) for BObjectList sorting.
 *  @param a  First meta_format.
 *  @param b  Second meta_format.
 *  @return Negative, 0, or positive depending on sort order. */
int
meta_format::Compare(const meta_format* a, const meta_format* b)
{
	int compare = CompareDescriptions(a, b);
	if (compare != 0)
		return compare;

	return a->id - b->id;
}


/** @brief Updates the team-global format list from FormatManager if it has changed
 *         since the last call.
 *
 *  We share one global list for all BMediaFormats in the team - since the format
 *  data can change at any time, we have to update the list to ensure that we are
 *  working on the latest data set.  The list is always sorted by description.
 *  The formats lock has to be held when you call this function.
 *
 *  @return B_OK if the list is current, or an error code on failure. */
static status_t
update_media_formats()
{
	if (!sLock.IsLocked())
		return B_NOT_ALLOWED;

	// We want the add-ons to register themselves with the format manager, so
	// the list is up to date.
	AddOnManager::GetInstance()->RegisterAddOns();

	BMessage reply;
	FormatManager::GetInstance()->GetFormats(sLastFormatsUpdate, reply);

	// do we need an update at all?
	bool needUpdate;
	if (reply.FindBool("need_update", &needUpdate) < B_OK)
		return B_ERROR;
	if (!needUpdate)
		return B_OK;

	// update timestamp and check if the message is okay
	type_code code;
	int32 count;
	if (reply.FindInt64("timestamp", &sLastFormatsUpdate) < B_OK
		|| reply.GetInfo("formats", &code, &count) < B_OK)
		return B_ERROR;

	// overwrite already existing formats

	int32 index = 0;
	for (; index < sFormats.CountItems() && index < count; index++) {
		meta_format* item = sFormats.ItemAt(index);

		const meta_format* newItem;
		ssize_t size;
		if (reply.FindData("formats", MEDIA_META_FORMAT_TYPE, index,
				(const void**)&newItem, &size) == B_OK)
			*item = *newItem;
	}

	// allocate additional formats

	for (; index < count; index++) {
		const meta_format* newItem;
		ssize_t size;
		if (reply.FindData("formats", MEDIA_META_FORMAT_TYPE, index,
				(const void**)&newItem, &size) == B_OK)
			sFormats.AddItem(new meta_format(*newItem));
	}

	// remove no longer used formats

	while (count < sFormats.CountItems())
		delete sFormats.RemoveItemAt(count);

	return B_OK;
}


//	#pragma mark -


/** @brief Constructs a BMediaFormats iterator; the iterator index starts at 0. */
BMediaFormats::BMediaFormats()
	:
	fIteratorIndex(0)
{
}


/** @brief Destructor. */
BMediaFormats::~BMediaFormats()
{
}


/** @brief Returns the initialisation status of the internal lock.
 *  @return B_OK if the lock is usable. */
status_t
BMediaFormats::InitCheck()
{
	return sLock.InitCheck();
}


/** @brief Finds the format description for a given media_format and family.
 *  @param format        The media_format to look up.
 *  @param family        The format family to match.
 *  @param _description  Receives the matching description on success.
 *  @return B_OK on success, B_MEDIA_BAD_FORMAT if no match is found. */
status_t
BMediaFormats::GetCodeFor(const media_format& format,
	media_format_family family,
	media_format_description* _description)
{
	BAutolock locker(sLock);

	status_t status = update_media_formats();
	if (status < B_OK)
		return status;

	// search for a matching format

	for (int32 index = sFormats.CountItems(); index-- > 0;) {
		meta_format* metaFormat = sFormats.ItemAt(index);

		if (metaFormat->Matches(format, family)) {
			*_description = metaFormat->description;
			return B_OK;
		}
	}

	return B_MEDIA_BAD_FORMAT;
}


/** @brief Finds the media_format associated with a given format description.
 *  @param description  The format description to look up.
 *  @param _format      Receives the associated media_format on success.
 *  @return B_OK on success, B_MEDIA_BAD_FORMAT if not found. */
status_t
BMediaFormats::GetFormatFor(const media_format_description& description,
	media_format* _format)
{
	BAutolock locker(sLock);

	status_t status = update_media_formats();
	if (status < B_OK) {
		ERROR("BMediaFormats: updating formats from server failed: %s!\n",
			strerror(status));
		return status;
	}
	TRACE("search for description family = %d, a = 0x%"
		B_PRId32 "x, b = 0x%" B_PRId32 "x\n",
		description.family, description.u.misc.file_format,
		description.u.misc.codec);

	// search for a matching format description

	meta_format other(description);
	const meta_format* metaFormat = sFormats.BinarySearch(other,
		meta_format::CompareDescriptions);
	TRACE("meta format == %p\n", metaFormat);
	if (metaFormat == NULL) {
		_format->Clear(); // clear to widlcard
		return B_MEDIA_BAD_FORMAT;
	}

	// found it!
	*_format = metaFormat->format;
	return B_OK;
}


/** @brief Looks up a BeOS-native format by its numeric format ID and optional
 *         media type.
 *  @param format   The BeOS format ID (e.g. from media_raw_audio_format).
 *  @param _format  Receives the associated media_format.
 *  @param type     If not B_MEDIA_UNKNOWN_TYPE, must match the found format's type.
 *  @return B_OK on success, or an error code on failure. */
status_t
BMediaFormats::GetBeOSFormatFor(uint32 format,
	media_format* _format, media_type type)
{
	BMediaFormats formats;

	media_format_description description;
	description.family = B_BEOS_FORMAT_FAMILY;
	description.u.beos.format = format;

	status_t status = formats.GetFormatFor(description, _format);
	if (status < B_OK)
		return status;

	if (type != B_MEDIA_UNKNOWN_TYPE && type != _format->type)
		return B_BAD_TYPE;

	return B_OK;
}


/** @brief Looks up an AVI format by codec ID and optional media type.
 *  @param codec    The AVI four-character codec code.
 *  @param _format  Receives the associated media_format.
 *  @param type     If not B_MEDIA_UNKNOWN_TYPE, must match the found format's type.
 *  @return B_OK on success, or an error code on failure. */
status_t
BMediaFormats::GetAVIFormatFor(uint32 codec,
	media_format* _format, media_type type)
{
	UNIMPLEMENTED();
	BMediaFormats formats;

	media_format_description description;
	description.family = B_AVI_FORMAT_FAMILY;
	description.u.avi.codec = codec;

	status_t status = formats.GetFormatFor(description, _format);
	if (status < B_OK)
		return status;

	if (type != B_MEDIA_UNKNOWN_TYPE && type != _format->type)
		return B_BAD_TYPE;

	return B_OK;
}


/** @brief Looks up a QuickTime format by vendor and codec IDs, with optional type
 *         validation.
 *  @param vendor   The QuickTime vendor identifier.
 *  @param codec    The QuickTime codec identifier.
 *  @param _format  Receives the associated media_format.
 *  @param type     If not B_MEDIA_UNKNOWN_TYPE, must match the found format's type.
 *  @return B_OK on success, or an error code on failure. */
status_t
BMediaFormats::GetQuicktimeFormatFor(uint32 vendor, uint32 codec,
	media_format* _format, media_type type)
{
	BMediaFormats formats;

	media_format_description description;
	description.family = B_QUICKTIME_FORMAT_FAMILY;
	description.u.quicktime.vendor = vendor;
	description.u.quicktime.codec = codec;

	status_t status = formats.GetFormatFor(description, _format);
	if (status < B_OK)
		return status;

	if (type != B_MEDIA_UNKNOWN_TYPE && type != _format->type)
		return B_BAD_TYPE;

	return B_OK;
}


/** @brief Resets the iterator to the beginning of the format list.
 *
 *  The sLock must be held by the caller (via Lock()) before calling this.
 *
 *  @return B_OK on success, B_NOT_ALLOWED if the lock is not held. */
status_t
BMediaFormats::RewindFormats()
{
	if (!sLock.IsLocked() || sLock.LockingThread() != find_thread(NULL)) {
		// TODO: Shouldn't we simply drop into the debugger in this case?
		return B_NOT_ALLOWED;
	}

	fIteratorIndex = 0;
	return B_OK;
}


/** @brief Returns the next registered format and its description.
 *
 *  The sLock must be held by the caller (via Lock()) before calling this.
 *  The first call triggers a format-list refresh if needed.
 *
 *  @param _format       Receives the media_format for the current entry.
 *  @param _description  Receives the description for the current entry.
 *  @return B_OK while formats remain, B_BAD_INDEX when the list is exhausted. */
status_t
BMediaFormats::GetNextFormat(media_format* _format,
	media_format_description* _description)
{
	if (!sLock.IsLocked() || sLock.LockingThread() != find_thread(NULL)) {
		// TODO: Shouldn't we simply drop into the debugger in this case?
		return B_NOT_ALLOWED;
	}

	if (fIteratorIndex == 0) {
		// This is the first call, so let's make sure we have current data to
		// operate on.
		status_t status = update_media_formats();
		if (status < B_OK)
			return status;
	}

	meta_format* format = sFormats.ItemAt(fIteratorIndex++);
	if (format == NULL)
		return B_BAD_INDEX;

	return B_OK;
}


/** @brief Acquires the team-global formats lock; must be called before iterating.
 *  @return \c true if the lock was acquired. */
bool
BMediaFormats::Lock()
{
	return sLock.Lock();
}


/** @brief Releases the team-global formats lock. */
void
BMediaFormats::Unlock()
{
	sLock.Unlock();
}


/** @brief Registers an array of format descriptions with an associated format,
 *         assigning codec IDs via FormatManager.
 *  @param descriptions      Array of descriptions to register.
 *  @param descriptionCount  Number of entries in \a descriptions.
 *  @param format            The media_format to associate (modified in place).
 *  @param flags             Registration flags.
 *  @param _reserved         Reserved; pass \c NULL.
 *  @return B_OK on success, or an error code on failure. */
status_t
BMediaFormats::MakeFormatFor(const media_format_description* descriptions,
	int32 descriptionCount, media_format* format, uint32 flags,
	void* _reserved)
{
	status_t status = FormatManager::GetInstance()->MakeFormatFor(descriptions,
		descriptionCount, *format, flags, _reserved);

	return status;
}


// #pragma mark - deprecated API


/** @brief Deprecated single-description variant of MakeFormatFor().
 *  @param description  The format description to register.
 *  @param inFormat     The input format.
 *  @param _outFormat   Receives a copy of \a inFormat with codec ID filled in.
 *  @return B_OK on success, or an error code on failure. */
status_t
BMediaFormats::MakeFormatFor(const media_format_description& description,
	const media_format& inFormat, media_format* _outFormat)
{
	*_outFormat = inFormat;
	return MakeFormatFor(&description, 1, _outFormat);
}
