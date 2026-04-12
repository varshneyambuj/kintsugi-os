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
 *   Copyright (c) 1991-2000, Be Incorporated. All rights reserved.
 *   Distributed under the terms of the OpenTracker License.
 */


/**
 * @file QueryContainerWindow.cpp
 * @brief BQueryContainerWindow — a Tracker container window for live query results.
 *
 * Specialises BContainerWindow to host a BQueryPoseView, which shows the
 * results of a saved query.  Overrides menu building to show a query-specific
 * Window menu and context menu, and provides SetupDefaultState() to copy
 * default display state from a per-type query template node.
 *
 * @see BContainerWindow, BQueryPoseView
 */


#include <Catalog.h>
#include <Locale.h>
#include <Menu.h>
#include <MenuItem.h>
#include <Path.h>
#include <PopUpMenu.h>
#include <MenuItem.h>
#include <Query.h>

#include "Attributes.h"
#include "Commands.h"
#include "QueryContainerWindow.h"
#include "QueryPoseView.h"
#include "Shortcuts.h"


//	#pragma mark - BQueryContainerWindow


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "QueryContainerWindow"


/**
 * @brief Construct a BQueryContainerWindow.
 *
 * @param windowList  Global window list managed by TTracker.
 * @param openFlags   Flags controlling how the window is opened.
 */
BQueryContainerWindow::BQueryContainerWindow(LockingList<BWindow>* windowList,
	uint32 openFlags)
	:
	BContainerWindow(windowList, openFlags)
{
}


/**
 * @brief Create and return a BQueryPoseView for the given model.
 *
 * @param model  The Model representing the saved query file.
 * @return A new BQueryPoseView; ownership is transferred to the window.
 */
BPoseView*
BQueryContainerWindow::NewPoseView(Model* model, uint32)
{
	return new BQueryPoseView(model);
}


/**
 * @brief Return the window's pose view cast to BQueryPoseView*.
 *
 * @return Typed pointer to the contained BQueryPoseView.
 */
BQueryPoseView*
BQueryContainerWindow::PoseView() const
{
	return static_cast<BQueryPoseView*>(fPoseView);
}


/**
 * @brief Instantiate the query pose view and add it to the window layout.
 *
 * @param model  The Model for the query file.
 */
void
BQueryContainerWindow::CreatePoseView(Model* model)
{
	fPoseView = NewPoseView(model, kListMode);

	fBorderedView->GroupLayout()->AddView(fPoseView);
	fBorderedView->EnableBorderHighlight(false);
	fBorderedView->GroupLayout()->SetInsets(0, 0, 1, 1);
}


/**
 * @brief Populate the Window pull-down menu for a query container.
 *
 * Adds Resize to Fit, Select, Select All, Invert Selection, Close, and
 * Close All items with appropriate targets.
 *
 * @param menu  The Window BMenu to populate.
 */
void
BQueryContainerWindow::AddWindowMenu(BMenu* menu)
{
	BMenuItem* item;

	item = Shortcuts()->ResizeToFitItem();
	item->SetTarget(this);
	menu->AddItem(item);

	item = Shortcuts()->SelectItem();
	item->SetTarget(PoseView());
	menu->AddItem(item);

	item = Shortcuts()->SelectAllItem();
	item->SetTarget(PoseView());
	menu->AddItem(item);

	item = Shortcuts()->InvertSelectionItem();
	item->SetTarget(PoseView());
	menu->AddItem(item);

	item = Shortcuts()->CloseItem();
	item->SetTarget(this);
	menu->AddItem(item);

	item = Shortcuts()->CloseAllInWorkspaceItem();
	item->SetTarget(this);
	menu->AddItem(item);
}


/**
 * @brief Populate the window-background context menu for a query container.
 *
 * @param menu  The context BMenu to populate.
 */
void
BQueryContainerWindow::AddWindowContextMenus(BMenu* menu)
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


/**
 * @brief Initialise the window display state from a per-query-type template node.
 *
 * Looks for a template node at kQueryTemplates/<sanitised MIME type> and
 * copies allowed attributes (frame, view state, columns) from it into the
 * window's state node.
 */
void
BQueryContainerWindow::SetupDefaultState()
{
	BNode defaultingNode;

	WindowStateNodeOpener opener(this, true);
		// this is our destination node, whatever it is for this window
	if (opener.StreamNode() == NULL)
		return;

	BString defaultStatePath(kQueryTemplates);
	BString sanitizedType(PoseView()->SearchForType());

	defaultStatePath += '/';
	int32 length = sanitizedType.Length();
	char* buf = sanitizedType.LockBuffer(length);
	for (int32 index = length - 1; index >= 0; index--)
		if (buf[index] == '/')
			buf[index] = '_';
	sanitizedType.UnlockBuffer(length);

	defaultStatePath += sanitizedType;

	PRINT(("looking for default query state at %s\n",
		defaultStatePath.String()));

	if (!DefaultStateSourceNode(defaultStatePath.String(), &defaultingNode,
			false)) {
		TRACE();
		return;
	}

	// copy over the attributes

	// set up a filter of the attributes we want copied
	const char* allowAttrs[] = {
		kAttrWindowFrame,
		kAttrViewState,
		kAttrViewStateForeign,
		kAttrColumns,
		kAttrColumnsForeign,
		0
	};

	// do it
	AttributeStreamMemoryNode memoryNode;
	NamesToAcceptAttrFilter filter(allowAttrs);
	AttributeStreamFileNode fileNode(&defaultingNode);
	*opener.StreamNode() << memoryNode << filter << fileNode;
}


bool
BQueryContainerWindow::ShouldHaveEditQueryItem(const entry_ref*)
{
	return true;
}


bool
BQueryContainerWindow::ActiveOnDevice(dev_t device) const
{
	return PoseView()->ActiveOnDevice(device);
}
