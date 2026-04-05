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
 *     Ambuj Varshney, varshney@ambuj.se
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 *   Copyright 2001-2015, Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       DarkWyrm <bpmagic@columbus.rr.com>
 *       Jérôme Duval, jerome.duval@free.fr
 *       Axel Dörfler, axeld@pinc-software.de
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Andrej Spielmann, <andrej.spielmann@seh.ox.ac.uk>
 */


/**
 * @file Font.cpp
 * @brief Implementation of BFont, the font description and metrics class
 *
 * BFont encapsulates a font family, style, and size, along with rendering attributes
 * such as shear, rotation, and spacing. It communicates with the app_server to query
 * font metrics and available font families.
 *
 * @see BView, BString
 */


#include <AppServerLink.h>
#include <FontPrivate.h>
#include <ObjectList.h>
#include <ServerProtocol.h>
#include <truncate_string.h>
#include <utf8_functions.h>

#include <Autolock.h>
#include <Font.h>
#include <Locker.h>
#include <Message.h>
#include <PortLink.h>
#include <Rect.h>
#include <Shape.h>
#include <String.h>

#include <new>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>


using namespace std;

/** @brief Sentinel value stored in fHeight.ascent when the cached height is invalid. */
const float kUninitializedAscent = INFINITY;

/** @brief Sentinel value stored in fExtraFlags when the cached extra flags are invalid. */
const uint32 kUninitializedExtraFlags = 0xffffffff;

// The actual objects which the globals point to
/** @brief Storage for the global plain system font instance. */
static BFont sPlainFont;

/** @brief Storage for the global bold system font instance. */
static BFont sBoldFont;

/** @brief Storage for the global fixed-width system font instance. */
static BFont sFixedFont;

/** @brief Pointer to the system plain font; equivalent to B_PLAIN_FONT. */
const BFont* be_plain_font = &sPlainFont;

/** @brief Pointer to the system bold font; equivalent to B_BOLD_FONT. */
const BFont* be_bold_font = &sBoldFont;

/** @brief Pointer to the system fixed-width font; equivalent to B_FIXED_FONT. */
const BFont* be_fixed_font = &sFixedFont;


struct style {
	BString	name;
	uint16	face;
	uint32	flags;
};

struct family {
	BString	name;
	uint32	flags;
	BObjectList<style> styles;
};

namespace {

class FontList : public BLocker {
public:
								FontList();
	virtual						~FontList();

	static	FontList*			Default();

			bool				UpdatedOnServer();

			status_t			FamilyAt(int32 index, font_family* _family,
									uint32* _flags);
			status_t			StyleAt(font_family family, int32 index,
									font_style* _style, uint16* _face,
									uint32* _flags);

			int32				CountFamilies();
			int32				CountStyles(font_family family);

private:
			status_t			_UpdateIfNecessary();
			status_t			_Update();
			uint32				_RevisionOnServer();
			family*				_FindFamily(font_family name);
	static	void				_InitSingleton();

private:
			BObjectList<family>	fFamilies;
			family*				fLastFamily;
			bigtime_t			fLastUpdate;
			uint32				fRevision;

	static	pthread_once_t		sDefaultInitOnce;
	static	FontList*			sDefaultInstance;
};

pthread_once_t FontList::sDefaultInitOnce = PTHREAD_ONCE_INIT;
FontList* FontList::sDefaultInstance = NULL;

}	// unnamed namespace


//	#pragma mark -


/**
 * @brief Compares two font family entries by name for sorted insertion.
 *
 * Used as a comparator for BObjectList binary search and binary insert
 * operations on the internal family list.
 *
 * @param a Pointer to the first family entry.
 * @param b Pointer to the second family entry.
 * @return Negative, zero, or positive value as per strcmp() semantics.
 * @note Currently uses a simple byte comparison; locale-aware collation is
 *       not yet implemented.
 */
static int
compare_families(const family* a, const family* b)
{
	// TODO: compare font names according to the user's locale settings
	return strcmp(a->name.String(), b->name.String());
}


namespace {

/**
 * @brief Constructs the FontList singleton and initialises its BLocker.
 *
 * Sets the internal family cache to empty and marks the revision as
 * uninitialized so that the first call to any public accessor triggers
 * a full update from the app_server.
 */
FontList::FontList()
	: BLocker("font list"),
	fLastFamily(NULL),
	fLastUpdate(0),
	fRevision(0)
{
}


/**
 * @brief Destroys the FontList, releasing all cached family and style data.
 */
FontList::~FontList()
{
}


/**
 * @brief Returns the process-wide FontList singleton, creating it if necessary.
 *
 * Uses pthread_once to guarantee that _InitSingleton() is called exactly once,
 * even if multiple threads race to access the singleton for the first time.
 *
 * @return Pointer to the singleton FontList instance.
 */
/*static*/ FontList*
FontList::Default()
{
	if (sDefaultInstance == NULL)
		pthread_once(&sDefaultInitOnce, &_InitSingleton);

	return sDefaultInstance;
}


/**
 * @brief Returns whether the app_server's font list is newer than the cached copy.
 *
 * Queries the server for the current font-list revision number and compares it
 * against the locally cached revision.
 *
 * @return @c true if the server has a newer font list, @c false if the cache
 *         is already up to date.
 */
bool
FontList::UpdatedOnServer()
{
	return _RevisionOnServer() != fRevision;
}


/**
 * @brief Retrieves the font family name and flags at the given index.
 *
 * Acquires the internal lock, updates the cache if necessary, then copies
 * the family name and optional flags into the caller-supplied buffers.
 *
 * @param index   Zero-based index into the sorted family list.
 * @param _family Output buffer for the family name; must not be NULL.
 * @param _flags  Optional output for family flags; may be NULL.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If @a index is out of range.
 */
status_t
FontList::FamilyAt(int32 index, font_family* _family, uint32* _flags)
{
	BAutolock locker(this);

	status_t status = _UpdateIfNecessary();
	if (status < B_OK)
		return status;

	::family* family = fFamilies.ItemAt(index);
	if (family == NULL)
		return B_BAD_VALUE;

	memcpy(*_family, family->name.String(), family->name.Length() + 1);
	if (_flags)
		*_flags = family->flags;
	return B_OK;
}


/**
 * @brief Retrieves the style name, face flags, and flags for a given family and index.
 *
 * Acquires the internal lock, updates the cache if necessary, locates the
 * requested family by name, and copies the style entry at @a index into the
 * caller-supplied buffers.
 *
 * @param familyName Name of the font family to query.
 * @param index      Zero-based index into the family's style list.
 * @param _style     Output buffer for the style name; must not be NULL.
 * @param _face      Optional output for the face flags; may be NULL.
 * @param _flags     Optional output for the style flags; may be NULL.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If the family is not found or @a index is out of range.
 * @note The face value returned is derived from the style name heuristically
 *       and may not be perfectly accurate for all fonts.
 */
status_t
FontList::StyleAt(font_family familyName, int32 index, font_style* _style,
	uint16* _face, uint32* _flags)
{
	BAutolock locker(this);

	status_t status = _UpdateIfNecessary();
	if (status < B_OK)
		return status;

	::family* family = _FindFamily(familyName);
	if (family == NULL)
		return B_BAD_VALUE;

	::style* style = family->styles.ItemAt(index);
	if (style == NULL)
		return B_BAD_VALUE;

	memcpy(*_style, style->name.String(), style->name.Length() + 1);
	if (_face)
		*_face = style->face;
	if (_flags)
		*_flags = style->flags;
	return B_OK;
}


/**
 * @brief Returns the number of font families in the cached list.
 *
 * Acquires the lock and refreshes the cache from the server if the cached
 * data is stale, then returns the count.
 *
 * @return The number of available font families.
 */
int32
FontList::CountFamilies()
{
	BAutolock locker(this);

	_UpdateIfNecessary();
	return fFamilies.CountItems();
}


/**
 * @brief Returns the number of styles available for the named font family.
 *
 * Acquires the lock, refreshes the cache if necessary, then looks up the
 * family by name and returns its style count.
 *
 * @param familyName Name of the font family to query.
 * @return The number of styles, or 0 if the family is not found.
 */
int32
FontList::CountStyles(font_family familyName)
{
	BAutolock locker(this);

	_UpdateIfNecessary();

	::family* family = _FindFamily(familyName);
	if (family == NULL)
		return 0;

	return family->styles.CountItems();
}


/**
 * @brief Fetches the complete family/style list from the app_server.
 *
 * Sends AS_GET_FAMILY_AND_STYLES requests for each family index until the
 * server signals completion, populating fFamilies in sorted order. If the
 * server's revision number changes during the update the method recurses to
 * ensure the local cache converges with the server state.
 *
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_NO_MEMORY If a family or style object could not be allocated.
 */
status_t
FontList::_Update()
{
	// check version

	uint32 revision = _RevisionOnServer();
	fLastUpdate = system_time();

	// are we up-to-date already?
	if (revision == fRevision)
		return B_OK;

	fFamilies.MakeEmpty();
	fLastFamily = NULL;

	BPrivate::AppServerLink link;

	for (int32 index = 0;; index++) {
		link.StartMessage(AS_GET_FAMILY_AND_STYLES);
		link.Attach<int32>(index);

		int32 status;
		if (link.FlushWithReply(status) != B_OK
			|| status != B_OK)
			break;

		::family* family = new (nothrow) ::family;
		if (family == NULL)
			return B_NO_MEMORY;

		link.ReadString(family->name);
		link.Read<uint32>(&family->flags);

		int32 styleCount;
		link.Read<int32>(&styleCount);

		for (int32 i = 0; i < styleCount; i++) {
			::style* style = new (nothrow) ::style;
			if (style == NULL) {
				delete family;
				return B_NO_MEMORY;
			}

			link.ReadString(style->name);
			link.Read<uint16>(&style->face);
			link.Read<uint32>(&style->flags);

			family->styles.AddItem(style);
		}

		fFamilies.BinaryInsert(family, compare_families);
	}

	fRevision = revision;

	// if the font list has been changed in the mean time, just update again
	if (UpdatedOnServer())
		_Update();

	return B_OK;
}


/**
 * @brief Refreshes the family cache from the server only if the cache is stale.
 *
 * The cache is considered fresh if it was populated within the last second.
 * If the cache is older than one second, _Update() is called.
 *
 * @return A status code forwarded from _Update(), or B_OK if the cache is
 *         still fresh.
 */
status_t
FontList::_UpdateIfNecessary()
{
	// an updated font list is at least valid for 1 second
	if (fLastUpdate > system_time() - 1000000)
		return B_OK;

	return _Update();
}


/**
 * @brief Queries the app_server for the current font-list revision number.
 *
 * Sends an AS_GET_FONT_LIST_REVISION request. If the server cannot be
 * reached the locally cached revision is returned so that the caller
 * behaves as if the list were already up to date.
 *
 * @return The server-side revision number, or fRevision on communication failure.
 */
uint32
FontList::_RevisionOnServer()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_FONT_LIST_REVISION);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK) {
		// Go on as if our list is up to date. If it is a one-time thing
		// we'll try again soon. If it isn't, we have a bigger problem.
		return fRevision;
	}

	uint32 revision;
	link.Read<uint32>(&revision);

	return revision;
}


