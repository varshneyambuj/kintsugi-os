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
 *   Copyright 2001-2016, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Axel Dörfler, axeld@pinc-software.de
 */


/**
 * @file GlobalFontManager.cpp
 * @brief Implementation of the system-wide font catalog and watcher.
 *
 * Discovers fonts under the system and per-user font directories, runs
 * as a BLooper so node monitor messages can be folded into the catalog
 * incrementally, and resolves the desktop default plain/bold/fixed
 * fonts at startup. Font directories are tracked by node_ref and watched
 * for additions, moves, and removals so newly installed packages take
 * effect without an app_server restart.
 *
 * @see FontManager, AppFontManager
 */


#include "GlobalFontManager.h"

#include <new>

#include <Autolock.h>
#include <Debug.h>
#include <Directory.h>
#include <Entry.h>
#include <File.h>
#include <FindDirectory.h>
#include <Message.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <String.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include "FontFamily.h"
#include "ServerConfig.h"
#include "ServerFont.h"


//#define TRACE_GLOBAL_FONT_MANAGER
#ifdef TRACE_GLOBAL_FONT_MANAGER
#	define FTRACE(x) debug_printf x
#else
#	define FTRACE(x) ;
#endif


// TODO: needs some more work for multi-user support

/** @brief Process-wide pointer to the singleton GlobalFontManager. */
GlobalFontManager* gFontManager = NULL;
extern FT_Library gFreeTypeLibrary;


/**
 * @brief One watched font directory and the styles loaded out of it.
 *
 * Each directory is tracked by its node_ref so node monitor events can
 * be matched back to it, the owning user/group is captured for future
 * permission checks, and the styles list is held by raw pointer (the
 * FontManager keeps the reference count).
 */
struct GlobalFontManager::font_directory {
	node_ref	directory;
	uid_t		user;
	gid_t		group;
	bool		scanned;
	BObjectList<FontStyle> styles;

	FontStyle* FindStyle(const node_ref& nodeRef) const;
};


/**
 * @brief Default family/style -> entry_ref mapping used as a fallback.
 *
 * Populated from a hard-coded list of Noto fonts at startup so the
 * desktop has known-good plain/bold/fixed defaults even when scanning
 * has not finished yet.
 */
struct GlobalFontManager::font_mapping {
	BString		family;
	BString		style;
	entry_ref	ref;
};


/**
 * @brief Returns the style in this directory whose file matches @a nodeRef.
 *
 * @param nodeRef  Node reference of a font file.
 * @return Matching style, or NULL when none is registered for that file.
 */
FontStyle*
GlobalFontManager::font_directory::FindStyle(const node_ref& nodeRef) const
{
	for (int32 i = styles.CountItems(); i-- > 0;) {
		FontStyle* style = styles.ItemAt(i);

		if (nodeRef == style->NodeRef())
			return style;
	}

	return NULL;
}


/**
 * @brief Assembles a BEntry from a parent node_ref and a leaf name.
 *
 * Used to translate node monitor message fields ("device", "directory",
 * "name") into a usable BEntry handle.
 *
 * @param nodeRef  Parent directory's node_ref.
 * @param name     Leaf file name within @a nodeRef.
 * @param entry    Output: populated entry.
 * @retval B_OK         On success.
 * @retval other        Whatever entry_ref::set_name() or BEntry::SetTo() returns.
 */
static status_t
set_entry(node_ref& nodeRef, const char* name, BEntry& entry)
{
	entry_ref ref;
	ref.device = nodeRef.device;
	ref.directory = nodeRef.node;

	status_t status = ref.set_name(name);
	if (status != B_OK)
		return status;

	return entry.SetTo(&ref);
}


//	#pragma mark -


/**
 * @brief Initializes FreeType, registers the system/user font paths, and
 *        precaches the default plain and bold fonts.
 *
 * Constructor work is split between getting FreeType ready (without
 * which nothing else can run) and the directory walk; the directory
 * scan itself is deferred to the looper thread via a B_PULSE message
 * so the constructor returns quickly.
 */
GlobalFontManager::GlobalFontManager()
	: BLooper("GlobalFontManager"),
	fDirectories(10),
	fMappings(10),

	fDefaultPlainFont(NULL),
	fDefaultBoldFont(NULL),
	fDefaultFixedFont(NULL),

	fScanned(false)
{
	fInitStatus = FT_Init_FreeType(&gFreeTypeLibrary) == 0 ? B_OK : B_ERROR;
	if (fInitStatus == B_OK) {
		_AddSystemPaths();
		_AddUserPaths();
		_LoadRecentFontMappings();

		fInitStatus = _SetDefaultFonts();

		if (fInitStatus == B_OK) {
			// Precache the plain and bold fonts
			_PrecacheFontFile(fDefaultPlainFont.Get());
			_PrecacheFontFile(fDefaultBoldFont.Get());

			// Post a message so we scan the initial paths.
			PostMessage(B_PULSE);
		}
	}
}


