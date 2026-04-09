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
 *   Copyright 2004-2010, Haiku. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Marcus Overhagen
 *       Axel Dörfler
 *       Stephan Aßmus <superstippi@gmx.de>
 */

/** @file AddOnManager.cpp
 *  @brief Implements AddOnManager, which discovers, registers, and indexes all media plug-in add-ons
 *         (readers, writers, decoders, encoders, and streamers) found in the system plug-in directories. */


#include "AddOnManager.h"

#include <stdio.h>
#include <string.h>

#include <Architecture.h>
#include <AutoDeleter.h>
#include <Autolock.h>
#include <Directory.h>
#include <Entry.h>
#include <FindDirectory.h>
#include <image.h>
#include <Path.h>

#include "MediaDebug.h"

#include "FormatManager.h"
#include "MetaFormat.h"


namespace BPrivate {
namespace media {


//	#pragma mark - ImageLoader

/*!	The ImageLoader class is a convenience class to temporarily load
	an image file, and unload it on deconstruction automatically.
*/
/** @brief RAII wrapper that loads an add-on image on construction and unloads it on destruction. */
class ImageLoader {
public:
	/** @brief Loads the add-on image at @p path.
	 *  @param path  Filesystem path of the add-on to load. */
	ImageLoader(BPath& path)
	{
		fImage = load_add_on(path.Path());
	}

	/** @brief Destructor. Unloads the image if it was loaded successfully. */
	~ImageLoader()
	{
		if (fImage >= B_OK)
			unload_add_on(fImage);
	}

	/** @brief Returns B_OK if the image was loaded, or an error code otherwise.
	 *  @return B_OK on success, or a negative error code. */
	status_t InitCheck() const { return fImage >= 0 ? B_OK : fImage; }