/**
 * @brief Looks up a family entry by name using binary search with a one-entry cache.
 *
 * Checks the most-recently-returned family first for O(1) repeated lookups of
 * the same family. Falls back to a binary search of the sorted fFamilies list.
 *
 * @param name The font family name to search for.
 * @return Pointer to the matching family entry, or NULL if not found.
 */
family*
FontList::_FindFamily(font_family name)
{
	if (fLastFamily != NULL && fLastFamily->name == name)
		return fLastFamily;

	::family family;
	family.name = name;
	fLastFamily = const_cast< ::family*>(fFamilies.BinarySearch(family,
		compare_families));
	return fLastFamily;
}


/**
 * @brief Allocates the FontList singleton; called exactly once via pthread_once.
 *
 * @see FontList::Default()
 */
/*static*/ void
FontList::_InitSingleton()
{
	sDefaultInstance = new FontList;
}

}	// unnamed namespace


//	#pragma mark -


/**
 * @brief Initialises the three global system font objects from the app_server.
 *
 * Called once during kit initialisation. Sends AS_GET_SYSTEM_FONTS and
 * populates sPlainFont, sBoldFont, and sFixedFont from the server reply.
 * On failure a debug message is printed and the globals retain their default
 * constructed values.
 *
 * @note This is an internal initialisation function and must not be called
 *       by application code.
 */
void
_init_global_fonts_()
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_SYSTEM_FONTS);

	int32 code;
	if (link.FlushWithReply(code) != B_OK
		|| code != B_OK) {
		printf("DEBUG: Couldn't initialize global fonts!\n");
		return;
	}

	char type[B_OS_NAME_LENGTH];

	while (link.ReadString(type, sizeof(type)) >= B_OK && type[0]) {
		BFont dummy;
		BFont* font = &dummy;

		if (!strcmp(type, "plain"))
			font = &sPlainFont;
		else if (!strcmp(type, "bold"))
			font = &sBoldFont;
		else if (!strcmp(type, "fixed"))
			font = &sFixedFont;

		link.Read<uint16>(&font->fFamilyID);
		link.Read<uint16>(&font->fStyleID);
		link.Read<float>(&font->fSize);
		link.Read<uint16>(&font->fFace);
		link.Read<uint32>(&font->fFlags);

		font->fHeight.ascent = kUninitializedAscent;
		font->fExtraFlags = kUninitializedExtraFlags;
	}
}


/**
 * @brief Internal font control hook; currently a no-op placeholder.
 *
 * @param font The font to operate on.
 * @param cmd  Command identifier.
 * @param data Command-specific data pointer.
 */
void
_font_control_(BFont* font, int32 cmd, void* data)
{
}


/**
 * @brief Retrieves font cache configuration parameters (stub).
 *
 * @param id  Identifier of the cache parameter to retrieve.
 * @param set Output buffer for the parameter value.
 * @return Always returns B_ERROR; not yet implemented.
 */
status_t
get_font_cache_info(uint32 id, void* set)
{
	return B_ERROR;
}


/**
 * @brief Sets font cache configuration parameters (stub).
 *
 * @param id  Identifier of the cache parameter to set.
 * @param set Pointer to the new parameter value.
 * @return Always returns B_ERROR; not yet implemented.
 */
status_t
set_font_cache_info(uint32 id, void* set)
{
	return B_ERROR;
}


/**
 * @brief Sets a named system font on the app_server.
 *
 * This internal function replaces the BeOS R5 global-area hack that the
 * Font preferences panel used to update system fonts. It sends
 * AS_SET_SYSTEM_FONT with the given name, family, style, and size.
 *
 * @param which  System font role name (e.g. "plain", "bold", "fixed").
 * @param family Font family name to assign.
 * @param style  Font style name to assign.
 * @param size   Point size to assign.
 */
// Private function used to replace the R5 hack which sets a system font
void
_set_system_font_(const char* which, font_family family, font_style style,
	float size)
{
	// R5 used a global area offset table to set the system fonts in the Font
	// preferences panel. Bleah.
	BPrivate::AppServerLink link;

	link.StartMessage(AS_SET_SYSTEM_FONT);
	link.AttachString(which, B_OS_NAME_LENGTH);
	link.AttachString(family, sizeof(font_family));
	link.AttachString(style, sizeof(font_style));
	link.Attach<float>(size);
	link.Flush();
}


/**
 * @brief Retrieves the default family, style, and size for a named system font.
 *
 * Sends AS_GET_SYSTEM_DEFAULT_FONT to the app_server and reads back the
 * default font family, style name, and point size for the specified role.
 *
 * @param which   System font role name (e.g. "plain", "bold", "fixed").
 * @param family  Output buffer for the default family name.
 * @param style   Output buffer for the default style name.
 * @param _size   Output pointer for the default point size.
 * @return A status code.
 * @retval B_OK On success.
 */
status_t
_get_system_default_font_(const char* which, font_family family,
	font_style style, float* _size)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_SYSTEM_DEFAULT_FONT);
	link.AttachString(which, B_OS_NAME_LENGTH);

	int32 status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK
		|| status < B_OK)
		return status;

	link.ReadString(family, sizeof(font_family));
	link.ReadString(style, sizeof(font_style));
	link.Read<float>(_size);
	return B_OK;
}


/**
 * @brief Returns the total number of font families installed on the system.
 *
 * The count reflects what the app_server has reported and is refreshed at
 * most once per second by the internal FontList cache.
 *
 * @return Number of available font families.
 * @see get_font_family(), count_font_styles()
 */
int32
count_font_families()
{
	return FontList::Default()->CountFamilies();
}


/**
 * @brief Returns the number of styles available for the given font family.
 *
 * @param family Name of the font family to query.
 * @return Number of styles available, or 0 if the family is not found.
 * @see get_font_style(), count_font_families()
 */
int32
count_font_styles(font_family family)
{
	return FontList::Default()->CountStyles(family);
}


/**
 * @brief Retrieves the font family name at the given index.
 *
 * Provides access to the sorted list of installed font families by index.
 * Combine with count_font_families() to iterate over all families.
 *
 * @param index  Zero-based index of the family to retrieve.
 * @param _name  Output buffer for the family name; must not be NULL.
 * @param _flags Optional output for font family flags; may be NULL.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If @a _name is NULL or @a index is out of range.
 * @see count_font_families(), get_font_style()
 */
