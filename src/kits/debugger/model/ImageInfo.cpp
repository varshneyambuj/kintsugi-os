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
 *   Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 *   Distributed under the terms of the MIT License.
 */


/**
 * @file ImageInfo.cpp
 * @brief Implementation of ImageInfo, a value-type description of a loaded
 *        image (executable or shared library) within a debugged team.
 *
 * ImageInfo records the team and image ids, image type, and the
 * text/data segment placement. It is the primary descriptor passed to
 * symbol resolution and image-debug-info loading paths.
 */

#include "ImageInfo.h"


/**
 * @brief Constructs an empty ImageInfo with invalid identifiers.
 */
ImageInfo::ImageInfo()
	:
	fTeam(-1),
	fImage(-1),
	fName(),
	fTextBase(0),
	fTextSize(0),
	fDataBase(0),
	fDataSize(0)
{
}

/**
 * @brief Copy-constructs from another ImageInfo.
 *
 * @param other Source instance to copy.
 */
ImageInfo::ImageInfo(const ImageInfo& other)
	:
	fTeam(other.fTeam),
	fImage(other.fImage),
	fName(other.fName),
	fType(other.fType),
	fTextBase(other.fTextBase),
	fTextSize(other.fTextSize),
	fDataBase(other.fDataBase),
	fDataSize(other.fDataSize)
{
}


/**
 * @brief Constructs a fully-populated ImageInfo.
 *
 * @param team     Owning team identifier.
 * @param image    Kernel image identifier.
 * @param name     Image path or display name.
 * @param type     Image type (executable, library, add-on).
 * @param textBase Base address of the text segment.
 * @param textSize Size of the text segment in bytes.
 * @param dataBase Base address of the data segment.
 * @param dataSize Size of the data segment in bytes.
 */
ImageInfo::ImageInfo(team_id team, image_id image, const BString& name,
	image_type type, target_addr_t textBase, target_size_t textSize,
	target_addr_t dataBase, target_size_t dataSize)
	:
	fTeam(team),
	fImage(image),
	fName(name),
	fType(type),
	fTextBase(textBase),
	fTextSize(textSize),
	fDataBase(dataBase),
	fDataSize(dataSize)
{
}


/**
 * @brief Replaces all fields with new values.
 *
 * @param team     Owning team identifier.
 * @param image    Kernel image identifier.
 * @param name     Image path or display name.
 * @param type     Image type (executable, library, add-on).
 * @param textBase Base address of the text segment.
 * @param textSize Size of the text segment in bytes.
 * @param dataBase Base address of the data segment.
 * @param dataSize Size of the data segment in bytes.
 */
void
ImageInfo::SetTo(team_id team, image_id image, const BString& name,
	image_type type, target_addr_t textBase, target_size_t textSize,
	target_addr_t dataBase, target_size_t dataSize)
{
	fTeam = team;
	fImage = image;
	fName = name;
	fType = type;
	fTextBase = textBase;
	fTextSize = textSize;
	fDataBase = dataBase;
	fDataSize = dataSize;
}
