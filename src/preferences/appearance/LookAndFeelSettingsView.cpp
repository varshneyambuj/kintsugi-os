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
 *   Copyright 2010-2020 Haiku, Inc. All rights reserved.
 *   Distributed under the terms of the MIT license.
 *
 *   Authors:
 *       Stephan Aßmus <superstippi@gmx.de>
 *       Alexander von Gluck <kallisti5@unixzen.com>
 *       John Scipione <jscipione@gmail.com>
 *       Ryan Leavengood <leavengood@gmail.com>
 */


/**
 * @file LookAndFeelSettingsView.cpp
 * @brief Implementation of the Look-and-feel tab in the Appearance preflet.
 *
 * Lets the user pick the window decorator add-on, the ControlLook
 * add-on, and the scroll-bar arrow style. Updates are applied
 * immediately; the parent window keeps a snapshot for Revert.
 */


#include "LookAndFeelSettingsView.h"

#include <stdio.h>
#include <stdlib.h>

#include <Alert.h>
#include <Alignment.h>
#include <AppFileInfo.h>
#include <Box.h>
#include <Button.h>
#include <Catalog.h>
#include <CheckBox.h>
#include <File.h>
#include <InterfaceDefs.h>
#include <InterfacePrivate.h>
#include <LayoutBuilder.h>
#include <Locale.h>
#include <MenuField.h>
#include <MenuItem.h>
#include <Path.h>
#include <PathFinder.h>
#include <PopUpMenu.h>
#include <ScrollBar.h>
#include <StringView.h>
#include <Size.h>
#include <Slider.h>
#include <SpaceLayoutItem.h>
#include <StringView.h>
#include <TextView.h>

#include "AppearanceWindow.h"
#include "FakeScrollBar.h"


#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "DecorSettingsView"
	// This was not renamed to keep from breaking translations


/** @brief Message constant emitted when the decorator menu changes. */
static const int32 kMsgSetDecor = 'deco';
/** @brief Message constant emitted by the decorator About button. */
static const int32 kMsgDecorInfo = 'idec';

/** @brief Message constant emitted when the ControlLook menu changes. */
static const int32 kMsgSetControlLook = 'ctlk';
/** @brief Message constant emitted by the ControlLook About button. */
static const int32 kMsgControlLookInfo = 'iclk';

/** @brief Legacy message; kept for binary compatibility but unused here. */
static const int32 kMsgDoubleScrollBarArrows = 'dsba';

/** @brief Message constant for the single-arrow scroll-bar preview. */
static const int32 kMsgArrowStyleSingle = 'mass';
/** @brief Message constant for the double-arrow scroll-bar preview. */
static const int32 kMsgArrowStyleDouble = 'masd';

/** @brief Compiled-in default for the double-arrow scroll-bar setting. */
static const bool kDefaultDoubleScrollBarArrowsSetting = false;


//	#pragma mark - LookAndFeelSettingsView


/**
 * @brief Constructs the Look-and-feel tab and its menus.
 *
 * Reads the active decorator, ControlLook add-on, and scroll-bar arrow
 * style; builds the corresponding popup menus and the two
 * FakeScrollBar previews; and lays everything out in a grid.
 *
 * @param name Identifier passed to the BView base class.
 */
