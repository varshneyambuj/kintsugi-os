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
 *   Copyright 2007-2009 Haiku.
 *   Distributed under the terms of the MIT License.
 *
 *   Authors:
 *       Oliver Ruiz Dorantes, oliver.ruiz.dorantes_at_gmail.com
 *       Ryan Leavengood, leavengood@gmail.com
 *       Fredrik Modéen, fredrik@modeen.se
 */


/**
 * @file JoyWin.cpp
 * @brief Main window of the Joysticks preference panel.
 *
 * JoyWin presents two side-by-side lists: one of game ports detected on the
 * machine and one of known controller descriptors found under the joystick
 * data directory. The user can probe a port for an attached controller,
 * disable a port, or open a calibration window. Selections are persisted as
 * symlinks under the joystick settings directory.
 *
 * @see PortItem, MessageWin, CalibWin
 */


#include "JoyWin.h"
#include "MessageWin.h"
#include "CalibWin.h"
#include "Global.h"
#include "PortItem.h"
//#include "FileReadWrite.h"

#include <stdio.h>
#include <stdlib.h>

#include <Box.h>
#include <Button.h>
#include <CheckBox.h>
#include <ListView.h>
#include <ListItem.h>
#include <ScrollView.h>
#include <String.h>
#include <StringView.h>
#include <Application.h>
#include <View.h>
#include <Path.h>
#include <Entry.h>
#include <Directory.h>
#include <Alert.h>
#include <File.h>
#include <SymLink.h>
#include <Messenger.h>
#include <Directory.h>
#include <Joystick.h>
#include <FindDirectory.h>
#include <Joystick.h>

/** @brief Devfs directory containing per-port joystick device nodes. */
#define JOYSTICKPATH "/dev/joystick/"
/** @brief System directory shipped with controller descriptor files. */
#define JOYSTICKFILEPATH "/boot/system/data/joysticks/"
/** @brief User settings directory holding port-to-controller symlinks. */
#define JOYSTICKFILESETTINGS "/boot/home/config/settings/joysticks/"
/** @brief Reusable alert text shown when no game port is selected. */
#define SELECTGAMEPORTFIRST "Select a game port first"

/**
 * @brief Shows a single-button informational alert.
 *
 * @param string Message text to display in the alert.
 *
 * @return The button index returned by BAlert::Go() (always 0 here).
 */
