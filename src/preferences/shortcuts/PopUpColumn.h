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
 * MIT License. Copyright 2015, Haiku, Inc.
 * Original author: Josef Gajdusek.
 */

/** @file PopUpColumn.h
    @brief BStringColumn variant whose cells expose a pop-up menu / inline editor. */

#ifndef POPUPCOLUMN_H
#define POPUPCOLUMN_H

#include <ColumnTypes.h>

class BPopUpMenu;

/**
 * @brief BColumnListView column that opens a pop-up menu (or text editor)
 *        when its cells are clicked.
 *
 * Used by the Shortcuts panel to expose modifier-state menus, the keycap
 * picker, and the application/command field. Supports three behaviors:
 * read-only menu, in-place editable text, or click-to-cycle through items.
 */
class PopUpColumn : public BStringColumn {
public:
						PopUpColumn(BPopUpMenu* menu, const char* name,
								float width, float minWidth, float maxWidth,
								uint32 truncate, bool editable = false,
								bool cycle = false, int cycleInit = 0,
								alignment align = B_ALIGN_LEFT);
	virtual				~PopUpColumn();

			void		MouseDown(BColumnListView* parent, BRow* row,
							BField* field, BRect fieldRect, BPoint point,
							uint32 buttons);

private:
			bool		fEditable;
			bool		fCycle;
			int			fCycleInit;
			BPopUpMenu*	fMenu;
};

#endif	// POPUPCOLUMN_H
