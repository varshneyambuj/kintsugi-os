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
 *   Distributed under the terms of the Be Sample Code License.
 */


/**
 * @file TrackerSettingsWindow.cpp
 * @brief TrackerSettingsWindow is the Tracker preferences dialog.
 *
 * Provides a two-panel layout: a BListView on the left selects the active
 * settings page (Desktop, Windows, Volume icons, Disk mount) and a BBox on
 * the right displays the corresponding SettingsView.  Defaults and Revert
 * buttons update all pages in one operation.
 *
 * @see TrackerSettings, SettingsView, SettingsItem
 */


#include <Catalog.h>
#include <ControlLook.h>
#include <InterfaceDefs.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <ScrollView.h>

#include "TrackerSettings.h"
#include "TrackerSettingsWindow.h"


namespace BPrivate {

class SettingsItem : public BStringItem {
public:
	SettingsItem(const char* label, SettingsView* view);

	void DrawItem(BView* owner, BRect rect, bool drawEverything);

	SettingsView* View();

private:
	SettingsView* fSettingsView;
};

}	// namespace BPrivate


const uint32 kSettingsViewChanged = 'Svch';
const uint32 kDefaultsButtonPressed = 'Apbp';
const uint32 kRevertButtonPressed = 'Rebp';


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "TrackerSettingsWindow"


//	#pragma mark - TrackerSettingsWindow


/**
 * @brief Construct the TrackerSettingsWindow and build its layout.
 *
 * Instantiates the settings page list, container box, and Defaults/Revert
 * buttons, then registers all built-in settings pages.
 */
TrackerSettingsWindow::TrackerSettingsWindow()
	:
	BWindow(BRect(80, 80, 450, 350), B_TRANSLATE("Tracker preferences"),
		B_TITLED_WINDOW, B_NOT_MINIMIZABLE | B_NOT_RESIZABLE
			| B_ASYNCHRONOUS_CONTROLS | B_NOT_ZOOMABLE
			| B_AUTO_UPDATE_SIZE_LIMITS)
{
	fSettingsTypeListView = new BListView("List View",
		B_SINGLE_SELECTION_LIST);

	BScrollView* scrollView = new BScrollView("scrollview",
		fSettingsTypeListView, B_FRAME_EVENTS | B_WILL_DRAW, false, true);

	fDefaultsButton = new BButton("Defaults", B_TRANSLATE("Defaults"),
		new BMessage(kDefaultsButtonPressed));
	fDefaultsButton->SetEnabled(false);

	fRevertButton = new BButton("Revert", B_TRANSLATE("Revert"),
		new BMessage(kRevertButtonPressed));
	fRevertButton->SetEnabled(false);

	fSettingsContainerBox = new BBox("SettingsContainerBox");

//	const float spacing = be_control_look->DefaultItemSpacing();

	BLayoutBuilder::Group<>(this)
		.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
			.Add(scrollView)
			.AddGroup(B_VERTICAL, B_USE_DEFAULT_SPACING)
				.Add(fSettingsContainerBox)
				.AddGroup(B_HORIZONTAL, B_USE_DEFAULT_SPACING)
					.Add(fDefaultsButton)
					.Add(fRevertButton)
					.AddGlue()
				.End()
			.End()
		.SetInsets(B_USE_WINDOW_SPACING)
		.End();

	fSettingsTypeListView->AddItem(new SettingsItem(B_TRANSLATE("Desktop"),
		new DesktopSettingsView()), kDesktopSettings);
	fSettingsTypeListView->AddItem(new SettingsItem(B_TRANSLATE("Windows"),
		new WindowsSettingsView()), kWindowsSettings);
	fSettingsTypeListView->AddItem(new SettingsItem(
		B_TRANSLATE("Volume icons"), new SpaceBarSettingsView()),
		kSpaceBarSettings);
	fSettingsTypeListView->AddItem(new SettingsItem(
		B_TRANSLATE("Disk mount"), new AutomountSettingsPanel()),
		kAutomountSettings);

	// constraint the listview width so that the longest item fits
	float width = 0;
	fSettingsTypeListView->GetPreferredSize(&width, NULL);
	width += B_V_SCROLL_BAR_WIDTH;
	fSettingsTypeListView->SetExplicitMinSize(BSize(width, 0));
	fSettingsTypeListView->SetExplicitMaxSize(BSize(width, B_SIZE_UNLIMITED));

	fSettingsTypeListView->SetSelectionMessage(
		new BMessage(kSettingsViewChanged));
	fSettingsTypeListView->Select(0);
}


