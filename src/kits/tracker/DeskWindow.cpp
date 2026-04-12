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
 *   Open Tracker License
 *
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *
 *   Permission is hereby granted, free of charge, to any person obtaining a copy of
 *   this software and associated documentation files (the "Software"), to deal in
 *   the Software without restriction, including without limitation the rights to
 *   use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *   of the Software, and to permit persons to whom the Software is furnished to do
 *   so, subject to the following conditions:
 *
 *   The above copyright notice and this permission notice applies to all licensees
 *   and shall be included in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF TITLE, MERCHANTABILITY,
 *   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 *   BE INCORPORATED BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 *   AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF, OR IN CONNECTION
 *   WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 *   Tracker(TM), Be(R), BeOS(R), and BeIA(TM) are trademarks or registered
 *   trademarks of Be Incorporated in the United States and other countries.
 *   All rights reserved.
 */


/**
 * @file DeskWindow.cpp
 * @brief The Tracker desktop window that hosts the DesktopPoseView and replicant shelf.
 *
 * BDeskWindow creates a full-screen, non-movable, always-behind window containing the
 * DesktopPoseView. It manages Tracker add-on shortcuts, the BShelf for replicants,
 * background image lifecycle, and desktop colour drag-and-drop.
 *
 * @see BContainerWindow, DesktopPoseView, BShelf
 */


#include "DeskWindow.h"

#include <AppFileInfo.h>
#include <Catalog.h>
#include <Debug.h>
#include <FindDirectory.h>
#include <Locale.h>
#include <Messenger.h>
#include <NodeMonitor.h>
#include <Path.h>
#include <PathFinder.h>
#include <PathMonitor.h>
#include <PopUpMenu.h>
#include <Resources.h>
#include <Screen.h>
#include <String.h>
#include <StringList.h>
#include <Volume.h>
#include <WindowPrivate.h>

#include <fcntl.h>
#include <unistd.h>

#include "Attributes.h"
#include "AutoLock.h"
#include "BackgroundImage.h"
#include "Commands.h"
#include "FSUtils.h"
#include "IconMenuItem.h"
#include "KeyInfos.h"
#include "MountMenu.h"
#include "PoseView.h"
#include "Shortcuts.h"
#include "TemplatesMenu.h"
#include "Tracker.h"


const char* kShelfPath = "tracker_shelf";
	// replicant support

const char* kShortcutsSettings = "shortcuts_settings";
const char* kDefaultShortcut = "BEOS:default_shortcut";
const uint32 kDefaultModifiers = B_OPTION_KEY | B_COMMAND_KEY;


/**
 * @brief EachElement callback that matches an AddOnInfo by model name.
 *
 * @param item        Candidate AddOnInfo entry.
 * @param castToName  C-string name to compare against.
 * @return @a item if the name matches, NULL otherwise.
 */
static struct AddOnInfo*
MatchOne(struct AddOnInfo* item, void* castToName)
{
	if (strcmp(item->model->Name(), (const char*)castToName) == 0) {
		// found match, bail out
		return item;
	}

	return 0;
}


/**
 * @brief Register a keyboard shortcut on @a window that launches the add-on @a model.
 *
 * @param model      The add-on model whose entry ref is embedded in the message.
 * @param key        Single character shortcut key; ignored if '\0'.
 * @param modifiers  Modifier key mask (e.g. B_OPTION_KEY | B_COMMAND_KEY).
 * @param window     The BDeskWindow that receives the shortcut.
 */
static void
AddOneShortcut(Model* model, char key, uint32 modifiers, BDeskWindow* window)
{
	if (key == '\0')
		return;

	BMessage* runAddOn = new BMessage(kLoadAddOn);
	runAddOn->AddRef("refs", model->EntryRef());
	window->AddShortcut(key, modifiers, runAddOn);
}


/**
 * @brief EachElement callback that restores an AddOnInfo's shortcut to its compiled default.
 *
 * @param item          The add-on info entry to revert.
 * @param castToWindow  The BDeskWindow used to remove the old and add the default shortcut.
 * @return NULL always (continue iterating).
 */
static struct AddOnInfo*
RevertToDefault(struct AddOnInfo* item, void* castToWindow)
{
	if (item->key != item->defaultKey || item->modifiers != kDefaultModifiers) {
		BDeskWindow* window = static_cast<BDeskWindow*>(castToWindow);
		if (window != NULL) {
			window->RemoveShortcut(item->key, item->modifiers);
			item->key = item->defaultKey;
			item->modifiers = kDefaultModifiers;
			AddOneShortcut(item->model, item->key, item->modifiers, window);
		}
	}

	return 0;
}


