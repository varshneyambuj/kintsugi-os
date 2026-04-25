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
 * MIT License. Copyright 2001-2009, Haiku.
 * Original authors: DarkWyrm, Axel Dörfler.
 */

/** @file GlobalFontManager.h
    @brief System-wide font manager that scans the install directories and
           watches them for changes via node monitoring. */

#ifndef GLOBAL_FONT_MANAGER_H
#define GLOBAL_FONT_MANAGER_H


#include "FontManager.h"

#include <AutoDeleter.h>
#include <Looper.h>
#include <ObjectList.h>


class BEntry;
class BPath;
struct node_ref;


class FontFamily;
class FontStyle;
class ServerFont;


/**
 * @brief Application-wide font catalog backed by the system font directories.
 *
 * Subclasses FontManager and BLooper: the looper thread receives node
 * monitor messages from the watched font directories and folds added,
 * moved, or removed font files into the cached family/style tables.
 * Defaults (plain/bold/fixed) are resolved lazily from a fallback chain
 * configured in ServerConfig.h.
 */
class GlobalFontManager : public FontManager, public BLooper {
public:
								GlobalFontManager();
	virtual						~GlobalFontManager();

			/** @brief Forwards Lock to the BLooper lock. */
			bool				Lock() { return BLooper::Lock(); }
			/** @brief Forwards Unlock to the BLooper lock. */
			void				Unlock() { BLooper::Unlock(); }
			/** @brief Returns true when the calling thread owns the looper lock. */
			bool				IsLocked() const { return BLooper::IsLocked(); }

			/** @brief Returns the result of FreeType library initialization. */
			status_t			InitCheck() { return fInitStatus; }

			void				SaveRecentFontMappings();

	virtual	void				MessageReceived(BMessage* message);

	virtual	int32				CountFamilies();

	virtual	int32				CountStyles(const char* family);
	virtual	int32				CountStyles(uint16 familyID);

			const ServerFont*	DefaultPlainFont() const;
			const ServerFont*	DefaultBoldFont() const;
			const ServerFont*	DefaultFixedFont() const;

	virtual	FontFamily*			GetFamily(uint16 familyID) const;
	virtual	FontFamily*			GetFamily(const char* name);

	virtual	FontStyle*			GetStyle(uint16 familyID, uint16 styleID) const;
	virtual	FontStyle*			GetStyle(const char* familyName,
									const char* styleName,
									uint16 familyID = 0xffff,
									uint16 styleID = 0xffff,
									uint16 face = 0);

	virtual	uint32				Revision();

private:
			struct font_directory;
			struct font_mapping;

			void				_AddDefaultMapping(const char* family,
									const char* style, const char* path);
			bool				_LoadRecentFontMappings();
			status_t			_AddMappedFont(const char* family,
									const char* style = NULL);
			void				_PrecacheFontFile(const ServerFont* font);
			void				_AddSystemPaths();
			void				_AddUserPaths();
			font_directory*		_FindDirectory(node_ref& nodeRef);
			void				_RemoveDirectory(font_directory* directory);
			status_t			_CreateDirectories(const char* path);
			status_t			_AddPath(const char* path);
			status_t			_AddPath(BEntry& entry,
									font_directory** _newDirectory = NULL);

			void				_ScanFontsIfNecessary();
			void				_ScanFonts();
			status_t			_ScanFontDirectory(font_directory& directory);
			status_t			_AddFont(font_directory& directory,
									BEntry& entry);
			void 				_RemoveStyle(font_directory& directory,
									FontStyle* style);
			void 				_RemoveStyle(dev_t device, uint64 directory,
									uint64 node);
			FontStyle*			_GetDefaultStyle(const char* familyName,
									const char* styleName,
									const char* fallbackFamily,
									const char* fallbackStyle,
									uint16 fallbackFace);
			status_t			_SetDefaultFonts();

private:
			status_t			fInitStatus;

			typedef BObjectList<font_directory, true>	DirectoryList;
			typedef BObjectList<font_mapping, true>		MappingList;

			DirectoryList		fDirectories;
			MappingList			fMappings;

			ObjectDeleter<ServerFont>
								fDefaultPlainFont;
			ObjectDeleter<ServerFont>
								fDefaultBoldFont;
			ObjectDeleter<ServerFont>
								fDefaultFixedFont;

			bool				fScanned;

};


/** @brief Process-wide pointer to the singleton GlobalFontManager. */
extern GlobalFontManager* gFontManager;

#endif	/* GLOBAL_FONT_MANAGER_H */
