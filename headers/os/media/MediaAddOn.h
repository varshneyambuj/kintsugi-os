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

/** @file MediaAddOn.h
 *  @brief Defines BMediaAddOn and related types for Media Kit plug-in add-ons.
 */

#ifndef _MEDIA_ADD_ON_H
#define _MEDIA_ADD_ON_H


#include <image.h>

#include <MediaDefs.h>
#include <Flattenable.h>


class BMediaNode;
class BMimeType;
struct entry_ref;

/** @brief Identifies a dormant (not yet instantiated) media node inside an add-on. */
struct dormant_node_info {
								dormant_node_info();
								~dormant_node_info();

			media_addon_id		addon;                       /**< ID of the hosting add-on. */
			int32				flavor_id;                   /**< Flavor index within the add-on. */
			char				name[B_MEDIA_NAME_LENGTH];   /**< Human-readable node name. */

private:
			char				reserved[128];
};

/** @brief Flags controlling where a flavor is instantiated. */
// flavor_flags
enum {
	B_FLAVOR_IS_GLOBAL	= 0x100000L,  /**< Force instantiation in media_addon_server (one instance). */
	B_FLAVOR_IS_LOCAL	= 0x200000L   /**< Force instantiation in the loading application. */
};

/** @brief Describes one "flavor" (capability variant) exported by a BMediaAddOn. */
struct flavor_info {
	const char*			name;             /**< Human-readable flavor name. */
	const char*			info;             /**< Descriptive text about this flavor. */
	uint64				kinds;            /**< Node kind flags (B_BUFFER_PRODUCER, etc.). */
	uint32				flavor_flags;     /**< Placement flags (B_FLAVOR_IS_GLOBAL, etc.). */
	int32				internal_id;      /**< Internal ID for BMediaAddOn use. */
	int32				possible_count;   /**< Maximum simultaneous instances; 0 means unlimited. */

	int32				in_format_count;  /**< Number of accepted input formats. */
	uint32				in_format_flags;  /**< Reserved; set to 0. */
	const media_format*	in_formats;       /**< Array of accepted input formats. */

	int32				out_format_count; /**< Number of produced output formats. */
	uint32				out_format_flags; /**< Reserved; set to 0. */
	const media_format*	out_formats;      /**< Array of produced output formats. */

	uint32				_reserved_[16];

private:
	flavor_info&		operator=(const flavor_info& other);
};

/** @brief A flattenable flavor_info that also carries its owning dormant_node_info. */
struct dormant_flavor_info : public flavor_info, public BFlattenable {
								dormant_flavor_info();
	virtual						~dormant_flavor_info();

								dormant_flavor_info(
									const dormant_flavor_info& other);
			dormant_flavor_info& operator=(const dormant_flavor_info& other);
			dormant_flavor_info& operator=(const flavor_info& other);

			dormant_node_info	node_info; /**< The dormant node this flavor belongs to. */

	/** @brief Sets the name field of this flavor.
	 *  @param name The new name string.
	 */
			void				set_name(const char* name);

	/** @brief Sets the info field of this flavor.
	 *  @param info The new description string.
	 */
			void				set_info(const char* info);

	/** @brief Appends an input format to the in_formats array.
	 *  @param format The format to add.
	 */
			void				add_in_format(const media_format& format);

	/** @brief Appends an output format to the out_formats array.
	 *  @param format The format to add.
	 */
			void				add_out_format(const media_format& format);

	/** @brief Returns false because this object is variable-sized. */
	virtual	bool				IsFixedSize() const;

	/** @brief Returns the type code used when flattening this object. */
	virtual	type_code			TypeCode() const;

	/** @brief Returns the number of bytes needed to flatten this object. */
	virtual	ssize_t				FlattenedSize() const;

	/** @brief Writes this object into a flat byte buffer.
	 *  @param buffer Destination buffer.
	 *  @param size Capacity of the buffer.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Flatten(void* buffer, ssize_t size) const;

	/** @brief Restores this object from a flat byte buffer.
	 *  @param type Type code of the flattened data.
	 *  @param buffer Source buffer.
	 *  @param size Length of the buffer in bytes.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			Unflatten(type_code type, const void* buffer,
									ssize_t size);
};


namespace BPrivate {
	namespace media {
		class DormantNodeManager;
	};
};


/** @brief Factory object that creates and manages BMediaNode instances from an add-on image.
 *
 *  Implement BMediaAddOn in a shared library and export make_media_addon() to
 *  register your node flavors with the media_server.  Each flavor describes one
 *  type of node your add-on can instantiate.
 */
class BMediaAddOn {
public:
	/** @brief Constructs the add-on associated with the given image.
	 *  @param image The image_id of the loaded add-on shared library.
	 */
	explicit					BMediaAddOn(image_id image);
	virtual						~BMediaAddOn();

	/** @brief Reports whether the add-on initialized successfully.
	 *  @param _failureText On failure, receives a human-readable error description.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			InitCheck(const char** _failureText);

	/** @brief Returns the number of flavors (node types) this add-on exports.
	 *  @return The flavor count.
	 */
	virtual	int32				CountFlavors();