/**
 * @brief EachElement callback that matches an AddOnInfo by entry ref equality.
 *
 * @param item         Candidate AddOnInfo entry.
 * @param castToOther  Model pointer whose EntryRef is compared.
 * @return @a item if the refs are equal, NULL otherwise.
 */
static struct AddOnInfo*
FindElement(struct AddOnInfo* item, void* castToOther)
{
	Model* other = static_cast<Model*>(castToOther);
	if (*item->model->EntryRef() == *other->EntryRef())
		return item;

	return 0;
}


/**
 * @brief Scan @a directory for Tracker add-on executables and register them in @a list.
 *
 * Resolves symlinks, reads the default shortcut string resource, registers keyboard
 * shortcuts, and begins watching the directory for changes.
 *
 * @param directory  The add-ons directory to scan.
 * @param window     The BDeskWindow that receives the registered shortcuts.
 * @param list       The global add-on info list to populate.
 */
static void
LoadAddOnDir(BDirectory directory, BDeskWindow* window,
	LockingList<AddOnInfo, true>* list)
{
	BEntry entry;
	while (directory.GetNextEntry(&entry) == B_OK) {
		Model* model = new Model(&entry);
		if (model->InitCheck() == B_OK && model->IsSymLink()) {
			// resolve symlinks
			Model* resolved = new Model(model->EntryRef(), true, true);
			if (resolved->InitCheck() == B_OK)
				model->SetLinkTo(resolved);
			else
				delete resolved;
		}
		if (model->InitCheck() != B_OK
			|| !model->ResolveIfLink()->IsExecutable()) {
			delete model;
			continue;
		}

		char* name = strdup(model->Name());
		if (!list->EachElement(MatchOne, name)) {
			struct AddOnInfo* item = new struct AddOnInfo;
			item->model = model;

			item->has_populate_menu = B_NO_INIT;

			BResources resources(model->ResolveIfLink()->EntryRef());
			size_t size;
			char* shortcut = (char*)resources.LoadResource(B_STRING_TYPE,
				kDefaultShortcut, &size);
			if (shortcut == NULL || strlen(shortcut) > 1)
				item->key = '\0';
			else
				item->key = shortcut[0];
			AddOneShortcut(model, item->key, kDefaultModifiers, window);
			item->defaultKey = item->key;
			item->modifiers = kDefaultModifiers;
			list->AddItem(item);

			// load supported types (if any)
			BFile file(item->model->EntryRef(), B_READ_ONLY);
			if (file.InitCheck() == B_OK) {
				BAppFileInfo info(&file);
				if (info.InitCheck() == B_OK) {
					BMessage types;
					if (info.GetSupportedTypes(&types) == B_OK) {
						int32 i = 0;
						BString supportedType;
						while (types.FindString("types", i, &supportedType) == B_OK) {
							item->supportedTypes.Add(supportedType);
							i++;
						}
					}
				}
			}
		}
		free(name);
	}

	node_ref nodeRef;
	directory.GetNodeRef(&nodeRef);

	TTracker::WatchNode(&nodeRef, B_WATCH_DIRECTORY, window);
}


// #pragma mark - BDeskWindow


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DeskWindow"


/**
 * @brief Construct the desktop window and create the DesktopPoseView for the Desk directory.
 *
 * @param windowList  The application-wide window list managed by TTracker.
 * @param openFlags   Flags controlling window-open behaviour.
 */
BDeskWindow::BDeskWindow(LockingList<BWindow>* windowList, uint32 openFlags)
	:
	BContainerWindow(windowList, openFlags, kDesktopWindowLook, kDesktopWindowFeel,
		B_NOT_MOVABLE | B_WILL_ACCEPT_FIRST_CLICK | B_NOT_ZOOMABLE | B_NOT_CLOSABLE
			| B_NOT_MINIMIZABLE | B_NOT_RESIZABLE | B_ASYNCHRONOUS_CONTROLS,
		B_ALL_WORKSPACES, false),
	fDeskShelf(NULL),
	fShortcutsSettings(NULL)
{
	// create pose view
	BDirectory deskDir;
	if (FSGetDeskDir(&deskDir) == B_OK) {
		BEntry entry;
		deskDir.GetEntry(&entry);
		Model* model = new Model(&entry, true);
		if (model->InitCheck() == B_OK)
			CreatePoseView(model);
		else
			delete model;
	}
}


/**
 * @brief Destructor; saves desktop pose locations and stops node watching.
 */