/**
 * @brief Releases the default fonts, all known fonts, and FreeType itself.
 */
GlobalFontManager::~GlobalFontManager()
{
	fDefaultPlainFont.Unset();
	fDefaultBoldFont.Unset();
	fDefaultFixedFont.Unset();

	_RemoveAllFonts();

	FT_Done_FreeType(gFreeTypeLibrary);
}


/**
 * @brief Handles node monitor notifications for watched font directories.
 *
 * Routes B_NODE_MONITOR opcodes through three handlers:
 *  - @c B_ENTRY_CREATED:  add a new directory to the watch list, or load
 *    a new font file into the appropriate directory.
 *  - @c B_ENTRY_MOVED:    detect entries appearing in or leaving watched
 *    directories, including renames within the watched set.
 *  - @c B_ENTRY_REMOVED:  drop the directory or the affected style.
 *
 * After dispatch, runs a deferred scan if any earlier event flagged the
 * cache as out of date.
 *
 * @param message  Incoming BMessage from the node monitor.
 */
void
GlobalFontManager::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_NODE_MONITOR:
		{
			int32 opcode;
			if (message->FindInt32("opcode", &opcode) != B_OK)
				return;

			switch (opcode) {
				case B_ENTRY_CREATED:
				{
					const char* name;
					node_ref nodeRef;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("directory", &nodeRef.node) != B_OK
						|| message->FindString("name", &name) != B_OK)
						break;

					// TODO: make this better (possible under Haiku)
					snooze(100000);
						// let the font be written completely before trying to open it

					BEntry entry;
					if (set_entry(nodeRef, name, entry) != B_OK)
						break;

					if (entry.IsDirectory()) {
						// a new directory to watch for us
						_AddPath(entry);
					} else {
						// a new font
						font_directory* directory = _FindDirectory(nodeRef);
						if (directory == NULL) {
							// unknown directory? how come?
							break;
						}

						_AddFont(*directory, entry);
					}
					break;
				}

				case B_ENTRY_MOVED:
				{
					// has the entry been moved into a monitored directory or has
					// it been removed from one?
					const char* name;
					node_ref nodeRef;
					uint64 fromNode;
					uint64 node;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("to directory", &nodeRef.node) != B_OK
						|| message->FindInt64("from directory", (int64 *)&fromNode) != B_OK
						|| message->FindInt64("node", (int64 *)&node) != B_OK
						|| message->FindString("name", &name) != B_OK)
						break;

					font_directory* directory = _FindDirectory(nodeRef);

					BEntry entry;
					if (set_entry(nodeRef, name, entry) != B_OK)
						break;

					if (directory != NULL) {
						// something has been added to our watched font directories

						// test, if the source directory is one of ours as well
						nodeRef.node = fromNode;
						font_directory* fromDirectory = _FindDirectory(nodeRef);

						if (entry.IsDirectory()) {
							if (fromDirectory == NULL) {
								// there is a new directory to watch for us
								_AddPath(entry);
								FTRACE(("new directory moved in"));
							} else {
								// A directory from our watched directories has
								// been renamed or moved within the watched
								// directories - we only need to update the
								// path names of the styles in that directory
								nodeRef.node = node;
								directory = _FindDirectory(nodeRef);
								if (directory != NULL) {
									for (int32 i = 0; i < directory->styles.CountItems(); i++) {
										FontStyle* style = directory->styles.ItemAt(i);
										style->UpdatePath(directory->directory);
									}
								}
								FTRACE(("directory renamed"));
							}
						} else {
							if (fromDirectory != NULL) {
								// find style in source and move it to the target
								nodeRef.node = node;
								FontStyle* style;
								while ((style = fromDirectory->FindStyle(nodeRef)) != NULL) {
									fromDirectory->styles.RemoveItem(style, false);
									directory->styles.AddItem(style);
									style->UpdatePath(directory->directory);
								}
								FTRACE(("font moved"));
							} else {
								FTRACE(("font added: %s\n", name));
								_AddFont(*directory, entry);
							}
						}
					} else {
						// and entry has been removed from our font directories
						if (entry.IsDirectory()) {
							if (entry.GetNodeRef(&nodeRef) == B_OK
								&& (directory = _FindDirectory(nodeRef)) != NULL)
								_RemoveDirectory(directory);
						} else {
							// remove font style from directory
							_RemoveStyle(nodeRef.device, fromNode, node);
						}
					}
					break;
				}

				case B_ENTRY_REMOVED:
				{
					node_ref nodeRef;
					uint64 directoryNode;
					if (message->FindInt32("device", &nodeRef.device) != B_OK
						|| message->FindInt64("directory", (int64 *)&directoryNode) != B_OK
						|| message->FindInt64("node", &nodeRef.node) != B_OK)
						break;

					font_directory* directory = _FindDirectory(nodeRef);
					if (directory != NULL) {
						// the directory has been removed, so we remove it as well
						_RemoveDirectory(directory);
					} else {
						// remove font style from directory
						_RemoveStyle(nodeRef.device, directoryNode, nodeRef.node);
					}
					break;
				}
			}
			break;
		}

		default:
			BLooper::MessageReceived(message);
			break;
	}

	// Scan fonts here if we need to, preventing other threads from having to do so.
	_ScanFontsIfNecessary();
}


