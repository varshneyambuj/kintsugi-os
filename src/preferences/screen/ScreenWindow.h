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
 * MIT License. Copyright 2001-2014, Haiku.
 * Original authors: Stefano Ceccherini (burton666@libero.it),
 *                   Axel Doerfler (axeld@pinc-software.de),
 *                   Thomas Kurschel, Rafael Romo,
 *                   John Scipione (jscipione@gmail.com).
 */

/** @file ScreenWindow.h
    @brief Main window of the Screen preferences app: resolution, refresh, color, and multi-monitor controls. */

#ifndef SCREEN_WINDOW_H
#define SCREEN_WINDOW_H


#include <Window.h>

#include "ScreenMode.h"


class BBox;
class BPopUpMenu;
class BMenuField;
class BSlider;
class BSpinner;
class BStringView;

class RefreshWindow;
class MonitorView;
class ScreenSettings;


/**
 * @brief Top-level window that orchestrates display configuration.
 *
 * Owns the resolution / color depth / refresh menus, the brightness
 * slider, the workspace layout spinners, and the monitor preview. Talks
 * to app_server through ScreenMode and applies changes either to the
 * current workspace or to all workspaces depending on the menu selection.
 */
class ScreenWindow : public BWindow {
public:
							ScreenWindow(ScreenSettings *settings);
	virtual					~ScreenWindow();

	virtual	bool			QuitRequested();
	virtual	void			MessageReceived(BMessage *message);
	virtual	void			WorkspaceActivated(int32 ws, bool state);
	virtual	void			ScreenChanged(BRect frame, color_space mode);

private:
			void			_BuildSupportedColorSpaces();

			void			_CheckApplyEnabled();
			void			_CheckResolutionMenu();
			void			_CheckColorMenu();
			void			_CheckRefreshMenu();

			void			_UpdateActiveMode();
			void			_UpdateActiveMode(int32 workspace);
			void			_UpdateWorkspaceButtons();
			void			_UpdateRefreshControl();
			void			_UpdateMonitorView();
			void			_UpdateControls();
			void			_UpdateOriginal();
			void			_UpdateMonitor();
			void			_UpdateColorLabel();

			void			_Apply();

			status_t		_WriteVesaModeFile(const screen_mode& mode) const;
			bool			_IsVesa() const { return fIsVesa; }

private:
			ScreenSettings*	fSettings;
			bool			fIsVesa;
			bool			fBootWorkspaceApplied;

			BBox*			fScreenBox;
			BStringView*	fDeviceInfo;
			MonitorView*	fMonitorView;
			BMenuItem*		fAllWorkspacesItem;

			BSpinner*		fColumnsControl;
			BSpinner*		fRowsControl;

			uint32			fSupportedColorSpaces;
			BMenuItem*		fUserSelectedColorSpace;

			BPopUpMenu*		fResolutionMenu;
			BMenuField*		fResolutionField;
			BPopUpMenu*		fColorsMenu;
			BMenuField*		fColorsField;
			BPopUpMenu*		fRefreshMenu;
			BMenuField*		fRefreshField;
			BMenuItem*		fOtherRefresh;

			BPopUpMenu*		fCombineMenu;
			BMenuField*		fCombineField;
			BPopUpMenu*		fSwapDisplaysMenu;
			BMenuField*		fSwapDisplaysField;
			BPopUpMenu*		fUseLaptopPanelMenu;
			BMenuField*		fUseLaptopPanelField;
			BPopUpMenu*		fTVStandardMenu;
			BMenuField*		fTVStandardField;

			BSlider*		fBrightnessSlider;

			BButton*		fDefaultsButton;
			BButton*		fApplyButton;
			BButton*		fRevertButton;

			ScreenMode		fScreenMode;
			ScreenMode		fUndoScreenMode;
				// screen modes for all workspaces

			screen_mode		fActive, fSelected, fOriginal;
				// screen modes for the current workspace

			uint32			fOriginalWorkspacesColumns;
			uint32			fOriginalWorkspacesRows;
			float			fOriginalBrightness;
			bool			fModified;
};

#endif	/* SCREEN_WINDOW_H */