BDeskWindow::~BDeskWindow()
{
	SaveDesktopPoseLocations();
		// explicit call to SavePoseLocations so that extended pose info
		// gets committed properly
	PoseView()->DisableSaveLocation();
		// prevent double-saving, this would slow down quitting
	PoseView()->StopSettingsWatch();
	stop_watching(this);
}


/**
 * @brief Complete desktop window initialisation after construction.
 *
 * Resizes to the screen frame, loads add-ons and shortcuts, calls the
 * base-class Init(), opens the replicant shelf, and optionally adds the
 * disks icon and icon-scaling shortcuts.
 */
void
BDeskWindow::Init(const BMessage*)
{
	// Set the size of the screen before calling the container window's
	// Init() because it will add volume poses to this window and
	// they will be clipped otherwise

	BScreen screen(this);
	fOldFrame = screen.Frame();

	ResizeTo(fOldFrame.Width(), fOldFrame.Height());

	InitKeyIndices();
	InitAddOnsList(false);
	ApplyShortcutPreferences(false);

	_inherited::Init();

	entry_ref ref;
	BPath path;
	if (!BootedInSafeMode() && FSFindTrackerSettingsDir(&path) == B_OK) {
		path.Append(kShelfPath);
		close(open(path.Path(), O_RDONLY | O_CREAT, S_IRUSR | S_IWUSR
			| S_IRGRP | S_IROTH));
		if (get_ref_for_path(path.Path(), &ref) == B_OK)
			fDeskShelf = new BShelf(&ref, PoseView());

		if (fDeskShelf != NULL)
			fDeskShelf->SetDisplaysZombies(true);
	}

	// Add icon view switching shortcuts. These are displayed in the context
	// menu, although they obviously don't work from those menu items.
	BMessage* message = new BMessage(kIconMode);
	AddShortcut('1', B_COMMAND_KEY, message, PoseView());

	message = new BMessage(kMiniIconMode);
	AddShortcut('2', B_COMMAND_KEY, message, PoseView());

	message = new BMessage(kIconMode);
	message->AddInt32("scale", 1);
	AddShortcut('+', B_COMMAND_KEY, message, PoseView());

	message = new BMessage(kIconMode);
	message->AddInt32("scale", 0);
	AddShortcut('-', B_COMMAND_KEY, message, PoseView());

	if (TrackerSettings().ShowDisksIcon()) {
		// create model for root of everything
		BEntry entry("/");
		Model model(&entry);
		if (model.InitCheck() == B_OK) {
			// add the root icon to desktop window
			BMessage message;
			message.what = B_NODE_MONITOR;
			message.AddInt32("opcode", B_ENTRY_CREATED);
			message.AddInt32("device", model.NodeRef()->device);
			message.AddInt64("node", model.NodeRef()->node);
			message.AddInt64("directory", model.EntryRef()->directory);
			message.AddString("name", model.EntryRef()->name);

			PostMessage(&message, PoseView());
		}
	}
}


/**
 * @brief (Re)build the add-ons list from all Tracker add-on directories.
 *
 * @param update  If true, clears the existing list and shortcuts before rebuilding.
 */
void
BDeskWindow::InitAddOnsList(bool update)
{
	AutoLock<LockingList<AddOnInfo, true> > lock(fAddOnsList);
	if (!lock.IsLocked())
		return;

	if (update) {
		for (int i = fAddOnsList->CountItems() - 1; i >= 0; i--) {
			AddOnInfo* item = fAddOnsList->ItemAt(i);
			RemoveShortcut(item->key, B_OPTION_KEY | B_COMMAND_KEY);
		}
		fAddOnsList->MakeEmpty(true);
	}

	BStringList addOnPaths;
	BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "Tracker", addOnPaths);
	int32 count = addOnPaths.CountStrings();
	for (int32 i = 0; i < count; i++)
		LoadAddOnDir(BDirectory(addOnPaths.StringAt(i)), this, fAddOnsList);
}


/**
 * @brief Read user shortcut overrides from shortcuts_settings and apply them.
 *
 * On the first call (@a update == false) also starts path monitoring on the settings file.
 * Reverts all shortcuts to their defaults before applying the user overrides.
 *
 * @param update  true when called in response to a settings-file change notification.
 */