/**
 * @brief Returns the current catalog revision, lazily scanning fonts first.
 *
 * @return Revision number after any pending scan completes.
 */
uint32
GlobalFontManager::Revision()
{
	BAutolock locker(this);

	_ScanFontsIfNecessary();

	return FontManager::Revision();
}


/**
 * @brief Persists recent font mappings to disk.
 *
 * @todo Implement; currently a no-op placeholder.
 */
void
GlobalFontManager::SaveRecentFontMappings()
{
}


/**
 * @brief Records a fallback mapping from family/style to an on-disk file.
 *
 * Used during startup to seed the @c fMappings list with known-good
 * entries before any directory scan has finished, so default font
 * selection has something to draw against.
 *
 * @param family  Family name to register.
 * @param style   Style name to register.
 * @param path    File path to the font file.
 */
void
GlobalFontManager::_AddDefaultMapping(const char* family, const char* style,
	const char* path)
{
	font_mapping* mapping = new (std::nothrow) font_mapping;
	if (mapping == NULL)
		return;

	mapping->family = family;
	mapping->style = style;
	BEntry entry(path);

	if (entry.GetRef(&mapping->ref) != B_OK
		|| !entry.Exists()
		|| !fMappings.AddItem(mapping))
		delete mapping;
}


/**
 * @brief Seeds the mapping list with hard-coded Noto fallbacks.
 *
 * Looks up the system fonts directory, then registers Noto Sans
 * Regular/Bold and Noto Sans Mono Regular as default mappings.
 *
 * @return  true when the fonts directory was found and mappings were
 *          added, false when @c B_BEOS_FONTS_DIRECTORY is unavailable.
 *
 * @todo Replace the hard-coded list with a persisted config.
 */
bool
GlobalFontManager::_LoadRecentFontMappings()
{
	// default known mappings
	// TODO: load them for real, and use these as a fallback

	BPath ttfontsPath;
	if (find_directory(B_BEOS_FONTS_DIRECTORY, &ttfontsPath) == B_OK) {
		ttfontsPath.Append("ttfonts");

		BPath veraFontPath = ttfontsPath;
		veraFontPath.Append("NotoSans-Regular.ttf");
		_AddDefaultMapping("Noto Sans", "Book", veraFontPath.Path());

		veraFontPath.SetTo(ttfontsPath.Path());
		veraFontPath.Append("NotoSans-Bold.ttf");
		_AddDefaultMapping("Noto Sans", "Bold", veraFontPath.Path());

		veraFontPath.SetTo(ttfontsPath.Path());
		veraFontPath.Append("NotoSansMono-Regular.ttf");
		_AddDefaultMapping("Noto Sans Mono", "Regular", veraFontPath.Path());

		return true;
	}

	return false;
}


/**
 * @brief Registers a font from the fallback mappings for @a familyName.
 *
 * Walks @c fMappings looking for an entry whose family (and, if
 * provided, style) match. When found, ensures the parent directory is
 * known and falls through to @ref _AddFont() to load the file.
 *
 * @param familyName  Family to add.
 * @param styleName   Optional style; when NULL the first matching family entry wins.
 * @retval B_OK                On success.
 * @retval B_ENTRY_NOT_FOUND   No mapping matched.
 * @retval other               Errors from BEntry / _AddFont().
 */