	/** @brief Returns the loaded image identifier.
	 *  @return The image_id of the loaded add-on. */
	image_id Image() const { return fImage; }

private:
	image_id	fImage;
};


//	#pragma mark -


/** @brief Constructs the AddOnManager with an empty registry and zeroed ID counters. */
AddOnManager::AddOnManager()
	:
	fLock("add-on manager"),
	fNextWriterFormatFamilyID(0),
	fNextEncoderCodecInfoID(0)
{
}


/** @brief Destructor. */
AddOnManager::~AddOnManager()
{
}


AddOnManager AddOnManager::sInstance;


/** @brief Returns the process-wide singleton AddOnManager instance.
 *  @return Pointer to the global AddOnManager. */
/* static */ AddOnManager*
AddOnManager::GetInstance()
{
	return &sInstance;
}


/** @brief Finds the add-on file reference for a decoder that supports @p format.
 *
 *  Scans the registered decoder list directory by directory (respecting the
 *  user-overrides-system shadowing order) and returns the first decoder whose
 *  supported formats match @p format.
 *
 *  @param _decoderRef  Output entry_ref set to the matching decoder add-on.
 *  @param format       The encoded media format that must be decodable.
 *  @return B_OK on success, B_MEDIA_BAD_FORMAT if the format is invalid,
 *          or B_ENTRY_NOT_FOUND if no decoder matched. */
status_t
AddOnManager::GetDecoderForFormat(entry_ref* _decoderRef,
	const media_format& format)
{
	if ((format.type == B_MEDIA_ENCODED_VIDEO
			|| format.type == B_MEDIA_ENCODED_AUDIO
			|| format.type == B_MEDIA_MULTISTREAM)
		&& format.Encoding() == 0) {
		return B_MEDIA_BAD_FORMAT;
	}
	if (format.type == B_MEDIA_NO_TYPE || format.type == B_MEDIA_UNKNOWN_TYPE)
		return B_MEDIA_BAD_FORMAT;

	BAutolock locker(fLock);
	RegisterAddOns();

	// Since the list of decoders is unsorted, we need to search for
	// a decoder by add-on directory, in order to maintain the shadowing
	// of system add-ons by user add-ons, in case they offer decoders
	// for the same format.

	char** directories = NULL;
	size_t directoryCount = 0;

	if (find_paths_etc(get_architecture(), B_FIND_PATH_ADD_ONS_DIRECTORY,
			"media/plugins", B_FIND_PATH_EXISTING_ONLY, &directories,
			&directoryCount) != B_OK) {
		printf("AddOnManager::GetDecoderForFormat: failed to locate plugins\n");
		return B_ENTRY_NOT_FOUND;
	}

	MemoryDeleter directoriesDeleter(directories);

	BPath path;
	for (uint i = 0; i < directoryCount; i++) {
		path.SetTo(directories[i]);
		if (_FindDecoder(format, path, _decoderRef))
			return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


/** @brief Finds the add-on file reference for an encoder that produces @p outputFormat.
 *
 *  @param _encoderRef    Output entry_ref set to the matching encoder add-on.
 *  @param outputFormat   The desired encoded output format.
 *  @return B_OK on success, B_MEDIA_BAD_FORMAT if the format is invalid,
 *          or B_ENTRY_NOT_FOUND if no encoder matched. */
status_t
AddOnManager::GetEncoderForFormat(entry_ref* _encoderRef,
	const media_format& outputFormat)
{
	if ((outputFormat.type == B_MEDIA_RAW_VIDEO
			|| outputFormat.type == B_MEDIA_RAW_AUDIO)) {
		return B_MEDIA_BAD_FORMAT;
	}

	if (outputFormat.type == B_MEDIA_NO_TYPE
			|| outputFormat.type == B_MEDIA_UNKNOWN_TYPE) {
		return B_MEDIA_BAD_FORMAT;
	}

	BAutolock locker(fLock);
	RegisterAddOns();

	char** directories = NULL;
	size_t directoryCount = 0;

	if (find_paths_etc(get_architecture(), B_FIND_PATH_ADD_ONS_DIRECTORY,
			"media/plugins", B_FIND_PATH_EXISTING_ONLY, &directories,
			&directoryCount) != B_OK) {
		printf("AddOnManager::GetDecoderForFormat: failed to locate plugins\n");
		return B_ENTRY_NOT_FOUND;
	}

	MemoryDeleter directoriesDeleter(directories);

	BPath path;
	for (uint i = 0; i < directoryCount; i++) {
		path.SetTo(directories[i]);
		if (_FindEncoder(outputFormat, path, _encoderRef))
			return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


/** @brief Fills @p outRefs with entry references for all registered reader add-ons.
 *
 *  Results are returned in directory priority order (user before system).
 *
 *  @param outRefs   Output array of entry_ref to receive the reader references.
 *  @param outCount  Output pointer set to the number of refs written.
 *  @param maxCount  Maximum number of entries that @p outRefs can hold.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND if the plug-in directories could not be located. */
status_t
AddOnManager::GetReaders(entry_ref* outRefs, int32* outCount,
	int32 maxCount)
{
	BAutolock locker(fLock);
	RegisterAddOns();

	*outCount = 0;

	// See GetDecoderForFormat() for why we need to scan the list by path.

	char** directories = NULL;
	size_t directoryCount = 0;

	if (find_paths_etc(get_architecture(), B_FIND_PATH_ADD_ONS_DIRECTORY,
			"media/plugins", B_FIND_PATH_EXISTING_ONLY, &directories,
			&directoryCount) != B_OK) {
		printf("AddOnManager::GetReaders: failed to locate plugins\n");
		return B_ENTRY_NOT_FOUND;
	}

	MemoryDeleter directoriesDeleter(directories);

	BPath path;
	for (uint i = 0; i < directoryCount; i++) {
		path.SetTo(directories[i]);
		_GetReaders(path, outRefs, outCount, maxCount);
	}

	return B_OK;
}


/** @brief Fills @p outRefs with entry references for all registered streamer add-ons.
 *
 *  @param outRefs   Output array of entry_ref to receive the streamer references.
 *  @param outCount  Output pointer set to the number of refs written.
 *  @param maxCount  Maximum number of entries that @p outRefs can hold.
 *  @return B_OK always. */
status_t
AddOnManager::GetStreamers(entry_ref* outRefs, int32* outCount,
	int32 maxCount)
{
	BAutolock locker(fLock);
	RegisterAddOns();

	int32 count = 0;
	streamer_info* info;
	for (fStreamerList.Rewind(); fStreamerList.GetNext(&info);) {
			if (count == maxCount)
				break;

			*outRefs = info->ref;
			outRefs++;
			count++;
	}

	*outCount = count;
	return B_OK;
}


/** @brief Retrieves the entry reference for the encoder with the given internal @p id.
 *
 *  @param _encoderRef  Output entry_ref set to the matching encoder add-on.
 *  @param id           The internal encoder codec ID to look up.
 *  @return B_OK on success, or B_ENTRY_NOT_FOUND if no match exists. */
status_t
AddOnManager::GetEncoder(entry_ref* _encoderRef, int32 id)
{
	BAutolock locker(fLock);
	RegisterAddOns();

	encoder_info* info;
	for (fEncoderList.Rewind(); fEncoderList.GetNext(&info);) {
		// check if the encoder matches the supplied format
		if (info->internalID == (uint32)id) {
			*_encoderRef = info->ref;
			return B_OK;
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/** @brief Retrieves the entry reference for the writer add-on with the given internal ID.
 *
 *  @param _ref        Output entry_ref set to the matching writer add-on.
 *  @param internalID  The internal writer format family ID to look up.
 *  @return B_OK on success, or B_ERROR if no match exists. */
status_t
AddOnManager::GetWriter(entry_ref* _ref, uint32 internalID)
{
	BAutolock locker(fLock);
	RegisterAddOns();

	writer_info* info;
	for (fWriterList.Rewind(); fWriterList.GetNext(&info);) {
		if (info->internalID == internalID) {
			*_ref = info->ref;
			return B_OK;
		}
	}

	return B_ERROR;
}


/** @brief Returns the media_file_format registered at position @p cookie.
 *
 *  @param _fileFormat  Output structure filled with the file format at the given index.
 *  @param cookie       Zero-based index into the registered writer file-format list.
 *  @return B_OK on success, or B_BAD_INDEX if @p cookie is out of range. */
status_t
AddOnManager::GetFileFormat(media_file_format* _fileFormat, int32 cookie)
{
	BAutolock locker(fLock);
	RegisterAddOns();

	media_file_format* fileFormat;
	if (fWriterFileFormats.Get(cookie, &fileFormat)) {
		*_fileFormat = *fileFormat;
		return B_OK;
	}

	return B_BAD_INDEX;
}


/** @brief Returns the encoder codec info at position @p cookie.
 *
 *  @param _codecInfo      Output structure filled with codec info.
 *  @param _formatFamily   Output set to the format family of the encoder.
 *  @param _inputFormat    Output set to the encoder's accepted input format.
 *  @param _outputFormat   Output set to the encoder's produced output format.
 *  @param cookie          Zero-based index into the registered encoder list.
 *  @return B_OK on success, or B_BAD_INDEX if @p cookie is out of range. */
status_t
AddOnManager::GetCodecInfo(media_codec_info* _codecInfo,
	media_format_family* _formatFamily,
	media_format* _inputFormat, media_format* _outputFormat, int32 cookie)
{
	BAutolock locker(fLock);
	RegisterAddOns();

	encoder_info* info;
	if (fEncoderList.Get(cookie, &info)) {
		*_codecInfo = info->codecInfo;
		*_formatFamily = info->formatFamily;
		*_inputFormat = info->intputFormat;
		*_outputFormat = info->outputFormat;
		return B_OK;
	}

	return B_BAD_INDEX;
}


// #pragma mark -


/** @brief Scans all media plug-in directories and registers any discovered add-ons.
 *
 *  This is a lazy-initialisation method: if any of the internal lists are already
 *  populated the scan is skipped.  Must be called with @c fLock held. */
void
AddOnManager::RegisterAddOns()
{
	// Check if add-ons are already registered.
	if (!fReaderList.IsEmpty() || !fWriterList.IsEmpty()
		|| !fDecoderList.IsEmpty() || !fEncoderList.IsEmpty()) {
		return;
	}

	char** directories = NULL;
	size_t directoryCount = 0;

	if (find_paths_etc(get_architecture(), B_FIND_PATH_ADD_ONS_DIRECTORY,
			"media/plugins", B_FIND_PATH_EXISTING_ONLY, &directories,
			&directoryCount) != B_OK) {
		return;
	}

	MemoryDeleter directoriesDeleter(directories);

	BPath path;
	for (uint i = 0; i < directoryCount; i++) {
		BDirectory directory;
		if (directory.SetTo(directories[i]) == B_OK) {
			entry_ref ref;
			while(directory.GetNextRef(&ref) == B_OK)
				_RegisterAddOn(ref);
		}
	}
}


/** @brief Loads the add-on at @p ref, probes its plug-in type, and inserts it into
 *         the appropriate internal registration list(s).
 *
 *  @param ref  Entry reference of the add-on file to register.
 *  @return B_OK on success, B_BAD_TYPE if instantiate_plugin cannot be found,
 *          B_ERROR if instantiation returns NULL, or another error code. */
status_t
AddOnManager::_RegisterAddOn(const entry_ref& ref)
{
	BPath path(&ref);

	ImageLoader loader(path);
	status_t status = loader.InitCheck();
	if (status != B_OK)
		return status;

	MediaPlugin* (*instantiate_plugin_func)();

	if (get_image_symbol(loader.Image(), "instantiate_plugin",
			B_SYMBOL_TYPE_TEXT, (void**)&instantiate_plugin_func) < B_OK) {
		printf("AddOnManager::_RegisterAddOn(): can't find instantiate_plugin "
			"in \"%s\"\n", path.Path());
		return B_BAD_TYPE;
	}

	MediaPlugin* plugin = (*instantiate_plugin_func)();
	if (plugin == NULL) {
		printf("AddOnManager::_RegisterAddOn(): instantiate_plugin in \"%s\" "
			"returned NULL\n", path.Path());
		return B_ERROR;
	}

	ReaderPlugin* reader = dynamic_cast<ReaderPlugin*>(plugin);
	if (reader != NULL)
		_RegisterReader(reader, ref);

	DecoderPlugin* decoder = dynamic_cast<DecoderPlugin*>(plugin);
	if (decoder != NULL)
		_RegisterDecoder(decoder, ref);

	WriterPlugin* writer = dynamic_cast<WriterPlugin*>(plugin);
	if (writer != NULL)
		_RegisterWriter(writer, ref);

	EncoderPlugin* encoder = dynamic_cast<EncoderPlugin*>(plugin);
	if (encoder != NULL)
		_RegisterEncoder(encoder, ref);

	StreamerPlugin* streamer = dynamic_cast<StreamerPlugin*>(plugin);
	if (streamer != NULL)
		_RegisterStreamer(streamer, ref);

	delete plugin;

	return B_OK;
}


/** @brief Removes all registrations associated with the add-on at @p ref.
 *
 *  Cleans up entries from all internal lists (reader, decoder, writer, encoder)
 *  and removes any associated format registrations from FormatManager.
 *
 *  @param ref  Entry reference of the add-on to unregister.
 *  @return B_OK always. */
status_t
AddOnManager::_UnregisterAddOn(const entry_ref& ref)
{
	BAutolock locker(fLock);

	// Remove any Readers exported by this add-on
	reader_info* readerInfo;
	for (fReaderList.Rewind(); fReaderList.GetNext(&readerInfo);) {
		if (readerInfo->ref == ref) {
			fReaderList.RemoveCurrent();
			break;
		}
	}

	// Remove any Decoders exported by this add-on
	decoder_info* decoderInfo;
	for (fDecoderList.Rewind(); fDecoderList.GetNext(&decoderInfo);) {
		if (decoderInfo->ref == ref) {
			media_format* format;
			for (decoderInfo->formats.Rewind();
					decoderInfo->formats.GetNext(&format);) {
				FormatManager::GetInstance()->RemoveFormat(*format);
			}
			fDecoderList.RemoveCurrent();
			break;
		}
	}

	// Remove any Writers exported by this add-on
	writer_info* writerInfo;
	for (fWriterList.Rewind(); fWriterList.GetNext(&writerInfo);) {
		if (writerInfo->ref == ref) {
			// Remove any formats from this writer
			media_file_format* writerFormat;
			for (fWriterFileFormats.Rewind();
				fWriterFileFormats.GetNext(&writerFormat);) {
				if (writerFormat->id.internal_id == writerInfo->internalID)
					fWriterFileFormats.RemoveCurrent();
			}
			fWriterList.RemoveCurrent();
			break;
		}
	}

	encoder_info* encoderInfo;
	for (fEncoderList.Rewind(); fEncoderList.GetNext(&encoderInfo);) {
		if (encoderInfo->ref == ref) {
			fEncoderList.RemoveCurrent();
			// Keep going, since we add multiple encoder infos per add-on.
		}
	}

	return B_OK;
}


/** @brief Adds a reader add-on to the internal reader registry if not already present.
 *
 *  @param reader  The ReaderPlugin to register (used only for type identification).
 *  @param ref     Entry reference of the add-on file. */
void
AddOnManager::_RegisterReader(ReaderPlugin* reader, const entry_ref& ref)
{
	BAutolock locker(fLock);

	reader_info* pinfo;
	for (fReaderList.Rewind(); fReaderList.GetNext(&pinfo);) {
		if (!strcmp(pinfo->ref.name, ref.name)) {
			// we already know this reader
			return;
		}
	}

	reader_info info;
	info.ref = ref;

	fReaderList.Insert(info);
}


/** @brief Adds a decoder add-on to the internal decoder registry if not already present.
 *
 *  Queries the plug-in for its list of supported formats and stores them alongside
 *  the entry reference so that format-matching lookups can be performed later.
 *
 *  @param plugin  The DecoderPlugin to query for supported formats.
 *  @param ref     Entry reference of the add-on file. */
void
AddOnManager::_RegisterDecoder(DecoderPlugin* plugin, const entry_ref& ref)
{
	BAutolock locker(fLock);

	decoder_info* pinfo;
	for (fDecoderList.Rewind(); fDecoderList.GetNext(&pinfo);) {
		if (!strcmp(pinfo->ref.name, ref.name)) {
			// we already know this decoder
			return;
		}
	}

	decoder_info info;
	info.ref = ref;

	media_format* formats = 0;
	size_t count = 0;
	if (plugin->GetSupportedFormats(&formats, &count) != B_OK) {
		printf("AddOnManager::_RegisterDecoder(): plugin->GetSupportedFormats"
			"(...) failed!\n");
		return;
	}
	for (uint i = 0 ; i < count ; i++)
		info.formats.Insert(formats[i]);

	fDecoderList.Insert(info);
}


/** @brief Adds a writer add-on to the internal writer registry if not already present.
 *
 *  Queries the plug-in for its list of supported file formats, assigns each an
 *  internal ID, and inserts them into the writer file-format list.
 *
 *  @param writer  The WriterPlugin to query for supported file formats.
 *  @param ref     Entry reference of the add-on file. */
void
AddOnManager::_RegisterWriter(WriterPlugin* writer, const entry_ref& ref)
{
	BAutolock locker(fLock);

	writer_info* pinfo;
	for (fWriterList.Rewind(); fWriterList.GetNext(&pinfo);) {
		if (!strcmp(pinfo->ref.name, ref.name)) {
			// we already know this writer
			return;
		}
	}

	writer_info info;
	info.ref = ref;
	info.internalID = fNextWriterFormatFamilyID++;

	// Get list of support media_file_formats...
	const media_file_format* fileFormats = NULL;
	size_t count = 0;
	if (writer->GetSupportedFileFormats(&fileFormats, &count) != B_OK) {
		printf("AddOnManager::_RegisterWriter(): "
			"plugin->GetSupportedFileFormats(...) failed!\n");
		return;
	}
	for (uint i = 0 ; i < count ; i++) {
		// Generate a proper ID before inserting this format, this encodes
		// the specific plugin in the media_file_format.
		media_file_format fileFormat = fileFormats[i];
		fileFormat.id.node = ref.directory;
		fileFormat.id.device = ref.device;
		fileFormat.id.internal_id = info.internalID;

		fWriterFileFormats.Insert(fileFormat);
	}

	fWriterList.Insert(info);
}


/** @brief Adds an encoder add-on to the internal encoder registry if not already present.
 *
 *  Iterates over all codec entries exported by the plug-in via RegisterNextEncoder()
 *  and inserts one encoder_info record per codec into the encoder list.
 *
 *  @param plugin  The EncoderPlugin to enumerate codecs from.
 *  @param ref     Entry reference of the add-on file. */
void
AddOnManager::_RegisterEncoder(EncoderPlugin* plugin, const entry_ref& ref)
{
	BAutolock locker(fLock);

	encoder_info* pinfo;
	for (fEncoderList.Rewind(); fEncoderList.GetNext(&pinfo);) {
		if (!strcmp(pinfo->ref.name, ref.name)) {
			// We already know this encoder. When we reject encoders with
			// the same name, we allow the user to overwrite system encoders
			// in her home folder.
			return;
		}
	}

	// Get list of supported encoders...

	encoder_info info;
	info.ref = ref;
	info.internalID = fNextEncoderCodecInfoID++;

	int32 cookie = 0;

	while (true) {
		memset(&info.codecInfo, 0, sizeof(media_codec_info));
		info.intputFormat.Clear();
		info.outputFormat.Clear();
		if (plugin->RegisterNextEncoder(&cookie,
			&info.codecInfo, &info.formatFamily, &info.intputFormat,
			&info.outputFormat) != B_OK) {
			break;
		}
		info.codecInfo.id = info.internalID;
		// NOTE: info.codecInfo.sub_id is for private use by the Encoder,
		// we don't touch it, but it is maintained and passed back to the
		// EncoderPlugin in NewEncoder(media_codec_info).

		if (!fEncoderList.Insert(info))
			break;
	}
}


/** @brief Adds a streamer add-on to the internal streamer registry if not already present.
 *
 *  @param streamer  The StreamerPlugin to register (used only for type identification).
 *  @param ref       Entry reference of the add-on file. */
void
AddOnManager::_RegisterStreamer(StreamerPlugin* streamer, const entry_ref& ref)
{
	BAutolock locker(fLock);

	streamer_info* pInfo;
	for (fStreamerList.Rewind(); fStreamerList.GetNext(&pInfo);) {
		if (!strcmp(pInfo->ref.name, ref.name)) {
			// We already know this streamer
			return;
		}
	}

	streamer_info info;
	info.ref = ref;
	fStreamerList.Insert(info);
}


/** @brief Searches the decoder list for a decoder in @p path that supports @p format.
 *
 *  Only decoders whose add-on file lives directly inside @p path are considered,
 *  which enforces the user-overrides-system directory priority.
 *
 *  @param format       The encoded media format to match against.
 *  @param path         The plug-in directory to restrict the search to.
 *  @param _decoderRef  Output entry_ref set to the matching decoder on success.
 *  @return true if a matching decoder was found, false otherwise. */
bool
AddOnManager::_FindDecoder(const media_format& format, const BPath& path,
	entry_ref* _decoderRef)
{
	node_ref nref;
	BDirectory directory;
	if (directory.SetTo(path.Path()) != B_OK
		|| directory.GetNodeRef(&nref) != B_OK) {
		return false;
	}

	decoder_info* info;
	for (fDecoderList.Rewind(); fDecoderList.GetNext(&info);) {
		if (info->ref.directory != nref.node)
			continue;

		media_format* decoderFormat;
		for (info->formats.Rewind(); info->formats.GetNext(&decoderFormat);) {
			// check if the decoder matches the supplied format
			if (!decoderFormat->Matches(&format))
				continue;

			*_decoderRef = info->ref;
			return true;
		}
	}
	return false;
}


/** @brief Searches the encoder list for an encoder in @p path that produces @p format.
 *
 *  Only encoders whose add-on file lives directly inside @p path are considered.
 *
 *  @param format       The desired encoded output format to match against.
 *  @param path         The plug-in directory to restrict the search to.
 *  @param _encoderRef  Output entry_ref set to the matching encoder on success.
 *  @return true if a matching encoder was found, false otherwise. */
bool
AddOnManager::_FindEncoder(const media_format& format, const BPath& path,
	entry_ref* _encoderRef)
{
	node_ref nref;
	BDirectory directory;
	if (directory.SetTo(path.Path()) != B_OK
		|| directory.GetNodeRef(&nref) != B_OK) {
		return false;
	}

	encoder_info* info;
	for (fEncoderList.Rewind(); fEncoderList.GetNext(&info);) {
		if (info->ref.directory != nref.node)
			continue;

		// check if the encoder matches the supplied format
		if (info->outputFormat.Matches(&format)) {
			*_encoderRef = info->ref;
			return true;
		}
		continue;
	}
	return false;
}


/** @brief Appends reader entry refs from @p path into the output array.
 *
 *  Only readers whose add-on file lives directly inside @p path are appended.
 *
 *  @param path      The plug-in directory to read from.
 *  @param outRefs   Output array to append entry refs into.
 *  @param outCount  In/out: current count; incremented for each reader appended.
 *  @param maxCount  Maximum number of entries that @p outRefs can hold. */
void
AddOnManager::_GetReaders(const BPath& path, entry_ref* outRefs,
	int32* outCount, int32 maxCount)
{
	node_ref nref;
	BDirectory directory;
	if (directory.SetTo(path.Path()) != B_OK
		|| directory.GetNodeRef(&nref) != B_OK) {
		return;
	}

	reader_info* info;
	for (fReaderList.Rewind(); fReaderList.GetNext(&info)
		&& *outCount < maxCount;) {
		if (info->ref.directory != nref.node)
			continue;

		outRefs[*outCount] = info->ref;
		(*outCount)++;
	}
}


} // namespace media
} // namespace BPrivate