void
BDeskWindow::ApplyShortcutPreferences(bool update)
{
	AutoLock<LockingList<AddOnInfo, true> > lock(fAddOnsList);
	if (!lock.IsLocked())
		return;

	if (!update) {
		BPath path;
		if (find_directory(B_USER_SETTINGS_DIRECTORY, &path) == B_OK) {
			path.Append(kShortcutsSettings);
			BPathMonitor::StartWatching(path.Path(), B_WATCH_STAT | B_WATCH_FILES_ONLY, this);
			fShortcutsSettings = new char[strlen(path.Path()) + 1];
			strcpy(fShortcutsSettings, path.Path());
		}
	}

	fAddOnsList->EachElement(RevertToDefault, this);

	BFile shortcutSettings(fShortcutsSettings, B_READ_ONLY);
	BMessage fileMsg;
	if (shortcutSettings.InitCheck() != B_OK || fileMsg.Unflatten(&shortcutSettings) != B_OK)
		return;

	int32 i = 0;
	BMessage message;
	while (fileMsg.FindMessage("spec", i++, &message) == B_OK) {
		int32 key;
		if (message.FindInt32("key", &key) != B_OK)
			continue;

		// only handle shortcuts referring add-ons
		BString command;
		if (message.FindString("command", &command) != B_OK)
			continue;

		bool isInAddOns = false;

		BStringList addOnPaths;
		BPathFinder::FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY, "Tracker/", addOnPaths);
		for (int32 i = 0; i < addOnPaths.CountStrings(); i++) {
			if (command.StartsWith(addOnPaths.StringAt(i))) {
				isInAddOns = true;
				break;
			}
		}

		if (!isInAddOns)
			continue;

		BEntry entry(command);
		if (entry.InitCheck() != B_OK)
			continue;

		const char* shortcut = GetKeyName(key);
		if (strlen(shortcut) != 1)
			continue;

		uint32 modifiers = B_COMMAND_KEY;
			// it's required by interface kit to at least
			// have B_COMMAND_KEY
		int32 value;
		if (message.FindInt32("mcidx", 0, &value) == B_OK)
			modifiers |= (value != 0 ? B_SHIFT_KEY : 0);

		if (message.FindInt32("mcidx", 1, &value) == B_OK)
			modifiers |= (value != 0 ? B_CONTROL_KEY : 0);

		if (message.FindInt32("mcidx", 3, &value) == B_OK)
			modifiers |= (value != 0 ? B_OPTION_KEY : 0);

		Model model(&entry);
		AddOnInfo* item = fAddOnsList->EachElement(FindElement, &model);
		if (item != NULL) {
			if (item->key != '\0')
				RemoveShortcut(item->key, item->modifiers);

			item->key = shortcut[0];
			item->modifiers = modifiers;
			AddOneShortcut(&model, item->key, item->modifiers, this);
		}
	}
}


/**
 * @brief Tear down the desktop window, cleaning up the navigation item and add-ons list.
 */
void
BDeskWindow::Quit()
{
	if (fNavigationItem != NULL) {
		// this duplicates BContainerWindow::Quit because
		// fNavigationItem can be part of fTrashContextMenu
		// and would get deleted with it
		BMenu* menu = fNavigationItem->Menu();
		if (menu != NULL)
			menu->RemoveItem(fNavigationItem);

		delete fNavigationItem;
		fNavigationItem = NULL;
	}

	fAddOnsList->MakeEmpty(true);
	delete fAddOnsList;

	delete fDeskShelf;

	// inherited will clean up the rest
	_inherited::Quit();
}


/**
 * @brief Factory method that creates a DesktopPoseView for the desktop model.
 *
 * @param model     The directory model for the desktop.
 * @param viewMode  Display mode (always kIconMode for the desktop).
 * @return A newly constructed DesktopPoseView.
 */
BPoseView*
BDeskWindow::NewPoseView(Model* model, uint32 viewMode)
{
	return new DesktopPoseView(model, viewMode);
}


/**
 * @brief Construct and configure the DesktopPoseView child view.
 *
 * Sets the view and low colours to the current screen desktop colour, sizes the
 * pose view to fill the window, and starts settings observation.
 *
 * @param model  The desktop directory model.
 */
void
BDeskWindow::CreatePoseView(Model* model)
{
	fPoseView = NewPoseView(model, kIconMode);
	fPoseView->SetIconMapping(false);
	fPoseView->SetEnsurePosesVisible(true);
	fPoseView->SetAutoScroll(false);

	BScreen screen(this);
	rgb_color desktopColor = screen.DesktopColor();
	if (desktopColor.alpha != 255) {
		desktopColor.alpha = 255;
#if B_BEOS_VERSION > B_BEOS_VERSION_5
		// This call seems to have the power to cause R5 to freeze!
		// Please report if commenting this out helped or helped not
		// on your system
		screen.SetDesktopColor(desktopColor);
#endif
	}

	fPoseView->SetViewColor(desktopColor);
	fPoseView->SetLowColor(desktopColor);

	fPoseView->SetResizingMode(B_FOLLOW_ALL);
	fPoseView->ResizeTo(Bounds().Size());
	AddChild(fPoseView);

	PoseView()->StartSettingsWatch();
}


