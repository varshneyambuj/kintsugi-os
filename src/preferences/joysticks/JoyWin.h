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
 * MIT License. Copyright 2007-2008, Haiku.
 * Original authors: Oliver Ruiz Dorantes, Ryan Leavengood, Fredrik Modéen.
 */

/** @file JoyWin.h
    @brief Main joystick preferences window: ports, controllers, probing. */

#ifndef _JOY_WIN_H
#define _JOY_WIN_H

#include <Window.h>

class BJoystick;
class BCheckBox;
class BStringView;
class BListView;
class PortItem;
class BButton;
class BEntry;
class BFile;

/**
 * @brief Main window of the Joysticks preference panel.
 *
 * Presents the list of game ports detected by the kernel alongside the
 * controller descriptors known to the system, and offers actions to probe
 * a port, disable it, or open the calibration window.
 */
class JoyWin : public BWindow {
	public:
		JoyWin(BRect frame,const char *title);
		virtual         ~JoyWin();
		virtual	void	MessageReceived(BMessage *message);
		virtual	bool	QuitRequested();

	private:		
		status_t		_AddToList(BListView *list, uint32 command, 
							const char* rootPath, BEntry *rootEntry = NULL);
		
		status_t		_Calibrate();
		status_t		_PerformProbe(const char* path);
		status_t		_ApplyChanges();
		status_t		_GetSettings();
		status_t		_CheckJoystickExist(const char* path);
		
		/*Show Alert Boxes*/
		status_t		_ShowProbeMesage(const char* str);
		status_t		_ShowCantFindFileMessage(const char* port);
		void	 		_ShowNoDeviceConnectedMessage(const char* joy, 
							const char* port);
		void			_ShowNoCompatibleJoystickMessage();
		
		/*Util*/
		BString			_FixPathToName(const char* port);
		BString			_BuildDisabledJoysticksFile();
		PortItem*		_GetSelectedItem(BListView* list);
		void			_SelectDeselectJoystick(BListView* list, bool enable);
		int32			_FindStringItemInList(BListView *view, 
						PortItem *item);
		BString			_FindFilePathForSymLink(const char* symLinkPath, 
						PortItem *item);
		status_t		_FindStick(const char* name);
		const char*		_FindSettingString(const char* name, const char* strPath);
		bool 			_GetLine(BString& string);

	//this one are used when we select joystick when portare selected
		bool 			fSystemUsedSelect;

		BJoystick*		fJoystick;
		BCheckBox*		fCheckbox;
		BStringView*	fGamePortS;
		BStringView*	fConControllerS;
		BListView*		fGamePortL;
		BListView*		fConControllerL;
		BButton*		fCalibrateButton;
		BButton*		fProbeButton;
		
		BFile*			fFileTempProbeJoystick;
//		int32  			fSourceEncoding;
   		char    		fBuffer[4096];
   		off_t   		fPositionInBuffer;
   		ssize_t 		fAmtRead;
};

#endif	/* _JOY_WIN_H */