status_t
get_font_family(int32 index, font_family* _name, uint32* _flags)
{
	if (_name == NULL)
		return B_BAD_VALUE;

	return FontList::Default()->FamilyAt(index, _name, _flags);
}


/**
 * @brief Retrieves the style name and flags for a font family at the given index.
 *
 * Convenience overload that omits the face output parameter.
 *
 * @param family  Name of the font family to query.
 * @param index   Zero-based index of the style to retrieve.
 * @param _name   Output buffer for the style name; must not be NULL.
 * @param _flags  Optional output for style flags; may be NULL.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If @a _name is NULL or the family/index is not found.
 * @see get_font_style(font_family, int32, font_style*, uint16*, uint32*)
 */
status_t
get_font_style(font_family family, int32 index, font_style* _name,
	uint32* _flags)
{
	return get_font_style(family, index, _name, NULL, _flags);
}


/**
 * @brief Retrieves the style name, face flags, and flags for a font family at the given index.
 *
 * @param family  Name of the font family to query.
 * @param index   Zero-based index of the style to retrieve.
 * @param _name   Output buffer for the style name; must not be NULL.
 * @param _face   Optional output for the face flags bitmask; may be NULL.
 * @param _flags  Optional output for style flags; may be NULL.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If @a _name is NULL or the family/index is not found.
 * @note The returned @a _face value is derived from the style name heuristically
 *       and is reliable for approximately 90–99% of font names.
 * @see count_font_styles(), get_font_family()
 */
status_t
get_font_style(font_family family, int32 index, font_style* _name,
	uint16* _face, uint32* _flags)
{
	// The face value returned by this function is not very reliable. At the
	// same time, the value returned should be fairly reliable, returning the
	// proper flag for 90%-99% of font names.

	if (_name == NULL)
		return B_BAD_VALUE;

	return FontList::Default()->StyleAt(family, index, _name, _face, _flags);
}


/**
 * @brief Checks whether the installed font family list has changed on the server.
 *
 * Queries the app_server to determine whether its font-list revision differs
 * from the locally cached revision. The @a checkOnly parameter is accepted for
 * API compatibility but is currently unused; the check is always performed.
 *
 * @param checkOnly If @c true, only report whether an update is available
 *                  without forcing a cache refresh (not yet enforced).
 * @return @c true if the server's list is newer than the local cache,
 *         @c false if the local cache is already up to date.
 * @see count_font_families(), get_font_family()
 */
bool
update_font_families(bool /*checkOnly*/)
{
	return FontList::Default()->UpdatedOnServer();
}


//	#pragma mark -


/**
 * @brief Default constructor; initialises the font to the system plain font.
 *
 * If be_plain_font has already been populated by the kit initialisation
 * routine the new object is copy-initialised from it. When constructing
 * the global sPlainFont object itself (i.e. during kit boot, before
 * be_plain_font is available) a set of hard-coded fallback metrics is used
 * to avoid infinite recursion.
 *
 * @see be_plain_font, _init_global_fonts_()
 */
BFont::BFont()
	:
	// initialise for be_plain_font (avoid circular definition)
	fFamilyID(0),
	fStyleID(0),
	fSize(10.0),
	fShear(90.0),
	fRotation(0.0),
	fFalseBoldWidth(0.0),
	fSpacing(B_BITMAP_SPACING),
	fEncoding(B_UNICODE_UTF8),
	fFace(0),
	fFlags(0),
	fExtraFlags(kUninitializedExtraFlags)
{
	if (be_plain_font != NULL && this != &sPlainFont)
		*this = *be_plain_font;
	else {
		fHeight.ascent = 7.0;
		fHeight.descent = 2.0;
		fHeight.leading = 13.0;
	}
}


/**
 * @brief Copy constructor; initialises the font as an exact duplicate of @a font.
 *
 * @param font The source BFont to copy.
 */
BFont::BFont(const BFont& font)
{
	*this = font;
}


/**
 * @brief Pointer-based copy constructor; initialises from @a font or falls back to plain.
 *
 * If @a font is NULL the new object is initialised from be_plain_font, mirroring
 * the behaviour of the special B_PLAIN_FONT constant.
 *
 * @param font Pointer to the source BFont, or NULL to use be_plain_font.
 */
BFont::BFont(const BFont* font)
{
	if (font != NULL)
		*this = *font;
	else
		*this = *be_plain_font;
}


/**
 * @brief Sets the font's family and style by name.
 *
 * Sends AS_GET_FAMILY_AND_STYLE_IDS to the app_server to resolve the names
 * to internal IDs. Either @a family or @a style (but not both) may be NULL;
 * passing both as NULL returns B_BAD_VALUE. When @a style is NULL the server
 * selects the best style for the current face flags.
 *
 * Invalidates the cached font height and extra flags so that subsequent
 * metric queries will refresh from the server.
 *
 * @param family Font family name, or NULL to keep the current family.
 * @param style  Font style name, or NULL to select by face.
 * @return A status code.
 * @retval B_OK On success.
 * @retval B_BAD_VALUE If both @a family and @a style are NULL.
 * @see SetFamilyAndFace(), GetFamilyAndStyle()
 */
// Sets the font's family and style all at once
status_t
BFont::SetFamilyAndStyle(const font_family family, const font_style style)
{
	if (family == NULL && style == NULL)
		return B_BAD_VALUE;

	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_FAMILY_AND_STYLE_IDS);
	link.AttachString(family, sizeof(font_family));
	link.AttachString(style, sizeof(font_style));
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(0xffff);
	link.Attach<uint16>(fFace);

	int32 status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status != B_OK)
		return status;

	link.Read<uint16>(&fFamilyID);
	link.Read<uint16>(&fStyleID);
	link.Read<uint16>(&fFace);

	fHeight.ascent = kUninitializedAscent;
	fExtraFlags = kUninitializedExtraFlags;

	return B_OK;
}


/**
 * @brief Sets the font's family and style from a packed 32-bit code.
 *
 * The high 16 bits of @a code encode the family ID and the low 16 bits encode
 * the style ID, matching the value returned by FamilyAndStyle().
 *
 * Unlike the R5 implementation, this method makes a server round-trip to
 * correctly update the face flags, which R5 failed to do.
 *
 * Invalidates the cached font height and extra flags.
 *
 * @param code Packed (familyID << 16) | styleID value.
 * @see FamilyAndStyle(), SetFamilyAndStyle(const font_family, const font_style)
 */
// Sets the font's family and style all at once
void
BFont::SetFamilyAndStyle(uint32 code)
{
	// R5 has a bug here: the face is not updated even though the IDs are set.
	// This is a problem because the face flag includes Regular/Bold/Italic
	// information in addition to stuff like underlining and strikethrough.
	// As a result, this will need a trip to the server and, thus, be slower
	// than R5's in order to be correct

	uint16 family, style;
	style = code & 0xFFFF;
	family = (code & 0xFFFF0000) >> 16;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_FAMILY_AND_STYLE_IDS);
	link.AttachString(NULL);	// no family and style name
	link.AttachString(NULL);
	link.Attach<uint16>(family);
	link.Attach<uint16>(style);
	link.Attach<uint16>(fFace);

	int32 fontcode;
	if (link.FlushWithReply(fontcode) != B_OK || fontcode != B_OK)
		return;

	link.Read<uint16>(&fFamilyID);
	link.Read<uint16>(&fStyleID);
	link.Read<uint16>(&fFace);
	fHeight.ascent = kUninitializedAscent;
	fExtraFlags = kUninitializedExtraFlags;
}


/**
 * @brief Sets the font's family and face style flags simultaneously.
 *
 * If @a family does not exist on the server only the face flags are updated;
 * the current family is preserved. If the exact @a face combination is
 * unavailable the server selects the closest matching style.
 *
 * Invalidates the cached font height and extra flags.
 *
 * @param family Font family name, or NULL to apply @a face to the current family.
 * @param face   Bitmask of face flags (e.g. B_BOLD_FACE, B_ITALIC_FACE).
 * @return A status code.
 * @retval B_OK On success.
 * @see SetFamilyAndStyle(), SetFace()
 */