/**
 * @brief Hide the window instead of destroying it when the user closes it.
 *
 * @return true only if the window is already hidden (allowing actual quit).
 */
bool
TrackerSettingsWindow::QuitRequested()
{
	if (IsHidden())
		return true;

	Hide();
	return false;
}


/**
 * @brief Dispatch preference-window messages to the appropriate handlers.
 *
 * @param message  Incoming message (settings changed, defaults, revert, or
 *                 settings view selection).
 */
void
TrackerSettingsWindow::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kSettingsContentsModified:
			_HandleChangedContents();
			break;

		case kDefaultsButtonPressed:
			_HandlePressedDefaultsButton();
			break;

		case kRevertButtonPressed:
			_HandlePressedRevertButton();
			break;

		case kSettingsViewChanged:
			_HandleChangedSettingsView();
			break;

		default:
			_inherited::MessageReceived(message);
			break;
	}
}


/**
 * @brief Refresh all settings pages and show the window on the current workspace.
 *
 * Calls RecordRevertSettings() and ShowCurrentSettings() on every page before
 * becoming visible so that the displayed values are up to date.
 */
void
TrackerSettingsWindow::Show()
{
	if (Lock()) {
		int32 itemCount = fSettingsTypeListView->CountItems();

		for (int32 i = 0; i < itemCount; i++) {
			_ViewAt(i)->RecordRevertSettings();
			_ViewAt(i)->ShowCurrentSettings();
		}

		fSettingsTypeListView->Invalidate();

		_UpdateButtons();

		Unlock();
	}

	if (IsHidden()) {
		// move to current workspace
		SetWorkspaces(B_CURRENT_WORKSPACE);
	}

	_inherited::Show();
}


/**
 * @brief Select and display the given settings page.
 *
 * @param page  The SettingsPage enumeration value to show.
 */
void
TrackerSettingsWindow::ShowPage(SettingsPage page)
{
	fSettingsTypeListView->Select(page);
}


/**
 * @brief Return the SettingsView for list item at index \a i.
 *
 * @param i  Zero-based index into the settings list.
 * @return Pointer to the SettingsView, or NULL if the index is out of range or
 *         the window could not be locked.
 */
SettingsView*
TrackerSettingsWindow::_ViewAt(int32 i)
{
	if (!Lock())
		return NULL;

	SettingsItem* item = dynamic_cast<SettingsItem*>(
		fSettingsTypeListView->ItemAt(i));

	Unlock();

	return item != NULL ? item->View() : NULL;
}


/**
 * @brief React to a settings modification: refresh the list and save settings.
 */
void
TrackerSettingsWindow::_HandleChangedContents()
{
	fSettingsTypeListView->Invalidate();
	_UpdateButtons();

	TrackerSettings().SaveSettings(false);
}


/**
 * @brief Enable or disable the Defaults and Revert buttons based on page state.
 */
void
TrackerSettingsWindow::_UpdateButtons()
{
	int32 itemCount = fSettingsTypeListView->CountItems();

	bool defaultable = false;
	bool revertable = false;

	for (int32 i = 0; i < itemCount; i++) {
		defaultable |= _ViewAt(i)->IsDefaultable();
		revertable |= _ViewAt(i)->IsRevertable();
	}

	fDefaultsButton->SetEnabled(defaultable);
	fRevertButton->SetEnabled(revertable);
}