	/** @brief Retrieves the flavor_info for a given index.
	 *  @param index Zero-based flavor index.
	 *  @param _info On return, points to the flavor_info structure.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetFlavorAt(int32 index,
									const flavor_info** _info);

	/** @brief Instantiates a media node for the given flavor.
	 *  @param info The flavor to instantiate.
	 *  @param config Optional saved configuration message.
	 *  @param _error On return, an error code if instantiation fails.
	 *  @return The new BMediaNode, or NULL on error.
	 */
	virtual	BMediaNode*			InstantiateNodeFor(const flavor_info* info,
									BMessage* config, status_t* _error);

	/** @brief Reads the current configuration of a node into a BMessage.
	 *  @param yourNode The node to query.
	 *  @param intoMessage The message to populate with configuration data.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetConfigurationFor(BMediaNode* yourNode,
									BMessage* intoMessage);

	/** @brief Returns true if this add-on wants to be started automatically.
	 *  @return True to enable auto-start.
	 */
	virtual	bool				WantsAutoStart();

	/** @brief Called during auto-start to retrieve each automatically started node.
	 *  @param index The auto-start index.
	 *  @param _node On return, the instantiated BMediaNode.
	 *  @param _internalID On return, the internal flavor ID.
	 *  @param _hasMore On return, true if more nodes remain.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			AutoStart(int index, BMediaNode** _node,
									int32* _internalID, bool* _hasMore);

	/** @brief Probes a file to see if this add-on's B_FILE_INTERFACE nodes can handle it.
	 *  @param file The file to examine.
	 *  @param ioMimeType In/out: the detected MIME type.
	 *  @param _quality On return, a 0.0-1.0 confidence score.
	 *  @param _internalID On return, the flavor that can handle the file.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SniffRef(const entry_ref& file,
									BMimeType* ioMimeType, float* _quality,
									int32* _internalID);

	/** @brief Probes a MIME type to see if this add-on can handle it (deprecated overload).
	 *  @param type The MIME type to examine.
	 *  @param _quality On return, a 0.0-1.0 confidence score.
	 *  @param _internalID On return, the matching flavor ID.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SniffType(const BMimeType& type,
									float* _quality, int32* _internalID);

	/** @brief Retrieves the list of readable and writable file formats for a flavor.
	 *  @param forNodeFlavorID The flavor ID to query.
	 *  @param _writableFormats Buffer for writable format entries.
	 *  @param writableFormatsCount Capacity of the writable formats buffer.
	 *  @param _writableFormatsTotalCount On return, total number of writable formats.
	 *  @param _readableFormats Buffer for readable format entries.
	 *  @param readableFormatsCount Capacity of the readable formats buffer.
	 *  @param _readableFormatsTotalCount On return, total number of readable formats.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			GetFileFormatList(int32 forNodeFlavorID,
									media_file_format* _writableFormats,
									int32 writableFormatsCount,
									int32* _writableFormatsTotalCount,
									media_file_format* _readableFormats,
									int32 readableFormatsCount,
									int32* _readableFormatsTotalCount,
									void* _reserved);

	/** @brief Probes a MIME type for a specific node kind; preferred over SniffType().
	 *  @param type The MIME type to examine.
	 *  @param kinds Required node kind flags.
	 *  @param _quality On return, a 0.0-1.0 confidence score.
	 *  @param _internalID On return, the matching flavor ID.
	 *  @param _reserved Reserved; pass NULL.
	 *  @return B_OK on success, or an error code.
	 */
	virtual	status_t			SniffTypeKind(const BMimeType& type,
									uint64 kinds, float* _quality,
									int32* _internalID, void* _reserved);

	/** @brief Returns the image ID of the loaded add-on shared library.
	 *  @return The image_id.
	 */
			image_id			ImageID();

	/** @brief Returns the system-assigned ID for this add-on.
	 *  @return The media_addon_id.
	 */
			media_addon_id		AddonID();

protected:
	/** @brief Triggers a re-scan of this add-on's flavor list by the media server.
	 *  @return B_OK on success, or an error code.
	 */
			status_t			NotifyFlavorChange();

	// TODO: Needs a Perform() virtual method!

private:
	// FBC padding and forbidden methods
								BMediaAddOn();
								BMediaAddOn(const BMediaAddOn& other);
			BMediaAddOn&		operator=(const BMediaAddOn& other);

			friend class BPrivate::media::DormantNodeManager;

	virtual	status_t			_Reserved_MediaAddOn_2(void*);
	virtual	status_t			_Reserved_MediaAddOn_3(void*);
	virtual	status_t			_Reserved_MediaAddOn_4(void*);
	virtual	status_t			_Reserved_MediaAddOn_5(void*);
	virtual	status_t			_Reserved_MediaAddOn_6(void*);
	virtual	status_t			_Reserved_MediaAddOn_7(void*);

			image_id 			fImage;
			media_addon_id		fAddon;

			uint32				_reserved_media_add_on_[7];
};


#if BUILDING_MEDIA_ADDON
	extern "C" _EXPORT BMediaAddOn* make_media_addon(image_id yourImage);
#endif


#endif // _MEDIA_ADD_ON_H