/**
 * @brief Forward workspace-activation events to the background image manager.
 *
 * @param workspace  The workspace index being activated.
 * @param state      true when entering the workspace.
 */
void
BDeskWindow::WorkspaceActivated(int32 workspace, bool state)
{
	if (fBackgroundImage != NULL)
		fBackgroundImage->WorkspaceActivated(PoseView(), workspace, state);
}


/**
 * @brief Persist the current desktop icon positions using the stored frame as reference.
 */
void
BDeskWindow::SaveDesktopPoseLocations()
{
	PoseView()->SavePoseLocations(&fOldFrame);
}


/**
 * @brief Handle a screen resolution or colour-space change.
 *
 * Saves pose locations, resizes the window to the new frame, notifies the
 * background image, and re-checks icon visibility.
 *
 * @param frame  New screen frame.
 * @param space  New colour space.
 */
void
BDeskWindow::ScreenChanged(BRect frame, color_space space)
{
	bool frameChanged = (frame != fOldFrame);

	SaveDesktopPoseLocations();
	fOldFrame = frame;
	ResizeTo(frame.Width(), frame.Height());

	if (fBackgroundImage != NULL)
		fBackgroundImage->ScreenChanged(frame, space);

	PoseView()->CheckPoseVisibility(frameChanged ? &frame : 0);
		// if frame changed, pass new frame so that icons can
		// get rearranged based on old pose info for the frame
}


/**
 * @brief Show the desktop window and display the background image for the current workspace.
 */
void
BDeskWindow::Show()
{
	if (fBackgroundImage != NULL)
		fBackgroundImage->Show(PoseView(), current_workspace());

	PoseView()->CheckPoseVisibility();

	_inherited::Show();
}


/**
 * @brief The desktop window never has scroll bars.
 *
 * @return false always.
 */
bool
BDeskWindow::ShouldAddScrollBars() const
{
	return false;
}


/**
 * @brief The desktop window never has a menu bar.
 *
 * @return false always.
 */
bool
BDeskWindow::ShouldAddMenus() const
{
	return false;
}


/**
 * @brief The desktop window never has a container-specific view.
 *
 * @return false always.
 */
bool
BDeskWindow::ShouldAddContainerView() const
{
	return false;
}


/**
 * @brief Handle colour drops, path-monitor notifications, and node-monitor messages.
 *
 * Colour drops set the desktop colour and notify the Backgrounds application.
 * B_PATH_MONITOR triggers a shortcut preference reload.
 * B_NODE_MONITOR triggers an add-on list rebuild and menu refresh.
 *
 * @param message  The incoming BMessage.
 */
void
BDeskWindow::MessageReceived(BMessage* message)
{
	if (message->WasDropped()) {
		const rgb_color* color;
		ssize_t size;
		// handle "roColour"-style color drops
		if (message->FindData("RGBColor", 'RGBC',
			(const void**)&color, &size) == B_OK) {
			BScreen(this).SetDesktopColor(*color);
			PoseView()->SetViewColor(*color);
			PoseView()->SetLowColor(*color);

			// Notify the backgrounds app that the background changed
			status_t initStatus;
			BMessenger messenger("application/x-vnd.Haiku-Backgrounds", -1,
				&initStatus);
			if (initStatus == B_OK)
				messenger.SendMessage(message);

			return;
		}
	}

	switch (message->what) {
		case B_PATH_MONITOR:
		{
			const char* path = "";
			if (message->FindString("watched_path", &path) == B_OK
					&& strcmp(path, fShortcutsSettings) == 0) {
				ApplyShortcutPreferences(true);
			}

			break;
		}
		case B_NODE_MONITOR:
		{
			PRINT(("will update addon shortcuts\n"));
			InitAddOnsList(true);
			ApplyShortcutPreferences(true);

			// a Tracker add-on may have loaded/unloaded
			TTracker* tracker = dynamic_cast<TTracker*>(be_app);
			if (tracker != NULL) {
				BMessage message(kRebuildAddOnMenus);
				tracker->PostMessageToAllContainerWindows(message);
			}
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}
