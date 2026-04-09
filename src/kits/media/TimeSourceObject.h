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
 * Incorporates work originally licensed under the MIT License.
 * Copyright 2002, Marcus Overhagen. All Rights Reserved.
 */

/** @file TimeSourceObject.h
    @brief Client-side proxy BTimeSource object that communicates with a time source node. */

#ifndef TIME_SOURCE_OBJECT_H
#define TIME_SOURCE_OBJECT_H


#include <TimeSource.h>

#include <MediaMisc.h>


namespace BPrivate {
namespace media {


/** @brief Concrete BTimeSource that proxies operations to a remote media_node time source. */
class TimeSourceObject : public BTimeSource {
public:
								TimeSourceObject(const media_node& node);

protected:
	/** @brief Forwards a time source operation to the actual time source node.
	    @param op The time source operation descriptor.
	    @param _reserved Reserved; pass NULL.
	    @return B_OK on success, or an error code. */
	virtual	status_t			TimeSourceOp(const time_source_op_info& op,
									void* _reserved);

	/** @brief Returns NULL as this object is not instantiated by an add-on.
	    @param _id Unused.
	    @return NULL. */
	virtual	BMediaAddOn*		AddOn(int32* _id) const;

	// override from BMediaNode
	/** @brief Cleans up the object when the node is deleted.
	    @param node The node being deleted.
	    @return B_OK on success, or an error code. */
	virtual status_t			DeleteHook(BMediaNode* node);
};


}	// namespace media
}	// namespace BPrivate


using namespace BPrivate::media;


#endif	// TIME_SOURCE_OBJECT_H
