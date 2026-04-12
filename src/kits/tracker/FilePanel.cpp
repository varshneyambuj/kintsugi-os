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
 * @file FilePanel.cpp
 * @brief Public BFilePanel implementation backed by the internal TFilePanel window.
 *
 * BFilePanel is a thin public-API wrapper around TFilePanel. Every mutating
 * method acquires the window lock before delegating, ensuring thread safety
 * when the panel is used from a non-window thread.
 *
 * @see TFilePanel, BRefFilter
 */

// Implementation for the public FilePanel object.


#include <sys/resource.h>

#include <BeBuild.h>
#include <Debug.h>
#include <FilePanel.h>
#include <Looper.h>
#include <Screen.h>
#include <Window.h>

#include "AutoLock.h"
#include "Commands.h"
#include "FilePanelPriv.h"


// prototypes for some private kernel calls that will some day be public
#ifndef _IMPEXP_ROOT
#	define _IMPEXP_ROOT
#endif


//	#pragma mark - BFilePanel


/**
 * @brief Construct a file panel backed by an internal TFilePanel window.
 *
 * Also increases the file-descriptor limit so that multiple open file panels
 * do not exhaust the process's descriptor table.
 *
 * @param mode               B_OPEN_PANEL or B_SAVE_PANEL.
 * @param target             Messenger that receives file-selected messages.
 * @param ref                Starting directory; NULL opens the default directory.
 * @param nodeFlavors        Bitmask of allowed node types (B_FILE_NODE, B_DIRECTORY_NODE, etc.).
 * @param multipleSelection  If true, the user may select more than one entry.
 * @param message            Custom message template; NULL uses the default.
 * @param filter             Optional BRefFilter to restrict visible entries.
 * @param modal              If true, the panel is application-modal.
 * @param hideWhenDone       If true, the panel hides after the user confirms.
 */
BFilePanel::BFilePanel(file_panel_mode mode, BMessenger* target,
	const entry_ref* ref, uint32 nodeFlavors, bool multipleSelection,
	BMessage* message, BRefFilter* filter, bool modal,
	bool hideWhenDone)
{
	// boost file descriptor limit so file panels in other apps don't have
	// problems
	struct rlimit rl;
	rl.rlim_cur = 512;
	rl.rlim_max = RLIM_SAVED_MAX;
	setrlimit(RLIMIT_NOFILE, &rl);

	BEntry startDir(ref);
	fWindow = new TFilePanel(mode, target, &startDir, nodeFlavors,
		multipleSelection, message, filter, 0, B_DOCUMENT_WINDOW_LOOK,
		(modal ? B_MODAL_APP_WINDOW_FEEL : B_NORMAL_WINDOW_FEEL),
		B_CURRENT_WORKSPACE, 0, hideWhenDone);

	static_cast<TFilePanel*>(fWindow)->SetClientObject(this);

	fWindow->SetIsFilePanel(true);
}


/**
 * @brief Destructor; quits the backing TFilePanel window.
 */
BFilePanel::~BFilePanel()
{
	if (fWindow->Lock())
		fWindow->Quit();
}


/**
 * @brief Show the file panel, moving it to the current workspace if necessary.
 *
 * If a parent window is found on the calling thread, the panel is positioned
 * relative to it like an alert dialog.
 */
void
BFilePanel::Show()
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	// if the window is already showing, don't jerk the workspaces around,
	// just pull it to us
	uint32 workspace = 1UL << (uint32)current_workspace();
	uint32 windowWorkspaces = fWindow->Workspaces();
	if (!(windowWorkspaces & workspace)) {
		// window in a different workspace, reopen in current
		fWindow->SetWorkspaces(workspace);
	}

	// Position like an alert, unless the parent is NULL and a position was
	// already restored from saved settings.
	BWindow* parent = dynamic_cast<BWindow*>(
		BLooper::LooperForThread(find_thread(NULL)));
	if (parent != NULL)
		fWindow->MoveTo(fWindow->AlertPosition(parent->Frame()));
	else {
		if (!static_cast<TFilePanel*>(fWindow)->DefaultStateRestored())
			fWindow->MoveTo(fWindow->AlertPosition(BScreen(fWindow).Frame()));
	}

	if (!IsShowing())
		fWindow->Show();

	fWindow->Activate();

#if 1
	// The Be Book gives the names for some of the child views so that apps
	// could move them around if they needed to, but we have most in layouts,
	// so once the window has been opened, we have to forcibly resize "PoseView"
	// (fBackView) to fully invalidate its layout in case any of the controls
	// in it have been moved.
	fWindow->FindView("PoseView")->ResizeBy(1, 1);
	fWindow->FindView("PoseView")->ResizeBy(-1, -1);
#endif
}


/**
 * @brief Hide the file panel without destroying its state.
 */
void
BFilePanel::Hide()
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	if (!fWindow->IsHidden())
		fWindow->QuitRequested();
}


/**
 * @brief Return whether the file panel window is currently visible.
 *
 * @return true if the backing window is not hidden.
 */
bool
BFilePanel::IsShowing() const
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return false;

	return !fWindow->IsHidden();
}


/**
 * @brief Deliver @a message to @a messenger (public convenience wrapper).
 *
 * @param messenger  Target messenger.
 * @param message    Message to deliver.
 */
void
BFilePanel::SendMessage(const BMessenger* messenger, BMessage* message)
{
	messenger->SendMessage(message);
}


/**
 * @brief Return the panel mode (open or save).
 *
 * @return B_OPEN_PANEL or B_SAVE_PANEL.
 */