// Sets the font's family and face all at once
status_t
BFont::SetFamilyAndFace(const font_family family, uint16 face)
{
	// To comply with the BeBook, this function will only set valid values
	// i.e. passing a nonexistent family will cause only the face to be set.
	// Additionally, if a particular  face does not exist in a family, the
	// closest match will be chosen.

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_FAMILY_AND_STYLE_IDS);
	link.AttachString(family, sizeof(font_family));
	link.AttachString(NULL);	// no style given
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(0xffff);
	link.Attach<uint16>(face);

	int32 status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status != B_OK)
		return status;

	link.Read<uint16>(&fFamilyID);
	link.Read<uint16>(&fStyleID);
	link.Read<uint16>(&fFace);
	fHeight.ascent = kUninitializedAscent;
	fExtraFlags = kUninitializedExtraFlags;

	return B_OK;
}


/**
 * @brief Sets the font size in points.
 *
 * Invalidates the cached font height so subsequent GetHeight() calls
 * will fetch fresh metrics from the server.
 *
 * @param size Point size; must be greater than zero.
 * @see Size()
 */
void
BFont::SetSize(float size)
{
	fSize = size;
	fHeight.ascent = kUninitializedAscent;
}


/**
 * @brief Sets the font's italic shear angle in degrees.
 *
 * A shear of 90.0° produces upright glyphs; values below 90° produce
 * a forward slant and values above 90° produce a backward slant.
 * Invalidates the cached font height.
 *
 * @param shear Shear angle in degrees, typically in the range [45.0, 135.0].
 * @see Shear()
 */
void
BFont::SetShear(float shear)
{
	fShear = shear;
	fHeight.ascent = kUninitializedAscent;
}


/**
 * @brief Sets the baseline rotation angle in degrees.
 *
 * 0.0° draws text horizontally; positive values rotate counter-clockwise.
 * Invalidates the cached font height.
 *
 * @param rotation Rotation angle in degrees.
 * @see Rotation()
 */
void
BFont::SetRotation(float rotation)
{
	fRotation = rotation;
	fHeight.ascent = kUninitializedAscent;
}


/**
 * @brief Sets the width of the artificial bold stroke expansion.
 *
 * False bold is a rendering technique that inflates glyph outlines by the
 * given number of pixels to simulate bold weight when a true bold variant
 * is unavailable.
 *
 * @param width Expansion width in pixels.
 * @see FalseBoldWidth()
 */
void
BFont::SetFalseBoldWidth(float width)
{
	fFalseBoldWidth = width;
}


/**
 * @brief Sets the glyph spacing mode.
 *
 * Controls how inter-glyph spacing is computed during rendering. Common
 * values include B_CHAR_SPACING, B_STRING_SPACING, B_BITMAP_SPACING, and
 * B_FIXED_SPACING.
 *
 * @param spacing One of the B_*_SPACING constants defined in Font.h.
 * @see Spacing()
 */
void
BFont::SetSpacing(uint8 spacing)
{
	fSpacing = spacing;
}


/**
 * @brief Sets the character encoding used to interpret string data.
 *
 * @param encoding One of the B_*_ENCODING constants (e.g. B_UNICODE_UTF8,
 *                 B_ISO_8859_1).
 * @see Encoding()
 */
void
BFont::SetEncoding(uint8 encoding)
{
	fEncoding = encoding;
}


/**
 * @brief Sets the face style flags, selecting the closest available style.
 *
 * If @a face is identical to the current face flags the call is a no-op.
 * Otherwise delegates to SetFamilyAndFace() with a NULL family to keep
 * the current family while changing the style.
 *
 * @param face Bitmask of face flags (e.g. B_BOLD_FACE, B_ITALIC_FACE).
 * @see Face(), SetFamilyAndFace()
 */
void
BFont::SetFace(uint16 face)
{
	if (face == fFace)
		return;

	SetFamilyAndFace(NULL, face);
}


/**
 * @brief Sets the font rendering flags.
 *
 * @param flags Bitmask of B_DISABLE_ANTIALIASING and related flag constants.
 * @see Flags()
 */
void
BFont::SetFlags(uint32 flags)
{
	fFlags = flags;
}


/**
 * @brief Retrieves the font's family and style names from the server.
 *
 * Either @a family or @a style may be NULL; at least one must be non-NULL for
 * the call to have any effect. On server communication failure the output
 * buffers are zeroed.
 *
 * @param family Output buffer for the family name; may be NULL.
 * @param style  Output buffer for the style name; may be NULL.
 * @see SetFamilyAndStyle(), Family(), Style()
 */
void
BFont::GetFamilyAndStyle(font_family* family, font_style* style) const
{
	if (family == NULL && style == NULL)
		return;

	// it's okay to call this function with either family or style set to NULL

	font_family familyBuffer;
	font_style styleBuffer;

	if (family == NULL)
		family = &familyBuffer;
	if (style == NULL)
		style = &styleBuffer;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_FAMILY_AND_STYLE);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK) {
		// the least we can do is to clear the buffers
		memset(*family, 0, sizeof(font_family));
		memset(*style, 0, sizeof(font_style));
		return;
	}

	link.ReadString(*family, sizeof(font_family));
	link.ReadString(*style, sizeof(font_style));
}


/**
 * @brief Returns the packed family and style IDs as a single 32-bit value.
 *
 * The high 16 bits contain the family ID and the low 16 bits contain the
 * style ID. This value can be passed to SetFamilyAndStyle(uint32).
 *
 * @return Packed (familyID << 16) | styleID.
 * @see SetFamilyAndStyle(uint32), GetFamilyAndStyle()
 */
uint32
BFont::FamilyAndStyle() const
{
	return (fFamilyID << 16UL) | fStyleID;
}


/**
 * @brief Returns the font's point size.
 * @return Current point size.
 * @see SetSize()
 */
float
BFont::Size() const
{
	return fSize;
}


/**
 * @brief Returns the font's shear angle in degrees.
 * @return Shear angle; 90.0° is upright.
 * @see SetShear()
 */
float
BFont::Shear() const
{
	return fShear;
}


/**
 * @brief Returns the font's baseline rotation angle in degrees.
 * @return Rotation angle; 0.0° is horizontal.
 * @see SetRotation()
 */
float
BFont::Rotation() const
{
	return fRotation;
}


/**
 * @brief Returns the false-bold stroke expansion width in pixels.
 * @return False-bold width.
 * @see SetFalseBoldWidth()
 */
float
BFont::FalseBoldWidth() const
{
	return fFalseBoldWidth;
}


/**
 * @brief Returns the glyph spacing mode.
 * @return One of the B_*_SPACING constants.
 * @see SetSpacing()
 */
uint8
BFont::Spacing() const
{
	return fSpacing;
}


/**
 * @brief Returns the character encoding.
 * @return One of the B_*_ENCODING constants.
 * @see SetEncoding()
 */
uint8
BFont::Encoding() const
{
	return fEncoding;
}


/**
 * @brief Returns the face style flags bitmask.
 * @return Current face flags (e.g. B_BOLD_FACE, B_ITALIC_FACE).
 * @see SetFace()
 */
uint16
BFont::Face() const
{
	return fFace;
}


/**
 * @brief Returns the font rendering flags.
 * @return Current flags bitmask.
 * @see SetFlags()
 */
uint32
BFont::Flags() const
{
	return fFlags;
}


/**
 * @brief Returns the text flow direction reported by the font.
 *
 * Fetches the extra flags from the server on the first call; subsequent
 * calls return the cached value.
 *
 * @return B_FONT_LEFT_TO_RIGHT or B_FONT_RIGHT_TO_LEFT.
 * @see IsFixed()
 */
font_direction
BFont::Direction() const
{
	_GetExtraFlags();
	return (font_direction)(fExtraFlags >> B_PRIVATE_FONT_DIRECTION_SHIFT);
}


/**
 * @brief Returns whether this font is a fixed-width (monospace) font.
 *
 * Fetches the extra flags from the server on the first call; subsequent
 * calls return the cached value.
 *
 * @return @c true if all glyphs have the same advance width.
 * @see IsFullAndHalfFixed()
 */
bool
BFont::IsFixed() const
{
	_GetExtraFlags();
	return (fExtraFlags & B_IS_FIXED) != 0;
}


/**
 * @brief Returns whether the font is a fixed-width font containing both full
 *        and half-width characters.
 *
 * This predicate supports CJK fonts that combine full-width Kanji glyphs
 * with half-width Roman glyphs in a single fixed-width typeface.
 *
 * @return @c true if the font is fixed-width and contains both full- and
 *         half-width characters.
 * @note This functionality was unimplemented in BeOS R5.
 * @see IsFixed()
 */
// Returns whether or not the font is fixed-width and contains both
// full and half-width characters.
bool
BFont::IsFullAndHalfFixed() const
{
	// This was left unimplemented as of R5. It is a way to work with both
	// Kanji and Roman characters in the same fixed-width font.

	_GetExtraFlags();
	return (fExtraFlags & B_PRIVATE_FONT_IS_FULL_AND_HALF_FIXED) != 0;
}


