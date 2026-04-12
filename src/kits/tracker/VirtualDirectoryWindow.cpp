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
 *   Copyright 2013-2015 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT License.
 *   Authors:
 *       John Scipione, jscipione@gmail.com
 *       Ingo Weinhold, ingo_weinhold@gmx.de
 *
 *   Open Tracker License
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file VirtualDirectoryWindow.cpp
 * @brief BContainerWindow subclass for displaying virtual (merged) directories.
 *
 * VirtualDirectoryWindow overrides pose-view creation and window-menu
 * construction so that the window presents a VirtualDirectoryPoseView
 * instead of a standard BPoseView.  It also resolves cross-application
 * refs via VirtualDirectoryManager before constructing the view.
 *
 * @see BContainerWindow, VirtualDirectoryPoseView, VirtualDirectoryManager
 */


#include "VirtualDirectoryWindow.h"

#include <Catalog.h>
#include <Locale.h>

#include <AutoLocker.h>

#include "Commands.h"
#include "Shortcuts.h"
#include "VirtualDirectoryManager.h"
#include "VirtualDirectoryPoseView.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "VirtualDirectoryWindow"


namespace BPrivate {

//	#pragma mark - VirtualDirectoryWindow


/**
 * @brief Construct a VirtualDirectoryWindow with the specified window attributes.
 *
 * @param windowList   Shared list of open Tracker windows.
 * @param openFlags    Flags controlling how the window is opened.
 * @param look         BWindow look constant.
 * @param feel         BWindow feel constant.
 * @param windowFlags  Additional BWindow flags.
 * @param workspace    Workspace bitmask for the window.
 */
VirtualDirectoryWindow::VirtualDirectoryWindow(LockingList<BWindow>* windowList,
	uint32 openFlags, window_look look, window_feel feel, uint32 windowFlags,
	uint32 workspace)
	:
	BContainerWindow(windowList, openFlags, look, feel, windowFlags, workspace)
{
}


/**
 * @brief Create and attach a VirtualDirectoryPoseView for the given model.
 *
 * @param model  The virtual directory model to display.
 */
void
VirtualDirectoryWindow::CreatePoseView(Model* model)
{
	fPoseView = NewPoseView(model, kListMode);
	if (fPoseView == NULL)
		return;

	fBorderedView->GroupLayout()->AddView(fPoseView);
	fBorderedView->EnableBorderHighlight(false);
	fBorderedView->GroupLayout()->SetInsets(0, 0, 1, 1);
}


/**
 * @brief Allocate a new VirtualDirectoryPoseView, resolving cross-app refs first.
 *
 * If the model's node_ref has been relocated by the VirtualDirectoryManager,
 * a replacement model is constructed from the resolved entry_ref before the
 * pose view is created.
 *
 * @param model     Model to display; may be replaced if the ref has changed.
 * @param viewMode  Initial view mode (e.g. kListMode).
 * @return A new VirtualDirectoryPoseView on success, or NULL on failure.
 */
BPoseView*
VirtualDirectoryWindow::NewPoseView(Model* model, uint32 viewMode)
{
	// If the model (or rather the entry_ref to it) came from another
	// application, it may refer to a subdirectory we cannot use directly. The
	// manager resolves the given refs to new ones, if necessary.
	VirtualDirectoryManager* manager = VirtualDirectoryManager::Instance();
	if (manager == NULL)
		return NULL;

	{
		AutoLocker<VirtualDirectoryManager> managerLocker(manager);
		BStringList directoryPaths;
		node_ref nodeRef;
		entry_ref entryRef;
		if (manager->ResolveDirectoryPaths(*model->NodeRef(),
				*model->EntryRef(), directoryPaths, &nodeRef, &entryRef)
				!= B_OK) {
			return NULL;
		}

		if (nodeRef != *model->NodeRef()) {
			// Indeed a new file. Create a new model.
			Model* newModel = new(std::nothrow) Model(&entryRef);
			if (newModel == NULL || newModel->InitCheck() != B_OK) {
				delete newModel;
				return NULL;
			}

			delete model;
			model = newModel;
		}
	}

	return new VirtualDirectoryPoseView(model);
}


/**
 * @brief Return the pose view cast to VirtualDirectoryPoseView.
 *
 * @return The window's VirtualDirectoryPoseView.
 */
VirtualDirectoryPoseView*
VirtualDirectoryWindow::PoseView() const
{
	return static_cast<VirtualDirectoryPoseView*>(fPoseView);
}


/**
 * @brief Add virtual-directory-specific items to the Window menu.
 *
 * @param menu  The Window menu to populate.
 */
void
VirtualDirectoryWindow::AddWindowMenu(BMenu* menu)
{
	BMenuItem* item;

	item = Shortcuts()->ResizeToFitItem();
	item->SetTarget(this);
	menu->AddItem(item);

	item = Shortcuts()->SelectItem();
	item->SetTarget(PoseView());
	menu->AddItem(item);

	item = Shortcuts()->SelectAllItem();
	item->SetTarget(this);
	menu->AddItem(item);

	item = Shortcuts()->InvertSelectionItem();
	item->SetTarget(PoseView());
	menu->AddItem(item);

	item = Shortcuts()->OpenParentItem();
	item->SetTarget(PoseView());
	menu->AddItem(item);

	item = Shortcuts()->CloseItem();
	item->SetTarget(this);
	menu->AddItem(item);
}


/**
 * @brief Add virtual-directory-specific items to the window context menu.
 *
 * @param menu  The context menu to populate.
 */
void
VirtualDirectoryWindow::AddWindowContextMenus(BMenu* menu)
{
	BMenuItem* resizeItem = Shortcuts()->ResizeToFitItem();
	menu->AddItem(resizeItem);
	menu->AddItem(Shortcuts()->SelectItem());
	menu->AddItem(Shortcuts()->SelectAllItem());
	BMenuItem* closeItem = Shortcuts()->CloseItem();
	menu->AddItem(closeItem);
	// target items as needed
	menu->SetTargetForItems(PoseView());
	closeItem->SetTarget(this);
	resizeItem->SetTarget(this);
}

} // namespace BPrivate