/**
 * @brief Restore all settings pages to their default values.
 */
void
TrackerSettingsWindow::_HandlePressedDefaultsButton()
{
	int32 itemCount = fSettingsTypeListView->CountItems();

	for (int32 i = 0; i < itemCount; i++) {
		if (_ViewAt(i)->IsDefaultable())
			_ViewAt(i)->SetDefaults();
	}

	_HandleChangedContents();
}


/**
 * @brief Revert all settings pages to their last-recorded state.
 */
void
TrackerSettingsWindow::_HandlePressedRevertButton()
{
	int32 itemCount = fSettingsTypeListView->CountItems();

	for (int32 i = 0; i < itemCount; i++) {
		if (_ViewAt(i)->IsRevertable())
			_ViewAt(i)->Revert();
	}

	_HandleChangedContents();
}


/**
 * @brief Swap the displayed settings view when the user selects a different page.
 *
 * Removes the currently visible child view from the container box and inserts
 * the newly selected SettingsView.
 */
void
TrackerSettingsWindow::_HandleChangedSettingsView()
{
	int32 currentSelection = fSettingsTypeListView->CurrentSelection();
	if (currentSelection < 0)
		return;

	BView* oldView = fSettingsContainerBox->ChildAt(0);

	if (oldView != NULL)
		oldView->RemoveSelf();

	SettingsItem* selectedItem = dynamic_cast<SettingsItem*>(
		fSettingsTypeListView->ItemAt(currentSelection));
	if (selectedItem != NULL) {
		fSettingsContainerBox->SetLabel(selectedItem->Text());

		BView* view = selectedItem->View();
		float tint = B_NO_TINT;
		view->SetViewUIColor(fSettingsContainerBox->ViewUIColor(&tint), tint);
		view->Hide();
		fSettingsContainerBox->AddChild(view);

		view->Show();
	}
}


//	#pragma mark - SettingsItem


/**
 * @brief Construct a SettingsItem pairing a list label with a SettingsView.
 *
 * @param label  Text shown in the settings list.
 * @param view   The SettingsView displayed when this item is selected.
 */
SettingsItem::SettingsItem(const char* label, SettingsView* view)
	:
	BStringItem(label),
	fSettingsView(view)
{
}


/**
 * @brief Draw this list item, using bold font when its settings have been modified.
 *
 * @param owner          The BView in which the item is drawn.
 * @param rect           Bounding rectangle for this item.
 * @param drawEverything If true, redraw the background even when not selected.
 */
void
SettingsItem::DrawItem(BView* owner, BRect rect, bool drawEverything)
{
	if (fSettingsView) {
		bool isRevertable = fSettingsView->IsRevertable();
		bool isSelected = IsSelected();

		if (isSelected || drawEverything) {
			rgb_color color;
			if (isSelected)
				color = ui_color(B_LIST_SELECTED_BACKGROUND_COLOR);
			else
				color = owner->ViewColor();

			owner->SetHighColor(color);
			owner->SetLowColor(color);
			owner->FillRect(rect);
		}

		if (isRevertable)
			owner->SetFont(be_bold_font);
		else
			owner->SetFont(be_plain_font);

		if (isSelected)
			owner->SetHighColor(ui_color(B_LIST_SELECTED_ITEM_TEXT_COLOR));
		else
			owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));

		font_height fheight;
		owner->GetFontHeight(&fheight);

		owner->DrawString(Text(),
			BPoint(rect.left + be_control_look->DefaultLabelSpacing(),
				rect.top + fheight.ascent + 2 + floorf(fheight.leading / 2)));

		owner->SetHighColor(ui_color(B_LIST_ITEM_TEXT_COLOR));
		owner->SetLowColor(owner->ViewColor());
	}
}


/**
 * @brief Return the SettingsView associated with this list item.
 *
 * @return Pointer to the SettingsView.
 */
SettingsView*
SettingsItem::View()
{
	return fSettingsView;
}