/**
 * @brief Returns the font-wide bounding box that encloses all glyphs.
 *
 * Queries the app_server for the bounding rectangle of the complete
 * glyph set at the current size.
 *
 * @return The bounding BRect in pixels, or an empty rect on failure.
 * @see GetBoundingBoxesAsGlyphs()
 */
BRect
BFont::BoundingBox() const
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_FONT_BOUNDING_BOX);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<float>(fSize);

	int32 code;
	if (link.FlushWithReply(code) != B_OK
		|| code != B_OK)
		return BRect(0, 0, 0 ,0);

	BRect box;
	link.Read<BRect>(&box);
	return box;
}


/**
 * @brief Returns the set of Unicode blocks covered by this font.
 *
 * Sends AS_GET_UNICODE_BLOCKS to the server and decodes the result into
 * a unicode_block bitmask. On failure all bits are set (indicating
 * coverage of all blocks) to allow callers to degrade gracefully.
 *
 * @return A unicode_block value with a bit set for each supported block.
 * @see IncludesBlock()
 */
unicode_block
BFont::Blocks() const
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_UNICODE_BLOCKS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);

	int32 status;
	if (link.FlushWithReply(status) != B_OK
		|| status != B_OK) {
		return unicode_block(~0LL, ~0LL);
	}

	unicode_block blocksForFont;
	link.Read<unicode_block>(&blocksForFont);

	return blocksForFont;
}

/**
 * @brief Returns whether the font contains glyphs for a Unicode code-point range.
 *
 * Queries the app_server to determine whether this font covers the block
 * bounded by [start, end].
 *
 * @param start First code point of the range to test.
 * @param end   Last code point of the range to test.
 * @return @c true if the font includes glyphs for the block, @c false otherwise
 *         or on server communication failure.
 * @see Blocks()
 */
bool
BFont::IncludesBlock(uint32 start, uint32 end) const
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_HAS_UNICODE_BLOCK);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<uint32>(start);
	link.Attach<uint32>(end);

	int32 status;
	if (link.FlushWithReply(status) != B_OK
		|| status != B_OK) {
		return false;
	}

	bool hasBlock;
	link.Read<bool>(&hasBlock);

	return hasBlock;
}


/**
 * @brief Returns the file format of the underlying font data.
 *
 * @return A font_file_format constant such as B_TRUETYPE_WINDOWS.
 *         Returns B_TRUETYPE_WINDOWS as a safe default on failure.
 */
font_file_format
BFont::FileFormat() const
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_FONT_FILE_FORMAT);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);

	int32 status;
	if (link.FlushWithReply(status) != B_OK
		|| status != B_OK) {
		// just take a safe bet...
		return B_TRUETYPE_WINDOWS;
	}

	uint16 format;
	link.Read<uint16>(&format);

	return (font_file_format)format;
}


/**
 * @brief Returns the number of tuned (bitmap-hinted) instances for this font.
 *
 * @return Number of tuned instances, or -1 on server communication failure.
 * @see GetTunedInfo()
 */
int32
BFont::CountTuned() const
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_TUNED_COUNT);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);

	int32 code;
	if (link.FlushWithReply(code) != B_OK
		|| code != B_OK)
		return -1;

	int32 count;
	link.Read<int32>(&count);
	return count;
}


/**
 * @brief Retrieves metadata for a tuned font instance at the given index.
 *
 * @param index Zero-based index into the list of tuned instances.
 * @param info  Output structure to receive the tuned font metadata;
 *              must not be NULL.
 * @see CountTuned()
 */
void
BFont::GetTunedInfo(int32 index, tuned_font_info* info) const
{
	if (info == NULL)
		return;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_TUNED_INFO);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<uint32>(index);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	link.Read<tuned_font_info>(info);
}


/**
 * @brief Truncates a string in place so that it fits within a given pixel width.
 *
 * Measures the string using the current font's escapements and inserts an
 * ellipsis according to @a mode when the string is too wide. The string is
 * not modified when @a mode is B_NO_TRUNCATION.
 *
 * @param inOut  The string to truncate; modified in place.
 * @param mode   Truncation mode: B_TRUNCATE_END, B_TRUNCATE_BEGINNING,
 *               B_TRUNCATE_MIDDLE, B_TRUNCATE_SMART, or B_NO_TRUNCATION.
 * @param width  Maximum allowed pixel width.
 * @see GetTruncatedStrings()
 */
// Truncates a string to a given _pixel_ width based on the font and size
void
BFont::TruncateString(BString* inOut, uint32 mode, float width) const
{
	if (mode == B_NO_TRUNCATION)
		return;

	// NOTE: Careful, we cannot directly use "inOut->String()" as result
	// array, because the string length increases by 3 bytes in the worst
	// case scenario.
	const char* string = inOut->String();
	GetTruncatedStrings(&string, 1, mode, width, inOut);
}


/**
 * @brief Truncates an array of strings so that each fits within a given pixel width.
 *
 * For each input string the glyph escapements are fetched, then the string is
 * truncated according to @a mode and stored in the corresponding @a resultArray
 * entry. The original @a stringArray is not modified.
 *
 * @param stringArray  Array of UTF-8 strings to truncate.
 * @param numStrings   Number of strings in @a stringArray.
 * @param mode         Truncation mode (B_TRUNCATE_END, B_TRUNCATE_BEGINNING, etc.).
 * @param width        Maximum allowed pixel width for each string.
 * @param resultArray  Output array of BStrings; must have at least @a numStrings entries.
 * @see TruncateString(), StringWidth()
 */
void
BFont::GetTruncatedStrings(const char* stringArray[], int32 numStrings,
	uint32 mode, float width, BString resultArray[]) const
{
	if (stringArray != NULL && numStrings > 0) {
		// the width of the "…" glyph
		float ellipsisWidth = StringWidth(B_UTF8_ELLIPSIS);

		for (int32 i = 0; i < numStrings; i++) {
			resultArray[i] = stringArray[i];
			int32 numChars = resultArray[i].CountChars();

			// get the escapement of each glyph in font units
			float* escapementArray = new float[numChars];
			GetEscapements(stringArray[i], numChars, NULL, escapementArray);

			truncate_string(resultArray[i], mode, width, escapementArray,
				fSize, ellipsisWidth, numChars);

			delete[] escapementArray;
		}
	}
}


/**
 * @brief Truncates an array of strings into caller-provided C-string buffers.
 *
 * Delegates to the BString overload and copies each result into the
 * corresponding @a resultArray buffer. Each buffer must be large enough to
 * hold the original string plus the ellipsis (at most 3 extra bytes).
 *
 * @param stringArray  Array of UTF-8 strings to truncate.
 * @param numStrings   Number of strings in @a stringArray.
 * @param mode         Truncation mode.
 * @param width        Maximum allowed pixel width for each string.
 * @param resultArray  Array of pre-allocated char buffers for the output.
 * @see GetTruncatedStrings(const char*[], int32, uint32, float, BString[]) const
 */
void
BFont::GetTruncatedStrings(const char* stringArray[], int32 numStrings,
	uint32 mode, float width, char* resultArray[]) const
{
	if (stringArray != NULL && numStrings > 0) {
		for (int32 i = 0; i < numStrings; i++) {
			BString* strings = new BString[numStrings];
			GetTruncatedStrings(stringArray, numStrings, mode, width, strings);

			for (int32 i = 0; i < numStrings; i++)
				strcpy(resultArray[i], strings[i].String());

			delete[] strings;
		}
	}
}


/**
 * @brief Returns the pixel width of a null-terminated UTF-8 string.
 *
 * @param string Null-terminated string to measure; returns 0.0 if NULL.
 * @return Width of @a string in pixels using the current font settings.
 * @see StringWidth(const char*, int32) const, GetStringWidths()
 */
float
BFont::StringWidth(const char* string) const
{
	if (string == NULL)
		return 0.0;

	int32 length = strlen(string);
	float width;
	GetStringWidths(&string, &length, 1, &width);

	return width;
}


/**
 * @brief Returns the pixel width of a UTF-8 string of a given byte length.
 *
 * @param string Pointer to the string data; returns 0.0 if NULL.
 * @param length Number of bytes to measure; returns 0.0 if less than 1.
 * @return Width of the specified string segment in pixels.
 * @see StringWidth(const char*) const, GetStringWidths()
 */
float
BFont::StringWidth(const char* string, int32 length) const
{
	if (!string || length < 1)
		return 0.0f;

	float width = 0.0f;
	GetStringWidths(&string, &length, 1, &width);

	return width;
}