static int
ShowMessage(char* string)
{
	BAlert *alert = new BAlert("Message", string, "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	return alert->Go();
}

/**
 * @brief Constructs the main JoyWin and populates its lists.
 *
 * Sets up the game-port and controller list views, action buttons, and the
 * disable checkbox; then enumerates devices via BJoystick and known
 * descriptor files under JOYSTICKFILEPATH.
 *
 * @param frame Initial window frame.
 * @param title Window title.
 */
JoyWin::JoyWin(BRect frame, const char *title)
	: BWindow(frame, title, B_DOCUMENT_WINDOW_LOOK, B_NORMAL_WINDOW_FEEL,
		B_NOT_ZOOMABLE), fSystemUsedSelect(false),
		fJoystick(new BJoystick)
{
	fProbeButton = new BButton(BRect(15.00, 260.00, 115.00, 285.00),
		"ProbeButton", "Probe", new BMessage(PROBE));

	fCalibrateButton = new BButton(BRect(270.00, 260.00, 370.00, 285.00),
		"CalibrateButton", "Calibrate", new BMessage(CALIBRATE));

	fGamePortL = new BListView(BRect(15.00, 30.00, 145.00, 250.00),
		"gamePort");
	fGamePortL->SetSelectionMessage(new BMessage(PORT_SELECTED));
	fGamePortL->SetInvocationMessage(new BMessage(PORT_INVOKE));

	fConControllerL = new BListView(BRect(175.00,30.00,370.00,250.00),
		"conController");
	fConControllerL->SetSelectionMessage(new BMessage(JOY_SELECTED));
	fConControllerL->SetInvocationMessage(new BMessage(JOY_INVOKE));

	fGamePortS = new BStringView(BRect(15, 5, 160, 25), "gpString",
		"Game port");
	fGamePortS->SetFont(be_bold_font);
	fConControllerS = new BStringView(BRect(170, 5, 330, 25), "ccString",
		"Connected controller");

	fConControllerS->SetFont(be_bold_font);

	fCheckbox = new BCheckBox(BRect(131.00, 260.00, 227.00, 280.00),
		"Disabled", "Disabled", new BMessage(DISABLEPORT));
	BBox *box = new BBox( Bounds(),"box", B_FOLLOW_ALL,
		B_WILL_DRAW | B_FRAME_EVENTS | B_FULL_UPDATE_ON_RESIZE,
		B_PLAIN_BORDER);

	box->SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	// Add listViews with their scrolls
	box->AddChild(new BScrollView("PortScroll", fGamePortL,
		B_FOLLOW_LEFT | B_FOLLOW_TOP_BOTTOM, B_WILL_DRAW, false, true));

	box->AddChild(new BScrollView("ConScroll", fConControllerL, B_FOLLOW_ALL,
		B_WILL_DRAW, false, true));

	// Adding object
	box->AddChild(fCheckbox);
	box->AddChild(fGamePortS);
	box->AddChild(fConControllerS);
	box->AddChild(fProbeButton);
	box->AddChild(fCalibrateButton);
	AddChild(box);

	SetSizeLimits(400, 600, Bounds().Height(), Bounds().Height());

	/* Add all the devices */
	int32 nr = fJoystick->CountDevices();
	for (int32 i = 0; i < nr;i++) {
		//BString str(path.Path());
		char buf[B_OS_NAME_LENGTH];
		fJoystick->GetDeviceName(i, buf, B_OS_NAME_LENGTH);
		fGamePortL->AddItem(new PortItem(buf));
	}
	fGamePortL->Select(0);

	/* Add the joysticks specifications */
	_AddToList(fConControllerL, JOY_SELECTED, JOYSTICKFILEPATH);

	_GetSettings();
}


/**
 * @brief Destructor; asks the application to terminate when the window is
 *        gone.
 */
JoyWin::~JoyWin()
{
	//delete fFileTempProbeJoystick;
	be_app_messenger.SendMessage(B_QUIT_REQUESTED);
}


/**
 * @brief Dispatches BMessages from the controls in this window.
 *
 * Handles port and controller selection events, the disable checkbox, the
 * Probe and Calibrate buttons, and list invocations. Unknown messages are
 * forwarded to the BWindow base class.
 *
 * @param message Incoming BMessage whose @c what code drives the dispatch.
 */
void
JoyWin::MessageReceived(BMessage *message)
{
//	message->PrintToStream();
	switch(message->what)
	{
		case DISABLEPORT:
		break;
		{
			PortItem *item = _GetSelectedItem(fGamePortL);
			if (item != NULL) {
				//ToDo: item->SetEnabled(true);
				//don't work as you can't select a item that are disabled
				if(fCheckbox->Value()) {
					item->SetEnabled(false);
					_SelectDeselectJoystick(fConControllerL, false);
				} else {
					item->SetEnabled(true);
					_SelectDeselectJoystick(fConControllerL, true);
					_PerformProbe(item->Text());
				}
			} //else
				//printf("We have a null value\n");
		break;
		}

		case PORT_SELECTED:
		{
			PortItem *item = _GetSelectedItem(fGamePortL);
			if (item != NULL) {
				fSystemUsedSelect = true;
				if (item->IsEnabled()) {
					//printf("SetEnabled = false\n");
					fCheckbox->SetValue(false);
					_SelectDeselectJoystick(fConControllerL, true);
				} else {
					//printf("SetEnabled = true\n");
					fCheckbox->SetValue(true);
					_SelectDeselectJoystick(fConControllerL, false);
				}

				if (_CheckJoystickExist(item->Text()) == B_ERROR) {
					if (_ShowCantFindFileMessage(item->Text()) == B_OK) {
						_PerformProbe(item->Text());
					}
				} else {
					BString str(_FindFilePathForSymLink(JOYSTICKFILESETTINGS,
						item));
					if (str != NULL) {
						BString str(_FixPathToName(str.String()));
						int32 id = _FindStringItemInList(fConControllerL,
									new PortItem(str.String()));
						if (id > -1) {
							fConControllerL->Select(id);
							item->SetJoystickName(BString(str.String()));
						}
					}
				}
			} else {
				fConControllerL->DeselectAll();
				ShowMessage((char*)SELECTGAMEPORTFIRST);
			}
		break;
		}

		case PROBE:
		case PORT_INVOKE:
		{
			PortItem *item = _GetSelectedItem(fGamePortL);
			if (item != NULL) {
				//printf("invoke.. inte null\n");
				_PerformProbe(item->Text());
			} else
				ShowMessage((char*)SELECTGAMEPORTFIRST);
		break;
		}

		case JOY_SELECTED:
		{
			if (!fSystemUsedSelect) {
				PortItem *controllerName = _GetSelectedItem(fConControllerL);
				PortItem *portname = _GetSelectedItem(fGamePortL);
				if (portname != NULL && controllerName != NULL) {
					portname->SetJoystickName(BString(controllerName->Text()));

					BString str = portname->GetOldJoystickName();
					if (str != NULL) {
						BString strOldFile(JOYSTICKFILESETTINGS);
						strOldFile.Append(portname->Text());
						BEntry entry(strOldFile.String());
						entry.Remove();
					}
					BString strLinkPlace(JOYSTICKFILESETTINGS);
					strLinkPlace.Append(portname->Text());

					BString strLinkTo(JOYSTICKFILEPATH);
					strLinkTo.Append(controllerName->Text());

					BDirectory *dir = new BDirectory();
					dir->CreateSymLink(strLinkPlace.String(),
						strLinkTo.String(), NULL);
				} else
					ShowMessage((char*)SELECTGAMEPORTFIRST);
			}

			fSystemUsedSelect = false;
		break;
		}

		case CALIBRATE:
		case JOY_INVOKE:
		{
			PortItem *controllerName = _GetSelectedItem(fConControllerL);
			PortItem *portname = _GetSelectedItem(fGamePortL);
			if (portname != NULL) {
				if (controllerName == NULL)
					_ShowNoDeviceConnectedMessage("known", portname->Text());
				else {
					_ShowNoDeviceConnectedMessage(controllerName->Text(), portname->Text());
					/*
					ToDo:
					Check for a device, and show calibrate window if so
					*/
				}
			} else
				ShowMessage((char*)SELECTGAMEPORTFIRST);
		break;
		}
		default:
			BWindow::MessageReceived(message);
			break;
	}
}


/**
 * @brief Persists pending changes before allowing the window to close.
 *
 * @return The default BWindow::QuitRequested() result.
 */
bool
JoyWin::QuitRequested()
{
	_ApplyChanges();
	return BWindow::QuitRequested();
}


//---------------------- Private ---------------------------------//
/**
 * @brief Recursively adds files under @a rootPath to @a list as PortItems.
 *
 * Used to populate the controller list from the system descriptor
 * directory. Only regular files are added; subdirectories are walked.
 *
 * @param list      Target list view receiving newly created PortItems.
 * @param command   Reserved for command association (currently unused).
 * @param rootPath  Filesystem path used as the prefix to strip from entries.
 * @param rootEntry Optional starting BEntry; when NULL the walk begins at
 *                  @a rootPath.
 *
 * @retval B_OK    Walk completed successfully.
 * @retval B_ERROR Both @a rootEntry and @a rootPath were NULL.
 */
status_t
JoyWin::_AddToList(BListView *list, uint32 command, const char* rootPath,
	BEntry *rootEntry)
{
	BDirectory root;

	if ( rootEntry != NULL )
		root.SetTo( rootEntry );
	else if ( rootPath != NULL )
		root.SetTo( rootPath );
	else
		return B_ERROR;

	BEntry entry;
	while ((root.GetNextEntry(&entry)) > B_ERROR ) {
		if (entry.IsDirectory()) {
			_AddToList(list, command, rootPath, &entry);
		} else {
			BPath path;
			entry.GetPath(&path);
			BString str(path.Path());
			str.RemoveFirst(rootPath);
			list->AddItem(new PortItem(str.String()));
		}
	}
	return B_OK;
}


/**
 * @brief Opens the placeholder calibration window.
 *
 * @return B_OK once the window has been allocated and shown.
 */
status_t
JoyWin::_Calibrate()
{
	CalibWin* calibw;
	BRect rect(100, 100, 500, 400);
	calibw = new CalibWin(rect, "Calibrate", B_DOCUMENT_WINDOW_LOOK,
		B_NORMAL_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE);
	calibw->Show();
	return B_OK;
}


/**
 * @brief Probes a game port for any known controller descriptor.
 *
 * Asks the user for confirmation, then iterates the controller list and
 * displays the search progress in a transient MessageWin. Always shows the
 * "no compatible joystick" alert at the end because real probing is not yet
 * implemented.
 *
 * @param path Device name of the port to probe (no leading
 *             /dev/joystick/ prefix).
 *
 * @retval B_OK  Probe completed (whether or not anything matched).
 * @retval other Whatever @c _ShowProbeMesage() returned when the user
 *               cancelled the prompt.
 */
status_t
JoyWin::_PerformProbe(const char* path)
{
	status_t err = B_ERROR;
	err = _ShowProbeMesage(path);
	if (err != B_OK) {
		return err;
	}

	MessageWin* mesgw = new MessageWin(Frame(),"Probing", B_MODAL_WINDOW_LOOK,
		B_MODAL_APP_WINDOW_FEEL, B_NOT_RESIZABLE | B_NOT_ZOOMABLE);

	mesgw->Show();
	int32 number = fConControllerL->CountItems();
	PortItem *item;
	for (int i = 0; i < number; i++) {
		// Do a search in JOYSTICKFILEPATH with item->Text() find the string
		// that starts with "gadget" (tex gadget = "GamePad Pro") remove
		// spacing and the "=" ty to open this one, if failed move to next and
		// try to open.. list those that suxesfully work
		fConControllerL->Select(i);
		int32 selected = fConControllerL->CurrentSelection();
		item = dynamic_cast<PortItem*>(fConControllerL->ItemAt(selected));
		BString str("Looking for: ");
		str << item->Text() << " in port " << path;
		mesgw->SetText(str.String());
		_FindSettingString(item->Text(), JOYSTICKFILEPATH);
		//Need a check to find joysticks (don't know how right now so show a
		// don't find message)
	}
	mesgw->Hide();

	//Need a if !found then show this message. else list joysticks.
	_ShowNoCompatibleJoystickMessage();
	return B_OK;
}


/**
 * @brief Persists the list of disabled ports.
 *
 * Builds the disabled-joysticks file content but currently does not write
 * it; serialization is left as a TODO.
 *
 * @return Always B_OK.
 *
 * @todo Save the string as @c disabled_joysticks under
 *       @c /boot/home/config/settings.
 */
status_t
JoyWin::_ApplyChanges()
{
	BString str = _BuildDisabledJoysticksFile();
	//ToDo; Save the string as the file "disabled_joysticks" under settings
	//   (/boot/home/config/settings/disabled_joysticks)
	return B_OK;
}


/**
 * @brief Reads the disabled-joysticks settings file at startup.
 *
 * @return Always B_OK.
 *
 * @todo Read @c /boot/home/config/settings/disabled_joysticks and apply the
 *       disabled state to matching port items.
 */
status_t
JoyWin::_GetSettings()
{
	// ToDo; Read the file "disabled_joysticks" and make the port with the
	// same name disabled (/boot/home/config/settings/disabled_joysticks)
	return B_OK;
}


/**
 * @brief Tests whether a settings symlink for @a path already exists.
 *
 * @param path Port name to look up under JOYSTICKFILESETTINGS.
 *
 * @retval B_OK    The settings file exists and is readable.
 * @retval B_ERROR No matching file was found.
 */
status_t
JoyWin::_CheckJoystickExist(const char* path)
{
	BString str(JOYSTICKFILESETTINGS);
	str << path;

	BFile file;
	status_t status = file.SetTo(str.String(), B_READ_ONLY | B_FAIL_IF_EXISTS);

	if (status == B_FILE_EXISTS || status == B_OK)
		return B_OK;
	else
		return B_ERROR;
}


/**
 * @brief Displays a confirmation dialog before probing a port.
 *
 * Warns the user that probing may, in theory, lock up the machine. Only
 * proceed if the user clicks Probe.
 *
 * @param device Port device name shown in the message body.
 *
 * @retval B_OK    User chose to proceed with probing.
 * @retval B_ERROR User cancelled or pressed Escape.
 */
status_t
JoyWin::_ShowProbeMesage(const char* device)
{
	BString str("An attempt will be made to probe the port '");
	str << device << "' to try to figure out what kind of joystick (if any) ";
	str << "are attached. There is a small chance this process might cause ";
	str << "your machine to lock up and require a reboot. Make sure you have ";
	str << "saved changes in all open applications before you start probing.";

	BAlert *alert = new BAlert("test1", str.String(), "Probe", "Cancel");
	alert->SetShortcut(1, B_ESCAPE);
	int32 bindex = alert->Go();

	if (bindex == 0)
		return B_OK;
	else
		return B_ERROR;
}


//Used when a files/joysticks are no were to be found
/**
 * @brief Prompts the user when a port has no associated descriptor file.
 *
 * Offers to autodetect the connected joystick.
 *
 * @param port Port device name shown in the message.
 *
 * @retval B_OK    User chose Probe to autodetect.
 * @retval B_ERROR User chose Stop.
 */
status_t
JoyWin::_ShowCantFindFileMessage(const char* port)
{
	BString str("The file '");
	str <<  _FixPathToName(port).String() << "' used by '" << port;
	str << "' cannot be found.\n Do you want to ";
	str << "try auto-detecting a joystick for this port?";

	BAlert *alert = new BAlert("test1", str.String(), "Stop", "Probe");
	alert->SetShortcut(1, B_ENTER);
	int32 bindex = alert->Go();

	if (bindex == 1)
		return B_OK;
	else
		return B_ERROR;
}


/**
 * @brief Shows the alert displayed when no compatible joystick is detected
 *        on the probed port.
 */
void
JoyWin::_ShowNoCompatibleJoystickMessage()
{
	BString str("There were no compatible joysticks detected on this game");
	str << " port. Try another port, or ask the manufacturer of your joystick";
	str << " for a driver designed for Haiku or BeOS.";

	BAlert *alert = new BAlert("test1", str.String(), "OK");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}

/**
 * @brief Shows an alert reporting that @a joy is not connected to @a port.
 *
 * @param joy  Name of the controller the user expected to use.
 * @param port Game port the controller was expected to be on.
 */
void
JoyWin::_ShowNoDeviceConnectedMessage(const char* joy, const char* port)
{
	BString str("There does not appear to be a ");
	str << joy << " device connected to the port '" << port << "'.";

	BAlert *alert = new BAlert("test1", str.String(), "Stop");
	alert->SetFlags(alert->Flags() | B_CLOSE_ON_ESCAPE);
	alert->Go();
}


// Use this function to get a string of disabled ports
/**
 * @brief Builds the textual content of the disabled-joysticks settings
 *        file.
 *
 * Iterates the port list and adds a @c disable line for every port that is
 * marked as disabled in the UI.
 *
 * @return A BString ready to be written to the settings file.
 */
BString
JoyWin::_BuildDisabledJoysticksFile()
{
	BString temp("# This is a list of disabled joystick devices.");
	temp << "# Do not include the /dev/joystick/ part of the device name.";

	int32 number = fGamePortL->CountItems();
	PortItem *item;
	for (int i = 0; i < number; i++) {
		item = dynamic_cast<PortItem*>(fGamePortL->ItemAt(i));
		if (!item->IsEnabled())
			temp << "disable = \"" <<  item->Text() << "\"";
	}
	return temp;
}


/**
 * @brief Returns the currently selected PortItem in @a list.
 *
 * @param list List view to query.
 *
 * @return The selected PortItem, or NULL if nothing is selected.
 */
PortItem*
JoyWin::_GetSelectedItem(BListView* list)
{
	int32 id = list->CurrentSelection();
	if (id > -1) {
		//PortItem *item = dynamic_cast<PortItem*>(list->ItemAt(id));
		return dynamic_cast<PortItem*>(list->ItemAt(id));
	} else {
		return NULL;
	}
}


/**
 * @brief Strips any directory prefix from @a port, returning the basename.
 *
 * @param port File path or port label.
 *
 * @return Bare name with leading directories removed.
 */
BString
JoyWin::_FixPathToName(const char* port)
{
	BString temp(port);
	temp = temp.Remove(0, temp.FindLast('/') + 1) ;
	return temp;
}


/**
 * @brief Updates the enabled state of every PortItem in a list.
 *
 * Clears the current selection and then forces every item's enabled flag to
 * @a enable.
 *
 * @param list   List view to walk.
 * @param enable New enabled state to apply to every item.
 */
void
JoyWin::_SelectDeselectJoystick(BListView* list, bool enable)
{
	list->DeselectAll();
	int32 number = fGamePortL->CountItems();
	PortItem *item;
	for (int i = 0; i < number; i++) {
		item = dynamic_cast<PortItem*>(list->ItemAt(i));
		if (!item) {
			printf("%s: PortItem at %d is null!\n", __func__, i);
			continue;
		}
		item->SetEnabled(enable);
	}
}


/**
 * @brief Searches @a view for a PortItem whose label matches @a item.
 *
 * @param view List view to scan.
 * @param item Item carrying the label to match against.
 *
 * @return Index of the matching item or -1 when no match is found.
 */
int32
JoyWin::_FindStringItemInList(BListView *view, PortItem *item)
{
	PortItem *strItem = NULL;
	int32 number = view->CountItems();
	for (int32 i = 0; i < number; i++) {
		strItem = dynamic_cast<PortItem*>(view->ItemAt(i));
		if (!strcmp(strItem->Text(), item->Text())) {
			return i;
		}
	}
	delete strItem;
	return -1;
}


/**
 * @brief Resolves the target of a settings symlink.
 *
 * Looks up @c symLinkPath/item->Text() and, if it is a symlink, returns the
 * absolute path it points at.
 *
 * @param symLinkPath Directory containing the symlink.
 * @param item        PortItem whose label is used as the file name.
 *
 * @return Absolute target path, or a NULL BString if the entry is not a
 *         symlink.
 */
BString
JoyWin::_FindFilePathForSymLink(const char* symLinkPath, PortItem *item)
{
	BPath path(symLinkPath);
	path.Append(item->Text());
	BEntry entry(path.Path());
	if (entry.IsSymLink()) {
		BSymLink symLink(&entry);
		BDirectory parent;
		entry.GetParent(&parent);
		symLink.MakeLinkedPath(&parent, &path);
		BString str(path.Path());
		return str;
	}
	return NULL;
}


/**
 * @brief Attempts to open a BJoystick on the named device.
 *
 * @param name Joystick device path passed to BJoystick::Open().
 *
 * @return The status_t reported by BJoystick::Open().
 *
 * @note The temporary BJoystick is leaked. Provided as a probe helper.
 */
status_t
JoyWin::_FindStick(const char* name)
{
	BJoystick *stick = new BJoystick();
	return stick->Open(name);
}


/**
 * @brief Tries to read a controller descriptor file by name.
 *
 * Currently only opens the file and reports any error; descriptor parsing
 * is left as a TODO and the function always returns the empty string.
 *
 * @param name    Descriptor file basename.
 * @param strPath Directory where descriptors are stored.
 *
 * @return Always the empty string.
 *
 * @todo Parse the descriptor and try to open the matching joystick.
 */
const char*
JoyWin::_FindSettingString(const char* name, const char* strPath)
{
	//Make a BJoystick try open it
	BString str;

	BPath path(strPath);
	path.Append(name);
	fFileTempProbeJoystick = new BFile(path.Path(), B_READ_ONLY);

	//status_t err = find_directory(B_SYSTEM_ETC_DIRECTORY, &path);
//	if (err == B_OK) {
		//BString str(path.Path());
		//str << "/joysticks/" << name;
		//printf("path'%s'\n", str.String());
		//err = file->SetTo(strPath, B_READ_ONLY);
		status_t err = fFileTempProbeJoystick->InitCheck();
		if (err == B_OK) {
			//FileReadWrite frw(fFileTempProbeJoystick);
			//printf("Get going\n");
			//printf("Opening file = %s\n", path.Path());
			//while (frw.Next(str)) {
				//printf("In While loop\n");
			//	printf("getline %s \n", str.String());
				//Test to open joystick with x number
			//}
			/*while (_GetLine(str)) {
				//printf("In While loop\n");
				printf("getline %s \n", str.String());
				//Test to open joystick with x number
			}*/
			return "";
		} else
			printf("BFile.SetTo error: %s, Path = %s\n", strerror(err), str.String());
//	} else
//		printf("find_directory error: %s\n", strerror(err));

//	delete file;
	return "";
}

/*
//Function to get a line from a file
bool
JoyWin::_GetLine(BString& string)
{
	// Fill up the buffer with the first chunk of code
	if (fPositionInBuffer == 0)
		fAmtRead = fFileTempProbeJoystick->Read(&fBuffer, sizeof(fBuffer));
	while (fAmtRead > 0) {
		while (fPositionInBuffer < fAmtRead) {
			// Return true if we hit a newline or the end of the file
			if (fBuffer[fPositionInBuffer] == '\n') {
				fPositionInBuffer++;
				//Convert begin
				int32 state = 0;
				int32 bufferLen = string.Length();
				int32 destBufferLen = bufferLen;
				char destination[destBufferLen];
//				if (fSourceEncoding)
//					convert_to_utf8(fSourceEncoding, string.String(), &bufferLen, destination, &destBufferLen, &state);
				string = destination;
				return true;
			}
			string += fBuffer[fPositionInBuffer];
			fPositionInBuffer++;
		}

		// Once the buffer runs out, grab some more and start again
		fAmtRead = fFileTempProbeJoystick->Read(&fBuffer, sizeof(fBuffer));
		fPositionInBuffer = 0;
	}
	return false;
}
*/
