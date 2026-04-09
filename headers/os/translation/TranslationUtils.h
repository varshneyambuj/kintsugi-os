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
 * This file incorporates work from the Haiku project:
 *   Copyright 2002-2009, Haiku Inc. All Rights Reserved.
 *   Distributed under the terms of the MIT License.
 *
 * Author:
 *   Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file TranslationUtils.h
 *  @brief Static utility functions for loading bitmaps, styled text, and building translation menus. */

#ifndef _TRANSLATION_UTILS_H
#define _TRANSLATION_UTILS_H


#include <GraphicsDefs.h>
#include <SupportDefs.h>
#include <TranslationDefs.h>


class BBitmap;
class BFile;
class BMenu;
class BMessage;
class BPositionIO;
class BTextView;
class BTranslatorRoster;
struct entry_ref;


/** @brief Collection of static helpers for common Translation Kit operations.
 *
 *  This class is not instantiable; all methods are static. */
class BTranslationUtils {
								BTranslationUtils();
								BTranslationUtils(
									const BTranslationUtils& other);
								~BTranslationUtils();

			BTranslationUtils&	operator=(const BTranslationUtils& other);

public:
	enum {
		B_TRANSLATION_MENU = 'BTMN'
	};

	/** @brief Loads a bitmap from an application resource or file by name.
	 *  @param name Resource or file name to load.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return Newly allocated BBitmap on success, or NULL. */
	static	BBitmap*			GetBitmap(const char* name,
									BTranslatorRoster* roster = NULL);

	/** @brief Loads a bitmap from an application resource by type code and resource ID.
	 *  @param type Resource type code.
	 *  @param id Resource ID.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return Newly allocated BBitmap on success, or NULL. */
	static	BBitmap*			GetBitmap(uint32 type, int32 id,
									BTranslatorRoster* roster = NULL);

	/** @brief Loads a bitmap from an application resource by type code and name.
	 *  @param type Resource type code.
	 *  @param name Resource name.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return Newly allocated BBitmap on success, or NULL. */
	static	BBitmap*			GetBitmap(uint32 type, const char* name,
									BTranslatorRoster* roster = NULL);

	/** @brief Loads a bitmap from a file path.
	 *  @param name Path to the image file.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return Newly allocated BBitmap on success, or NULL. */
	static	BBitmap*			GetBitmapFile(const char* name,
									BTranslatorRoster* roster = NULL);

	/** @brief Loads a bitmap from a file identified by entry_ref.
	 *  @param ref Reference to the image file.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return Newly allocated BBitmap on success, or NULL. */
	static	BBitmap*			GetBitmap(const entry_ref* ref,
									BTranslatorRoster* roster = NULL);

	/** @brief Translates a bitmap from a BPositionIO stream.
	 *  @param stream Input stream containing image data.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return Newly allocated BBitmap on success, or NULL. */
	static	BBitmap*			GetBitmap(BPositionIO* stream,
									BTranslatorRoster* roster = NULL);

	/** @brief Sets the default color space used when loading bitmaps.
	 *  @param space Desired color_space value. */
	static	void				SetBitmapColorSpace(color_space space);

	/** @brief Returns the default color space used when loading bitmaps.
	 *  @return Current color_space setting. */
	static	color_space			BitmapColorSpace();

	/** @brief Reads styled text from a stream and inserts it into a BTextView.
	 *  @param fromStream Source stream containing styled text data.
	 *  @param intoView Destination BTextView.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return B_OK on success, or an error code. */
	static	status_t			GetStyledText(BPositionIO* fromStream,
									BTextView* intoView,
									BTranslatorRoster* roster = NULL);

	/** @brief Reads styled text from a stream with a specific encoding and inserts it into a BTextView.
	 *  @param fromStream Source stream containing styled text data.
	 *  @param intoView Destination BTextView.
	 *  @param encoding Character encoding name (e.g. "UTF-8").
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return B_OK on success, or an error code. */
	static	status_t			GetStyledText(BPositionIO* fromStream,
									BTextView* intoView, const char* encoding,
									BTranslatorRoster* roster = NULL);

	/** @brief Writes the styled text content of a BTextView to a stream.
	 *  @param fromView Source BTextView.
	 *  @param intoStream Destination stream.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return B_OK on success, or an error code. */
	static	status_t			PutStyledText(BTextView* fromView,
									BPositionIO* intoStream,
									BTranslatorRoster* roster = NULL);

	/** @brief Saves styled text from a BTextView to a StyledEdit-compatible file.
	 *  @param fromView Source BTextView.
	 *  @param intoFile Destination BFile.
	 *  @return B_OK on success, or an error code. */
	static	status_t			WriteStyledEditFile(BTextView* fromView,
									BFile* intoFile);

	/** @brief Saves styled text from a BTextView to a StyledEdit-compatible file with a specific encoding.
	 *  @param fromView Source BTextView.
	 *  @param intoFile Destination BFile.
	 *  @param encoding Character encoding name (e.g. "UTF-8").
	 *  @return B_OK on success, or an error code. */
	static	status_t			WriteStyledEditFile(BTextView* fromView,
									BFile* intoFile, const char* encoding);

	/** @brief Returns the default settings message for a translator.
	 *  @param translator ID of the translator.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return Newly allocated BMessage on success, or NULL. */
	static	BMessage*			GetDefaultSettings(translator_id translator,
									BTranslatorRoster* roster = NULL);

	/** @brief Returns the default settings message for a translator identified by name and version.
	 *  @param name Translator name.
	 *  @param version Translator version.
	 *  @return Newly allocated BMessage on success, or NULL. */
	static	BMessage*			GetDefaultSettings(const char* name,
									int32 version);

	/** @brief Populates a BMenu with one item per translator that can read fromType.
	 *  @param intoMenu Menu to populate.
	 *  @param fromType Source data type code.
	 *  @param model Optional prototype BMessage for each menu item.
	 *  @param idName Field name for the translator ID stored in each item's message.
	 *  @param typeName Field name for the output type stored in each item's message.
	 *  @param roster Roster to use; NULL uses the default roster.
	 *  @return B_OK on success, or an error code. */
	static	status_t			AddTranslationItems(BMenu* intoMenu,
									uint32 fromType,
									const BMessage* model = NULL,
									const char* idName = NULL,
									const char* typeName = NULL,
									BTranslatorRoster* roster = NULL);

private:
	static	translator_info*	_BuildTranslatorInfo(const translator_id id,
									const translation_format* format);
	static	int					_CompareTranslatorInfoByName(const translator_info* info1,
									const translator_info* info2);

	static	color_space			sBitmapSpace;
};


#endif	// _TRANSLATION_UTILS_H