/**
 * @brief Measures the pixel widths of multiple strings in a single server request.
 *
 * Batches all strings into a single AS_GET_STRING_WIDTHS message and
 * receives the widths in one reply, which is more efficient than calling
 * StringWidth() repeatedly.
 *
 * @param stringArray  Array of UTF-8 string pointers.
 * @param lengthArray  Array of byte lengths, one per string.
 * @param numStrings   Number of strings to measure.
 * @param widthArray   Output array receiving the pixel width of each string;
 *                     must have at least @a numStrings entries.
 * @see StringWidth()
 */
void
BFont::GetStringWidths(const char* stringArray[], const int32 lengthArray[],
	int32 numStrings, float widthArray[]) const
{
	if (stringArray == NULL || lengthArray == NULL || numStrings < 1
		|| widthArray == NULL) {
		return;
	}

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_STRING_WIDTHS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<float>(fSize);
	link.Attach<uint8>(fSpacing);
	link.Attach<int32>(numStrings);

	// TODO: all strings into a single array???
	// we do have a maximum message length, and it could be easily touched
	// here...
	for (int32 i = 0; i < numStrings; i++)
		link.AttachString(stringArray[i], lengthArray[i]);

	status_t status;
	if (link.FlushWithReply(status) != B_OK || status != B_OK)
		return;

	link.Read(widthArray, sizeof(float) * numStrings);
}


/**
 * @brief Retrieves per-glyph advance widths (escapements) as floats.
 *
 * Convenience overload that passes a NULL delta to the full version,
 * applying no inter-word or inter-character spacing adjustment.
 *
 * @param charArray       UTF-8 encoded character sequence.
 * @param numChars        Number of characters in @a charArray.
 * @param escapementArray Output array of escapement values in font units;
 *                        must have at least @a numChars entries.
 * @see GetEscapements(const char[], int32, escapement_delta*, float[]) const
 */
void
BFont::GetEscapements(const char charArray[], int32 numChars,
	float escapementArray[]) const
{
	GetEscapements(charArray, numChars, NULL, escapementArray);
}


/**
 * @brief Retrieves per-glyph escapements as floats, with optional spacing delta.
 *
 * Sends AS_GET_ESCAPEMENTS_AS_FLOATS to the server. The escapement of each
 * glyph is expressed in font units (i.e. as a fraction of the point size).
 *
 * @param charArray       UTF-8 encoded character sequence.
 * @param numChars        Number of characters to measure.
 * @param delta           Optional spacing adjustment; NULL applies zero delta.
 * @param escapementArray Output array; must have at least @a numChars entries.
 * @see GetEscapements(const char[], int32, escapement_delta*, BPoint[]) const
 */
void
BFont::GetEscapements(const char charArray[], int32 numChars,
	escapement_delta* delta, float escapementArray[]) const
{
	if (charArray == NULL || numChars < 1 || escapementArray == NULL)
		return;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_ESCAPEMENTS_AS_FLOATS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<float>(fSize);
	link.Attach<uint8>(fSpacing);
	link.Attach<float>(fRotation);
	link.Attach<uint32>(fFlags);

	link.Attach<float>(delta ? delta->nonspace : 0.0f);
	link.Attach<float>(delta ? delta->space : 0.0f);
	link.Attach<int32>(numChars);

	// TODO: Should we not worry about the port capacity here?!?
	uint32 bytesInBuffer = UTF8CountBytes(charArray, numChars);
	link.Attach<int32>(bytesInBuffer);
	link.Attach(charArray, bytesInBuffer);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	link.Read(escapementArray, numChars * sizeof(float));
}


/**
 * @brief Retrieves per-glyph escapements as BPoint vectors, without offset data.
 *
 * Convenience overload that passes NULL for the offset array.
 *
 * @param charArray       UTF-8 encoded character sequence.
 * @param numChars        Number of characters to measure.
 * @param delta           Optional spacing adjustment; NULL applies zero delta.
 * @param escapementArray Output array of BPoint escapements; must have at least
 *                        @a numChars entries.
 * @see GetEscapements(const char[], int32, escapement_delta*, BPoint[], BPoint[]) const
 */
void
BFont::GetEscapements(const char charArray[], int32 numChars,
	escapement_delta* delta, BPoint escapementArray[]) const
{
	GetEscapements(charArray, numChars, delta, escapementArray, NULL);
}


/**
 * @brief Retrieves per-glyph escapements and optional per-glyph offsets as BPoint vectors.
 *
 * Sends AS_GET_ESCAPEMENTS to the server. If @a offsetArray is non-NULL it
 * is filled with per-glyph origin offsets in addition to the escapements.
 *
 * @param charArray       UTF-8 encoded character sequence.
 * @param numChars        Number of characters to measure.
 * @param delta           Optional spacing adjustment; NULL applies zero delta.
 * @param escapementArray Output array of escapement BPoints; must have at
 *                        least @a numChars entries.
 * @param offsetArray     Optional output array for per-glyph offsets; may be
 *                        NULL if offset data is not required.
 * @see GetEscapements(const char[], int32, float[]) const
 */
void
BFont::GetEscapements(const char charArray[], int32 numChars,
	escapement_delta* delta, BPoint escapementArray[],
	BPoint offsetArray[]) const
{
	if (charArray == NULL || numChars < 1 || escapementArray == NULL)
		return;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_ESCAPEMENTS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<float>(fSize);
	link.Attach<uint8>(fSpacing);
	link.Attach<float>(fRotation);
	link.Attach<uint32>(fFlags);

	link.Attach<float>(delta ? delta->nonspace : 0.0);
	link.Attach<float>(delta ? delta->space : 0.0);
	link.Attach<bool>(offsetArray != NULL);
	link.Attach<int32>(numChars);

	// TODO: Should we not worry about the port capacity here?!?
	uint32 bytesInBuffer = UTF8CountBytes(charArray, numChars);
	link.Attach<int32>(bytesInBuffer);
	link.Attach(charArray, bytesInBuffer);

	int32 code;
	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	link.Read(escapementArray, sizeof(BPoint) * numChars);
	if (offsetArray)
		link.Read(offsetArray, sizeof(BPoint) * numChars);
}


/**
 * @brief Retrieves the left and right edge insets for each glyph.
 *
 * Edge insets describe the blank space between the glyph's bounding box and
 * its escapement box. They are used by callers that need precise glyph
 * placement, for example when implementing kerning on top of the server's
 * standard spacing.
 *
 * @param charArray  UTF-8 encoded character sequence.
 * @param numChars   Number of characters to query.
 * @param edgeArray  Output array of edge_info structs; must have at least
 *                   @a numChars entries.
 * @see GetEscapements(), GetBoundingBoxesAsGlyphs()
 */
void
BFont::GetEdges(const char charArray[], int32 numChars,
	edge_info edgeArray[]) const
{
	if (!charArray || numChars < 1 || !edgeArray)
		return;

	int32 code;
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_EDGES);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<int32>(numChars);

	uint32 bytesInBuffer = UTF8CountBytes(charArray, numChars);
	link.Attach<int32>(bytesInBuffer);
	link.Attach(charArray, bytesInBuffer);

	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	link.Read(edgeArray, sizeof(edge_info) * numChars);
}


/**
 * @brief Retrieves the font's ascent, descent, and leading metrics.
 *
 * Returns cached values when available. If the cache is invalid (e.g. after
 * a call to SetSize()) a round-trip to the server is made to refresh the
 * metrics.
 *
 * @param _height Output structure receiving ascent, descent, and leading
 *                values in pixels; must not be NULL.
 * @see SetSize(), GetEscapements()
 */
void
BFont::GetHeight(font_height* _height) const
{
	if (_height == NULL)
		return;

	if (fHeight.ascent == kUninitializedAscent) {
		// we don't have the font height cached yet
		BPrivate::AppServerLink link;

		link.StartMessage(AS_GET_FONT_HEIGHT);
		link.Attach<uint16>(fFamilyID);
		link.Attach<uint16>(fStyleID);
		link.Attach<float>(fSize);

		int32 code;
		if (link.FlushWithReply(code) != B_OK || code != B_OK)
			return;

		// Who put that "const" to this method? :-)
		// We made fHeight mutable for this, but we should drop the "const"
		// when we can
		link.Read<font_height>(&fHeight);
	}

	*_height = fHeight;
}


/**
 * @brief Retrieves per-glyph bounding boxes measured in glyph-space.
 *
 * Each bounding box is relative to the glyph's own origin, ignoring the
 * advance width of preceding glyphs.
 *
 * @param charArray        UTF-8 encoded character sequence.
 * @param numChars         Number of characters to query.
 * @param mode             Whether to return screen or printing metrics
 *                         (B_SCREEN_METRIC or B_PRINTING_METRIC).
 * @param boundingBoxArray Output array of BRects; must have at least
 *                         @a numChars entries.
 * @see GetBoundingBoxesAsString(), _GetBoundingBoxes()
 */
