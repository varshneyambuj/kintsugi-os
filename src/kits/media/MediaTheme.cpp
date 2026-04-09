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
 *   Copyright 2001-2006, Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Marcus Overhagen
 *       Axel Dörfler, axeld@pinc-software.de
 */

/** @file MediaTheme.cpp
 *  @brief Implements BMediaTheme, the abstract base class for themes that
 *         generate UI controls for BParameterWeb parameter graphs.
 */


#include "DefaultMediaTheme.h"
#include "MediaDebug.h"

#include <MediaTheme.h>
#include <StringView.h>
#include <locks.h>

#include <string.h>


static mutex sLock = MUTEX_INITIALIZER("media theme lock");
BMediaTheme* BMediaTheme::sDefaultTheme;


/**
 * @brief Destructor. Frees the name and info strings.
 */
BMediaTheme::~BMediaTheme()
{
	CALLED();

	free(fName);
	free(fInfo);
}


/**
 * @brief Return the name of this theme.
 *
 * @return Pointer to the theme name string.
 */
const char *
BMediaTheme::Name()
{
	return fName;
}


/**
 * @brief Return the informational description of this theme.
 *
 * @return Pointer to the theme info string.
 */
const char *
BMediaTheme::Info()
{
	return fInfo;
}


/**
 * @brief Return the unique identifier of this theme.
 *
 * @return Theme ID.
 */
int32
BMediaTheme::ID()
{
	return fID;
}


/**
 * @brief Retrieve the add-on entry ref for themes that live in add-ons.
 *
 * @param ref  If non-NULL and this theme is an add-on, receives the entry ref.
 * @return true if this theme is an add-on and @p ref was filled in, false otherwise.
 */
bool
BMediaTheme::GetRef(entry_ref* ref)
{
	if (!fIsAddOn || ref == NULL)
		return false;

	*ref = fAddOnRef;
	return true;
}


/**
 * @brief Create a BView that represents the entire parameter web.
 *
 * Uses the preferred theme (or @p usingTheme if supplied) to build the view.
 * Returns a placeholder string view if no theme is available.
 *
 * @param web          The parameter web to render.
 * @param hintRect     Optional hint for the initial view position and size.
 * @param usingTheme   Theme to use; NULL selects the preferred theme.
 * @return A newly allocated BView, or NULL on failure.
 */
BView *
BMediaTheme::ViewFor(BParameterWeb* web, const BRect* hintRect,
	BMediaTheme* usingTheme)
{
	CALLED();

	// use default theme if none was specified
	if (usingTheme == NULL)
		usingTheme = PreferredTheme();

	if (usingTheme == NULL) {
		BStringView* view = new BStringView(BRect(0, 0, 200, 30), "",
			"No BMediaTheme available, sorry!");
		view->ResizeToPreferred();
		return view;
	}

	return usingTheme->MakeViewFor(web, hintRect);
}


/**
 * @brief Set or reset the application-wide preferred media theme.
 *
 * If @p defaultTheme is NULL the preferred theme is reset to the built-in
 * DefaultMediaTheme. Ownership of @p defaultTheme is transferred.
 *
 * @param defaultTheme  New preferred theme, or NULL to reset to the default.
 * @return B_OK always.
 */
status_t
BMediaTheme::SetPreferredTheme(BMediaTheme* defaultTheme)
{
	CALLED();

	// ToDo: this method should probably set some global settings file
	//	to make the new preferred theme available to all applications

	MutexLocker locker(sLock);

	if (defaultTheme == NULL) {
		// if the current preferred theme is not the default media theme,
		// delete it, and set it back to the default
		if (dynamic_cast<BPrivate::DefaultMediaTheme *>(sDefaultTheme) == NULL)
			sDefaultTheme = new BPrivate::DefaultMediaTheme();

		return B_OK;
	}

	// this method takes possession of the BMediaTheme passed, even
	// if it fails, so it has to delete it
	if (defaultTheme != sDefaultTheme)
		delete sDefaultTheme;

	sDefaultTheme = defaultTheme;

	return B_OK;
}


/**
 * @brief Return the current application-wide preferred media theme.
 *
 * Lazily creates a DefaultMediaTheme if none has been installed yet.
 *
 * @return Pointer to the preferred BMediaTheme (never NULL).
 */
BMediaTheme *
BMediaTheme::PreferredTheme()
{
	CALLED();

	MutexLocker locker(sLock);

	// ToDo: should look in the global prefs file for the preferred
	//	add-on and load this from disk - in the meantime, just use
	//	the default theme

	if (sDefaultTheme == NULL)
		sDefaultTheme = new BPrivate::DefaultMediaTheme();

	return sDefaultTheme;
}


/**
 * @brief Return a background bitmap for the given background kind (not implemented).
 *
 * @param bg  Background kind identifier.
 * @return Always NULL.
 */
BBitmap *
BMediaTheme::BackgroundBitmapFor(bg_kind bg)
{
	UNIMPLEMENTED();
	return NULL;
}


/**
 * @brief Return a background colour for the given background kind (not implemented).
 *
 * @param bg  Background kind identifier.
 * @return The system panel background colour.
 */
rgb_color
BMediaTheme::BackgroundColorFor(bg_kind bg)
{
	UNIMPLEMENTED();
	return ui_color(B_PANEL_BACKGROUND_COLOR);
}


/**
 * @brief Return a foreground colour for the given foreground kind (not implemented).
 *
 * @param fg  Foreground kind identifier.
 * @return White {255,255,255}.
 */
rgb_color
BMediaTheme::ForegroundColorFor(fg_kind fg)
{
	UNIMPLEMENTED();
	rgb_color dummy = {255, 255, 255};

	return dummy;
}


/**
 * @brief Protected constructor for concrete theme subclasses.
 *
 * @param name  Human-readable theme name (copied).
 * @param info  Theme description string (copied).
 * @param ref   Optional entry ref for add-on themes; NULL for built-in themes.
 * @param id    Unique theme identifier.
 */
//! protected BMediaTheme
BMediaTheme::BMediaTheme(const char* name, const char* info,
	const entry_ref* ref, int32 id)
	:
	fID(id)
{
	fName = strdup(name);
	fInfo = strdup(info);

	// ToDo: is there something else here, which has to be done?

	if (ref) {
		fIsAddOn = true;
		fAddOnRef = *ref;
	} else
		fIsAddOn = false;
}


/**
 * @brief Create a default fallback control view for a parameter using the
 *        built-in DefaultMediaTheme.
 *
 * @param parameter  The parameter for which to create a view; may be NULL.
 * @return A BControl, or NULL if @p parameter is NULL.
 */
BControl *
BMediaTheme::MakeFallbackViewFor(BParameter *parameter)
{
	if (parameter == NULL)
		return NULL;

	return BPrivate::DefaultMediaTheme::MakeViewFor(parameter);
}


/*
private unimplemented
BMediaTheme::BMediaTheme()
BMediaTheme::BMediaTheme(const BMediaTheme &clone)
BMediaTheme & BMediaTheme::operator=(const BMediaTheme &clone)
*/

/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_0(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_1(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_2(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_3(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_4(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_5(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_6(void *) { return B_ERROR; }
/** @brief Reserved for future binary compatibility. @return B_ERROR. */
status_t BMediaTheme::_Reserved_ControlTheme_7(void *) { return B_ERROR; }