status_t
GlobalFontManager::_AddMappedFont(const char* familyName, const char* styleName)
{
	FTRACE(("_AddMappedFont(family = \"%s\", style = \"%s\")\n",
		familyName, styleName));

	for (int32 i = 0; i < fMappings.CountItems(); i++) {
		font_mapping* mapping = fMappings.ItemAt(i);

		if (mapping->family == familyName) {
			if (styleName != NULL && mapping->style != styleName)
				continue;

			BEntry entry(&mapping->ref);
			if (entry.InitCheck() != B_OK)
				continue;

			// find parent directory

			node_ref nodeRef;
			nodeRef.device = mapping->ref.device;
			nodeRef.node = mapping->ref.directory;
			font_directory* directory = _FindDirectory(nodeRef);
			if (directory == NULL) {
				// unknown directory, maybe this is a user font - try
				// to create the missing directory
				BPath path(&entry);
				if (path.GetParent(&path) != B_OK
					|| _CreateDirectories(path.Path()) != B_OK
					|| (directory = _FindDirectory(nodeRef)) == NULL)
					continue;
			}

			return _AddFont(*directory, entry);
		}
	}

	return B_ENTRY_NOT_FOUND;
}


/**
 * @brief Resolves a default style with a chain of fallbacks.
 *
 * Tries the preferred (family, style) pair first, then a fallback
 * (family, style), then a face-mask scan, and finally style 0 of
 * family 0 as the absolute last resort.
 *
 * @param familyName       Preferred family.
 * @param styleName        Preferred style.
 * @param fallbackFamily   Family used when the preferred pair is missing.
 * @param fallbackStyle    Style used together with @a fallbackFamily.
 * @param fallbackFace     Face mask used as the next-to-last resort.
 * @return  Best available FontStyle, or NULL when the catalog is empty.
 */
FontStyle*
GlobalFontManager::_GetDefaultStyle(const char* familyName, const char* styleName,
	const char* fallbackFamily, const char* fallbackStyle,
	uint16 fallbackFace)
{
	// try to find a matching font
	FontStyle* style = GetStyle(familyName, styleName);
	if (style == NULL) {
		style = GetStyle(fallbackFamily, fallbackStyle);
		if (style == NULL) {
			style = FindStyleMatchingFace(fallbackFace);
			if (style == NULL && FamilyAt(0) != NULL)
				style = FamilyAt(0)->StyleAt(0);
		}
	}

	return style;
}


/**
 * @brief Resolves the desktop default plain, bold, and fixed fonts.
 *
 * Each default is selected by @ref _GetDefaultStyle() against the
 * compile-time DEFAULT_/FALLBACK_ macros; the fixed font additionally
 * has its spacing forced to @c B_FIXED_SPACING.
 *
 * @retval B_OK         All three defaults were resolved.
 * @retval B_ERROR      No usable plain font is available.
 * @retval B_NO_MEMORY  Allocation of a ServerFont failed.
 */
status_t
GlobalFontManager::_SetDefaultFonts()
{
	FontStyle* style = NULL;

	// plain font
	style = _GetDefaultStyle(DEFAULT_PLAIN_FONT_FAMILY, DEFAULT_PLAIN_FONT_STYLE,
		FALLBACK_PLAIN_FONT_FAMILY, FALLBACK_PLAIN_FONT_STYLE, B_REGULAR_FACE);
	if (style == NULL)
		return B_ERROR;

	fDefaultPlainFont.SetTo(new (std::nothrow) ServerFont(*style,
		DEFAULT_FONT_SIZE));
	if (!fDefaultPlainFont.IsSet())
		return B_NO_MEMORY;

	// bold font
	style = _GetDefaultStyle(DEFAULT_BOLD_FONT_FAMILY, DEFAULT_BOLD_FONT_STYLE,
		FALLBACK_BOLD_FONT_FAMILY, FALLBACK_BOLD_FONT_STYLE, B_BOLD_FACE);

	fDefaultBoldFont.SetTo(new (std::nothrow) ServerFont(*style,
		DEFAULT_FONT_SIZE));
	if (!fDefaultBoldFont.IsSet())
		return B_NO_MEMORY;

	// fixed font
	style = _GetDefaultStyle(DEFAULT_FIXED_FONT_FAMILY, DEFAULT_FIXED_FONT_STYLE,
		FALLBACK_FIXED_FONT_FAMILY, FALLBACK_FIXED_FONT_STYLE, B_REGULAR_FACE);

	fDefaultFixedFont.SetTo(new (std::nothrow) ServerFont(*style,
		DEFAULT_FONT_SIZE));
	if (!fDefaultFixedFont.IsSet())
		return B_NO_MEMORY;

	fDefaultFixedFont->SetSpacing(B_FIXED_SPACING);

	return B_OK;
}


/**
 * @brief Removes @a style from @a directory and from the catalog.
 *
 * The FontStyle object itself may persist if other code still holds a
 * reference; the manager simply unhooks it from the live tables.
 *
 * @param directory  Directory the style currently belongs to.
 * @param style      Style being removed.
 */