file_panel_mode
BFilePanel::PanelMode() const
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return B_OPEN_PANEL;

	if (static_cast<TFilePanel*>(fWindow)->IsSavePanel())
		return B_SAVE_PANEL;

	return B_OPEN_PANEL;
}


/**
 * @brief Return a copy of the panel's target messenger.
 *
 * @return The current BMessenger target.
 */
BMessenger
BFilePanel::Messenger() const
{
	BMessenger target;

	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return target;

	return *static_cast<TFilePanel*>(fWindow)->Target();
}


/**
 * @brief Set a new target messenger for file-selection notifications.
 *
 * @param target  The new BMessenger.
 */
void
BFilePanel::SetTarget(BMessenger target)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SetTarget(target);
}


/**
 * @brief Replace the message template sent when the user confirms a selection.
 *
 * @param message  New message template; the panel takes ownership.
 */
void
BFilePanel::SetMessage(BMessage* message)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SetMessage(message);
}


/**
 * @brief Force the panel to rescan its current directory.
 */
void
BFilePanel::Refresh()
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->Refresh();
}


/**
 * @brief Return the currently installed BRefFilter.
 *
 * @return Pointer to the active BRefFilter, or NULL if none is set.
 */
BRefFilter*
BFilePanel::RefFilter() const
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return 0;

	return static_cast<TFilePanel*>(fWindow)->Filter();
}


/**
 * @brief Replace the entry filter applied to the directory listing.
 *
 * @param filter  New BRefFilter; NULL removes any existing filter.
 */
void
BFilePanel::SetRefFilter(BRefFilter* filter)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SetRefFilter(filter);
}


/**
 * @brief Change the label text on one of the panel's buttons.
 *
 * @param button  Which button to relabel (B_DEFAULT_BUTTON or B_CANCEL_BUTTON).
 * @param text    New label string.
 */
void
BFilePanel::SetButtonLabel(file_panel_button button, const char* text)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SetButtonLabel(button, text);
}


/**
 * @brief Update which node types the panel allows the user to select.
 *
 * @param flavors  Bitmask of B_FILE_NODE, B_DIRECTORY_NODE, B_SYMLINK_NODE.
 */
void
BFilePanel::SetNodeFlavors(uint32 flavors)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SetNodeFlavors(flavors);
}


/**
 * @brief Retrieve the entry_ref of the directory currently displayed.
 *
 * @param ref  Output entry_ref filled with the current panel directory.
 */
void
BFilePanel::GetPanelDirectory(entry_ref* ref) const
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	*ref = *static_cast<TFilePanel*>(fWindow)->TargetModel()->EntryRef();
}


/**
 * @brief Pre-fill the save-panel text field with @a text.
 *
 * @param text  The initial filename string shown in the Save panel text field.
 */
void
BFilePanel::SetSaveText(const char* text)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SetSaveText(text);
}


/**
 * @brief Navigate the panel to the directory identified by @a ref.
 *
 * @param ref  Entry ref of the directory to display.
 */
void
BFilePanel::SetPanelDirectory(const entry_ref* ref)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SwitchDirectory(ref);
}


/**
 * @brief Navigate the panel to the directory at the given filesystem @a path.
 *
 * @param path  Absolute filesystem path of the directory to display.
 */
void
BFilePanel::SetPanelDirectory(const char* path)
{
	entry_ref ref;
	status_t err = get_ref_for_path(path, &ref);
	if (err < B_OK)
	  return;

	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SwitchDirectory(&ref);
}


/**
 * @brief Navigate the panel to the directory referenced by @a entry.
 *
 * @param entry  A BEntry pointing to the desired directory.
 */
void
BFilePanel::SetPanelDirectory(const BEntry* entry)
{
	entry_ref ref;

	if (entry && entry->GetRef(&ref) == B_OK)
		SetPanelDirectory(&ref);
}


/**
 * @brief Navigate the panel to @a dir.
 *
 * @param dir  The BDirectory to display.
 */
void
BFilePanel::SetPanelDirectory(const BDirectory* dir)
{
	BEntry entry;

	if (dir && (dir->GetEntry(&entry) == B_OK))
		SetPanelDirectory(&entry);
}


/**
 * @brief Return a pointer to the backing TFilePanel window.
 *
 * @return The internal BWindow used by this panel.
 */
BWindow*
BFilePanel::Window() const
{
	return fWindow;
}


/**
 * @brief Reset the selected-entry iterator to the beginning of the selection.
 */
void
BFilePanel::Rewind()
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->Rewind();
}


/**
 * @brief Retrieve the next selected entry_ref from the current selection.
 *
 * @param ref  Output entry_ref filled with the next selected item.
 * @return B_OK on success, B_ENTRY_NOT_FOUND when exhausted, or B_ERROR on failure.
 */
status_t
BFilePanel::GetNextSelectedRef(entry_ref* ref)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return B_ERROR;

	return static_cast<TFilePanel*>(fWindow)->GetNextEntryRef(ref);

}


/**
 * @brief Control whether the panel auto-hides after the user confirms.
 *
 * @param on  true to hide on confirm; false to keep the panel open.
 */
void
BFilePanel::SetHideWhenDone(bool on)
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return;

	static_cast<TFilePanel*>(fWindow)->SetHideWhenDone(on);
}


/**
 * @brief Return whether the panel hides itself after the user confirms.
 *
 * @return true if hide-when-done is enabled.
 */
bool
BFilePanel::HidesWhenDone(void) const
{
	AutoLock<BWindow> lock(fWindow);
	if (!lock)
		return false;

	return static_cast<TFilePanel*>(fWindow)->HidesWhenDone();
}


void
BFilePanel::WasHidden()
{
	// hook function
}


void
BFilePanel::SelectionChanged()
{
	// hook function
}
