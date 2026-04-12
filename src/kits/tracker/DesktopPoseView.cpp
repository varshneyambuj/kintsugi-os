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
 * @file DesktopPoseView.cpp
 * @brief BPoseView subclass that drives the integrated desktop display.
 *
 * DesktopPoseView extends BPoseView to show the unified Desktop directory.
 * It manages workspace colour updates, background image adoption, the Trash pose,
 * and volume-visibility changes signalled by TTracker settings broadcasts.
 *
 * @see BPoseView, BDeskWindow, TTracker
 */


#include "DesktopPoseView.h"

#include <NodeMonitor.h>
#include <Path.h>
#include <Screen.h>
#include <Volume.h>
#include <VolumeRoster.h>

#include "Background.h"
#include "Commands.h"
#include "FSUtils.h"
#include "PoseList.h"
#include "Tracker.h"
#include "TrackerDefaults.h"
#include "TrackerSettings.h"
#include "TrackerString.h"


//	#pragma mark - DesktopPoseView


/**
 * @brief Construct the desktop pose view for @a model in the given @a viewMode.
 *
 * Adds B_DRAW_ON_CHILDREN to allow background rendering beneath replicants.
 *
 * @param model     The desktop directory model.
 * @param viewMode  Initial view mode (typically kIconMode).
 */
DesktopPoseView::DesktopPoseView(Model* model, uint32 viewMode)
	:
	BPoseView(model, viewMode)
{
	SetFlags(Flags() | B_DRAW_ON_CHILDREN);
}


/**
 * @brief Install the pose-view message filter and complete base-class attachment.
 */
void
DesktopPoseView::AttachedToWindow()
{
	AddFilter(new TPoseViewFilter(this));

	_inherited::AttachedToWindow();
}


/**
 * @brief Handle workspace-activation and background-restore messages.
 *
 * On B_WORKSPACE_ACTIVATED, adopts the screen desktop colour and redraws.
 * On B_RESTORE_BACKGROUND_IMAGE, also forwards a synthetic activation to child replicants.
 *
 * @param message  The incoming BMessage.
 */
void
DesktopPoseView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case B_WORKSPACE_ACTIVATED:
		{
			bool active;
			int32 workspace;
			if (message->FindBool("active", &active) != B_OK || !active
				|| message->FindInt32("workspace", &workspace) != B_OK
				|| workspace != current_workspace()) {
				break;
			}

			AdoptSystemColors();
			Invalidate();

			_inherited::MessageReceived(message);
			break;
		}

		case B_RESTORE_BACKGROUND_IMAGE:
		{
			AdoptSystemColors();
			Invalidate();

			// call workspace activated on child replicant views
			BMessage forward(B_WORKSPACE_ACTIVATED);
			forward.AddBool("active", true);
			forward.AddInt32("workspace", current_workspace());
			_inherited::MessageReceived(&forward);
			break;
		}

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Build a directory entry iterator for the desktop, starting at @a ref.
 *
 * Creates a CachedEntryIteratorList over the Desktop directory and starts
 * node monitoring on it via the TTracker WatchNode helpers.
 *
 * @param nodeMonitoringTarget  BPoseView that should receive node-monitor messages.
 * @param ref                   Entry ref for the Desktop directory.
 * @return A heap-allocated CachedEntryIteratorList, or NULL on failure.
 */