void
GlobalFontManager::_RemoveStyle(font_directory& directory, FontStyle* style)
{
	FTRACE(("font removed: %s\n", style->Name()));

	directory.styles.RemoveItem(style);

	_RemoveFont(style->Family()->ID(), style->ID());
}


/**
 * @brief Removes a style identified by raw node monitor fields.
 *
 * Used from the B_ENTRY_REMOVED / B_ENTRY_MOVED handlers where only the
 * device + parent directory inode + file inode are available; resolves
 * those into a font_directory and a FontStyle and delegates to the
 * other _RemoveStyle() overload.
 *
 * @param device         Device ID from the node monitor message.
 * @param directoryNode  Inode of the parent directory.
 * @param node           Inode of the removed file.
 */
void
GlobalFontManager::_RemoveStyle(dev_t device, uint64 directoryNode, uint64 node)
{
	// remove font style from directory
	node_ref nodeRef;
	nodeRef.device = device;
	nodeRef.node = directoryNode;

	font_directory* directory = _FindDirectory(nodeRef);
	if (directory != NULL) {
		// find style in directory and remove it
		nodeRef.node = node;
		FontStyle* style;
		while ((style = directory->FindStyle(nodeRef)) != NULL)
			_RemoveStyle(*directory, style);
	}
}


/**
 * @brief Returns the number of system font families, scanning if needed.
 *
 * @return Family count.
 */
int32
GlobalFontManager::CountFamilies()
{
	_ScanFontsIfNecessary();

	return FontManager::CountFamilies();
}


/**
 * @brief Returns the number of styles in a named family, scanning if needed.
 *
 * @param familyName  Family to query.
 * @return Style count, or 0 when @a familyName is unknown.
 */
int32
GlobalFontManager::CountStyles(const char* familyName)
{
	_ScanFontsIfNecessary();

	FontFamily* family = GetFamily(familyName);
	if (family)
		return family->CountStyles();

	return 0;
}


/**
 * @brief Returns the number of styles in a family by ID, scanning if needed.
 *
 * @param familyID  Numeric family ID.
 * @return Style count, or 0 when @a familyID is unknown.
 */
int32
GlobalFontManager::CountStyles(uint16 familyID)
{
	_ScanFontsIfNecessary();

	FontFamily* family = GetFamily(familyID);
	if (family)
		return family->CountStyles();

	return 0;
}


/**
 * @brief Forwarding override exposing FontManager::GetStyle to BLooper users.
 *
 * @param familyID  Numeric family ID.
 * @param styleID   Numeric style ID.
 * @return The matching FontStyle, or NULL.
 */
FontStyle*
GlobalFontManager::GetStyle(uint16 familyID, uint16 styleID) const
{
	return FontManager::GetStyle(familyID, styleID);
}


/**
 * @brief Resolves a style with extra fallbacks for the system catalog.
 *
 * Behaves like FontManager::GetStyle() but additionally consults the
 * @c fMappings list and triggers a full scan before giving up, so a
 * style that exists on disk but has not been loaded yet can still be
 * found.
 *
 * @param familyName  Family name, or NULL/"" to use @a familyID.
 * @param styleName   Style name, or NULL/"" to use @a styleID / @a face.
 * @param familyID    Family ID fallback when @a familyName is empty.
 * @param styleID     Style ID fallback when both names are empty.
 * @param face        Face mask used as the last-resort selector.
 * @return The closest FontStyle, or NULL.
 *
 * @note  Caller must hold the manager lock.
 */
FontStyle*
GlobalFontManager::GetStyle(const char* familyName, const char* styleName,
	uint16 familyID, uint16 styleID, uint16 face)
{
	ASSERT(IsLocked());

	if (styleID != 0xffff && (familyName == NULL || !familyName[0])
		&& (styleName == NULL || !styleName[0])) {
		return GetStyle(familyID, styleID);
	}

	// find family

	FontFamily* family;
	if (familyName != NULL && familyName[0])
		family = GetFamily(familyName);
	else
		family = GetFamily(familyID);

	if (family == NULL)
		return NULL;

	// find style

	if (styleName != NULL && styleName[0]) {
		FontStyle* fontStyle = family->GetStyle(styleName);
		if (fontStyle != NULL)
			return fontStyle;

		// before we fail, we try the mappings for a match
		if (_AddMappedFont(family->Name(), styleName) == B_OK) {
			fontStyle = family->GetStyle(styleName);
			if (fontStyle != NULL)
				return fontStyle;
		}

		_ScanFonts();
		return family->GetStyle(styleName);
	}

	// try to get from face
	return family->GetStyleMatchingFace(face);
}