LookAndFeelSettingsView::LookAndFeelSettingsView(const char* name)
	:
	BView(name, 0),
	fDecorInfoButton(NULL),
	fDecorMenuField(NULL),
	fDecorMenu(NULL),
	fControlLookInfoButton(NULL),
	fControlLookMenuField(NULL),
	fControlLookMenu(NULL),
	fArrowStyleSingle(NULL),
	fArrowStyleDouble(NULL),
	fSavedDecor(NULL),
	fCurrentDecor(NULL),
	fSavedControlLook(NULL),
	fCurrentControlLook(NULL),
	fSavedDoubleArrowsValue(_DoubleScrollBarArrows())
{
	fCurrentDecor = fDecorUtility.CurrentDecorator()->ShortcutName();
	fSavedDecor = fCurrentDecor;

	// Decorator menu
	_BuildDecorMenu();
	fDecorMenuField = new BMenuField("decorator",
		B_TRANSLATE("Decorator:"), fDecorMenu);

	fDecorInfoButton = new BButton(B_TRANSLATE("About"),
		new BMessage(kMsgDecorInfo));

	BPrivate::get_control_look(fCurrentControlLook);
	fSavedControlLook = fCurrentControlLook;

	// ControlLook menu
	_BuildControlLookMenu();
	fControlLookMenuField = new BMenuField("controllook",
		B_TRANSLATE("ControlLook:"), fControlLookMenu);
	fControlLookMenuField->SetToolTip(
		B_TRANSLATE("No effect on running applications"));

	fControlLookInfoButton = new BButton(B_TRANSLATE("About"),
		new BMessage(kMsgControlLookInfo));

	// scroll bar arrow style
	BBox* arrowStyleBox = new BBox("arrow style");
	arrowStyleBox->SetLabel(B_TRANSLATE("Arrow style"));

	fArrowStyleSingle = new FakeScrollBar(true, false,
		new BMessage(kMsgArrowStyleSingle));
	fArrowStyleDouble = new FakeScrollBar(true, true,
		new BMessage(kMsgArrowStyleDouble));

	BView* arrowStyleView;
	arrowStyleView = BLayoutBuilder::Group<>()
		.AddGroup(B_VERTICAL, 1)
			.Add(new BStringView("single", B_TRANSLATE("Single:")))
			.Add(fArrowStyleSingle)
			.AddStrut(B_USE_DEFAULT_SPACING)
			.Add(new BStringView("double", B_TRANSLATE("Double:")))
			.Add(fArrowStyleDouble)
			.SetInsets(B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING,
				B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
			.End()
		.View();
	arrowStyleBox->AddChild(arrowStyleView);
	arrowStyleBox->SetExplicitAlignment(BAlignment(B_ALIGN_LEFT,
		B_ALIGN_VERTICAL_CENTER));
	arrowStyleBox->SetExplicitMaxSize(BSize(B_SIZE_UNLIMITED, B_SIZE_UNSET));

	BStringView* scrollBarLabel
		= new BStringView("scroll bar", B_TRANSLATE("Scroll bar:"));
	scrollBarLabel->SetExplicitAlignment(
		BAlignment(B_ALIGN_LEFT, B_ALIGN_TOP));

	// control layout
	BLayoutBuilder::Grid<>(this, B_USE_DEFAULT_SPACING, B_USE_DEFAULT_SPACING)
		.Add(fDecorMenuField->CreateLabelLayoutItem(), 0, 0)
		.Add(fDecorMenuField->CreateMenuBarLayoutItem(), 1, 0)
		.Add(fDecorInfoButton, 2, 0)
		.Add(fControlLookMenuField->CreateLabelLayoutItem(), 0, 1)
		.Add(fControlLookMenuField->CreateMenuBarLayoutItem(), 1, 1)
		.Add(fControlLookInfoButton, 2, 1)
		.Add(scrollBarLabel, 0, 2)
		.Add(arrowStyleBox, 1, 2)
		.AddGlue(0, 3)
		.SetInsets(B_USE_WINDOW_SPACING);

	// TODO : Decorator Preview Image?
}


/**
 * @brief Destructor; child views are owned by the BView hierarchy.
 */
LookAndFeelSettingsView::~LookAndFeelSettingsView()
{
}


/**
 * @brief Wires control targets and pre-marks the active arrow style.
 *
 * Called by the BView framework once the view enters its window.
 */
void
LookAndFeelSettingsView::AttachedToWindow()
{
	AdoptParentColors();

	if (Parent() == NULL)
		SetViewUIColor(B_PANEL_BACKGROUND_COLOR);

	fDecorMenu->SetTargetForItems(this);
	fDecorInfoButton->SetTarget(this);
	fControlLookMenu->SetTargetForItems(this);
	fControlLookInfoButton->SetTarget(this);
	fArrowStyleSingle->SetTarget(this);
	fArrowStyleDouble->SetTarget(this);

	if (fSavedDoubleArrowsValue)
		fArrowStyleDouble->SetValue(B_CONTROL_ON);
	else
		fArrowStyleSingle->SetValue(B_CONTROL_ON);
}


/**
 * @brief Dispatches decorator, ControlLook and arrow-style messages.
 *
 * Handles menu selections, About-dialog requests, and scroll-bar
 * preview clicks. Decorator and ControlLook info are formatted into
 * BAlert dialogs; arrow-style clicks toggle the global setting.
 *
 * @param message The incoming BMessage.
 */
void
LookAndFeelSettingsView::MessageReceived(BMessage* message)
{
	switch (message->what) {
		case kMsgSetDecor:
		{
			BString newDecor;
			if (message->FindString("decor", &newDecor) == B_OK)
				_SetDecor(newDecor);
			break;
		}

		case kMsgDecorInfo:
		{
			DecorInfo* decor = fDecorUtility.FindDecorator(fCurrentDecor);
			if (decor == NULL)
				break;

			BString authorsText(decor->Authors().String());
			authorsText.ReplaceAll(", ", "\n\t");

			BString infoText(B_TRANSLATE("%decorName\n\n"
				"Authors:\n\t%decorAuthors\n\n"
				"URL: %decorURL\n"
				"License: %decorLic\n\n"
				"%decorDesc\n"));

			infoText.ReplaceFirst("%decorName", decor->Name().String());
			infoText.ReplaceFirst("%decorAuthors", authorsText.String());
			infoText.ReplaceFirst("%decorLic", decor->LicenseName().String());
			infoText.ReplaceFirst("%decorURL", decor->SupportURL().String());
			infoText.ReplaceFirst("%decorDesc",
				decor->ShortDescription().String());

			BAlert* infoAlert = new BAlert(B_TRANSLATE("About decorator"),
				infoText.String(), B_TRANSLATE("OK"));
			infoAlert->SetFlags(infoAlert->Flags() | B_CLOSE_ON_ESCAPE);
			infoAlert->Go();

			break;
		}

		case kMsgSetControlLook:
		{
			BString controlLook;
			if (message->FindString("control_look", &controlLook) == B_OK)
				_SetControlLook(controlLook);
			break;
		}

		case kMsgControlLookInfo:
		{
			BString infoText(B_TRANSLATE("Default Haiku ControlLook"));
			BString path;
			if (!BPrivate::get_control_look(path))
				break;

			if (path.Length() > 0) {
				BFile file(path.String(), B_READ_ONLY);
				if (file.InitCheck() != B_OK)
					break;

				BAppFileInfo info(&file);
				struct version_info version;
				if (info.InitCheck() != B_OK
					|| info.GetVersionInfo(&version, B_APP_VERSION_KIND) != B_OK)
					break;

				infoText = version.short_info;
				infoText << "\n" << version.long_info;
			}

			BAlert* infoAlert = new BAlert(B_TRANSLATE("About control look"),
				infoText.String(), B_TRANSLATE("OK"));
			infoAlert->SetFlags(infoAlert->Flags() | B_CLOSE_ON_ESCAPE);
			infoAlert->Go();

			break;
		}

		case kMsgArrowStyleSingle:
			_SetDoubleScrollBarArrows(false);
			break;

		case kMsgArrowStyleDouble:
			_SetDoubleScrollBarArrows(true);
			break;

		default:
			BView::MessageReceived(message);
			break;
	}
}


//	#pragma mark - LookAndFeelSettingsView private methods


/**
 * @brief Activates the decorator with the given shortcut name.
 *
 * Forwards to the DecorInfo overload after looking up @a name.
 *
 * @param name Decorator shortcut name (file leaf without "Decorator").
 */
void
LookAndFeelSettingsView::_SetDecor(const BString& name)
{
	_SetDecor(fDecorUtility.FindDecorator(name));
}


/**
 * @brief Activates the given decorator and updates the menu mark.
 *
 * No-op if @a decorInfo is @c NULL or the decorator could not be set.
 * Notifies the parent window so it can refresh Defaults/Revert.
 *
 * @param decorInfo Decorator descriptor to activate, or @c NULL.
 */
void
LookAndFeelSettingsView::_SetDecor(DecorInfo* decorInfo)
{
	if (fDecorUtility.SetDecorator(decorInfo) == B_OK) {
		fCurrentDecor = fDecorUtility.CurrentDecorator()->ShortcutName();
		BString decorName = fDecorUtility.CurrentDecorator()->Name();
		fDecorMenu->FindItem(_DecorLabel(decorName))->SetMarked(true);
		Window()->PostMessage(kMsgUpdate);
	}
}


/**
 * @brief Populates the decorator popup with installed decorators.
 *
 * Marks the entry corresponding to @c fCurrentDecor and skips any
 * NULL entries returned by DecorInfoUtility.
 */
void
LookAndFeelSettingsView::_BuildDecorMenu()
{
	fDecorMenu = new BPopUpMenu(B_TRANSLATE("Choose Decorator"));

	// collect the current system decor settings
	int32 count = fDecorUtility.CountDecorators();
	for (int32 i = 0; i < count; ++i) {
		DecorInfo* decor = fDecorUtility.DecoratorAt(i);
		if (decor == NULL) {
			fprintf(stderr, "Decorator : error NULL entry @ %" B_PRId32
				" / %" B_PRId32 "\n", i, count);
			continue;
		}

		BMessage* message = new BMessage(kMsgSetDecor);
		message->AddString("decor", decor->ShortcutName());

		BMenuItem* item = new BMenuItem(_DecorLabel(decor->Name()), message);
		fDecorMenu->AddItem(item);
		if (decor->ShortcutName() == fCurrentDecor)
			item->SetMarked(true);
	}
}


/**
 * @brief Returns @a name with a trailing "Decorator" stripped off.
 *
 * @param name Full decorator name.
 * @return Pointer to a BString-internal buffer; valid only for the
 *         current scope.
 */
const char*
LookAndFeelSettingsView::_DecorLabel(const BString& name)
{
	BString label(name);
	return label.RemoveLast("Decorator").Trim().String();
}


/**
 * @brief Activates the ControlLook add-on at @a path system-wide.
 *
 * An empty @a path selects the built-in default ControlLook. The menu
 * mark is updated and the parent window is notified.
 *
 * @param path Filesystem path to a ControlLook add-on, or empty string
 *             for the default.
 */
void
LookAndFeelSettingsView::_SetControlLook(const BString& path)
{
	fCurrentControlLook = path;
	BPrivate::set_control_look(path);

	if (path.Length() > 0) {
		BEntry entry(path.String());
		const char* label = _ControlLookLabel(entry.Name());
		fControlLookMenu->FindItem(label)->SetMarked(true);
	} else
		fControlLookMenu->FindItem(B_TRANSLATE("Default"))->SetMarked(true);

	Window()->PostMessage(kMsgUpdate);
}


/**
 * @brief Populates the ControlLook popup with installed add-ons.
 *
 * Always inserts a "Default" entry first; then walks every
 * @c B_FIND_PATH_ADD_ONS_DIRECTORY/control_look directory and adds an
 * item for each file found.
 */
void
LookAndFeelSettingsView::_BuildControlLookMenu()
{
	BPathFinder pathFinder;
	BStringList paths;
	BDirectory dir;

	fControlLookMenu = new BPopUpMenu(B_TRANSLATE("Choose ControlLook"));

	BMessage* message = new BMessage(kMsgSetControlLook);
	message->AddString("control_look", "");

	BMenuItem* item = new BMenuItem(B_TRANSLATE("Default"), message);
	if (fCurrentControlLook.Length() == 0)
		item->SetMarked(true);
	fControlLookMenu->AddItem(item);

	status_t error = pathFinder.FindPaths(B_FIND_PATH_ADD_ONS_DIRECTORY,
		"control_look", paths);

	int32 count = paths.CountStrings();
	for (int32 i = 0; i < count; ++i) {
		if (error != B_OK || dir.SetTo(paths.StringAt(i)) != B_OK)
			continue;

		BEntry entry;
		while (dir.GetNextEntry(&entry) == B_OK) {
			BPath path(paths.StringAt(i), entry.Name());
			message = new BMessage(kMsgSetControlLook);
			message->AddString("control_look", path.Path());

			item = new BMenuItem(_ControlLookLabel(entry.Name()), message);
			fControlLookMenu->AddItem(item);
			if (BString(path.Path()) == fCurrentControlLook)
				item->SetMarked(true);
		}
	}
}


/**
 * @brief Returns @a name with a trailing "ControlLook" stripped off.
 *
 * @param name Full ControlLook file leaf.
 * @return Pointer to a BString-internal buffer; valid only for the
 *         current scope.
 */
const char*
LookAndFeelSettingsView::_ControlLookLabel(const char* name)
{
	BString label(name);
	return label.RemoveLast("ControlLook").Trim().String();
}


/**
 * @brief Returns the system-wide double-arrow scroll-bar setting.
 *
 * @return @c true if double-arrow mode is currently active.
 */
bool
LookAndFeelSettingsView::_DoubleScrollBarArrows()
{
	scroll_bar_info info;
	get_scroll_bar_info(&info);

	return info.double_arrows;
}


/**
 * @brief Toggles the system double-arrow scroll-bar setting.
 *
 * No-op if @a doubleArrows already matches the current value. Updates
 * the FakeScrollBar previews and notifies the parent window.
 *
 * @param doubleArrows @c true to enable double arrows, @c false to
 *                     restore single arrows.
 */
void
LookAndFeelSettingsView::_SetDoubleScrollBarArrows(bool doubleArrows)
{
	scroll_bar_info info;
	get_scroll_bar_info(&info);

	if (info.double_arrows == doubleArrows)
		return;

	info.double_arrows = doubleArrows;
	set_scroll_bar_info(&info);

	if (doubleArrows)
		fArrowStyleDouble->SetValue(B_CONTROL_ON);
	else
		fArrowStyleSingle->SetValue(B_CONTROL_ON);

	Window()->PostMessage(kMsgUpdate);
}


/**
 * @brief Reports whether any setting differs from compiled-in defaults.
 *
 * @return @c true if the decorator, ControlLook, or scroll-bar arrow
 *         setting differs from the system default.
 */
bool
LookAndFeelSettingsView::IsDefaultable()
{
	return fCurrentDecor != fDecorUtility.DefaultDecorator()->ShortcutName()
		|| fCurrentControlLook.Length() != 0
		|| _DoubleScrollBarArrows() != false;
}


/**
 * @brief Resets the decorator, ControlLook and arrow style to defaults.
 */
void
LookAndFeelSettingsView::SetDefaults()
{
	_SetDecor(fDecorUtility.DefaultDecorator());
	_SetControlLook(BString(""));
	_SetDoubleScrollBarArrows(false);
}


/**
 * @brief Reports whether any setting differs from the saved snapshot.
 *
 * @return @c true if at least one of decorator, ControlLook or arrow
 *         style differs from the value captured at construction.
 */
bool
LookAndFeelSettingsView::IsRevertable()
{
	return fCurrentDecor != fSavedDecor
		|| fCurrentControlLook != fSavedControlLook
		|| _DoubleScrollBarArrows() != fSavedDoubleArrowsValue;
}


/**
 * @brief Restores decorator, ControlLook and arrow style from the snapshot.
 *
 * No-op when nothing has changed.
 */
void
LookAndFeelSettingsView::Revert()
{
	if (IsRevertable()) {
		_SetDecor(fSavedDecor);
		_SetControlLook(fSavedControlLook);
		_SetDoubleScrollBarArrows(fSavedDoubleArrowsValue);
	}
}
