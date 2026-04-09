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
 * Incorporates work from the Haiku project, originally licensed under the
 * MIT License. Copyright 2004-2009, Haiku. All rights reserved.
 * Original authors: Marcus Overhagen, Axel Dörfler,
 *                   Stephan Aßmus <superstippi@gmx.de>
 */

/** @file AddOnManager.h
    @brief Manager for codec add-ons (reader, writer, encoder, decoder). */

#ifndef _ADD_ON_MANAGER_H
#define _ADD_ON_MANAGER_H


/*!	Manager for codec add-ons (reader, writer, encoder, decoder)
	BMediaAddOn are handled in the NodeManager
*/


#include "DataExchange.h"
#include "TList.h"

#include "DecoderPlugin.h"
#include "EncoderPlugin.h"
#include "ReaderPlugin.h"
#include "StreamerPlugin.h"
#include "WriterPlugin.h"


namespace BPrivate {
namespace media {

/** @brief Manages registration and lookup of media codec add-ons including
           readers, writers, encoders, decoders, and streamers. */
class AddOnManager {
public:
								AddOnManager();
								~AddOnManager();

			/** @brief Returns the singleton AddOnManager instance.
			    @return Pointer to the global AddOnManager. */
			static AddOnManager* GetInstance();

			/** @brief Finds a decoder add-on capable of handling the given format.
			    @param _ref Output entry_ref for the decoder add-on.
			    @param format The media format to decode.
			    @return B_OK on success, or an error code. */
			status_t			GetDecoderForFormat(entry_ref* _ref,
									const media_format& format);

			/** @brief Finds an encoder add-on capable of producing the given output format.
			    @param _ref Output entry_ref for the encoder add-on.
			    @param outputFormat The desired output media format.
			    @return B_OK on success, or an error code. */
			status_t			GetEncoderForFormat(entry_ref* _ref,
									const media_format& outputFormat);

			/** @brief Retrieves a list of available reader add-ons.
			    @param _ref Output array of entry_refs for reader add-ons.
			    @param _count Output number of readers found.
			    @param maxCount Maximum number of entries the array can hold.
			    @return B_OK on success, or an error code. */
			status_t			GetReaders(entry_ref* _ref,
									int32* _count, int32 maxCount);

			/** @brief Retrieves a list of available streamer add-ons.
			    @param _ref Output array of entry_refs for streamer add-ons.
			    @param _count Output number of streamers found.
			    @param maxCount Maximum number of entries the array can hold.
			    @return B_OK on success, or an error code. */
			status_t			GetStreamers(entry_ref* _ref,
									int32* _count, int32 maxCount);

			/** @brief Retrieves an encoder add-on by its internal ID.
			    @param _ref Output entry_ref for the encoder.
			    @param id Internal encoder identifier.
			    @return B_OK on success, or an error code. */
			status_t			GetEncoder(entry_ref* _ref, int32 id);

			/** @brief Retrieves a writer add-on by its internal format family ID.
			    @param _ref Output entry_ref for the writer.
			    @param internalID Internal writer identifier.
			    @return B_OK on success, or an error code. */
			status_t			GetWriter(entry_ref* _ref,
									uint32 internalID);

			/** @brief Retrieves information about a supported file format by cookie.
			    @param _fileFormat Output media file format descriptor.
			    @param cookie Iterator cookie; start at 0 and increment each call.
			    @return B_OK on success, B_BAD_INDEX when enumeration is complete. */
			status_t			GetFileFormat(media_file_format* _fileFormat,
									int32 cookie);

			/** @brief Retrieves codec information for an encoder by cookie.
			    @param _codecInfo Output codec info structure.
			    @param _formatFamily Output format family of the codec.
			    @param _inputFormat Output accepted input format.
			    @param _outputFormat Output produced output format.
			    @param cookie Iterator cookie; start at 0 and increment each call.
			    @return B_OK on success, B_BAD_INDEX when enumeration is complete. */
			status_t			GetCodecInfo(media_codec_info* _codecInfo,
									media_format_family* _formatFamily,
									media_format* _inputFormat,
									media_format* _outputFormat, int32 cookie);

			/** @brief Scans add-on directories and registers all discovered add-ons. */
			void				RegisterAddOns();

private:

			status_t			_RegisterAddOn(const entry_ref& ref);
			status_t			_UnregisterAddOn(const entry_ref& ref);

			void				_RegisterReader(ReaderPlugin* reader,
									const entry_ref& ref);
			void				_RegisterDecoder(DecoderPlugin* decoder,
									const entry_ref& ref);

			void				_RegisterWriter(WriterPlugin* writer,
									const entry_ref& ref);
			void				_RegisterEncoder(EncoderPlugin* encoder,
									const entry_ref& ref);

			void				_RegisterStreamer(StreamerPlugin* streamer,
									const entry_ref& ref);

			bool				_FindDecoder(const media_format& format,
									const BPath& path,
									entry_ref* _decoderRef);

			bool				_FindEncoder(const media_format& format,
									const BPath& path,
									entry_ref* _decoderRef);

			void				_GetReaders(const BPath& path,
									entry_ref* outRefs, int32* outCount,
									int32 maxCount);

private:
			/** @brief Holds an entry_ref for a registered reader add-on. */
			struct reader_info {
				entry_ref			ref;
			};
			/** @brief Holds an entry_ref and internal ID for a registered writer add-on. */
			struct writer_info {
				entry_ref			ref;
				uint32				internalID;
			};
			/** @brief Holds an entry_ref and supported format list for a decoder add-on. */
			struct decoder_info {
				entry_ref			ref;
				List<media_format>	formats;
			};
			/** @brief Holds full metadata for a registered encoder add-on. */
			struct encoder_info {
				entry_ref			ref;
				uint32				internalID;
				media_codec_info	codecInfo;
				media_format_family	formatFamily;
				media_format		intputFormat;
				media_format		outputFormat;
			};
			/** @brief Holds an entry_ref for a registered streamer add-on. */
			struct streamer_info {
				entry_ref			ref;
			};

			BLocker				fLock;
			List<reader_info>	fReaderList;
			List<writer_info>	fWriterList;
			List<decoder_info>	fDecoderList;
			List<encoder_info>	fEncoderList;
			List<streamer_info> fStreamerList;

			List<media_file_format> fWriterFileFormats;

			uint32				fNextWriterFormatFamilyID;
			uint32				fNextEncoderCodecInfoID;

			static AddOnManager	sInstance;
};

} // namespace media
} // namespace BPrivate

#endif // _ADD_ON_MANAGER_H