void
BFont::GetBoundingBoxesAsGlyphs(const char charArray[], int32 numChars,
	font_metric_mode mode, BRect boundingBoxArray[]) const
{
	_GetBoundingBoxes(charArray, numChars, mode, false, NULL,
		boundingBoxArray, false);
}


/**
 * @brief Retrieves per-glyph bounding boxes as they would appear in a rendered string.
 *
 * Each bounding box is positioned relative to the start of the string, taking
 * the cumulative advance widths and the optional @a delta adjustment into account.
 *
 * @param charArray        UTF-8 encoded character sequence.
 * @param numChars         Number of characters to query.
 * @param mode             Metric mode (B_SCREEN_METRIC or B_PRINTING_METRIC).
 * @param delta            Optional spacing adjustment; may be NULL.
 * @param boundingBoxArray Output array of BRects; must have at least
 *                         @a numChars entries.
 * @see GetBoundingBoxesAsGlyphs(), GetBoundingBoxesForStrings()
 */
void
BFont::GetBoundingBoxesAsString(const char charArray[], int32 numChars,
	font_metric_mode mode, escapement_delta* delta,
	BRect boundingBoxArray[]) const
{
	_GetBoundingBoxes(charArray, numChars, mode, true, delta,
		boundingBoxArray, true);
}


/**
 * @brief Internal helper that implements both GetBoundingBoxesAsGlyphs() and
 *        GetBoundingBoxesAsString().
 *
 * Selects the server message (AS_GET_BOUNDINGBOXES_CHARS vs.
 * AS_GET_BOUNDINGBOXES_STRING) based on @a asString and sends all font
 * parameters and character data in a single request.
 *
 * @param charArray          UTF-8 encoded character sequence.
 * @param numChars           Number of characters.
 * @param mode               Metric mode (screen or printing).
 * @param string_escapement  If @c true, positions boxes cumulatively along
 *                           the string baseline.
 * @param delta              Optional spacing delta; an empty delta is sent
 *                           when NULL.
 * @param boundingBoxArray   Output array of BRects.
 * @param asString           Selects the server command variant.
 */
void
BFont::_GetBoundingBoxes(const char charArray[], int32 numChars,
	font_metric_mode mode, bool string_escapement, escapement_delta* delta,
	BRect boundingBoxArray[], bool asString) const
{
	if (charArray == NULL || numChars < 1 || boundingBoxArray == NULL)
		return;

	int32 code;
	BPrivate::AppServerLink link;

	link.StartMessage(asString
		? AS_GET_BOUNDINGBOXES_STRING : AS_GET_BOUNDINGBOXES_CHARS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<float>(fSize);
	link.Attach<float>(fRotation);
	link.Attach<float>(fShear);
	link.Attach<float>(fFalseBoldWidth);
	link.Attach<uint8>(fSpacing);

	link.Attach<uint32>(fFlags);
	link.Attach<font_metric_mode>(mode);
	link.Attach<bool>(string_escapement);

	if (delta != NULL) {
		link.Attach<escapement_delta>(*delta);
	} else {
		escapement_delta emptyDelta = {0, 0};
		link.Attach<escapement_delta>(emptyDelta);
	}

	link.Attach<int32>(numChars);
	uint32 bytesInBuffer = UTF8CountBytes(charArray, numChars);
	link.Attach<int32>(bytesInBuffer);
	link.Attach(charArray, bytesInBuffer);

	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	link.Read(boundingBoxArray, sizeof(BRect) * numChars);
}


/**
 * @brief Retrieves the bounding box for each complete string in an array.
 *
 * Each element of @a boundingBoxArray receives the bounding rectangle that
 * encloses the entire corresponding string when rendered with the current font.
 *
 * @param stringArray      Array of null-terminated UTF-8 strings.
 * @param numStrings       Number of strings.
 * @param mode             Metric mode (B_SCREEN_METRIC or B_PRINTING_METRIC).
 * @param deltas           Optional per-string spacing deltas; an empty delta
 *                         is used for any NULL entry.
 * @param boundingBoxArray Output array of BRects; must have at least
 *                         @a numStrings entries.
 * @see GetBoundingBoxesAsString(), GetBoundingBoxesAsGlyphs()
 */
void
BFont::GetBoundingBoxesForStrings(const char* stringArray[], int32 numStrings,
	font_metric_mode mode, escapement_delta deltas[],
	BRect boundingBoxArray[]) const
{
	if (!stringArray || numStrings < 1 || !boundingBoxArray)
		return;

	int32 code;
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_BOUNDINGBOXES_STRINGS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<float>(fSize);
	link.Attach<float>(fRotation);
	link.Attach<float>(fShear);
	link.Attach<float>(fFalseBoldWidth);
	link.Attach<uint8>(fSpacing);
	link.Attach<uint32>(fFlags);
	link.Attach<font_metric_mode>(mode);
	link.Attach<int32>(numStrings);

	for (int32 i = 0; i < numStrings; i++)
		link.AttachString(stringArray[i]);

	if (deltas) {
		for (int32 i = 0; i < numStrings; i++)
			link.Attach<escapement_delta>(deltas[i]);
	} else {
		escapement_delta emptyDelta = {0, 0};
		for (int32 i = 0; i < numStrings; i++)
			link.Attach<escapement_delta>(emptyDelta);
	}

	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	link.Read(boundingBoxArray, sizeof(BRect) * numStrings);
}


/**
 * @brief Retrieves the outline BShape for each glyph in a character array.
 *
 * Sends AS_GET_GLYPH_SHAPES and reads back one BShape per glyph. Each shape
 * describes the glyph's vector outline, which can be used for path-based
 * rendering or hit testing.
 *
 * @param charArray       UTF-8 encoded character sequence.
 * @param numChars        Number of characters.
 * @param glyphShapeArray Array of BShape pointers to fill; each must point
 *                        to a valid, empty BShape. Must have at least
 *                        @a numChars entries.
 * @note The BShape wire format between client and server is not yet finalised.
 */
void
BFont::GetGlyphShapes(const char charArray[], int32 numChars,
	BShape* glyphShapeArray[]) const
{
	// TODO: implement code specifically for passing BShapes to and
	// from the server
	if (!charArray || numChars < 1 || !glyphShapeArray)
		return;

	int32 code;
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_GLYPH_SHAPES);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<float>(fSize);
	link.Attach<float>(fShear);
	link.Attach<float>(fRotation);
	link.Attach<float>(fFalseBoldWidth);
	link.Attach<uint32>(fFlags);
	link.Attach<int32>(numChars);

	uint32 bytesInBuffer = UTF8CountBytes(charArray, numChars);
	link.Attach<int32>(bytesInBuffer);
	link.Attach(charArray, bytesInBuffer);

	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	for (int32 i = 0; i < numChars; i++)
		link.ReadShape(glyphShapeArray[i]);
}


/**
 * @brief Tests whether the font (or its fallbacks) contains a glyph for each character.
 *
 * Convenience overload that enables fallback fonts. Delegates to
 * GetHasGlyphs(const char[], int32, bool[], bool) with @c useFallbacks = true.
 *
 * @param charArray UTF-8 encoded character sequence.
 * @param numChars  Number of characters to test.
 * @param hasArray  Output array of booleans; must have at least @a numChars entries.
 * @see GetHasGlyphs(const char[], int32, bool[], bool) const
 */
void
BFont::GetHasGlyphs(const char charArray[], int32 numChars,
	bool hasArray[]) const
{
	GetHasGlyphs(charArray, numChars, hasArray, true);
}


/**
 * @brief Tests whether the font contains a glyph for each character, with
 *        optional fallback font consideration.
 *
 * @param charArray    UTF-8 encoded character sequence.
 * @param numChars     Number of characters to test.
 * @param hasArray     Output boolean array; each element is @c true if a glyph
 *                     is available, @c false otherwise. Must have at least
 *                     @a numChars entries.
 * @param useFallbacks If @c true, fallback fonts are consulted when the primary
 *                     font lacks the glyph.
 * @see GetHasGlyphs(const char[], int32, bool[]) const
 */
void
BFont::GetHasGlyphs(const char charArray[], int32 numChars, bool hasArray[],
	bool useFallbacks) const
{
	if (!charArray || numChars < 1 || !hasArray)
		return;

	int32 code;
	BPrivate::AppServerLink link;

	link.StartMessage(AS_GET_HAS_GLYPHS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);
	link.Attach<int32>(numChars);

	uint32 bytesInBuffer = UTF8CountBytes(charArray, numChars);
	link.Attach<int32>(bytesInBuffer);
	link.Attach(charArray, bytesInBuffer);

	link.Attach<bool>(useFallbacks);

	if (link.FlushWithReply(code) != B_OK || code != B_OK)
		return;

	link.Read(hasArray, sizeof(bool) * numChars);
}