EntryListBase*
DesktopPoseView::InitDesktopDirentIterator(BPoseView* nodeMonitoringTarget,
	const entry_ref* ref)
{
	// the desktop dirent iterator knows how to iterate over all the volumes,
	// integrated onto the desktop

	Model sourceModel(ref, false, true);
	if (sourceModel.InitCheck() != B_OK)
		return NULL;

	CachedEntryIteratorList* result = new CachedEntryIteratorList();

	ASSERT(!sourceModel.IsQuery());
	ASSERT(!sourceModel.IsVirtualDirectory());
	ASSERT(sourceModel.Node() != NULL);

	BDirectory* sourceDirectory = dynamic_cast<BDirectory*>(sourceModel.Node());
	ThrowOnAssert(sourceDirectory != NULL);

	// build an iterator list, start with boot
	EntryListBase* perDesktopIterator
		= new CachedDirectoryEntryList(*sourceDirectory);

	result->AddItem(perDesktopIterator);
	if (nodeMonitoringTarget != NULL) {
		TTracker::WatchNode(sourceModel.NodeRef(), B_WATCH_DIRECTORY | B_WATCH_CHILDREN
				| B_WATCH_NAME | B_WATCH_STAT | B_WATCH_INTERIM_STAT | B_WATCH_ATTR,
			nodeMonitoringTarget);
	}

	if (result->Rewind() != B_OK) {
		delete result;
		if (nodeMonitoringTarget != NULL)
			nodeMonitoringTarget->HideBarberPole();

		return NULL;
	}

	return result;
}


/**
 * @brief Adopt the current screen desktop colour as the view and low colour.
 */
void
DesktopPoseView::AdoptSystemColors()
{
	BScreen screen(Window());
	rgb_color background = screen.DesktopColor();
	SetLowColor(background);
	SetViewColor(background);

	AdaptToBackgroundColorChange();
}


/**
 * @brief Indicate that this view does not track system colours automatically.
 *
 * @return false always; colour is managed manually via AdoptSystemColors().
 */
bool
DesktopPoseView::HasSystemColors() const
{
	return false;
}


/**
 * @brief Delegate to InitDesktopDirentIterator() with this view as the monitor target.
 *
 * @param ref  Entry ref for the directory to iterate.
 * @return A new entry iterator, or NULL on failure.
 */
EntryListBase*
DesktopPoseView::InitDirentIterator(const entry_ref* ref)
{
	return InitDesktopDirentIterator(this, ref);
}


/**
 * @brief Confirm that adding poses from a background thread is safe for the desktop view.
 *
 * @return true always.
 */
bool
DesktopPoseView::AddPosesThreadValid(const entry_ref*) const
{
	return true;
}


/**
 * @brief Finalize pose population and ensure the Trash icon is created last.
 */
void
DesktopPoseView::AddPosesCompleted()
{
	_inherited::AddPosesCompleted();

	// Create Trash pose after other poses have been added
	// so that it is positioned in the next available space.
	CreateTrashPose();
}


/**
 * @brief Locate the system Trash directory and create a pose for it on the desktop.
 *
 * Also starts watching the Trash node for attribute changes so the icon updates.
 */
void
DesktopPoseView::CreateTrashPose()
{
	BPath path;
	if (find_directory(B_TRASH_DIRECTORY, &path) != B_OK)
		return;

	BDirectory trashDir(path.Path());
	BEntry entry;
	if (trashDir.GetEntry(&entry) != B_OK)
		return;

	// redraw Trash icon when attribute changes
	node_ref nref;
	if (entry.GetNodeRef(&nref) == B_OK)
		WatchNewNode(&nref, B_WATCH_ATTR, BMessenger(this));

	Model* trashModel = new Model(&entry);
	PoseInfo poseInfo;
	ReadPoseInfo(trashModel, &poseInfo);

	CreatePose(trashModel, &poseInfo, false, NULL, NULL, true);
}


/**
 * @brief Return whether this desktop view represents the node identified by @a ref.
 *
 * @param ref  The node_ref to test.
 * @return true if the base-class implementation considers this view the owner.
 */
bool
DesktopPoseView::Represents(const node_ref* ref) const
{
	// When the Tracker is set up to integrate non-boot beos volumes,
	// it represents the home/Desktop folders of all beos volumes

	return _inherited::Represents(ref);
}


/**
 * @brief Return whether this desktop view represents the entry identified by @a ref.
 *
 * @param ref  The entry_ref to test.
 * @return true if the corresponding node_ref matches this view.
 */
