/*
 * Copyright 2025, Kintsugi OS Contributors.
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
 * This file incorporates work from the Haiku project, originally
 * distributed under the MIT License.
 * Copyright 2005-2009, Haiku.
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 *
 * Author: Ambuj Varshney, ambuj@kintsugi-os.org
 */

/** @file WorkspacePrivate.h
 *  @brief Internal implementation details of Workspace including display layout and configuration. */

#ifndef WORKSPACE_PRIVATE_H
#define WORKSPACE_PRIVATE_H


#include "ScreenConfigurations.h"
#include "WindowList.h"
#include "Workspace.h"

#include <Accelerant.h>
#include <ObjectList.h>
#include <String.h>


/** @brief Associates a display identifier, origin, and display mode for one screen in a workspace. */
struct display_info {
	BString			identifier; /**< Unique string identifying the display. */
	BPoint			origin;     /**< Position of this display in the virtual desktop. */
	display_mode	mode;       /**< Active display mode for this screen. */
};


/** @brief Holds all mutable state for a single workspace: its window list, displays, and color. */
class Workspace::Private {
public:
								Private();
								~Private();

	/** @brief Returns the index of this workspace.
	 *  @return Zero-based workspace index. */
			int32				Index() const { return fWindows.Index(); }

	/** @brief Returns the mutable window list for this workspace.
	 *  @return Reference to the WindowList. */
			WindowList&			Windows() { return fWindows; }

			// displays

	/** @brief Populates the display list from the current Desktop screen layout.
	 *  @param desktop Desktop to query for screen information. */
			void				SetDisplaysFromDesktop(Desktop* desktop);

	/** @brief Returns the number of displays associated with this workspace.
	 *  @return Display count. */
			int32				CountDisplays() const
									{ return fDisplays.CountItems(); }

	/** @brief Returns the display_info at the given index.
	 *  @param index Zero-based display index.
	 *  @return Pointer to the display_info, or NULL if out of range. */
			const display_info*	DisplayAt(int32 index) const
									{ return fDisplays.ItemAt(index); }

			// configuration

	/** @brief Returns the background color of this workspace.
	 *  @return Reference to the background rgb_color. */
			const rgb_color&	Color() const { return fColor; }

	/** @brief Sets the background color of this workspace.
	 *  @param color New background color. */
			void				SetColor(const rgb_color& color);

	/** @brief Returns the currently active screen configuration for this workspace.
	 *  @return Reference to the current ScreenConfigurations. */
			ScreenConfigurations& CurrentScreenConfiguration()
									{ return fCurrentScreenConfiguration; }

	/** @brief Returns the stored (persistent) screen configuration for this workspace.
	 *  @return Reference to the stored ScreenConfigurations. */
			ScreenConfigurations& StoredScreenConfiguration()
									{ return fStoredScreenConfiguration; }

	/** @brief Restores workspace settings (color, display config) from a BMessage.
	 *  @param settings Source settings message. */
			void				RestoreConfiguration(const BMessage& settings);

	/** @brief Stores workspace settings into a BMessage for persistence.
	 *  @param settings Destination settings message. */
			void				StoreConfiguration(BMessage& settings);

private:
			void				_SetDefaults();

			WindowList			fWindows;

			BObjectList<display_info> fDisplays;

			ScreenConfigurations fStoredScreenConfiguration;
			ScreenConfigurations fCurrentScreenConfiguration;
			rgb_color			fColor;
};

#endif	/* WORKSPACE_PRIVATE_H */