/**
 * @brief Reads the font file end-to-end so the kernel populates its file cache.
 *
 * Cheap warm-up to avoid the first text drawing operation hitting disk.
 * Skipped silently when memory cannot be allocated, since caching is a
 * pure optimization.
 *
 * @param font  Font whose backing file should be primed; may be NULL.
 */
void
GlobalFontManager::_PrecacheFontFile(const ServerFont* font)
{
	if (font == NULL)
		return;

	size_t bufferSize = 32768;
	uint8* buffer = new (std::nothrow) uint8[bufferSize];
	if (buffer == NULL) {
		// We don't care. Pre-caching doesn't make sense anyways when there
		// is not enough RAM...
		return;
	}

	BFile file(font->Path(), B_READ_ONLY);
	if (file.InitCheck() != B_OK) {
		delete[] buffer;
		return;
	}

	while (true) {
		// We just want the file in the kernel file cache...
		ssize_t read = file.Read(buffer, bufferSize);
		if (read < (ssize_t)bufferSize)
			break;
	}

	delete[] buffer;
}


/**
 * @brief Registers the system-wide font directories under the watcher.
 *
 * Always adds @c B_SYSTEM_FONTS_DIRECTORY; in non-TEST builds also
 * adds @c B_SYSTEM_NONPACKAGED_FONTS_DIRECTORY so locally installed
 * fonts are picked up.
 */