/**
 * @brief Copy-assignment operator; copies all font attributes from @a font.
 *
 * @param font Source BFont to copy.
 * @return Reference to this BFont.
 */
BFont&
BFont::operator=(const BFont& font)
{
	fFamilyID = font.fFamilyID;
	fStyleID = font.fStyleID;
	fSize = font.fSize;
	fShear = font.fShear;
	fRotation = font.fRotation;
	fFalseBoldWidth = font.fFalseBoldWidth;
	fSpacing = font.fSpacing;
	fEncoding = font.fEncoding;
	fFace = font.fFace;
	fHeight = font.fHeight;
	fFlags = font.fFlags;
	fExtraFlags = font.fExtraFlags;

	return *this;
}


/**
 * @brief Returns whether two fonts are equal in all visual attributes.
 *
 * Compares family ID, style ID, size, shear, rotation, false-bold width,
 * spacing, encoding, and face. Rendering flags are intentionally excluded.
 *
 * @param font The font to compare against.
 * @return @c true if both fonts have identical visual attributes.
 * @see operator!=()
 */
bool
BFont::operator==(const BFont& font) const
{
	return fFamilyID == font.fFamilyID
		&& fStyleID == font.fStyleID
		&& fSize == font.fSize
		&& fShear == font.fShear
		&& fRotation == font.fRotation
		&& fFalseBoldWidth == font.fFalseBoldWidth
		&& fSpacing == font.fSpacing
		&& fEncoding == font.fEncoding
		&& fFace == font.fFace;
}


/**
 * @brief Returns whether two fonts differ in any visual attribute.
 *
 * The logical negation of operator==().
 *
 * @param font The font to compare against.
 * @return @c true if any visual attribute differs between the two fonts.
 * @see operator==()
 */
bool
BFont::operator!=(const BFont& font) const
{
	return fFamilyID != font.fFamilyID
		|| fStyleID != font.fStyleID
		|| fSize != font.fSize
		|| fShear != font.fShear
		|| fRotation != font.fRotation
		|| fFalseBoldWidth != font.fFalseBoldWidth
		|| fSpacing != font.fSpacing
		|| fEncoding != font.fEncoding
		|| fFace != font.fFace;
}


/**
 * @brief Prints a human-readable description of the font to standard output.
 *
 * Outputs the family name and ID, style name and ID, face flags, shear,
 * rotation, point size, cached height metrics, and encoding in a single
 * line prefixed with "BFont { ".
 *
 * @see GetFamilyAndStyle(), GetHeight()
 */
void
BFont::PrintToStream() const
{
	font_family family;
	font_style style;
	GetFamilyAndStyle(&family, &style);

	printf("BFont { %s (%d), %s (%d) 0x%x %f/%f %fpt (%f %f %f), %d }\n",
		family, fFamilyID, style, fStyleID, fFace, fShear, fRotation, fSize,
		fHeight.ascent, fHeight.descent, fHeight.leading, fEncoding);
}


/**
 * @brief Fetches and caches the extra font flags from the app_server.
 *
 * Skips the server round-trip if the flags are already cached. On failure
 * the flags are initialised to left-to-right direction as a safe default.
 * Declared const because it only mutates the mutable fExtraFlags field.
 *
 * @see IsFixed(), IsFullAndHalfFixed(), Direction()
 */
void
BFont::_GetExtraFlags() const
{
	// TODO: this has to be const in order to allow other font getters to
	// stay const as well
	if (fExtraFlags != kUninitializedExtraFlags)
		return;

	BPrivate::AppServerLink link;
	link.StartMessage(AS_GET_EXTRA_FONT_FLAGS);
	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status != B_OK) {
		// use defaut values for the flags
		fExtraFlags = (uint32)B_FONT_LEFT_TO_RIGHT
			<< B_PRIVATE_FONT_DIRECTION_SHIFT;
		return;
	}

	link.Read<uint32>(&fExtraFlags);
}


/**
 * @brief Loads a font file from disk and sets this BFont to its first face.
 *
 * Convenience overload that uses index 0 and instance 0.
 *
 * @param path Filesystem path to the font file.
 * @return A status code.
 * @retval B_OK On success.
 * @see LoadFont(const char*, uint16, uint16)
 */
status_t
BFont::LoadFont(const char* path)
{
	return LoadFont(path, 0, 0);
}


/**
 * @brief Loads a specific face from a font file and sets this BFont to it.
 *
 * Sends AS_ADD_FONT_FILE to register the font with the server. On success
 * the family ID, style ID, and face flags are updated; the cached height
 * and extra flags are invalidated.
 *
 * @param path     Filesystem path to the font file.
 * @param index    Zero-based face index within the font file (for TTC/OTC collections).
 * @param instance Named-instance index for variable fonts.
 * @return A status code.
 * @retval B_OK On success.
 * @see UnloadFont(), LoadFont(const area_id, size_t, size_t, uint16, uint16)
 */
status_t
BFont::LoadFont(const char* path, uint16 index, uint16 instance)
{
	BPrivate::AppServerLink link;
	link.StartMessage(AS_ADD_FONT_FILE);
	link.AttachString(path);
	link.Attach<uint16>(index);
	link.Attach<uint16>(instance);
	status_t status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status != B_OK) {
		return status;
	}

	link.Read<uint16>(&fFamilyID);
	link.Read<uint16>(&fStyleID);
	link.Read<uint16>(&fFace);
	fHeight.ascent = kUninitializedAscent;
	fExtraFlags = kUninitializedExtraFlags;

	return B_OK;
}


/**
 * @brief Loads a font from a shared memory area and sets this BFont to its first face.
 *
 * Convenience overload that uses index 0 and instance 0.
 *
 * @param fontAreaID Area ID of a shared memory region containing font data.
 * @param size       Number of bytes of font data.
 * @param offset     Byte offset within the area where font data begins.
 * @return A status code.
 * @retval B_OK On success.
 * @see LoadFont(const area_id, size_t, size_t, uint16, uint16)
 */
status_t
BFont::LoadFont(const area_id fontAreaID, size_t size, size_t offset)
{
	return LoadFont(fontAreaID, size, offset, 0, 0);
}


/**
 * @brief Loads a specific face from font data in a shared memory area.
 *
 * Sends AS_ADD_FONT_MEMORY to register the in-memory font with the server.
 * On success the family ID, style ID, and face flags are updated; the cached
 * height and extra flags are invalidated.
 *
 * @param fontAreaID Area ID of a shared memory region containing font data.
 * @param size       Number of bytes of font data.
 * @param offset     Byte offset within the area where font data begins.
 * @param index      Zero-based face index within the font data.
 * @param instance   Named-instance index for variable fonts.
 * @return A status code.
 * @retval B_OK On success.
 * @see UnloadFont(), LoadFont(const char*, uint16, uint16)
 */
status_t
BFont::LoadFont(const area_id fontAreaID, size_t size, size_t offset, uint16 index, uint16 instance)
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_ADD_FONT_MEMORY);

	link.Attach<int32>(fontAreaID);
	link.Attach<size_t>(size);
	link.Attach<size_t>(offset);
	link.Attach<uint16>(index);
	link.Attach<uint16>(instance);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status != B_OK) {
		return status;
	}

	link.Read<uint16>(&fFamilyID);
	link.Read<uint16>(&fStyleID);
	link.Read<uint16>(&fFace);
	fHeight.ascent = kUninitializedAscent;
	fExtraFlags = kUninitializedExtraFlags;

	return B_OK;
}


/**
 * @brief Unloads a previously loaded font and resets this BFont to the plain system font.
 *
 * Sends AS_REMOVE_FONT to the server to deregister the font identified by
 * the current family and style IDs. On success the family ID, style ID, and
 * face flags are set to the values of the system plain font and the height
 * cache is invalidated.
 *
 * @return A status code.
 * @retval B_OK On success.
 * @see LoadFont()
 */
status_t
BFont::UnloadFont()
{
	BPrivate::AppServerLink link;

	link.StartMessage(AS_REMOVE_FONT);

	link.Attach<uint16>(fFamilyID);
	link.Attach<uint16>(fStyleID);

	status_t status = B_ERROR;
	if (link.FlushWithReply(status) != B_OK || status != B_OK) {
		return status;
	}

	// reset to plain font
	fFamilyID = sPlainFont.fFamilyID;
	fStyleID = sPlainFont.fStyleID;
	fFace = sPlainFont.fFace;
	fExtraFlags = sPlainFont.fExtraFlags;

	fHeight.ascent = kUninitializedAscent;

	return B_OK;
}