bool
DesktopPoseView::Represents(const entry_ref* ref) const
{
	BEntry entry(ref);
	node_ref nref;
	entry.GetNodeRef(&nref);
	return Represents(&nref);
}


/**
 * @brief Subscribe to the TTracker settings changes relevant to the desktop.
 *
 * Begins watching kShowDisksIconChanged, kVolumesOnDesktopChanged, and
 * kDesktopIntegrationChanged through the TTracker observer mechanism.
 */
void
DesktopPoseView::StartSettingsWatch()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker != NULL && tracker->LockLooper()) {
		tracker->StartWatching(this, kShowDisksIconChanged);
		tracker->StartWatching(this, kVolumesOnDesktopChanged);
		tracker->StartWatching(this, kDesktopIntegrationChanged);
		tracker->UnlockLooper();
	}
}


/**
 * @brief Unsubscribe from TTracker settings broadcasts started by StartSettingsWatch().
 */
void
DesktopPoseView::StopSettingsWatch()
{
	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	if (tracker != NULL && tracker->LockLooper()) {
		tracker->StopWatching(this, kShowDisksIconChanged);
		tracker->StopWatching(this, kVolumesOnDesktopChanged);
		tracker->StopWatching(this, kDesktopIntegrationChanged);
		tracker->UnlockLooper();
	}
}


/**
 * @brief React to a show-disks-icon setting change by adding or removing the root pose.
 *
 * @param message  Settings notification message carrying the new ShowDisksIcon value.
 */
void
DesktopPoseView::AdaptToVolumeChange(BMessage* message)
{
	if (Window() == NULL)
		return;

	TTracker* tracker = dynamic_cast<TTracker*>(be_app);
	ThrowOnAssert(tracker != NULL);

	bool showDisksIcon = kDefaultShowDisksIcon;
	message->FindBool("ShowDisksIcon", &showDisksIcon);

	BEntry entry("/");
	Model model(&entry);
	if (model.InitCheck() == B_OK) {
		BMessage entryMessage;
		entryMessage.what = B_NODE_MONITOR;

		if (showDisksIcon)
			entryMessage.AddInt32("opcode", B_ENTRY_CREATED);
		else {
			entryMessage.AddInt32("opcode", B_ENTRY_REMOVED);
			entry_ref ref;
			if (entry.GetRef(&ref) == B_OK) {
				BContainerWindow* disksWindow = tracker->FindContainerWindow(&ref);
				if (disksWindow != NULL) {
					disksWindow->Lock();
					disksWindow->Close();
				}
			}
		}

		entryMessage.AddInt32("device", model.NodeRef()->device);
		entryMessage.AddInt64("node", model.NodeRef()->node);
		entryMessage.AddInt64("directory", model.EntryRef()->directory);
		entryMessage.AddString("name", model.EntryRef()->name);

		Window()->PostMessage(&entryMessage, this);
	}

	ToggleDisksVolumes();
}


/**
 * @brief React to a desktop-integration setting change by refreshing volume visibility.
 *
 * @param message  Settings notification message (content is not currently inspected).
 */
void
DesktopPoseView::AdaptToDesktopIntegrationChange(BMessage* message)
{
	ToggleDisksVolumes();
}


/**
 * @brief Update the text high-colour to remain readable against the current background.
 *
 * Chooses black or white based on the relative brightness of the low (background) colour
 * versus the global desktop colour.
 */
void
DesktopPoseView::AdaptToBackgroundColorChange()
{
	// The Desktop text color is chosen independently for the Desktop.
	// The text color is chosen globally for all directories.
	// It's fairly easy to get something unreadable (even with the default
	// settings, it's expected that text will be black on white in Tracker
	// folders, but white on blue on the desktop).

	int32 desktopBrightness = ui_color(B_DESKTOP_COLOR).Brightness();
	SetHighColor(LowColor().Brightness() <= desktopBrightness ? kWhite : kBlack);
}