void
GlobalFontManager::_AddSystemPaths()
{
	BPath path;
	if (find_directory(B_SYSTEM_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());

	// We don't scan these in test mode to help shave off some startup time
#if !TEST_MODE
	if (find_directory(B_SYSTEM_NONPACKAGED_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());
#endif
}


/**
 * @brief Registers the per-user font directories under the watcher.
 *
 * Skipped in TEST builds to keep tests deterministic.
 *
 * @todo Honor a "safe mode" flag and avoid loading user fonts then.
 */
void
GlobalFontManager::_AddUserPaths()
{
#if !TEST_MODE
	// TODO: avoids user fonts in safe mode
	BPath path;
	if (find_directory(B_USER_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());
	if (find_directory(B_USER_NONPACKAGED_FONTS_DIRECTORY, &path, true) == B_OK)
		_AddPath(path.Path());
#endif
}


/**
 * @brief Runs a full scan when the catalog is flagged as out of date.
 *
 * Invariant: after this call returns, every directory tracked by the
 * manager has been walked at least once.
 */
void
GlobalFontManager::_ScanFontsIfNecessary()
{
	if (!fScanned)
		_ScanFonts();
}


/**
 * @brief Scans every directory currently tracked by the manager.
 */
void
GlobalFontManager::_ScanFonts()
{
	if (fScanned)
		return;

	for (int32 i = fDirectories.CountItems(); i-- > 0;) {
		font_directory* directory = fDirectories.ItemAt(i);

		if (directory->scanned)
			continue;

		_ScanFontDirectory(*directory);
	}

	fScanned = true;
}


/**
 * @brief Loads every face (and named instance) of a font file into the catalog.
 *
 * Opens @a entry once with index -1 to get the face count, then walks
 * each face. For variable fonts the high 16 bits of the FreeType face
 * index encode the named instance, so the inner loop iterates over the
 * variation count as well.
 *
 * @param directory  Directory the file lives in.
 * @param entry      The font file to load.
 * @retval B_OK         All faces were processed.
 * @retval B_ERROR      A FreeType call failed unrecoverably.
 * @retval other        Errors propagated from FontManager::_AddFont().
 */
status_t
GlobalFontManager::_AddFont(font_directory& directory, BEntry& entry)
{
	node_ref nodeRef;
	status_t status = entry.GetNodeRef(&nodeRef);
	if (status < B_OK)
		return status;

	BPath path;
	status = entry.GetPath(&path);
	if (status < B_OK)
		return status;

	FT_Face face;
	FT_Error error = FT_New_Face(gFreeTypeLibrary, path.Path(), -1, &face);
	if (error != 0)
		return B_ERROR;
	FT_Long count = face->num_faces;
	FT_Done_Face(face);

	for (FT_Long i = 0; i < count; i++) {
		FT_Error error = FT_New_Face(gFreeTypeLibrary, path.Path(), -(i + 1), &face);
		if (error != 0)
			return B_ERROR;
		uint32 variableCount = (face->style_flags & 0x7fff0000) >> 16;
		FT_Done_Face(face);

		uint32 j = variableCount == 0 ? 0 : 1;
		do {
			FT_Long faceIndex = i | (j << 16);
			error = FT_New_Face(gFreeTypeLibrary, path.Path(), faceIndex, &face);
			if (error != 0)
				return B_ERROR;

			uint16 familyID, styleID;
			status = FontManager::_AddFont(face, nodeRef, path.Path(), familyID, styleID);
			if (status == B_NAME_IN_USE) {
				status = B_OK;
				j++;
				continue;
			}
			if (status < B_OK)
				return status;
			directory.styles.AddItem(GetStyle(familyID, styleID));
			j++;
		} while (j <= variableCount);
	}

	return B_OK;
}


/**
 * @brief Finds the tracked font_directory matching @a nodeRef.
 *
 * @param nodeRef  Directory node reference.
 * @return  Matching font_directory, or NULL when none is tracked.
 */
GlobalFontManager::font_directory*
GlobalFontManager::_FindDirectory(node_ref& nodeRef)
{
	for (int32 i = fDirectories.CountItems(); i-- > 0;) {
		font_directory* directory = fDirectories.ItemAt(i);

		if (directory->directory == nodeRef)
			return directory;
	}

	return NULL;
}


/**
 * @brief Stops watching a directory and removes it from the tracked list.
 *
 * @param directory  Directory to forget; @c delete is called on it.
 *
 * @todo Also remove the styles previously loaded from this directory.
 */
void
GlobalFontManager::_RemoveDirectory(font_directory* directory)
{
	FTRACE(("FontManager: Remove directory (%" B_PRIdINO ")!\n",
		directory->directory.node));

	fDirectories.RemoveItem(directory, false);

	// TODO: remove styles from this directory!

	watch_node(&directory->directory, B_STOP_WATCHING, this);
	delete directory;
}


/**
 * @brief Convenience overload accepting a path string.
 *
 * @param path  File system path to track.
 * @return  Result of the BEntry-based @c _AddPath() overload.
 */
status_t
GlobalFontManager::_AddPath(const char* path)
{
	BEntry entry;
	status_t status = entry.SetTo(path);
	if (status != B_OK)
		return status;

	return _AddPath(entry);
}


/**
 * @brief Adds @a entry as a watched font directory if not already known.
 *
 * Records the directory's owner and group, starts a node monitor on
 * it, and resets the @c fScanned flag so the next access triggers a
 * walk.
 *
 * @param entry          Directory entry to register.
 * @param _newDirectory  Optional output: the resulting font_directory
 *                       (newly created or already-known).
 * @retval B_OK         On success.
 * @retval B_NO_MEMORY  Could not allocate the bookkeeping struct.
 * @retval other        BEntry / stat errors.
 *
 * @note Failure to start the node monitor is logged but not fatal.
 */
status_t
GlobalFontManager::_AddPath(BEntry& entry, font_directory** _newDirectory)
{
	node_ref nodeRef;
	status_t status = entry.GetNodeRef(&nodeRef);
	if (status != B_OK)
		return status;

	// check if we are already know this directory

	font_directory* directory = _FindDirectory(nodeRef);
	if (directory != NULL) {
		if (_newDirectory)
			*_newDirectory = directory;
		return B_OK;
	}

	// it's a new one, so let's add it

	directory = new (std::nothrow) font_directory;
	if (directory == NULL)
		return B_NO_MEMORY;

	struct stat stat;
	status = entry.GetStat(&stat);
	if (status != B_OK) {
		delete directory;
		return status;
	}

	directory->directory = nodeRef;
	directory->user = stat.st_uid;
	directory->group = stat.st_gid;
	directory->scanned = false;

	status = watch_node(&nodeRef, B_WATCH_DIRECTORY, this);
	if (status != B_OK) {
		// we cannot watch this directory - while this is unfortunate,
		// it's not a critical error
		printf("could not watch directory %" B_PRIdDEV ":%" B_PRIdINO "\n",
			nodeRef.device, nodeRef.node);
			// TODO: should go into syslog()
	} else {
		BPath path(&entry);
		FTRACE(("FontManager: now watching: %s\n", path.Path()));
	}

	fDirectories.AddItem(directory);

	if (_newDirectory)
		*_newDirectory = directory;

	fScanned = false;
	return B_OK;
}


/**
 * @brief Builds intermediate font_directories on demand for mapped fonts.
 *
 * Walks @a path upwards until a known parent directory is hit, then
 * unwinds back down adding each missing level via @ref _AddPath(). Used
 * by @ref _AddMappedFont() to make sure user-installed fonts that live
 * deep inside an already-watched root can still be found.
 *
 * @param path  Absolute filesystem path to materialize.
 * @retval B_OK                 The directory chain is now tracked.
 * @retval B_ENTRY_NOT_FOUND    Walked past the root without finding a
 *                              known ancestor.
 * @retval other                Propagated BEntry / _AddPath errors.
 */
status_t
GlobalFontManager::_CreateDirectories(const char* path)
{
	FTRACE(("_CreateDirectories(path = %s)\n", path));

	if (!strcmp(path, "/")) {
		// we walked our way up to the root
		return B_ENTRY_NOT_FOUND;
	}

	BEntry entry;
	status_t status = entry.SetTo(path);
	if (status != B_OK)
		return status;

	node_ref nodeRef;
	status = entry.GetNodeRef(&nodeRef);
	if (status != B_OK)
		return status;

	// check if we are already know this directory

	font_directory* directory = _FindDirectory(nodeRef);
	if (directory != NULL)
		return B_OK;

	// We don't know this one yet - keep walking the path upwards
	// and try to find a match.

	BPath parent(path);
	status = parent.GetParent(&parent);
	if (status != B_OK)
		return status;

	status = _CreateDirectories(parent.Path());
	if (status != B_OK)
		return status;

	// We have our match, create sub directory

	return _AddPath(path);
}


/**
 * @brief Walks a directory and registers each contained font file.
 *
 * Recurses into subdirectories (adding them to the watcher first), and
 * for every file calls @ref _AddFont(). Marks the directory scanned so
 * subsequent calls become no-ops.
 *
 * @param fontDirectory  Directory to scan.
 * @retval B_OK    Walk completed (even if individual files failed).
 * @retval other   BDirectory::SetTo() error.
 */
status_t
GlobalFontManager::_ScanFontDirectory(font_directory& fontDirectory)
{
	// This bad boy does all the real work. It loads each entry in the
	// directory. If a valid font file, it adds both the family and the style.

	if (fontDirectory.scanned)
		return B_OK;

	BDirectory directory;
	status_t status = directory.SetTo(&fontDirectory.directory);
	if (status != B_OK)
		return status;

	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		if (entry.IsDirectory()) {
			// scan this directory recursively
			font_directory* newDirectory;
			if (_AddPath(entry, &newDirectory) == B_OK && newDirectory != NULL
				&& !newDirectory->scanned) {
				_ScanFontDirectory(*newDirectory);
			}

			continue;
		}

// TODO: Commenting this out makes my "Unicode glyph lookup"
// work with our default fonts. The real fix is to select the
// Unicode char map (if supported), and/or adjust the
// utf8 -> glyph-index mapping everywhere to handle other
// char maps. We could also ignore fonts that don't support
// the Unicode lookup as a temporary "solution".
#if 0
		FT_CharMap charmap = _GetSupportedCharmap(face);
		if (!charmap) {
		    FT_Done_Face(face);
		    continue;
    	}

		face->charmap = charmap;
#endif

		_AddFont(fontDirectory, entry);
			// takes over ownership of the FT_Face object
	}

	fontDirectory.scanned = true;
	return B_OK;
}


/**
 * @brief Finds a FontFamily by name, falling through to mappings + scan.
 *
 * Tries the in-memory family list, then the @c fMappings list, and
 * finally a full directory scan before declaring the family absent.
 *
 * @param name  Family to look up; NULL is permitted and returns NULL.
 * @return Family pointer, or NULL when not present anywhere.
 */
FontFamily*
GlobalFontManager::GetFamily(const char* name)
{
	if (name == NULL)
		return NULL;

	FontFamily* family = _FindFamily(name);
	if (family != NULL)
		return family;

	if (fScanned)
		return NULL;

	// try font mappings before failing
	if (_AddMappedFont(name) == B_OK)
		return _FindFamily(name);

	_ScanFonts();
	return _FindFamily(name);
}


/**
 * @brief Forwarding override exposing FontManager::GetFamily by ID.
 *
 * @param familyID  Numeric family ID.
 * @return Family pointer, or NULL when @a familyID is unknown.
 */
FontFamily*
GlobalFontManager::GetFamily(uint16 familyID) const
{
	return FontManager::GetFamily(familyID);
}


/**
 * @brief Returns the desktop default plain (proportional) font.
 *
 * @return Pointer owned by the manager; never null after a successful
 *         constructor.
 */
const ServerFont*
GlobalFontManager::DefaultPlainFont() const
{
	return fDefaultPlainFont.Get();
}


/**
 * @brief Returns the desktop default bold font.
 *
 * @return Pointer owned by the manager.
 */
const ServerFont*
GlobalFontManager::DefaultBoldFont() const
{
	return fDefaultBoldFont.Get();
}


/**
 * @brief Returns the desktop default fixed-width font.
 *
 * @return Pointer owned by the manager.
 */
const ServerFont*
GlobalFontManager::DefaultFixedFont() const
{
	return fDefaultFixedFont.Get();
}
